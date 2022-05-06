#include "catch.hpp"

#include <quapi/quapi.h>

// Just a basic test to get the testing infrastructure going.
TEST_CASE("signature") {
  REQUIRE(quapi_signature() == std::string_view("QuAPI"));
}
