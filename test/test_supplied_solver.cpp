#include "util.hpp"
#include <catch.hpp>
#include <cstdlib>
#include <quapi/quapi.h>

TEST_CASE("User-Supplied SAT solver from env SOLVER_SAT", "[user]") {
  const char* path = std::getenv("SOLVER_SAT");
  if(!path)
    return;

  REQUIRE(file_exists(path));

  QuAPISolver S(quapi_init(path, NULL, NULL, 2, 2, 1, NULL, NULL));
  quapi_solver* s = S.get();

  // Code from the example without quantify.

  quapi_add(s, 1), quapi_add(s, -2), quapi_add(s, 0);
  quapi_add(s, -1), quapi_add(s, 2), quapi_add(s, 0);
  quapi_assume(s, 1);
  int status = quapi_solve(s);
  REQUIRE(status == 10);
  quapi_assume(s, -1);
  status = quapi_solve(s);
  REQUIRE(status == 10);
}

TEST_CASE("User-Supplied QBF solver from env SOLVER_QBF", "[user]") {
  const char* path = std::getenv("SOLVER_QBF");
  if(!path)
    return;

  REQUIRE(file_exists(path));

  QuAPISolver S(quapi_init(path, NULL, NULL, 2, 2, 1, NULL, NULL));
  quapi_solver* s = S.get();

  // Code from the example.

  quapi_quantify(s, -1);
  quapi_quantify(s, 2);
  quapi_add(s, 1), quapi_add(s, -2), quapi_add(s, 0);
  quapi_add(s, -1), quapi_add(s, 2), quapi_add(s, 0);
  quapi_assume(s, 1);
  int status = quapi_solve(s);
  REQUIRE(status == 10);
  quapi_assume(s, -1);
  status = quapi_solve(s);
  REQUIRE(status == 10);
}
