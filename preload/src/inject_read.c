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

int
__uflow(FILE* file);

/* This read wrapper still has big issues! To also wrap java solvers, one needs
 * to better integrate with JNI. This could be extracted into it's own injector
 * that may be compiled into QuAPI if JNI is found on the build machine. */

static bool quapi_entry_fgetc = false;
static bool quapi_entry_fgetc_unlocked = false;
static bool quapi_entry_getc = false;
static bool quapi_entry_getc_unlocked = false;
static bool quapi_entry_read = false;
static bool quapi_entry_fread = false;
static bool quapi_entry___uflow = false;

#define xstr(a) str(a)
#define str(a) #a
#define QUAPI_CHECK_AND_REPORT_ENTRY(F)               \
  if(!quapi_entry_##F) {                              \
    quapi_entry_##F = true;                           \
    dbg("Entered preloaded runtime through " str(F)); \
  }

QUAPI_PRELOAD_EXPORT int
fgetc(FILE* stream) {
  if(stream == global_runtime.default_stdin) {
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
  if(stream == global_runtime.default_stdin) {
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
  if(stream == global_runtime.default_stdin) {
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
  if(stream == global_runtime.default_stdin) {
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
  if(stream == global_runtime.default_stdin || fileno(stream) == STDIN_FILENO)
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
