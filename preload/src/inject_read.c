#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <quapi/runtime.h>

#include <quapi_preload_export.h>

#ifdef read
#undef read
#endif

#ifdef getc
#undef getc
#endif

#ifdef fgetc
#undef fgetc
#endif

#ifdef getc_unlocked
#undef getc_unlocked
#endif

#ifdef fgetc_unlocked
#undef fgetc_unlocked
#endif

// ZLib interfacing types

int
__uflow(FILE* file);

/* This read wrapper still has big issues! To also wrap java solvers, one needs
 * to better integrate with JNI. This could be extracted into it's own injector
 * that may be compiled into QuAPI if JNI is found on the build machine. */

static bool quapi_entry_fopen = false;
static bool quapi_entry_fclose = false;
static bool quapi_entry_fgetc = false;
static bool quapi_entry_fgetc_unlocked = false;
static bool quapi_entry_getc = false;
static bool quapi_entry_getc_unlocked = false;
static bool quapi_entry_read = false;
static bool quapi_entry_fread = false;
static bool quapi_entry_gzread = false;
static bool quapi_entry_gzdopen = false;
static bool quapi_entry_gzclose = false;
static bool quapi_entry___uflow = false;

extern bool quapi_runtime_read_detected;

static FILE* quapi_dev_stdin_mock = NULL;

#define xstr(a) str(a)
#define str(a) #a
#define QUAPI_CHECK_AND_REPORT_ENTRY(F)               \
  if(!quapi_entry_##F) {                              \
    quapi_entry_##F = true;                           \
    quapi_runtime_read_detected = true;               \
    dbg("Entered preloaded runtime through " str(F)); \
  }

static struct gzFile_s* quapi_gzFile_stdin = NULL;
QUAPI_PRELOAD_EXPORT
struct gzFile_s*
gzdopen(int fd, const char* mode) {
  QUAPI_CHECK_AND_REPORT_ENTRY(gzdopen)
  if(fd == STDIN_FILENO) {
    quapi_gzFile_stdin = (struct gzFile_s*)1;
    return quapi_gzFile_stdin;
  }
  return global_runtime.gzdopen(fd, mode);
}

QUAPI_PRELOAD_EXPORT
int
gzread(struct gzFile_s* f, char* data, unsigned int size) {
  QUAPI_CHECK_AND_REPORT_ENTRY(gzread)
  if(f == quapi_gzFile_stdin) {
    return quapi_read(&global_runtime, data, size);
  } else {
    return global_runtime.gzread(f, data, size);
  }
}

QUAPI_PRELOAD_EXPORT
int
gzclose(struct gzFile_s* f) {
  QUAPI_CHECK_AND_REPORT_ENTRY(gzclose)
  if(f == quapi_gzFile_stdin) {
    return 0;
  } else {
    return global_runtime.gzclose(f);
  }
}

QUAPI_PRELOAD_EXPORT
FILE*
fopen(const char* path, const char* mode) {
  QUAPI_CHECK_AND_REPORT_ENTRY(fopen)
  if(strcmp(path, "/dev/stdin") == 0 && !quapi_dev_stdin_mock) {
    FILE* mock = calloc(1, sizeof(FILE));
    quapi_dev_stdin_mock = mock;
    return mock;
  }
  return global_runtime.fopen(path, mode);
}

QUAPI_PRELOAD_EXPORT
int
fclose(FILE* f) {
  QUAPI_CHECK_AND_REPORT_ENTRY(fclose)
  if(f == quapi_dev_stdin_mock) {
    quapi_dev_stdin_mock = NULL;
    free(f);
    return 0;
  }
  return global_runtime.fclose(f);
}

QUAPI_PRELOAD_EXPORT int
fgetc(FILE* stream) {
  if(stream == global_runtime.default_stdin || stream == quapi_dev_stdin_mock) {
    QUAPI_CHECK_AND_REPORT_ENTRY(fgetc)
    return (stream)->_IO_read_ptr >= (stream)->_IO_read_end
             ? __uflow(stream)
             : *(unsigned char*)(stream)->_IO_read_ptr++;
  } else {
    assert(global_runtime.fgetc);
    return global_runtime.fgetc(stream);
  }
}

QUAPI_PRELOAD_EXPORT int
fgetc_unlocked(FILE* stream) {
  if(stream == global_runtime.default_stdin || stream == quapi_dev_stdin_mock) {
    QUAPI_CHECK_AND_REPORT_ENTRY(fgetc_unlocked)
    return (stream)->_IO_read_ptr >= (stream)->_IO_read_end
             ? __uflow(stream)
             : *(unsigned char*)(stream)->_IO_read_ptr++;
  } else {
    assert(global_runtime.fgetc_unlocked);
    return global_runtime.fgetc_unlocked(stream);
  }
}

QUAPI_PRELOAD_EXPORT int
getc(FILE* stream) {
  if(stream == global_runtime.default_stdin || stream == quapi_dev_stdin_mock) {
    QUAPI_CHECK_AND_REPORT_ENTRY(getc)
    return (stream)->_IO_read_ptr >= (stream)->_IO_read_end
             ? __uflow(stream)
             : *(unsigned char*)(stream)->_IO_read_ptr++;
  } else {
    assert(global_runtime.getc);
    return global_runtime.getc(stream);
  }
}

QUAPI_PRELOAD_EXPORT int
getc_unlocked(FILE* stream) {
  if(stream == global_runtime.default_stdin || stream == quapi_dev_stdin_mock) {
    QUAPI_CHECK_AND_REPORT_ENTRY(getc_unlocked)
    return (stream)->_IO_read_ptr >= (stream)->_IO_read_end
             ? __uflow(stream)
             : *(unsigned char*)(stream)->_IO_read_ptr++;
  } else {
    assert(global_runtime.getc_unlocked);
    return global_runtime.getc_unlocked(stream);
  }
}

QUAPI_PRELOAD_EXPORT ssize_t
read(int fd, void* data, size_t size) {
  if(fd == STDIN_FILENO) {
    QUAPI_CHECK_AND_REPORT_ENTRY(read)
    return quapi_read(&global_runtime, data, size);
  } else {
    assert(global_runtime.read);
    return global_runtime.read(fd, data, size);
  }
}

QUAPI_PRELOAD_EXPORT
size_t
fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
  QUAPI_CHECK_AND_REPORT_ENTRY(fread)
  if(stream == global_runtime.default_stdin || fileno(stream) == STDIN_FILENO ||
     stream == quapi_dev_stdin_mock)
    return read(STDIN_FILENO, ptr, size * nmemb);
  else
    return global_runtime.fread(ptr, size, nmemb, stream);
}

#ifdef __linux__
/*
getc_unlocked uses uflow like this:

#define __getc_unlocked_body(_fp)                               \
  (__glibc_unlikely((_fp)->_IO_read_ptr >= (_fp)->_IO_read_end) \
     ? __uflow(_fp)                                             \
     : *(unsigned char*)(_fp)->_IO_read_ptr++)
*/

// Body of getc_unlocked that is normally inlined must also be handled.
// https://code.woboq.org/userspace/glibc/libio/bits/types/struct_FILE.h.html#102
// https://code.woboq.org/userspace/glibc/libio/genops.c.html#__uflow
QUAPI_PRELOAD_EXPORT int
__uflow(FILE* file) {
  QUAPI_CHECK_AND_REPORT_ENTRY(__uflow)
  if(!file->_IO_read_base) {
    file->_IO_read_base = malloc(64);
  }
  ssize_t read = quapi_read(&global_runtime, file->_IO_read_base, 64);
  if(read == 0) {
    // Read has ended! Give EOF.
    free(file->_IO_read_base);
    file->_IO_read_base = NULL;

    // Unset the environment, so that spawned processes don't read QuAPI again.
    unsetenv("LD_PRELOAD");

    return EOF;
  }
  file->_IO_read_ptr = file->_IO_read_base;
  file->_IO_read_end = file->_IO_read_base + read;
  return *(file->_IO_read_ptr++);
}
#endif

#undef QUAPI_CHECK_AND_REPORT_ENTRY
#undef str
#undef xstr
