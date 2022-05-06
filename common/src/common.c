#include <quapi/definitions.h>
#include <quapi/message.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char*
quapi_state_str(quapi_state state) {
  switch(state) {
    case QUAPI_INPUT:
      return "INPUT";
    case QUAPI_INPUT_LITERALS:
      return "INPUT_LITERALS";
    case QUAPI_INPUT_ASSUMPTIONS:
      return "INPUT_ASSUMPTIONS";
    case QUAPI_WORKING:
      return "WORKING";
    case QUAPI_SAT:
      return "SAT";
    case QUAPI_UNSAT:
      return "UNSAT";
    case QUAPI_ERROR:
      return "ERROR";
    case QUAPI_ABORTED:
      return "ABORTED";
    case QUAPI_UNKNOWN:
      return "UNKNOWN";
    case QUAPI_UNDEFINED:
      return "UNDEFINED";
  }
  return "UNKNOWN_STATE";
}

const char*
quapi_preload_state_str(quapi_preload_state state) {
  switch(state) {
    case QUAPI_PRELOAD_UNINITIATED:
      return "UNINITIATED";
    case QUAPI_PRELOAD_WAITING_FOR_HEADER:
      return "WAITING_FOR_HEADER";
    case QUAPI_PRELOAD_READING_PREFIX:
      return "READING_PREFIX";
    case QUAPI_PRELOAD_READING_EXISTS:
      return "READING_EXISTS";
    case QUAPI_PRELOAD_READING_FORALL:
      return "READING_FORALL";
    case QUAPI_PRELOAD_READING_MATRIX:
      return "READING_MATRIX";
    case QUAPI_PRELOAD_READING_CLAUSE:
      return "READING_CLAUSE";
    case QUAPI_PRELOAD_WORKING:
      return "WORKING";
    case QUAPI_PRELOAD_UNKNOWN:
      return "UNKNOWN";
  }
}

const char*
quapi_status_str(quapi_status status) {
  switch(status) {
    case QUAPI_OK:
      return "OK";
    case QUAPI_WRITE_ERROR:
      return "WRITE_ERROR";
    case QUAPI_ALLOC_ERROR:
      return "ALLOC_ERROR";
    case QUAPI_PARAMETER_ERROR:
      return "PARAMETER_ERROR";
    case QUAPI_INVALID_SOLVER_STATE_ERROR:
      return "INVALID_SOLVER_STATE_ERROR";
    case QUAPI_OTHER_ERROR:
      return "OTHER_ERROR";
  }
  return "UNKNOWN_STATUS";
}

const char*
quapi_msg_type_str(quapi_msg_type t) {
  switch(t) {
    case QUAPI_MSG_UNDEFINED:
      return "UNDEFINED";
    case QUAPI_MSG_HEADER:
      return "HEADER";
    case QUAPI_MSG_QUANTIFIER:
      return "QUANTIFIER";
    case QUAPI_MSG_LITERAL:
      return "LITERAL";
    case QUAPI_MSG_SOLVE:
      return "SOLVE";
    case QUAPI_MSG_FORK:
      return "FORK";
    case QUAPI_MSG_FORK_REPORT:
      return "FORK REPORT";
    case QUAPI_MSG_EXIT_CODE:
      return "EXIT CODE";
    case QUAPI_MSG_DESTRUCTED:
      return "DESTRUCTED";
  }
  return "UNKNOWN MESSAGE";
}

bool
quapi_check_debug() {
  static int debug = -1;
  if(debug == -1) {
    const char* quapi_debug = getenv("QUAPI_DEBUG");
    debug = quapi_debug != NULL;
  }
  return debug;
}

bool
quapi_check_trace() {
  static int trace = -1;
  if(trace == -1) {
    const char* quapi_trace = getenv("QUAPI_TRACE");
    trace = quapi_trace != NULL;
  }
  return trace;
}

void
dbg(const char* format, ...) {
  if(quapi_check_debug()) {
    flockfile(stderr);
    fprintf(stderr, "[QuAPI] [DEBUG] [%d] ", getpid());
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    funlockfile(stderr);
  }
}

void
trc(const char* format, ...) {
  if(quapi_check_trace()) {
    flockfile(stderr);
    fprintf(stderr, "[QuAPI] [TRACE] [%d] ", getpid());
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    funlockfile(stderr);
  }
}

void
err(const char* format, ...) {
  flockfile(stderr);
  fprintf(stderr, "[QuAPI] [ERROR] [%d] ", getpid());
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  funlockfile(stderr);
}

inline static void
msg_to_arr(quapi_msg* src, char tgt[]) {
  memcpy(tgt, src->arr, sizeof(quapi_msg_data));
  tgt[sizeof(quapi_msg_data)] = src->msg.type;
}

#define READ_VAR(X) X = *((typeof(X)*)(trail + i)), i += sizeof(typeof(X))
#define WRITE_VAR(X) *(typeof(X)*)&data[i] = X, i += sizeof(typeof(X))

#define MALLOC_CHECK(X) \
  if(!X)                \
  err("Failed malloc!"), exit(-1)

static quapi_status
serialize_header(quapi_msg* msg,
                 char** dataptr,
                 size_t* len,
                 quapi_msg_header_data* hdata) {
  quapi_msg_header* header = &msg->msg.data.header;
  int32_t litcount = hdata->literals;
  int32_t clausecount = hdata->clauses;
  int32_t prefixdepth = hdata->prefixdepth;

  char* data = malloc(sizeof(quapi_msg_data) + sizeof(quapi_msg_type_packed) +
                      sizeof(int32_t) * 3 + sizeof(int) * 6);
  if(!data)
    return QUAPI_ALLOC_ERROR;

  size_t i = 0;

  msg_to_arr(msg, data);
  i += sizeof(msg->arr);

  WRITE_VAR(litcount);
  WRITE_VAR(clausecount);
  WRITE_VAR(prefixdepth);
  WRITE_VAR(hdata->forked_child_read_pipe[0]);
  WRITE_VAR(hdata->forked_child_read_pipe[1]);
  WRITE_VAR(hdata->forked_child_write_pipe[0]);
  WRITE_VAR(hdata->forked_child_write_pipe[1]);
  WRITE_VAR(hdata->message_to_parent_pipe[0]);
  WRITE_VAR(hdata->message_to_parent_pipe[1]);

  *dataptr = data;
  *len = i;

  return QUAPI_OK;
}

quapi_status
quapi_write_msg_to_fd(int fd, quapi_msg* msg, quapi_msg_header_data* hdata) {
  assert(msg);

  size_t len = sizeof(msg->arr);
  char data_stack[len];
  char* data = data_stack;

  if(msg->msg.type == QUAPI_MSG_HEADER) {
    assert(hdata);
    quapi_status s = serialize_header(msg, &data, &len, hdata);
    if(s != QUAPI_OK)
      return s;
  } else {
    msg_to_arr(msg, data_stack);
  }

  trc("Write message with len %zu of type %s to fd %d",
      len,
      quapi_msg_type_str(msg->msg.type),
      fd);
  ssize_t r = write(fd, data, len);

  if(msg->msg.type == QUAPI_MSG_HEADER && data != msg->arr) {
    free(data);
  }

  if(r == len)
    return QUAPI_OK;
  else if(r == -1) {
    err("Could not write message of type %s to fd %d! Error: %s",
        quapi_msg_type_str(msg->msg.type),
        fd,
        strerror(errno));
    return QUAPI_WRITE_ERROR;
  }

  return QUAPI_OTHER_ERROR;
}

quapi_status
quapi_write_msg_to_file(FILE* f, quapi_msg* msg, quapi_msg_header_data* hdata) {
  assert(msg);

  size_t len = sizeof(msg->arr);
  char data_stack[len];
  char* data = data_stack;

  if(msg->msg.type == QUAPI_MSG_HEADER) {
    assert(hdata);
    quapi_status s = serialize_header(msg, &data, &len, hdata);
    if(s != QUAPI_OK)
      return s;
  } else {
    msg_to_arr(msg, data_stack);
  }

  trc("Write message with len %zu of type %s to fd %d via FILE",
      len,
      quapi_msg_type_str(msg->msg.type),
      fileno(f));

  ssize_t r = fwrite(data, len, 1, f);

  if(msg->msg.type == QUAPI_MSG_HEADER && data != msg->arr) {
    free(data);
  }

  if(r == 1) {
    switch(msg->msg.type) {
      case QUAPI_MSG_HEADER:
      case QUAPI_MSG_FORK:
      case QUAPI_MSG_SOLVE:
        fflush(f);
        break;
      default:
        break;
    }
    return QUAPI_OK;
  } else if(r == -1) {
    err("Could not write message of type %s to fd %d! Error: %s",
        quapi_msg_type_str(msg->msg.type),
        fileno(f),
        strerror(errno));
    return QUAPI_WRITE_ERROR;
  }

  return QUAPI_OTHER_ERROR;
}

static void
read_trailing_into_header(quapi_msg* msg,
                          quapi_msg_header_data* hdata,
                          read_t read,
                          fread_t fread,
                          FILE* f) {
  quapi_msg_header* h = &msg->msg.data.header;
  const size_t len = sizeof(int32_t) * 3 + sizeof(int) * 6;
  char trail[len];

  if(read)
    read(STDIN_FILENO, trail, len);
  else if(fread)
    fread(trail, len, 1, f);

  size_t i = 0;

  READ_VAR(hdata->literals);
  READ_VAR(hdata->clauses);
  READ_VAR(hdata->prefixdepth);
  READ_VAR(hdata->forked_child_read_pipe[0]);
  READ_VAR(hdata->forked_child_read_pipe[1]);
  READ_VAR(hdata->forked_child_write_pipe[0]);
  READ_VAR(hdata->forked_child_write_pipe[1]);
  READ_VAR(hdata->message_to_parent_pipe[0]);
  READ_VAR(hdata->message_to_parent_pipe[1]);
}

bool
quapi_read_msg_from_fd(int fd,
                       quapi_msg* msg,
                       quapi_msg_header_data* hdata,
                       read_t read) {
  ssize_t s = read(fd, msg->arr, sizeof(msg->arr));
  if(s != sizeof(msg->arr)) {
    if(s == -1) {
      err(
        "Could not read a message from fd %d! Error: %s", fd, strerror(errno));
    } else if(s > 0) {
      err("Could not read full %d bytes of quapi_message from fd %d! Only "
          "read %d bytes.",
          sizeof(msg->arr),
          fd,
          s);
    }
    return false;
  }

  quapi_msg_type t = msg->arr[sizeof(quapi_msg_data)];
  msg->msg.type = t;

  trc("Read message of type %s (read %d bytes)",
      quapi_msg_type_str(msg->msg.type),
      s);

  if(msg->msg.type == QUAPI_MSG_HEADER) {
    read_trailing_into_header(msg, hdata, read, NULL, NULL);

    trc("Read header message! Literals: %d, Clauses: %d",
        hdata->literals,
        hdata->clauses);
  }

  return true;
}

bool
quapi_read_msg_from_file(FILE* f,
                         quapi_msg* msg,
                         quapi_msg_header_data* hdata,
                         fread_t fread) {
  assert(f);
  assert(msg);
  assert(fread);

  ssize_t s = fread(msg->arr, sizeof(msg->arr), 1, f);
  if(s != 1) {
    if(ferror(f)) {
      err("Could not read a message from fd %d via file! Error: %s",
          fileno(f),
          strerror(errno));
      return false;
    }
    if(feof(f)) {
      return false;
    }
  }

  quapi_msg_type t = msg->arr[sizeof(quapi_msg_data)];
  msg->msg.type = t;

  trc("Read message of type %s (read %d bytes)",
      quapi_msg_type_str(msg->msg.type),
      s * (sizeof(quapi_msg_data) + sizeof(quapi_msg_type_packed)));

  if(msg->msg.type == QUAPI_MSG_HEADER) {
    read_trailing_into_header(msg, hdata, NULL, fread, f);

    trc("Read header message! Literals: %d, Clauses: %d",
        hdata->literals,
        hdata->clauses);
  }

  return true;
}

// Fix message sizes, so they don't grow unexpectedly.
static_assert(sizeof(quapi_msg_inner) == 8,
              "Messages must be exactly 8 bytes wide");
static_assert(sizeof(quapi_msg_data) == 4,
              "Message data be exactly 4 bytes wide");
