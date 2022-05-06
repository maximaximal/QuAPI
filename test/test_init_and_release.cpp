#include "catch.hpp"

#include <quapi/quapi.h>

TEST_CASE("init and release") {
  quapi_solver* solver =
    quapi_init(nullptr, NULL, NULL, 0, 0, 0, NULL, NULL);
  REQUIRE(!solver);
}
