#define _GNU_SOURCE

#include "quapi/definitions.h"
#include "quapi/message.h"
#include <assert.h>
#include <bits/types/struct_timespec.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if __linux__
#include <libgen.h>
#endif

#ifndef WITHOUT_PCRE2
// Use regular UTF-8 encoding.
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

#include <quapi/quapi.h>
#include <quapi/zero-copy-pipes-linux.h>
#include <quapi_export.h>

extern char** environ;

#define MYPOLL_CHILD 0
#define MYPOLL_EVENTFD 1
#define MYPOLL_SOLVERCHILD 2

typedef struct quapi_solver {
  quapi_config config;
  volatile quapi_state state;

  int read_pipe[2];
  int write_pipe[2];
  int pid;

  int universal_prefix_depth;

#ifdef USING_ZEROCOPY
  quapi_zerocopy_pipe* write_pipe_stream;
  quapi_zerocopy_pipe* solverchild_write_pipe_stream;
#else
  FILE* write_pipe_stream;
  FILE* solverchild_write_pipe_stream;
#endif

  int32_t written_clauses;
  int32_t written_assumptions;
  int32_t written_quantifier_literals;

  pid_t solverchild_pid;
  quapi_stdout_cb stdout_cb;
  void* stdout_cb_userdata;

  struct pollfd out_pollfds[3];

#ifndef WITHOUT_PCRE2
  pcre2_code* re_SAT;
  pcre2_match_data* re_SAT_match_data;
  pcre2_code* re_UNSAT;
  pcre2_match_data* re_UNSAT_match_data;
#else
#endif
} quapi_solver;

static bool
file_exists(const char* path) {
  struct stat buffer;
  return (stat(path, &buffer) == 0);
}

static void
log_cb_stderr(const char* msg) {
  fprintf(stderr, "[QuAPI] [LOG] %s\n", msg);
}

static size_t
get_null_terminated_array_size(const char* const* arr) {
  assert(arr);
  size_t len = 1;
  for(; *arr; ++arr) {
    ++len;
  }
  return len;
}

static char**
copy_str_array(const char** arr, size_t prefix_len, size_t postfix_len) {
  size_t len = get_null_terminated_array_size(arr);
  char** arr_copy = calloc(len + prefix_len + postfix_len, sizeof(char*));
  for(size_t i = prefix_len; i < (len + prefix_len) - 1; ++i) {
    arr_copy[i] = strdup(arr[i - prefix_len]);
  }
  arr_copy[len + prefix_len + postfix_len - 1] = NULL;
  return arr_copy;
}

static void
free_str_array(char* const* arr) {
  if(!arr)
    return;

  size_t len = get_null_terminated_array_size((const char**)arr);
  for(size_t i = 0; i < len - 1; ++i) {
    free(arr[i]);
  }
  free((char**)arr);
}

QUAPI_EXPORT const char*
quapi_signature() {
  return "QuAPI";
}

#define PARENT_WRITE s->read_pipe[1]
#define PARENT_READ s->write_pipe[0]
#define CHILD_READ s->read_pipe[0]
#define CHILD_WRITE s->write_pipe[1]
#define PARENT_FORKED_WRITE s->forked_child_read_pipe[1]

static bool
fork_and_exec(quapi_solver* s) {
  // Inspired from https://stackoverflow.com/q/19191030

  pipe(s->read_pipe);
  pipe(s->write_pipe);
  pipe(s->config.header.forked_child_read_pipe);
  pipe(s->config.header.forked_child_write_pipe);
  pipe(s->config.header.message_to_parent_pipe);

  {
    int fd = s->config.header.forked_child_write_pipe[0];
    int r = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if(r == -1) {
      err("Could not set solver child STDOUT to O_NONBLOCK! Error: ",
          strerror(errno));
      return false;
    }
  }

  s->pid = fork();

  if(s->pid > 0) {// Parent
    close(CHILD_READ);
    close(CHILD_WRITE);
    close(s->config.header.forked_child_read_pipe[0]);
    close(s->config.header.forked_child_write_pipe[1]);

#ifdef USING_ZEROCOPY
    s->write_pipe_stream = quapi_zerocopy_pipe_fdopen(PARENT_WRITE, "wb");
    s->solverchild_write_pipe_stream = quapi_zerocopy_pipe_fdopen(
      s->config.header.forked_child_read_pipe[1], "wb");
#else
    s->write_pipe_stream = fdopen(PARENT_WRITE, "wb");
    s->solverchild_write_pipe_stream =
      fdopen(s->config.header.forked_child_read_pipe[1], "wb");
#endif

    dbg("Fork successful! New pid: %d", s->pid);

    return true;
  } else if(s->pid == 0) {// Child
    close(PARENT_READ);
    close(PARENT_WRITE);
    close(s->config.header.forked_child_read_pipe[1]);
    close(s->config.header.forked_child_write_pipe[0]);

    const char* path = s->config.executable_path;
    char* const* argv = s->config.executable_argv;
    char* const* envp = s->config.executable_envp;

    dbg("Forked, logging from child. Executing %s", path);

    int r = 0;
    r = dup2(CHILD_READ, STDIN_FILENO);

    if(r == -1)
      err("Could not run dup2(CHILD_READ, STDIN_FILENO)! Error: %s",
          strerror(errno));
    r = dup2(CHILD_WRITE, STDOUT_FILENO);
    if(r == -1)
      err("Could not run dup2(CHILD_WRITE, STDOUT_FILENO)! Error: %s",
          strerror(errno));
    r = close(CHILD_READ);
    if(r == -1)
      err("Could not run close(CHILD_READ)! Error: %s", strerror(errno));
    r = close(CHILD_WRITE);
    if(r == -1)
      err("Could not run close(CHILD_READ)! Error: %s", strerror(errno));

    int e = execvpe(path, argv, envp);
    if(e == -1) {
      err("Could not execvpe! Killing this process. Error: %s",
          strerror(errno));
      exit(-1);
    }

    assert(false);
    // Can never reach this because of fork and execve semantics.
  } else {
    err("Fork failed!");
    return false;
  }
}

static const char* preload_so_paths[] = {
  "./libquapi_preload.so",
  "../libquapi_preload.so",
  "../quapi/build/libquapi_preload.so",
  "./quapi/libquapi_preload.so",
  "./third_party/quapi/libquapi_preload.so",
  "./_deps/quapi-build/libquapi_preload.so",
  "~/quapi/build/libquapi_preload.so",
  "/usr/local/lib/libquapi_preload.so",
  "/usr/lib/libquapi_preload.so"
};

#ifdef __linux__
static char ldpreload_path_from_dirname[1024];
#endif

static const char*
get_preload_so_path() {
  static const char* p = NULL;

  if(p)
    return p;

  const char* envpreload = getenv("QUAPI_PRELOAD_PATH");
  if(envpreload && file_exists(envpreload)) {
    p = envpreload;
    return envpreload;
  }

#ifdef __linux__
  {
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 20);
    if(len > 0) {
      size_t end = MIN(len, sizeof(buf) - 1);
      buf[end] = '\0';
      char* dir = dirname(buf);
      strcpy(ldpreload_path_from_dirname, dir);
      strcat(ldpreload_path_from_dirname, "/libquapi_preload.so");
      trc("Dir: %s", ldpreload_path_from_dirname);
      if(file_exists(ldpreload_path_from_dirname)) {
        return ldpreload_path_from_dirname;
      }
    }
  }
#endif

  size_t n = sizeof(preload_so_paths) / sizeof(preload_so_paths[0]);
  for(size_t i = 0; i < n; ++i) {
    const char* p_ = preload_so_paths[i];
    if(file_exists(p_)) {
      p = p_;
      return p_;
    }
  }
  return NULL;
}

static bool
setup_eventfd(quapi_solver* s) {
  int fd = eventfd(0, 0);
  if(fd == -1) {
    err("Could not create eventfd! Error: %s", strerror(errno));
    return false;
  }
  struct pollfd* pfd = &s->out_pollfds[MYPOLL_EVENTFD];
  pfd->fd = fd;
  pfd->events = POLLIN;
  pfd->revents = 0;
  return true;
}

#ifndef WITHOUT_PCRE2
static bool
compile_regex(const char* regex,
              pcre2_code** tgt,
              pcre2_match_data** tgt_match_data) {
  int errornumber;
  size_t erroroffset;

  pcre2_code* c = pcre2_compile((const unsigned char*)regex,
                                PCRE2_ZERO_TERMINATED,
                                0,
                                &errornumber,
                                &erroroffset,
                                NULL);

  if(!c) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
    printf("PCRE2 compilation of %s failed at offset %d: %s\n",
           regex,
           (int)erroroffset,
           buffer);
    return false;
  }

  *tgt = c;

  *tgt_match_data = pcre2_match_data_create_from_pattern(c, NULL);

  return true;
}

static bool
setup_regex(quapi_solver* s) {
  assert(!s->re_SAT);
  assert(!s->re_UNSAT);

  if(!compile_regex(s->config.SAT_regex, &s->re_SAT, &s->re_SAT_match_data))
    return false;
  if(!compile_regex(
       s->config.UNSAT_regex, &s->re_UNSAT, &s->re_UNSAT_match_data))
    return false;

  return true;
}
#endif

QUAPI_EXPORT quapi_solver*
quapi_init(const char* path,
           const char** argv,
           const char** envp,
           int litcount,
           int clausecount,
           int maxassumptions,
           const char* SAT_regex,
           const char* UNSAT_regex) {
  if(!path)
    return NULL;

  if(maxassumptions < 0)
    return NULL;

  const char* preload_path = get_preload_so_path();
  if(!preload_path) {
    err("Cannot get preload path!");
    return NULL;
  }

  dbg("Using preload path \"%s\"", preload_path);

  if(UNSAT_regex == NULL ^ SAT_regex == NULL) {
    err("Either both SAT and UNSAT are set, or both are set to NULL!");
    return NULL;
  }

#ifdef USING_ZEROCOPY
  dbg("Zero-Copy active in library! Ensure it is also active in the "
      "runtime build.");
#else
  dbg("Zero-Copy disabled in library! Ensure it is also disabled in "
      "the runtime build.");
#endif

  quapi_solver* s = malloc(sizeof(quapi_solver));

  s->state = QUAPI_INPUT;
  s->written_clauses = 0;
  s->written_assumptions = 0;
  s->written_quantifier_literals = 0;
  s->universal_prefix_depth = -1;
  s->config.log_cb = log_cb_stderr;
  s->config.executable_path = path;
  s->config.header.literals = litcount;
  s->config.header.clauses = clausecount;
  s->config.header.prefixdepth = maxassumptions;
  s->write_pipe_stream = NULL;
  s->solverchild_write_pipe_stream = NULL;
  s->stdout_cb = NULL;
  s->stdout_cb_userdata = NULL;

  if(maxassumptions > 0) {
    s->config.header.clauses += maxassumptions;
  }

#ifndef WITHOUT_PCRE2
  s->re_SAT = NULL;
  s->re_UNSAT = NULL;
  s->re_SAT_match_data = NULL;
  s->re_UNSAT_match_data = NULL;
#endif

  if(SAT_regex) {
#ifdef WITHOUT_PCRE2
    err("No PCRE2 support compiled into QuAPI! Regex cannot be used and must "
        "be set to NULL");
    goto ERROR;
#else
    s->config.SAT_regex = strdup(SAT_regex);
    s->config.UNSAT_regex = strdup(UNSAT_regex);

    if(!setup_regex(s)) {
      free(s);
      return NULL;
    }
#endif
  } else {
    s->config.SAT_regex = NULL;
    s->config.UNSAT_regex = NULL;
  }

  if(argv && argv[0] != NULL) {
    if(strcmp(argv[0], s->config.executable_path) == 0) {
      s->config.executable_argv = copy_str_array(argv, 0, 0);
    } else {
      char** a = copy_str_array(argv, 1, 0);
      a[0] = strdup(path);
      s->config.executable_argv = a;
    }
  } else {
    char** argv = calloc(2, sizeof(char*));
    argv[0] = strdup(path);
    argv[1] = NULL;
    s->config.executable_argv = argv;
  }

  if(quapi_check_debug()) {
    size_t i = 0;
    for(char* const* arg = s->config.executable_argv; *arg != NULL; ++arg) {
      dbg("Argv[%d]=%s", i++, *arg);
    }
  }

  if(envp) {
    envp = (const char**)copy_str_array(envp, 1, 0);
  } else {
    envp = (const char**)copy_str_array((const char**)environ, 1, 0);
  }

#ifndef LIBASAN_PATH
  // LD_PRELOAD=...\0
  {
    const size_t ldpreload_size = strlen(preload_path) + 13;
    char* ldpreload = malloc(ldpreload_size);
    snprintf(ldpreload, ldpreload_size, "LD_PRELOAD=%s", preload_path);
    ldpreload[ldpreload_size - 1] = '\0';
    envp[0] = ldpreload;
  }
#else
  // LD_PRELOAD=...\0
  {
    dbg("Address Sanitizer active, detected in CMake! Loading ASAN: %s",
        LIBASAN_PATH);
    const size_t ldpreload_size =
      strlen(LIBASAN_PATH) + 14 + strlen(preload_path);
    char* ldpreload = malloc(ldpreload_size);
    snprintf(ldpreload,
             ldpreload_size,
             "LD_PRELOAD=%s:%s",
             LIBASAN_PATH,
             preload_path);
    ldpreload[ldpreload_size - 1] = '\0';
    envp[0] = ldpreload;
  }
#endif

  s->config.executable_envp = (char* const*)envp;

  // Setup an eventfd for stopping the solver.
  if(!setup_eventfd(s))
    goto ERROR;

  // Fork and Execute subprocess!
  bool forked = fork_and_exec(s);
  if(!forked)
    goto ERROR;

  {
    struct pollfd* pfd = &s->out_pollfds[MYPOLL_CHILD];
    pfd->fd = s->config.header.message_to_parent_pipe[0];
    pfd->events = POLLIN;
  }
  {
    struct pollfd* pfd = &s->out_pollfds[MYPOLL_SOLVERCHILD];
    pfd->fd = s->config.header.forked_child_write_pipe[0];

    // Only wait for input if there is some regex to match against or if the
    // callback was registered! Would be useless otherwise.
#ifndef WITHOUT_PCRE2
    if(s->re_SAT) {
      pfd->events = POLLIN;
    } else {
#endif
      pfd->events = 0;
#ifndef WITHOUT_PCRE2
    }
#endif
  }

  quapi_msg header_msg = { .msg.data.header.api_version = QUAPI_API_VERSION,
                           .msg.type = QUAPI_MSG_HEADER };
  quapi_write_msg_to_file(s->write_pipe_stream, &header_msg, &s->config.header);

  // Wait for the start message after initiating the solver. This states that
  // everything worked as it should and the read() was captured.
  quapi_msg start_msg;
  bool success = quapi_read_msg_from_fd(
    s->config.header.message_to_parent_pipe[0], &start_msg, NULL, &read);
  if(!success) {
    err("Could not read start message from child!");
    goto ERROR;
  }
  if(start_msg.msg.type != QUAPI_MSG_STARTED) {
    err("Received message was not a STARTED message, but a %s message!",
        quapi_msg_type_str(start_msg.msg.type));
    goto ERROR;
  }

  return s;
ERROR:
  quapi_release(s);
  return NULL;
}

QUAPI_EXPORT void
quapi_release(quapi_solver* s) {
  assert(s);

  free(s->config.SAT_regex);
  free(s->config.UNSAT_regex);
#ifndef WITHOUT_PCRE2
  if(s->re_SAT_match_data)
    pcre2_match_data_free(s->re_SAT_match_data);
  if(s->re_UNSAT_match_data)
    pcre2_match_data_free(s->re_UNSAT_match_data);
  if(s->re_SAT)
    pcre2_code_free(s->re_SAT);
  if(s->re_UNSAT)
    pcre2_code_free(s->re_UNSAT);
#endif

#ifdef USING_ZEROCOPY
  quapi_zerocopy_pipe_close(s->write_pipe_stream);
  quapi_zerocopy_pipe_close(s->solverchild_write_pipe_stream);
#else
  if(s->write_pipe_stream)
    fclose(s->write_pipe_stream);
  if(s->solverchild_write_pipe_stream)
    fclose(s->solverchild_write_pipe_stream);
#endif

  close(s->out_pollfds[MYPOLL_EVENTFD].fd);

  free_str_array(s->config.executable_envp);
  s->config.executable_envp = NULL;

  free_str_array(s->config.executable_argv);
  s->config.executable_argv = NULL;

  free(s);
}

QUAPI_EXPORT void
quapi_quantify(quapi_solver* s, int32_t lit_or_zero) {
  assert(s->state == QUAPI_INPUT);

  if(lit_or_zero < 0 &&
     s->written_quantifier_literals < s->config.header.prefixdepth) {
    s->universal_prefix_depth = s->written_quantifier_literals;
  }

  if(s->config.header.prefixdepth > s->written_quantifier_literals &&
     lit_or_zero < 0) {
    // The quantifier must be switched to an existential quantifier, else the
    // formula would be invalid!
    lit_or_zero = -lit_or_zero;
  }

  QUAPI_GIVE_MSGS(msg, 1, s->write_pipe_stream)

  msg->msg.type = QUAPI_MSG_QUANTIFIER;
  msg->msg.data.quantifier.lit = lit_or_zero;

  quapi_write_msg_to_file(s->write_pipe_stream, msg, NULL);

  if(lit_or_zero != 0) {
    ++s->written_quantifier_literals;
  }
}

QUAPI_EXPORT void
quapi_add(quapi_solver* s, int32_t lit_or_zero) {
  assert(s->state == QUAPI_INPUT_LITERALS || s->state == QUAPI_INPUT);

  s->state = QUAPI_INPUT_LITERALS;

  QUAPI_GIVE_MSGS(msg, 1, s->write_pipe_stream)

  msg->msg.type = QUAPI_MSG_LITERAL;
  msg->msg.data.literal.lit = lit_or_zero;

  quapi_write_msg_to_file(s->write_pipe_stream, msg, NULL);

  if(lit_or_zero == 0) {
    ++s->written_clauses;
  }
}

static bool
make_solvable(quapi_solver* s) {
  if(s->state == QUAPI_INPUT_LITERALS || s->state == QUAPI_INPUT) {
    s->state = QUAPI_INPUT_ASSUMPTIONS;

    // Wait for exit code and report only if there is no SAT_regex to match
    // against.
    QUAPI_GIVE_MSGS(fork_msg, 1, s->write_pipe_stream)
    fork_msg->msg.type = QUAPI_MSG_FORK;
    fork_msg->msg.data.fork.wait_for_exit_code_and_report =
      s->config.SAT_regex ? 0 : 1;

    quapi_status status;
    status = quapi_write_msg_to_file(s->write_pipe_stream, fork_msg, NULL);

    if(status != QUAPI_OK) {
      return false;
    }

    quapi_msg fork_result_msg;

    bool success;
    do {
      success =
        quapi_read_msg_from_fd(s->config.header.message_to_parent_pipe[0],
                               &fork_result_msg,
                               NULL,
                               &read);
    } while(success && fork_result_msg.msg.type != QUAPI_MSG_FORK_REPORT);

    if(!success) {
      err("Could not read fork report message!");
      return false;
    }

    s->solverchild_pid = fork_result_msg.msg.data.fork_report.solver_child_pid;
    dbg("Solverchild has PID %d", s->solverchild_pid);
  }

  return true;
}

QUAPI_EXPORT bool
quapi_assume(quapi_solver* s, int32_t lit_or_zero) {
  assert(s->state == QUAPI_INPUT_LITERALS ||
         s->state == QUAPI_INPUT_ASSUMPTIONS);

  // Zero makes no sense with assumptions!
  if(lit_or_zero == 0)
    return true;

  if(s->written_clauses >=
     s->config.header.clauses + s->config.header.prefixdepth) {
    err("When writing literal %d: written_clauses=%d >= "
        "config.header.clauses=%d + "
        "config.header.prefixdepth=%d",
        lit_or_zero,
        s->written_clauses,
        s->config.header.clauses,
        s->config.header.prefixdepth);

    return false;
  }
  assert(s->written_clauses <
         (s->config.header.clauses) + s->config.header.prefixdepth);

  if(!make_solvable(s))
    return false;

  s->state = QUAPI_INPUT_ASSUMPTIONS;

  {
    QUAPI_GIVE_MSGS(lit_msg, 1, s->solverchild_write_pipe_stream)

    lit_msg->msg.type = QUAPI_MSG_LITERAL;
    lit_msg->msg.data.literal.lit = lit_or_zero;
    quapi_status status =
      quapi_write_msg_to_file(s->solverchild_write_pipe_stream, lit_msg, NULL);
    if(status != QUAPI_OK)
      return false;
  }

  {
    QUAPI_GIVE_MSGS(endclause_lit_msg, 1, s->solverchild_write_pipe_stream)
    endclause_lit_msg->msg.type = QUAPI_MSG_LITERAL;
    endclause_lit_msg->msg.data.literal.lit = 0;
    quapi_status status = quapi_write_msg_to_file(
      s->solverchild_write_pipe_stream, endclause_lit_msg, NULL);
    if(status != QUAPI_OK)
      return false;
  }

  ++s->written_clauses;
  ++s->written_assumptions;

  return true;
}

static void
abort_solverchild(quapi_solver* s) {
  kill(s->solverchild_pid, SIGKILL);
}

QUAPI_EXPORT void
quapi_reset_assumptions(quapi_solver* s) {
  if(s->state == QUAPI_INPUT_ASSUMPTIONS) {
    abort_solverchild(s);

    // Wait for child to exit.
    waitpid(s->solverchild_pid, NULL, 0);

    // Reset clauses and stuff.
    s->written_clauses -= s->written_assumptions;
    s->written_assumptions = 0;

    // Reset to literals so the next assume will fork again.
    s->state = QUAPI_INPUT_LITERALS;
  }
}

static size_t
read_all_available_into_buffer(int fd,
                               char** buf,
                               size_t* datalen,
                               size_t* buflen) {
  size_t linecount = 0;
  if(!*buf) {
    *buflen = 1024;
    *buf = malloc(*buflen);
    if(!*buf) {
      err("Could not malloc buffer of size %d for reading from solver child!",
          *buflen);
      exit(-1);
    }
  }
  assert(*buf);
  FILE* f = fdopen(fd, "r");
  int c;
  char* b = *buf;
  while((c = fgetc(f)) != EOF && c != 0) {
    if(c == '\n')
      ++linecount;
    // else
    //   dbg("Got character %c in line %zu", c, linecount);

    (*buf)[(*datalen)++] = c;
    if(*datalen + 1 == *buflen) {
      *buflen *= 2;
      *buf = realloc(*buf, *buflen);
      if(!*buf) {
        err("Could not malloc buffer of size %d for reading from solver child!",
            *buflen);
        exit(-1);
      }
    }
  }
  (*buf)[*datalen] = '\0';
  // Purely for debugging:
  // dbg("Done reading. Datalen: %d. Data: %s", *datalen, *buf);
  return linecount;
}

#ifndef WITHOUT_PCRE2
static bool
match_regex(pcre2_code* re,
            pcre2_match_data* data,
            const char* subject,
            size_t subject_length) {
  int rc;
  rc = pcre2_match(re,                            /* the compiled pattern */
                   (const unsigned char*)subject, /* the subject string */
                   subject_length, /* the length of the subject */
                   0,              /* start at offset 0 in the subject */
                   0,              /* default options */
                   data,           /* block for storing the result */
                   NULL);          /* use default match context */

  // dbg("Match Regex against %s with result %d", subject, rc);

  return rc >= 0;
}
#endif

typedef struct S_data {
  quapi_solver* s;

  int retcode;

  struct pollfd* active_pfd;

  char* buf;
  size_t datalen;
  size_t buflen;
} S_data;

static void*
S_POLL(S_data* d);
static void*
S_HANDLE_CHILD(S_data* d);
static void*
S_HANDLE_EVENTFD(S_data* d);
static void*
S_HANDLE_SOLVERCHILD(S_data* d);

typedef void*(S_state)(S_data*);

static void*
S_POLL(S_data* d) {
  S_state* actions[] = { S_HANDLE_CHILD,
                         S_HANDLE_EVENTFD,
                         S_HANDLE_SOLVERCHILD };
  const size_t fds = sizeof(actions) / sizeof(actions[0]);

  // Handle events from last call to poll.
  for(size_t i = 0; i < fds; ++i) {
    struct pollfd* pfd = &d->s->out_pollfds[i];
    d->active_pfd = pfd;

    if(pfd->revents & POLLIN) {
      pfd->revents &= ~POLLIN;
      return actions[i];
    }
  }

  // Poll again.
  int r = poll(d->s->out_pollfds, fds, -1);
  if(r == -1) {
    switch(errno) {
      case EFAULT:
        err("poll() returned EFAULT!");
        d->retcode = 0;
        return NULL;
      case EINTR:
        dbg("poll() returned EINTR! Some signal occurred! Repeating poll...");
        return S_POLL;
      case EINVAL:
        err("poll() returned EINVAL! Some error with parameters.");
        d->retcode = 0;
        return NULL;
      case ENOMEM:
        err("poll() returned ENOMEM! No memory in kernel.");
        d->retcode = 0;
        return NULL;
    }
  }

  return S_POLL;
}

static void*
S_HANDLE_CHILD(S_data* d) {
  quapi_msg msg;
  bool s = quapi_read_msg_from_fd(d->active_pfd->fd, &msg, NULL, &read);
  if(!s)
    return S_POLL;

  switch(msg.msg.type) {
    case QUAPI_MSG_DESTRUCTED:
      dbg("Solver child was destructed before a valid result was read from "
          "STDOUT!");
      d->retcode = 0;
      return NULL;
    case QUAPI_MSG_EXIT_CODE:
      dbg("Solver child exited with exit code %d, received a message from "
          "child.",
          msg.msg.data.exit_code.exit_code);
      if(msg.msg.data.exit_code.exit_code == 0 && d->retcode == 0 &&
         d->s->stdout_cb) {
        /* There is some more data! The real exit code will be given by the
         * callback function. */
        dbg("There is a callback function for string output set! Proceed "
            "reading until the callback function gives a return code != 0.");
        return S_POLL;
      } else {
        d->retcode = msg.msg.data.exit_code.exit_code;
        return NULL;
      }
    default:
      err("Read unsupported message in S_HANDLE_CHILD: %s!",
          quapi_msg_type_str(msg.msg.type));
      d->retcode = 0;
      return NULL;
  }
}

static size_t
cutout_line_from_buf(S_data* d) {
  assert(d->buf);
  char* linefeed = strchr(d->buf, '\n');
  if(!linefeed)
    return 0;
  *linefeed = '\0';
  return linefeed - d->buf + 1;
}

static void
move_next_line_to_front(S_data* d, size_t linelength) {
  memmove(d->buf, d->buf + linelength, d->datalen - linelength);
  d->datalen -= linelength;
}

static void*
S_HANDLE_SOLVERCHILD(S_data* d) {
  size_t lines = read_all_available_into_buffer(
    d->active_pfd->fd, &d->buf, &d->datalen, &d->buflen);

  while(lines > 0) {
    assert(d->buf);
    size_t linelength = cutout_line_from_buf(d);
    // dbg("Line Length: %d of buf %s", linelength, d->buf);
    assert(linelength);
    --lines;

#ifndef WITHOUT_PCRE2
    bool sat =
      match_regex(d->s->re_SAT, d->s->re_SAT_match_data, d->buf, linelength);
    if(sat) {
      d->retcode = 10;
      return NULL;
    }

    bool unsat = match_regex(
      d->s->re_UNSAT, d->s->re_UNSAT_match_data, d->buf, linelength);
    if(unsat) {
      d->retcode = 20;
      return NULL;
    }
#endif

    if(d->s->stdout_cb) {
      int ret = d->s->stdout_cb(d->buf, d->s->stdout_cb_userdata);
      if(ret != 0) {
        d->retcode = ret;
        return NULL;
      }
    }

    move_next_line_to_front(d, linelength);
  }

  return S_POLL;
}

static void*
S_HANDLE_EVENTFD(S_data* d) {
  dbg("Received an abort via eventfd! revents: %d", d->active_pfd->revents);

  uint64_t evfdval;
  read(d->active_pfd->fd, &evfdval, sizeof(evfdval));

  abort_solverchild(d->s);

  d->retcode = 0;
  return NULL;
}

static int
solve_internal(quapi_solver* s) {
  if(!(s->state == QUAPI_INPUT || s->state == QUAPI_INPUT_LITERALS ||
       s->state == QUAPI_INPUT_ASSUMPTIONS)) {
    err("Solver is in invalid state %s for solving!",
        quapi_state_str(s->state));
    return 0;
  }

  s->state = QUAPI_WORKING;

  {
    QUAPI_GIVE_MSGS(solve_msg, 1, s->solverchild_write_pipe_stream)
    solve_msg->msg.type = QUAPI_MSG_SOLVE;
    quapi_status status = quapi_write_msg_to_file(
      s->solverchild_write_pipe_stream, solve_msg, NULL);
    if(status != QUAPI_OK)
      return 0;
  }

  s->out_pollfds[0].revents = 0;
  s->out_pollfds[1].revents = 0;
  s->out_pollfds[2].revents = 0;

  S_data d = { .s = s,
               .buf = NULL,
               .buflen = 0,
               .datalen = 0,
               .active_pfd = NULL,
               .retcode = 0 };
  S_state* S = S_POLL;
  while(S)
    S = S(&d);

  if(d.buf)
    free(d.buf);

  return d.retcode;
}

static bool
allow_missing_universal_assumptions() {
  const char* allow = getenv("QUAPI_ALLOW_MISSING_UNIVERSAL_ASSUMPTIONS");
  return allow;
}

QUAPI_EXPORT int
quapi_solve(quapi_solver* s) {
  if(s->written_assumptions < s->universal_prefix_depth &&
     !allow_missing_universal_assumptions()) {
    err("Not enough assumptions to assign all leading universal "
        "quantifiers! The universal prefix goes up to length %d and the "
        "maximum assumption count is %d, but only %d assumptions were applied. "
        "Set QUAPI_ALLOW_MISSING_UNIVERSAL_ASSUMPTIONS envvar to allow this.",
        s->universal_prefix_depth,
        s->config.header.prefixdepth,
        s->written_assumptions);
    return 0;
  }

  if(!make_solvable(s))
    return 0;

  int r = solve_internal(s);
  s->state = QUAPI_INPUT_LITERALS;
  s->written_clauses -= s->written_assumptions;
  s->written_assumptions = 0;
  return r;
}

QUAPI_EXPORT void
quapi_terminate(quapi_solver* s) {
  assert(s);
  union intwrite {
    int64_t i;
    char c[8];
  };
  union intwrite buf;
  struct pollfd* pfd = &s->out_pollfds[MYPOLL_EVENTFD];
  buf.i = 1;
  write(pfd->fd, &buf, sizeof(buf));
}

QUAPI_EXPORT quapi_state
quapi_get_state(quapi_solver* s) {
  assert(s);
  return s->state;
}

QUAPI_EXPORT void
quapi_set_stdout_cb(quapi_solver* s,
                    quapi_stdout_cb stdout_cb,
                    void* userdata) {
  // Set the POLLIN flag, it is required now.
  struct pollfd* pfd = &s->out_pollfds[MYPOLL_SOLVERCHILD];
  pfd->events = POLLIN;

  s->stdout_cb = stdout_cb;
  s->stdout_cb_userdata = userdata;
}
