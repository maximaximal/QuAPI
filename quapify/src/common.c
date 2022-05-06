#include "common.h"
#include "stdarg.h"
#include "stdio.h"

static void
print_message(const char* fmt, va_list* ap) {
  fputs("c ", stderr);
  vfprintf(stderr, fmt, *ap);
  fputc('\n', stderr);
  fflush(stderr);
}

void
message(const char* fmt, ...) {
  if(!option_verbose)
    return;
  va_list ap;
  va_start(ap, fmt);
  print_message(fmt, &ap);
  va_end(ap);
}
