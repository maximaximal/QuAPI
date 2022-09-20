#ifndef QUAPI_DEFINITIONS_H
#define QUAPI_DEFINITIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef MIN
#define MIN(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })
#endif

#ifndef MAX
#define MAX(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })
#endif

/// The API version may increase with time and is sent with the header message.
/// The runtime then may switch to other processing strategies if older API
/// versions were received.
#define QUAPI_API_VERSION 3

typedef enum quapi_state {
  QUAPI_INPUT,
  QUAPI_INPUT_LITERALS,
  QUAPI_INPUT_ASSUMPTIONS,
  QUAPI_WORKING,
  QUAPI_SAT,
  QUAPI_UNSAT,
  QUAPI_ABORTED,
  QUAPI_ERROR,
  QUAPI_UNKNOWN,
  QUAPI_UNDEFINED,
} quapi_state;

const char*
quapi_state_str(quapi_state state);

typedef enum quapi_preload_state {
  QUAPI_PRELOAD_UNINITIATED,
  QUAPI_PRELOAD_WAITING_FOR_HEADER,
  QUAPI_PRELOAD_READING_PREFIX,
  QUAPI_PRELOAD_READING_EXISTS,
  QUAPI_PRELOAD_READING_FORALL,
  QUAPI_PRELOAD_READING_MATRIX,
  QUAPI_PRELOAD_READING_CLAUSE,
  QUAPI_PRELOAD_WORKING,
  QUAPI_PRELOAD_UNKNOWN
} quapi_preload_state;

const char*
quapi_preload_state_str(quapi_preload_state state);

typedef enum quapi_status {
  QUAPI_OK,
  QUAPI_WRITE_ERROR,
  QUAPI_ALLOC_ERROR,
  QUAPI_PARAMETER_ERROR,
  QUAPI_INVALID_SOLVER_STATE_ERROR,
  QUAPI_OTHER_ERROR,
} quapi_status;

const char*
quapi_status_str(quapi_status status);

typedef struct quapi_msg_header_data {
  int32_t literals;
  int32_t clauses;
  int32_t prefixdepth;
  int forked_child_read_pipe[2];
  int forked_child_write_pipe[2];
  int message_to_parent_pipe[2];
} quapi_msg_header_data;

/* Callback for logging data catched from stderr. NULL terminated string. */
typedef void (*quapi_log_cb)(const char*);

typedef struct quapi_config {
  const char* executable_path;
  char* const* executable_argv;
  char* const* executable_envp;

  quapi_msg_header_data header;

  quapi_log_cb log_cb;

  char* SAT_regex;
  char* UNSAT_regex;

} quapi_config;

typedef struct quapi_solver quapi_solver;

bool
quapi_check_debug();

bool
quapi_check_trace();

void
trc(const char* format, ...);

void
dbg(const char* format, ...);

void
err(const char* format, ...);

struct gzFile_s {
  unsigned have;
  unsigned char* next;
  int64_t pos;
};

typedef ssize_t (*read_t)(int, void*, size_t);
typedef ssize_t (*fread_t)(void*, size_t, size_t, FILE*);
typedef FILE* (*fopen_t)(const char*, const char*);
typedef int (*fclose_t)(FILE*);
typedef int (*getc_t)(FILE*);
typedef int (*fgetc_t)(FILE*);
typedef int (*getc_unlocked_t)(FILE*);
typedef int (*fgetc_unlocked_t)(FILE*);
typedef struct gzFile_s* (*gzdopen_t)(int fd, const char* mode);
typedef int (*gzread_t)(struct gzFile_s*, char* buf, unsigned int len);
typedef int (*gzclose_t)(struct gzFile_s*);

/** @brief Function signature for stdout parsing.
 */
typedef int (*quapi_stdout_cb)(const char*, void* userdata);

#ifdef __cplusplus
}
#include <iostream>

inline std::ostream&
operator<<(std::ostream& o, quapi_state s) {
  return o << quapi_state_str(s);
}

inline std::ostream&
operator<<(std::ostream& o, quapi_status s) {
  return o << quapi_status_str(s);
}
#endif

#endif
