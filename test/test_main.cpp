#define CATCH_CONFIG_EXPERIMENTAL_REDIRECT
#define CATCH_CONFIG_RUNNER

#include "catch.hpp"

int
main(int argc, char* argv[]) {
  Catch::Session session;

  bool debug = false;
  bool trace = false;

  using namespace Catch::clara;
  auto cli =
    session.cli() |
    Opt(debug)["--debug"]// the option names it will respond to
    ("Activate QUAPI_DEBUG envvar") |
    Opt(trace)["--trace"]("Activate QUAPI_TRACE envvar");// description string
                                                         // for the help output

  session.cli(cli);

  int returnCode = session.applyCommandLine(argc, argv);
  if(returnCode != 0)// Indicates a command line error
    return returnCode;

  if(debug)
    setenv("QUAPI_DEBUG", "1", 1);
  if(trace)
    setenv("QUAPI_TRACE", "1", 1);

  return session.run();
}
