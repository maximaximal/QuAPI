#define CATCH_CONFIG_EXPERIMENTAL_REDIRECT

#include "util.hpp"
#include <catch.hpp>
#include <cstdlib>
#include <quapi/quapi.h>

static bool cadical_tests_enabled = true;

#ifndef WITHOUT_PCRE2

TEST_CASE("cadical") {
  const char* spath = "/usr/local/bin/cadical";
  const char* argv[] = { "--quiet", NULL };

  if(!cadical_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("CaDiCaL not found in " + std::string(spath) +
         "! Cannot test quapi on cadical. Maybe some other test works.");
    cadical_tests_enabled = false;
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
                           "s SATISFIABLE",
                           "s UNSATISFIABLE"));
  REQUIRE(s.get());

  int32_t clauses[clausecount][3] = {
    { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 2, -3, 0 }
  };

  for(const auto& cl : clauses) {
    for(int32_t l : cl) {
      quapi_add(s.get(), l);
    }
  }

  quapi_assume(s.get(), 2);

  int r = 0;

  r = quapi_solve(s.get());
  REQUIRE(r == 10);

  quapi_assume(s.get(), 1);

  r = quapi_solve(s.get());
  REQUIRE(r == 10);
}

#endif

#ifndef WITHOUT_PCRE2

TEST_CASE("cadical with invalid REGEXes") {
  const char* spath = "/usr/local/bin/cadical";
  const char* argv[] = { "--quiet", NULL };

  if(!cadical_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("CaDiCaL not found in " + std::string(spath) +
         "! Cannot test quapi on cadical. Maybe some other test works.");
    cadical_tests_enabled = false;
    return;
  }

  // One more, as assumptions are later given as additional clauses after
  // forking.
  const int litcount = 4;
  const int clausecount = 5;

  QuAPISolver s(quapi_init(spath,
                           argv,
                           NULL,
                           litcount,
                           clausecount,
                           1,
                           "s SATISFIABLE -",
                           "s UNSATISFIABLE -"));
  REQUIRE(s.get());

  int32_t clauses[clausecount][3] = {
    { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 2, -3, 0 }, { 1, 2, 0 }
  };

  for(size_t i = 0; i < clausecount - 1; ++i) {
    const auto& cl = clauses[i];
    for(int32_t l : cl) {
      quapi_add(s.get(), l);
    }
  }

  for(int32_t l : clauses[clausecount - 1]) {
    quapi_assume(s.get(), l);
  }

  int r = 0;

  r = quapi_solve(s.get());
  REQUIRE(r == 0);
}

#endif

TEST_CASE("cadical without regex and just return code") {
  const char* spath = "/usr/local/bin/cadical";
  const char* argv[] = { "--quiet", NULL };

  if(!cadical_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("CaDiCaL not found in " + std::string(spath) +
         "! Cannot test quapi on cadical. Maybe some other test works.");
    cadical_tests_enabled = false;
    return;
  }

  // One more, as assumptions are later given as additional clauses after
  // forking.
  const int litcount = 4;
  const int clausecount = 4;

  QuAPISolver s(
    quapi_init(spath, argv, NULL, litcount, clausecount, 1, NULL, NULL));
  REQUIRE(s.get());

  int32_t clauses[clausecount][3] = {
    { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 2, -3, 0 }
  };

  for(const auto& cl : clauses) {
    for(int32_t l : cl) {
      quapi_add(s.get(), l);
    }
  }

  quapi_assume(s.get(), 2);

  int r = 0;

  r = quapi_solve(s.get());
  REQUIRE(r == 10);

  quapi_assume(s.get(), 1);

  r = quapi_solve(s.get());
  REQUIRE(r == 10);
}

TEST_CASE("cadical with unfinished assumptions") {
  const char* spath = "/usr/local/bin/cadical";
  const char* argv[] = { "--quiet", NULL };

  if(!cadical_tests_enabled)
    return;

  if(!file_exists(spath)) {
    WARN("CaDiCaL not found in " + std::string(spath) +
         "! Cannot test quapi on cadical. Maybe some other test works.");
    cadical_tests_enabled = false;
    return;
  }

  // One more, as assumptions are later given as additional clauses after
  // forking.
  const int litcount = 4;
  const int clausecount = 4;

  QuAPISolver s(
    quapi_init(spath, argv, NULL, litcount, clausecount, 1, NULL, NULL));
  REQUIRE(s.get());

  int32_t clauses[clausecount][3] = {
    { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 2, -3, 0 }
  };

  for(const auto& cl : clauses) {
    for(int32_t l : cl) {
      quapi_add(s.get(), l);
    }
  }

  int r = 0;

  r = quapi_solve(s.get());
  REQUIRE(r == 10);
}
