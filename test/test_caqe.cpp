#define CATCH_CONFIG_EXPERIMENTAL_REDIRECT

#include "util.hpp"
#include <catch.hpp>
#include <quapi/quapi.h>
#include <cstdlib>

static bool caqe_tests_enabled = true;

#ifndef WITHOUT_PCRE2

TEST_CASE("caqe") {
  const char* spath = "/usr/local/bin/caqe";
  const char* argv[] = { NULL };

  if(!caqe_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("Caqe not found in " + std::string(spath) +
         "! Cannot test quapi on caqe. Maybe some other test works.");
    caqe_tests_enabled = false;
    return;
  }

  // One more, as assumptions are later given as additional clauses after
  // forking.
  const int litcount = 4;
  const int clausecount = 4;

  QuAPISolver s(quapi_init(spath,
                           argv,
                           NULL,
                           litcount,
                           clausecount,
                           1,
                           "^c Satisfiable",
                           "^c Unsatisfiable"));
  REQUIRE(s.get());

  int32_t clauses[clausecount][3] = {
    { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 2, -3, 0 }
  };

  quapi_quantify(s.get(), 1);

  for(const auto& cl : clauses) {
    for(int32_t l : cl) {
      quapi_add(s.get(), l);
    }
  }

  quapi_assume(s.get(), 1);

  int r = quapi_solve(s.get());
  REQUIRE(r == 10);

  quapi_assume(s.get(), 2);

  r = quapi_solve(s.get());
  REQUIRE(r == 10);
}

TEST_CASE("caqe with alternating prefix") {
  const char* spath = "/usr/local/bin/caqe";
  const char* argv[] = { NULL };

  if(!caqe_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("Caqe not found in " + std::string(spath) +
         "! Cannot test quapi on caqe. Maybe some other test works.");
    caqe_tests_enabled = false;
    return;
  }

  QuAPISolver s(quapi_init(
    spath, argv, NULL, 3, 3, 2, "^c Satisfiable", "^c Unsatisfiable"));
  quapi_solver* S = s.get();

  REQUIRE(S);

  // Exists 1
  quapi_quantify(S, 1);
  // Forall 2
  quapi_quantify(S, -2);
  // Exists 3
  quapi_quantify(S, 3);

  quapi_add(S, 1);
  quapi_add(S, 0);

  quapi_add(S, 2);
  quapi_add(S, 3);
  quapi_add(S, 0);

  quapi_add(S, -2);
  quapi_add(S, 3);
  quapi_add(S, 0);

  quapi_assume(S, 1);
  quapi_assume(S, 2);

  int r = quapi_solve(S);
  REQUIRE(r == 10);

  quapi_assume(S, 1);
  quapi_assume(S, -2);

  r = quapi_solve(S);
  REQUIRE(r == 10);

  quapi_assume(S, -1);
  quapi_assume(S, -2);

  r = quapi_solve(S);
  REQUIRE(r == 20);
}

#endif
