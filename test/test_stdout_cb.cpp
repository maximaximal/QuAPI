#include "catch.hpp"

#include <quapi/quapi.h>

TEST_CASE("stdout callback function", "[cb]") {
  const char* argv[] = {
    "bash",
    "-c",
    "while IFS='$\\n' read -r line; do :; done; echo test",
    NULL
  };

  bool stdout_cb_opened = false;

  QuAPISolver s(quapi_init("bash", argv, NULL, 1, 1, 1, NULL, NULL));
  REQUIRE(s.get());

  quapi_add(s.get(), 1);
  quapi_add(s.get(), 0);

  quapi_set_stdout_cb(
    s.get(),
    [](const char* line, void* userdata) {
      bool& b = *static_cast<bool*>(userdata);
      REQUIRE(std::string(line) == "test");
      b = true;
      return 1;
    },
    &stdout_cb_opened);

  quapi_assume(s.get(), 1);

  int retcode = quapi_solve(s.get());

  REQUIRE(retcode == 1);
  REQUIRE(stdout_cb_opened);
}
