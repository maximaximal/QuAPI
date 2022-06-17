#define _GNU_SOURCE

#include "../include/quapi/zero-copy-pipes-linux.h"
#include "../include/quapi/definitions.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/kernel-page-flags.h>
#include <sys/mman.h>

static const size_t buf_size = 1 << 16;

typedef struct quapi_zerocopy_pipe {
  int fd;
  int current_read_memfd;
  int current_buf;
  char* buf[2];
  void* prepared;
  bool read;
  size_t read_pos;
} quapi_zerocopy_pipe;

static bool
mode_contains_read(const char* mode) {
  size_t len = strlen(mode);
  for(size_t i = 0; i < len; ++i)
    if(mode[i] == 'r')
      return true;
  return false;
}

quapi_zerocopy_pipe*
quapi_zerocopy_pipe_fdopen(int fd, const char* mode) {
  quapi_zerocopy_pipe* p = calloc(1, sizeof(quapi_zerocopy_pipe));
  if(!p) {
    err("calloc for new zerocopy pipe returned NULL!");
    return NULL;
  }

  p->read = mode_contains_read(mode);
  p->fd = fd;

  if(p->read) {
    p->current_read_memfd = memfd_create("quapi_zerocopy_pipe_fd", 0);
    if(p->current_read_memfd == -1) {
      err("memfd_create failed: %s", strerror(errno));
      goto ERROR;
    }
    // The file should be emptied, otherwise splice
    size_t written = 0;
    ssize_t ret = write(p->current_read_memfd, &written, sizeof(written));
    if(ret == -1) {
      err("write failed: %s", strerror(errno));
      goto ERROR;
    }
    p->buf[0] =
      mmap(NULL, buf_size, PROT_READ, MAP_SHARED, p->current_read_memfd, 0);
    if(!p->buf[0]) {
      err("mmap failed: %s", strerror(errno));
      goto ERROR;
    }
  } else {
    p->buf[0] = aligned_alloc(buf_size, buf_size);
    if(!p->buf[0])
      err("aligned_alloc(%zu, %zu) failed for buffer 0", buf_size, buf_size);
    p->buf[1] = aligned_alloc(buf_size, buf_size);
    if(!p->buf[1])
      err("aligned_alloc(%zu, %zu) failed for buffer 0", buf_size, buf_size);

    // Have the pages be huge to apparently gain some performance.
    madvise(p->buf[0], buf_size, MADV_HUGEPAGE);
    madvise(p->buf[1], buf_size, MADV_HUGEPAGE);

    // Written must be initialized to be 0!
    *((size_t*)&p->buf[0][0]) = 0;
    *((size_t*)&p->buf[1][0]) = 0;

    if(!p->buf[0] || !p->buf[1]) {
      goto ERROR;
    }
  }

  return p;
ERROR:
#ifdef USING_ZEROCOPY
  quapi_zerocopy_pipe_close(p);
#endif

  return NULL;
}

void
quapi_zerocopy_pipe_close(quapi_zerocopy_pipe* pipe) {
  if(!pipe)
    return;

  if(pipe->read) {
    if(pipe->buf[0]) {
      int res = munmap(pipe->buf[0], buf_size);
      if(res == -1) {
        err("munmap failed: %s", strerror(errno));
      }
      pipe->buf[0] = NULL;
    }

    if(pipe->current_read_memfd) {
      close(pipe->current_read_memfd);
      pipe->current_read_memfd = 0;
    }
  } else {
    if(pipe->buf[0])
      free(pipe->buf[0]);
    if(pipe->buf[1])
      free(pipe->buf[1]);
  }

  free(pipe);
}

static ssize_t
flush_out(quapi_zerocopy_pipe* p) {
  assert(!p->read);
  dbg("Doing flush out");

  // https://github.com/bitonic/pipes-speed-test/blob/master/write.cpp#L42
  struct pollfd pollfd = { .fd = p->fd, .events = POLLOUT | POLLWRBAND };

  struct iovec bufvec = { .iov_base = p->buf[p->current_buf],
                          .iov_len = buf_size };

  size_t written = *((size_t*)&p->buf[p->current_buf][0]);

  if(written > buf_size) {
    err("Written > buf_size when flushing!");
    exit(EXIT_FAILURE);
  }

  while(bufvec.iov_len > 0) {
    poll(&pollfd, 1, -1);
    ssize_t ret = vmsplice(p->fd, &bufvec, 1, SPLICE_F_GIFT);
    if(ret < 0 && errno == EPIPE) {
      err("EPIPE error! Could not write.");
      return -2;
    }
    if(ret < 0 && errno == EAGAIN) {
      continue;
    }
    if(ret < 0) {
      err("vmsplice failed: %s", strerror(errno));
      return -1;
    }
    bufvec.iov_base = (void*)(((char*)bufvec.iov_base) + ret);
    bufvec.iov_len -= ret;
  }

  trc("Flushed %zu bytes from buffer %d into zerocopy pipe using vmsplice. "
      "Buffer contained "
      "%zu written bytes.",
      buf_size,
      p->current_buf,
      written);

  {
    // Reset the written counter to 0 in the new current buffer.
    p->current_buf = (p->current_buf + 1) % 2;
    size_t* written = (size_t*)&p->buf[p->current_buf][0];
    *written = 0;
  }

  return buf_size;
}

void
quapi_zerocopy_pipe_flush(quapi_zerocopy_pipe* pipe) {
  size_t* written = (size_t*)&pipe->buf[pipe->current_buf][0];
  if(*written > 0)
    flush_out(pipe);
}

static bool
ensure_space_free(size_t effective_size,
                  char** current_buf,
                  size_t** written,
                  quapi_zerocopy_pipe* pipe) {
  if(**written + effective_size + sizeof(*written) >= buf_size) {
    ssize_t s = flush_out(pipe);
    if(s != buf_size)
      return false;

    *current_buf = pipe->buf[pipe->current_buf];
    *written = (size_t*)&(*current_buf)[0];
    *current_buf += sizeof(*written);
  }

  return true;
}

ssize_t
quapi_zerocopy_pipe_write(const void* data,
                          size_t size,
                          size_t count,
                          quapi_zerocopy_pipe* pipe) {
  size_t effective_size = size * count;
  char* current_buf = pipe->buf[pipe->current_buf];
  size_t* written = (size_t*)&current_buf[0];
  current_buf += sizeof(written);

  if(!ensure_space_free(effective_size, &current_buf, &written, pipe))
    return -1;

  trc("Zero Copy Pipe Write: Written: %zu, Additional Effective Size: %zu, "
      "Buffer: %d",
      *written,
      effective_size,
      pipe->current_buf);

  if(data == pipe->prepared) {
    trc("Message of effective size %zu being written from already prepared "
        "memory %p.",
        effective_size,
        data);
    pipe->prepared = ((char*)pipe->prepared) + effective_size;
  } else {
    trc("Message of effective size %zu being written was not prepared! Address "
        "of data: %p and of prepared %p.",
        effective_size,
        data,
        pipe->prepared);
    memcpy(current_buf + *written, data, effective_size);
  }
  *written += effective_size;

  return count;
}

void*
quapi_zerocopy_pipe_prepare_write(size_t size,
                                  size_t count,
                                  quapi_zerocopy_pipe* pipe) {
  size_t effective_size = size * count;
  char* current_buf = pipe->buf[pipe->current_buf];
  size_t* written = (size_t*)&current_buf[0];
  current_buf += sizeof(written);

  if(!ensure_space_free(effective_size, &current_buf, &written, pipe))
    return NULL;

  pipe->prepared = current_buf += *written;
  trc(
    "Prepare memory %p of effective size %zu", pipe->prepared, effective_size);
  return pipe->prepared;
}

static bool
flush_read(quapi_zerocopy_pipe* pipe) {
  // From
  // https://github.com/bitonic/pipes-speed-test/blob/master/read.cpp#L42
  assert(pipe->current_read_memfd > 0);
  assert(pipe->buf[0]);

  struct pollfd pollfd;
  pollfd.fd = pipe->fd;
  pollfd.events = POLLIN | POLLPRI;

  poll(&pollfd, 1, -1);

  while(true) {
    off64_t offout = 0;
    ftruncate(pipe->current_read_memfd, 0);
    ssize_t ret = splice(pipe->fd,
                         NULL,
                         pipe->current_read_memfd,
                         &offout,
                         buf_size,
                         SPLICE_F_MOVE);

    if(ret < 0 && errno == EAGAIN) {
      continue;
    }

    if(ret < 0) {
      err("splice from fd %d to fd %d failed: %s",
          pipe->fd,
          pipe->current_read_memfd,
          strerror(errno));
      return false;
    }

    if(offout == 0)
      return false;

    break;
  }

  size_t* written = (size_t*)&pipe->buf[0][0];
  trc("Spliced a full %zu B buffer from fd %d into buffer for reading. Buffer "
      "contains %zu written bytes.",
      buf_size,
      pipe->fd,
      *written);

  return true;
}

void*
quapi_zerocopy_pipe_read(size_t size, quapi_zerocopy_pipe* pipe) {
  assert(pipe->read);

  size_t* written = (size_t*)&pipe->buf[0][0];

  trc("Currently, written in read end contains %zu and requesting %zu bytes, "
      "current read pos is %zu",
      *written,
      size,
      pipe->read_pos);

  char* buf = &pipe->buf[0][sizeof(size_t)];
  if(pipe->read_pos + size > *written) {
    assert(pipe->read_pos - *written == 0);
    if(!flush_read(pipe)) {
      return NULL;
    }
    written = (size_t*)&pipe->buf[0][0];
    buf = pipe->buf[0] + sizeof(size_t);
    pipe->read_pos = 0;

    if(*written > buf_size) {
      err("Buffer indicates more written bytes than the buffer is large! This "
          "is a programming error.");
      exit(EXIT_FAILURE);
    }
  }
  void* data = buf + pipe->read_pos;
  size_t old_read_pos = pipe->read_pos;
  pipe->read_pos += size;
  return data;
}
