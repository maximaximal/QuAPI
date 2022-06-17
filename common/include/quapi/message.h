#ifndef QUAPI_MESSAGE_H
#define QUAPI_MESSAGE_H

#include <quapi/definitions.h>
#include <quapi/zero-copy-pipes-linux.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

typedef enum quapi_msg_type {
  QUAPI_MSG_UNDEFINED,
  QUAPI_MSG_HEADER,
  QUAPI_MSG_QUANTIFIER,
  QUAPI_MSG_LITERAL,
  QUAPI_MSG_FORK,
  QUAPI_MSG_FORK_REPORT,
  QUAPI_MSG_STARTED,
  QUAPI_MSG_SOLVE,
  QUAPI_MSG_EXIT_CODE,
  QUAPI_MSG_DESTRUCTED,
} quapi_msg_type;

typedef uint8_t quapi_msg_type_packed;

bool
quapi_msg_is_known(quapi_msg_type t);

const char*
quapi_msg_type_str(quapi_msg_type t);

typedef struct quapi_msg_header {
  int32_t api_version;
} quapi_msg_header;

typedef struct quapi_msg_quantifier {
  int32_t lit;
} quapi_msg_quantifier;

typedef struct quapi_msg_lit {
  int32_t lit;
} quapi_msg_lit;

typedef struct quapi_msg_fork {
  int wait_for_exit_code_and_report : 1;
} quapi_msg_fork;

typedef struct quapi_msg_started {
  int32_t api_version;
} quapi_msg_started;

typedef struct quapi_msg_fork_report {
  pid_t solver_child_pid;
} quapi_msg_fork_report;

typedef struct quapi_msg_solve {
} quapi_msg_solve;

typedef struct quapi_msg_exit_code {
  int32_t exit_code;
} quapi_msg_exit_code;

typedef union quapi_msg_data {
  quapi_msg_header header;
  quapi_msg_quantifier quantifier;
  quapi_msg_lit literal;
  quapi_msg_fork fork;
  quapi_msg_fork_report fork_report;
  quapi_msg_started started;
  quapi_msg_solve solve;
  quapi_msg_exit_code exit_code;
} quapi_msg_data;

// Packed, so that only 5 bytes have to be communicated.
typedef struct __attribute__((__packed__)) quapi_msg_inner {
  quapi_msg_data data;
  quapi_msg_type_packed type;
} quapi_msg_inner;

typedef union quapi_msg {
  quapi_msg_inner msg;
  char arr[sizeof(quapi_msg_type_packed) + sizeof(quapi_msg_data)];
} quapi_msg;

/** @brief Write a message to a file descriptor.
 *
 * The hdata variable is generally not needed and can be set to NULL, except
 * when sending a header message.
 */
quapi_status
quapi_write_msg_to_fd(int fd, quapi_msg* msg, quapi_msg_header_data* hdata);

/** @brief Write a message to a file stream. Flushes automatically on SOLVE or
 * FORK.
 *
 * The hdata variable is generally not needed and can be set to NULL, except
 * when sending a header message.
 */
quapi_status
quapi_write_msg_to_file(ZEROCOPY_PIPE_OR_FILE* f,
                        quapi_msg* msg,
                        quapi_msg_header_data* hdata);

/** @brief Read a message from a file descriptor
 */
bool
quapi_read_msg_from_fd(int fd,
                       quapi_msg* msg,
                       quapi_msg_header_data* hdata,
                       read_t read);

/** @brief Read a message from a file stream
 */
#ifdef USING_ZEROCOPY
quapi_msg*
quapi_read_msg_from_file(ZEROCOPY_PIPE_OR_FILE* f,
                         quapi_msg_header_data* hdata,
                         fread_t fread);
#else
bool
quapi_read_msg_from_file(ZEROCOPY_PIPE_OR_FILE* f,
                         quapi_msg* msg,
                         quapi_msg_header_data* hdata,
                         fread_t fread);
#endif

#ifdef __cplusplus
}
#endif

#endif
