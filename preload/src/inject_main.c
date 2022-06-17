#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>

#include "quapi/definitions.h"
#include <quapi/message.h>
#include <quapi/runtime.h>

#include <quapi_preload_export.h>

static bool quapi_runtime_initiated = false;

bool quapi_runtime_send_destructed_msg = true;
bool quapi_runtime_read_detected = false;

QUAPI_PRELOAD_EXPORT void
on_constructed(void) __attribute__((constructor));

QUAPI_PRELOAD_EXPORT void
on_constructed(void) {
#ifdef __linux__
  if(quapi_check_debug()) {
    char path[512];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path));
    if(len > 0) {
      size_t end = MIN(len, sizeof(path) - 1);
      path[end] = '\0';
      dbg("Initiating from executable \"%s\"", path);
    }
  }
#endif

  if(!quapi_runtime_initiated) {
    quapi_runtime_initiated = true;
    quapi_runtime_init(&global_runtime);
  }
}

QUAPI_PRELOAD_EXPORT void
on_destructed(void) __attribute__((destructor));

QUAPI_PRELOAD_EXPORT void
on_destructed(void) {
  if(!quapi_runtime_read_detected) {
    err("No supported read call was overriden or no read(STDIN) was ever "
        "called! Check how the solver reads its input.");
    return;
  }

#ifdef USING_ZEROCOPY
  if(global_runtime.in_stream) {
    quapi_zerocopy_pipe_close(global_runtime.in_stream);
    global_runtime.in_stream = NULL;
  }
#endif

  // This message can be turned off when there is no REGEX to match against. In
  // that case, the PID is waited on using waitpid() and an
  // QUAPI_MSG_EXIT_CODE message is sent to the grandparent of this process
  // by its parent.
  if(quapi_runtime_send_destructed_msg) {
    quapi_msg destructed_msg = { .msg.type = QUAPI_MSG_DESTRUCTED };
    quapi_write_msg_to_fd(global_runtime.header_data.message_to_parent_pipe[1],
                          &destructed_msg,
                          NULL);
  }
}
