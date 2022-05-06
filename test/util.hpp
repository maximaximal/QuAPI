#pragma once

#include <functional>
#include <string_view>

bool
file_exists(const char* path);

struct quapi_solver;

struct FillerAndExpected {
  using Filler = std::function<void(quapi_solver*)>;
  FillerAndExpected(std::string_view name,
                    Filler f,
                    int literals,
                    int clauses,
                    int prefixdepth,
                    std::string_view e)
    : name(name)
    , filler(f)
    , literals(literals)
    , clauses(clauses)
    , prefixdepth(prefixdepth)
    , expected(e) {}

  std::string_view name;
  Filler filler;
  int literals, clauses, prefixdepth;
  std::string_view expected;
};
