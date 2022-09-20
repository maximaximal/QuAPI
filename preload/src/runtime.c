#define _GNU_SOURCE

#include <quapi/definitions.h>
#include <quapi/message.h>
#include <quapi/runtime.h>
#include <quapi/timing.h>

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <quapi_preload_export.h>

extern bool quapi_runtime_send_destructed_msg;

QUAPI_PRELOAD_NO_EXPORT quapi_runtime global_runtime = { .fopen = NULL,
                                                         .fclose = NULL,
                                                         .read = NULL,
                                                         .getc = NULL,
                                                         .fgetc = NULL,
                                                         .getc_unlocked = NULL,
                                                         .fgetc_unlocked = NULL,
                                                         .state =
                                                           &WAITING_FOR_HEADER,

                                                         .filler_clause_len = 0,
                                                         .outbuf_len = 0,
                                                         .outbuf_written = 0,
                                                         .written_clauses = 0,
                                                         .repeat_state = false,
                                                         .quantifier_count = 0,

                                                         .old_stdout = 0,
                                                         .initiated = false };

quapi_preload_state
quapi_preload_state_func_to_state(quapi_preload_state_func f) {
  if(f == &WAITING_FOR_HEADER)
    return QUAPI_PRELOAD_WAITING_FOR_HEADER;
  else if(f == &READING_PREFIX)
    return QUAPI_PRELOAD_READING_PREFIX;
  else if(f == &READING_EXISTS)
    return QUAPI_PRELOAD_READING_EXISTS;
  else if(f == &READING_FORALL)
    return QUAPI_PRELOAD_READING_FORALL;
  else if(f == &READING_CLAUSE)
    return QUAPI_PRELOAD_READING_CLAUSE;
  else if(f == &READING_MATRIX)
    return QUAPI_PRELOAD_READING_MATRIX;
  else if(f == &WORKING)
    return QUAPI_PRELOAD_WORKING;
  else
    return QUAPI_PRELOAD_UNKNOWN;
}

// int_to_str and digits2 taken and inspired from MIT-licensed fmt
inline static const char*
digits2(size_t value) {
  // GCC generates slightly better code when value is pointer-size.
  return &"0001020304050607080910111213141516171819"
          "2021222324252627282930313233343536373839"
          "4041424344454647484950515253545556575859"
          "6061626364656667686970717273747576777879"
          "8081828384858687888990919293949596979899"[value * 2];
}

inline static char*
int_to_str_(int i, char* out, size_t size) {
  out += size;
  char* end = out;

  while(i >= 100) {
    out -= 2;
    memcpy(out, digits2((size_t)i % 100), 2);
    i /= 100;
  }
  if(i < 10) {
    *--out = '0' + i;
    return out;
  }
  out -= 2;
  memcpy(out, digits2((size_t)i), 2);
  return out;
}

inline static char*
int_to_str(int i, char* out, size_t size) {
  bool negative = i < 0;
  if(negative)
    i = 0 - i;

  char* begin = int_to_str_(i, out, size);
  if(negative)
    *--begin = '-';

  return begin;
}

// Stringification func: https://stackoverflow.com/a/2653351
#define xstr(a) str(a)
#define str(a) #a

#define READ_FUNC(NAME)                                   \
  runtime->NAME = (NAME##_t)dlsym(RTLD_NEXT, xstr(NAME)); \
  if(runtime->NAME)                                       \
    dbg("Successfully read function " xstr(NAME) ": %p", runtime->NAME);

QUAPI_PRELOAD_NO_EXPORT void
quapi_runtime_init(quapi_runtime* runtime) {
  assert(!runtime->initiated);
  dbg("Initiating...");

#ifdef USING_ZEROCOPY
  dbg("Zero-Copy active in preloaded runtime! Ensure it is also active in the "
      "library build.");
#else
  dbg("Zero-Copy disabled in preloaded runtime! Ensure it is also disabled in "
      "the library build.");
#endif

  quapi_timing_construct();

  READ_FUNC(fopen);
  READ_FUNC(fclose);
  READ_FUNC(read);
  READ_FUNC(fread);
  READ_FUNC(getc);
  READ_FUNC(fgetc);
  READ_FUNC(getc_unlocked);
  READ_FUNC(fgetc_unlocked);
  READ_FUNC(gzdopen);
  READ_FUNC(gzread);
  READ_FUNC(gzclose);

  runtime->default_stdin = stdin;
  runtime->old_stdout = STDOUT_FILENO;
#ifdef USING_ZEROCOPY
  trc("Before pipe fdopen");
  runtime->in_stream = quapi_zerocopy_pipe_fdopen(STDIN_FILENO, "rb");
  trc("After pipe fdopen");
#else
  runtime->in_stream = fdopen(STDIN_FILENO, "rb");
#endif
  runtime->outbuf = runtime->outbuf_stack;

  if(!runtime->in_stream) {
    err("Could not open another new FILE from STDIN!");
  }

  // Initialisation of the filler clause, which will be re-initiated once the
  // first message arrives (the header). The first quantifier would also re-do
  // the filler clause.
  memset(runtime->filler_clause, 0, sizeof(runtime->filler_clause));
  memcpy(runtime->filler_clause, "-1 1 0\n", 7);
  runtime->filler_clause_len = 7;

  dbg("Done initiating quapi! Waiting for reads.");

  runtime->initiated = true;
}

/** Just a debugging aid, print everything until the pipe is over. */
static void
read_and_print(quapi_runtime* r) {
  while(true) {
    char c;
    ssize_t s = r->read(STDIN_FILENO, &c, 1);
    if(s == 1) {
      dbg("Reading: \"%c\"", c);
    } else {
      dbg("Stopped reading, error: %s", strerror(errno));
      return;
    }
  }
}

static quapi_msg*
read_msg(quapi_runtime* runtime) {
#ifdef USING_ZEROCOPY
  assert(runtime->in_stream);
  runtime->last_read_msg = quapi_read_msg_from_file(
    runtime->in_stream, &runtime->header_data, runtime->fread);
  return runtime->last_read_msg;
#else
  quapi_msg* msg = &runtime->read_msg;
  runtime->last_read_msg = msg;

  if(!quapi_read_msg_from_file(
       runtime->in_stream, msg, &runtime->header_data, runtime->fread))
    return NULL;

  return &runtime->read_msg;
#endif
}

static int
process_buf_for_dbg_print(char* buf, size_t content_len, size_t buf_len) {
  if(content_len + 1 >= buf_len)
    return 0;

  if(buf[content_len - 1] == '\n' && content_len + 4 < buf_len) {
    content_len -= 1;
    // This is the unicode symbol for the return character to visualize a new
    // line.
    buf[content_len++] = 0xE2;
    buf[content_len++] = 0x8F;
    buf[content_len++] = 0x8E;
    buf[content_len++] = '\0';
    return 2;
  }
  buf[content_len++] = '\0';
  return 1;
}

/** @brief Update the buffer.
 *
 * Returns 0 < bytes < buflen if buffer was updated.
 */
static ssize_t
update_buf(quapi_runtime* r, char* buf, size_t buflen) {
  // Allow for explicit closing from the state machine.
  if(r->outbuf_len == -2)
    return 0;

  ssize_t buflen_ssize_t = buflen;
  ssize_t len = MIN(r->outbuf_len - r->outbuf_written, buflen_ssize_t);

  /*
   * Useful debugging snippet

  dbg("OUTBUF LEN: %d, OUTBUF WRITTEN: %d, BUFLEN: %d, LEN: %d",
      r->outbuf_len,
      r->outbuf_written,
      buflen,
      len);
  */

  if(len > 0) {
    memcpy(buf, r->outbuf + r->outbuf_written, len);
    r->outbuf_written += len;

    // Nice trace messages.
    if(quapi_check_trace()) {
      if(len + 1 < buflen) {
        int str_trace = process_buf_for_dbg_print(buf, len, buflen);
        if(str_trace > 0) {
          trc("Giving \"%s\" to process. Len %d, Buflen %d", buf, len, buflen);
          buf[len] = '\0';
        }
        if(str_trace == 2) {
          buf[len - 1] = '\n';
        }
      } else {
        char print_buf[len + 5];
        memcpy(print_buf, buf, len);
        process_buf_for_dbg_print(print_buf, len, len + 5);

        trc("Giving \"%s\" to process. Len %d. Outbuf_written: %d. Using "
            "temporary buffer.",
            print_buf,
            r->outbuf_written,
            len);
      }
    }
  }
  return len;
}

static void
advance_state(quapi_runtime* r) {
  if(r->outbuf_len == -2)
    return;

  if(r->repeat_state) {
    r->outbuf_len = 0;
    r->state = r->state(r, &r->last_read_msg->msg);
    return;
  }

  // States may loop indefinitely or if they request a new message they loop to
  // there.
  r->outbuf_len = -1;
  while(r->outbuf_len == -1) {
    quapi_msg* msg = read_msg(r);

    if(!msg) {
      // No other messages! Could mean the peer exited.
      dbg("Exit because peer did not send another message before closing.");
      exit(EXIT_SUCCESS);
    }

    r->outbuf_len = 0;
    while(r->outbuf_len == 0) {
      quapi_preload_state before = quapi_preload_state_func_to_state(r->state);

      r->state = r->state(r, &msg->msg);

      quapi_preload_state after = quapi_preload_state_func_to_state(r->state);

      if(before != after)
        trc("State transition from %s to %s",
            quapi_preload_state_str(before),
            quapi_preload_state_str(after));
      else
        trc("State stayed in %s", quapi_preload_state_str(before));
    }
  }
}

QUAPI_PRELOAD_NO_EXPORT ssize_t
quapi_read(quapi_runtime* runtime, char* buf, size_t buflen) {
  static bool first_read = true;
  if(first_read) {
    quapi_timing_firstread();
    first_read = false;
  }

  // First, try to fill the buffer. Afterwards, check for new messages.
  ssize_t bytes = 0;
  if((bytes = update_buf(runtime, buf, buflen)) == 0) {
    runtime->outbuf = runtime->outbuf_stack;
    runtime->outbuf_written = 0;
    advance_state(runtime);
    bytes = update_buf(runtime, buf, buflen);
  }

  if(bytes == 0 && buflen > 0) {
    buf[0] = EOF;
  }

  return bytes;
}

static bool sighandler_sigchld_initialized = false;
static void
sighandler_sigchld(int sig) {}

static void
fork_solving_child(quapi_runtime* r, quapi_msg_fork fork_msg) {
  if(fork_msg.wait_for_exit_code_and_report) {
    if(!sighandler_sigchld_initialized) {
      signal(SIGCHLD, &sighandler_sigchld);
      sighandler_sigchld_initialized = true;
    }

    quapi_runtime_send_destructed_msg = false;
  }
  r->solver_child_pid = fork();
  if(r->solver_child_pid > 0) {// Parent (seeding process that spawns new
                               // childs. Remains in full contact with parent)
    quapi_msg fork_report_msg = { .msg.type = QUAPI_MSG_FORK_REPORT,
                                  .msg.data.fork_report.solver_child_pid =
                                    r->solver_child_pid };
    quapi_write_msg_to_fd(
      r->header_data.message_to_parent_pipe[1], &fork_report_msg, NULL);

    dbg("Fork successful, new pid of forked solver child: %d",
        r->solver_child_pid);

    if(fork_msg.wait_for_exit_code_and_report) {
      signal(SIGCHLD, &sighandler_sigchld);

      dbg("Waiting for exit of child to collect exit code.");
      int status = 0;
      int waitpid_return = waitpid(r->solver_child_pid, &status, 0);
      if(waitpid_return == -1) {
        err("waitpid(%d) for solver child failed with error %s",
            r->solver_child_pid,
            strerror(errno));
      }
      int exit_status = 0;
      if(WIFEXITED(status)) {
        dbg("Child terminated normally.");
        exit_status = WEXITSTATUS(status);
      } else if(WIFSIGNALED(status) && WTERMSIG(status) != SIGKILL) {
        err("Child NOT terminated normally!");
        if(WIFSIGNALED(status)) {
          int signal = WTERMSIG(status);
          err("Child was signaled! Signal: %d (%s)", signal, strsignal(signal));
        }
      }
      dbg("Waited for child to exit and got exit code %d", exit_status);
      quapi_msg exit_code_msg = { .msg.type = QUAPI_MSG_EXIT_CODE,
                                  .msg.data.exit_code.exit_code = exit_status };
      quapi_write_msg_to_fd(
        r->header_data.message_to_parent_pipe[1], &exit_code_msg, NULL);
    }
  } else if(r->solver_child_pid == 0) {// Solving child. Reads more literals.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);

    dup2(r->header_data.forked_child_read_pipe[0], STDIN_FILENO);
    close(r->header_data.forked_child_read_pipe[0]);

    dup2(r->header_data.forked_child_write_pipe[1], STDOUT_FILENO);
    close(r->header_data.forked_child_read_pipe[1]);

#ifdef USING_ZEROCOPY
    if(r->in_stream)
      quapi_zerocopy_pipe_close(r->in_stream);
    r->in_stream = quapi_zerocopy_pipe_fdopen(STDIN_FILENO, "rb");
#else
    r->in_stream = fdopen(STDIN_FILENO, "rb");
#endif

    dbg("Forked into solver child and logging this message from there.");
  } else {
    err("Fork failed!");
  }
}

static void*
WAITING_FOR_HEADER(quapi_runtime* r, quapi_msg_inner* msg) {
  r->outbuf_len = snprintf(r->outbuf,
                           sizeof(r->outbuf_stack),
                           "p cnf %d %d\n",
                           r->header_data.literals,
                           r->header_data.clauses);

  if(r->header_data.literals == 0) {
    memcpy(r->filler_clause, "0\n", 2);
    r->filler_clause_len = 2;
  }

  switch(msg->type) {
    case QUAPI_MSG_HEADER:
      quapi_timing_header();
      if(msg->data.header.api_version == QUAPI_API_VERSION) {
        dbg("API versions match! Both runtime and application use %d",
            QUAPI_API_VERSION);
      } else {
        err("API version mismatch! Runtime is %d and application uses %d! This "
            "runtime does not support such a mismatch. Errors may occur.",
            QUAPI_API_VERSION,
            msg->data.header.api_version);
      }

      // Notify the parent that the child started successfully.
      quapi_msg started_msg = { .msg.type = QUAPI_MSG_STARTED,
                                .msg.data.started.api_version =
                                  QUAPI_API_VERSION };
      quapi_write_msg_to_fd(
        r->header_data.message_to_parent_pipe[1], &started_msg, NULL);

      return READING_PREFIX;
    default:
      err("Received invalid message type %s in state WAITING_FOR_HEADER",
          quapi_msg_type_str(msg->type));
      return READING_PREFIX;
  }
}

static void*
READING_PREFIX(quapi_runtime* r, quapi_msg_inner* msg) {
  static bool first_time = true;
  if(first_time) {
    quapi_timing_msg_after_header();
    first_time = false;
  }

  switch(msg->type) {
    case QUAPI_MSG_QUANTIFIER: {
      int lit = msg->data.quantifier.lit;
      if(lit > 0) {
        r->outbuf_len =
          snprintf(r->outbuf, sizeof(r->outbuf_stack), "e %d", lit);
        if(r->quantifier_count++ == 0) {
          r->filler_clause_len = snprintf(
            r->filler_clause, sizeof(r->filler_clause), "%d -%d 0\n", lit, lit);
        }
        return READING_EXISTS;
      } else if(lit < 0) {
        r->outbuf_len =
          snprintf(r->outbuf, sizeof(r->outbuf_stack), "a %d", -lit);
        ++r->quantifier_count;
        return READING_FORALL;
      } else {
        err("Received invalid quantifier 0 while READING_PREFIX!");
        return READING_PREFIX;
      }
    }
    case QUAPI_MSG_LITERAL:
      return READING_MATRIX;
    case QUAPI_MSG_FORK:
      // Read next message first.
      r->outbuf_len = -1;
      fork_solving_child(r, msg->data.fork);
      return READING_PREFIX;
    case QUAPI_MSG_SOLVE:
      // Directly continue in READING_MATRIX.
      r->repeat_state = true;
      return READING_MATRIX;
    default:
      err("Received message of invalid type %s while READING_PREFIX",
          msg->type);
      return READING_PREFIX;
  }
}
static void*
READING_EXISTS(quapi_runtime* r, quapi_msg_inner* msg) {
  switch(msg->type) {
    case QUAPI_MSG_QUANTIFIER: {
      int lit = msg->data.quantifier.lit;
      if(lit < 0) {
        r->outbuf = r->outbuf_stack;
        r->outbuf_len =
          snprintf(r->outbuf, sizeof(r->outbuf_stack), " 0\na %d", -lit);
        ++r->quantifier_count;
        return READING_FORALL;
      } else if(lit > 0) {
        r->outbuf = int_to_str(lit, r->outbuf_stack, sizeof(r->outbuf_stack));
        *--r->outbuf = ' ';
        r->outbuf_len = r->outbuf_stack + sizeof(r->outbuf_stack) - r->outbuf;
        ++r->quantifier_count;
        return READING_EXISTS;
      } else {
        r->outbuf = r->outbuf_stack;
        r->outbuf_len = snprintf(r->outbuf, sizeof(r->outbuf_stack), " 0\n");
        return READING_PREFIX;
      }
    }
    case QUAPI_MSG_LITERAL:
      r->outbuf = r->outbuf_stack;
      r->outbuf_len = snprintf(
        r->outbuf, sizeof(r->outbuf_stack), " 0\n%d", msg->data.literal.lit);
      return READING_CLAUSE;
    default:
      err("Received message of invalid type %s while READING_EXISTS",
          msg->type);
      return READING_EXISTS;
  }
}
static void*
READING_FORALL(quapi_runtime* r, quapi_msg_inner* msg) {
  switch(msg->type) {
    case QUAPI_MSG_QUANTIFIER: {
      int lit = msg->data.quantifier.lit;
      if(lit > 0) {
        r->outbuf = r->outbuf_stack;
        r->outbuf_len =
          snprintf(r->outbuf, sizeof(r->outbuf_stack), " 0\ne %d", lit);
        ++r->quantifier_count;
        return READING_EXISTS;
      } else if(lit < 0) {
        r->outbuf = int_to_str(-lit, r->outbuf_stack, sizeof(r->outbuf_stack));
        *--r->outbuf = ' ';
        r->outbuf_len = r->outbuf_stack + sizeof(r->outbuf_stack) - r->outbuf;
        ++r->quantifier_count;
        return READING_FORALL;
      } else {
        r->outbuf = r->outbuf_stack;
        r->outbuf_len = snprintf(r->outbuf, sizeof(r->outbuf_stack), " 0\n");
        return READING_PREFIX;
      }
    }
    case QUAPI_MSG_LITERAL:
      r->outbuf = r->outbuf_stack;
      r->outbuf_len = snprintf(
        r->outbuf, sizeof(r->outbuf_stack), " 0\n%d", msg->data.literal.lit);
      return READING_CLAUSE;
    default:
      err("Received message of invalid type %s while READING_EXISTS",
          msg->type);
      return READING_EXISTS;
  }
}
static void*
READING_CLAUSE(quapi_runtime* r, quapi_msg_inner* msg) {
  switch(msg->type) {
    case QUAPI_MSG_LITERAL: {
      int lit = msg->data.literal.lit;
      r->outbuf = int_to_str(lit, r->outbuf_stack, sizeof(r->outbuf_stack) - 1);
      *--r->outbuf = ' ';
      r->outbuf_len = r->outbuf_stack + sizeof(r->outbuf_stack) - 1 - r->outbuf;
      if(lit == 0) {
        r->outbuf_stack[sizeof(r->outbuf_stack) - 1] = '\n';
        ++r->outbuf_len;
        ++r->written_clauses;
        return READING_MATRIX;
      }
      return READING_CLAUSE;
    }
    default:
      err("Received message of invalid type %s while READING_CLAUSE",
          msg->type);
      return READING_MATRIX;
  }
  return NULL;
}
static void*
READING_MATRIX(quapi_runtime* r, quapi_msg_inner* msg) {
  switch(msg->type) {
    case QUAPI_MSG_LITERAL:
      r->outbuf = int_to_str(
        msg->data.literal.lit, r->outbuf_stack, sizeof(r->outbuf_stack));
      r->outbuf_len = r->outbuf_stack + sizeof(r->outbuf_stack) - r->outbuf;
      if(msg->data.literal.lit == 0)
        return READING_MATRIX;
      else
        return READING_CLAUSE;
    case QUAPI_MSG_FORK:
      fork_solving_child(r, msg->data.fork);
      // Request another message to be read.
      r->outbuf_len = -1;
      return READING_MATRIX;
    case QUAPI_MSG_SOLVE:
      // Now, all assumptions have to be filled out! If they aren't, this has to
      // loop until they are.
      if(r->written_clauses < r->header_data.clauses) {
        dbg("Not written enough clauses! Require %d, have %d. Literals: %d. "
            "Writing filler clause.",
            r->header_data.clauses,
            r->written_clauses,
            r->header_data.literals);
        memcpy(r->outbuf, r->filler_clause, r->filler_clause_len);
        r->outbuf_len = r->filler_clause_len;
        ++r->written_clauses;
        r->repeat_state = true;
        return READING_MATRIX;
      } else {
        r->repeat_state = false;
        dbg("Wrote enough clauses! Require %d, have %d. Literals: %d. ",
            r->header_data.clauses,
            r->written_clauses,
            r->header_data.literals);
      }
      return WORKING;
    default:
      err("Received message of invalid type %s while READING_MATRIX",
          quapi_msg_type_str(msg->type));
      return READING_MATRIX;
  }
}
static void*
WORKING(quapi_runtime* r, quapi_msg_inner* msg) {
#ifdef USING_ZEROCOPY
  quapi_zerocopy_pipe_close(r->in_stream);
  r->in_stream = NULL;
#endif
  close(STDIN_FILENO);
  // Close the stream.
  r->outbuf_len = -2;
  return WORKING;
}
