#include "util.hpp"

#include <sys/stat.h>

bool
file_exists(const char* path) {
  struct stat buffer;
  return (stat(path, &buffer) == 0);
}
