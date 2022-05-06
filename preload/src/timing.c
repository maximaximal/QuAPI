#include <quapi/definitions.h>
#include <quapi/timing.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool enabled = false;

static struct timespec time_construct;
static struct timespec time_firstread;
static struct timespec time_header;
static struct timespec time_msgafterheader;

static int64_t
timespec_to_nanos(struct timespec* t) {
  int64_t n = t->tv_nsec;
  int64_t sec = t->tv_sec;
  sec *= 1000000000;// Seconds to nanos
  return n + sec;
}

static void
gettime_into(const char* name, struct timespec* target) {
  int s = clock_gettime(CLOCK_MONOTONIC, target);
  if(s != 0) {
    err("clock_gettime(CLOCK_MONOTONIC, &%s) failed! Error: %s",
        name,
        strerror(errno));
  }
}

static void
end_timing() {
  int64_t tconstruct = timespec_to_nanos(&time_construct),
          tfirstread = timespec_to_nanos(&time_firstread),
          theader = timespec_to_nanos(&time_header),
          tmsgafterheader = timespec_to_nanos(&time_msgafterheader);

  int64_t dconstructtofirstread = tfirstread - tconstruct;
  int64_t dfirstreadtoheader = theader - tfirstread;
  int64_t dheadertoafterheader = tmsgafterheader - theader;

  fprintf(stderr,
          "[QuAPI] [Timing] %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
          " %" PRId64 " %" PRId64 " %" PRId64 "\n",
          tconstruct,
          tfirstread,
          theader,
          tmsgafterheader,
          dconstructtofirstread,
          dfirstreadtoheader,
          dheadertoafterheader);
}

#define xstr(a) str(a)
#define str(a) #a
#define GETTIME_INTO(V) gettime_into(str(V), &V)

void
quapi_timing_construct() {
  const char* quapi_timing = getenv("QUAPI_TIMING");
  if(quapi_timing) {
    enabled = true;
  }

  if(!enabled)
    return;

  gettime_into("time_construct", &time_construct);
  GETTIME_INTO(time_construct);
}

void
quapi_timing_firstread() {
  if(!enabled)
    return;

  GETTIME_INTO(time_firstread);
}

void
quapi_timing_header() {
  if(!enabled)
    return;

  GETTIME_INTO(time_header);
}

void
quapi_timing_msg_after_header() {
  if(!enabled)
    return;

  GETTIME_INTO(time_msgafterheader);

  end_timing();
}

#undef GETTIME_INFO
#undef xstr
#undef str
