#include "catch.hpp"

#include <chrono>
#include <thread>

#include "quapi/definitions.h"
#include <quapi/quapi.h>

/* This test is very interesting, as it allows better debugging into the
 * printing process.
 */
TEST_CASE("abort a solver") {
  const char* argv[] = { "bash",
                         "-c",
                         "while read line; do echo \"Solver Output\"; done < "
                         "\"${1:-/dev/stdin}\"; sleep 10;",
                         NULL };

  QuAPISolver s(quapi_init("bash", argv, NULL, 3, 1, 1, NULL, NULL));
  REQUIRE(s.get());

  quapi_quantify(s.get(), 1);
  quapi_quantify(s.get(), -2);

  quapi_add(s.get(), 1);
  quapi_add(s.get(), 2);
  quapi_add(s.get(), 0);

  quapi_assume(s.get(), 3);

  std::thread abortThread([&] {
    // Wait until the solver is working.
    while(quapi_get_state(s.get()) != QUAPI_WORKING) {
    }
    quapi_terminate(s.get());
  });

  quapi_solve(s.get());

  abortThread.join();

  REQUIRE(quapi_get_state(s.get()) == QUAPI_INPUT_LITERALS);
}
