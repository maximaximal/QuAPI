#ifndef QUAPI_ZERO_COPY_LINUX_H
#define QUAPI_ZERO_COPY_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/* This optimization is based on this article:
 * https://mazzo.li/posts/fast-pipes.html
 *
 * It replaces FILE and fwrite with a custom implementation based on zero-copy
 * splicing into and out of buffers.
 */

#if defined(__linux__) && defined(QUAPI_USE_ZEROCOPY_IF_AVAILABLE)
#define USING_ZEROCOPY

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct quapi_zerocopy_pipe quapi_zerocopy_pipe;

quapi_zerocopy_pipe*
quapi_zerocopy_pipe_fdopen(int fd, const char* mode);

void
quapi_zerocopy_pipe_close(quapi_zerocopy_pipe* pipe);

ssize_t
quapi_zerocopy_pipe_write(const void* data,
                          size_t size,
                          size_t count,
                          quapi_zerocopy_pipe* pipe);

void*
quapi_zerocopy_pipe_prepare_write(size_t size,
                                  size_t count,
                                  quapi_zerocopy_pipe* pipe);

void
quapi_zerocopy_pipe_flush(quapi_zerocopy_pipe* pipe);

void*
quapi_zerocopy_pipe_read(size_t size, quapi_zerocopy_pipe* pipe);

#define QUAPI_GIVE_MSGS(NAME, COUNT, PIPE) \
  quapi_msg* NAME =                        \
    quapi_zerocopy_pipe_prepare_write(sizeof(quapi_msg), COUNT, PIPE);

#define ZEROCOPY_PIPE_OR_FILE quapi_zerocopy_pipe

#else

#define QUAPI_GIVE_MSGS(NAME, COUNT, IGNORED) \
  quapi_msg NAME##_[COUNT];                   \
  quapi_msg* NAME = &NAME##_[0];

#define ZEROCOPY_PIPE_OR_FILE FILE
#endif

#ifdef __cplusplus
}
#endif

#endif
