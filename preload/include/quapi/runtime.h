#ifndef QUAPI_RUNTIME_H
#define QUAPI_RUNTIME_H

#include <quapi/definitions.h>
#include <quapi/message.h>
#include <quapi/zero-copy-pipes-linux.h>

#include "quapi_preload_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef struct quapi_runtime quapi_runtime;

static void*
WAITING_FOR_HEADER(quapi_runtime*, quapi_msg_inner*);
static void*
READING_PREFIX(quapi_runtime*, quapi_msg_inner*);
static void*
READING_EXISTS(quapi_runtime*, quapi_msg_inner*);
static void*
READING_FORALL(quapi_runtime*, quapi_msg_inner*);
static void*
READING_CLAUSE(quapi_runtime*, quapi_msg_inner*);
static void*
READING_MATRIX(quapi_runtime*, quapi_msg_inner*);
static void*
WORKING(quapi_runtime*, quapi_msg_inner*);

typedef void* (*quapi_preload_state_func)(quapi_runtime*,
                                              quapi_msg_inner*);

quapi_preload_state
quapi_preload_state_func_to_state(quapi_preload_state_func f);

typedef struct quapi_runtime {
  fopen_t fopen;
  fclose_t fclose;
  read_t read;
  fread_t fread;
  fgetc_t fgetc;
  getc_t getc;
  getc_unlocked_t getc_unlocked;
  fgetc_unlocked_t fgetc_unlocked;
  gzdopen_t gzdopen;
  gzread_t gzread;
  gzclose_t gzclose;

  ZEROCOPY_PIPE_OR_FILE* in_stream;
  FILE* default_stdin;

  quapi_msg read_msg;
  quapi_msg_header_data header_data;
  quapi_msg* last_read_msg;

  quapi_preload_state_func state;

  char* outbuf;
  char outbuf_stack[64];
  char filler_clause[64];
  size_t filler_clause_len;
  ssize_t outbuf_len;
  ssize_t outbuf_written;
  pid_t solver_child_pid;
  int written_clauses;
  bool repeat_state;
  size_t quantifier_count;

  // Old stdout before fork or STDOUT in parent process. Always available for
  // writing messages.
  int old_stdout;
  bool initiated;
} quapi_runtime;

QUAPI_PRELOAD_NO_EXPORT extern quapi_runtime global_runtime;

void
quapi_runtime_init(quapi_runtime* runtime);

ssize_t
quapi_read(quapi_runtime* runtime, char* buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif
