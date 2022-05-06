#ifndef UTILITIES_H
#define UTILITIES_H

#include <stdbool.h>

bool
kissat_has_suffix(const char* str, const char* suffix);

#define ABS(A) (assert((int)(A) != INT_MIN), (A) < 0 ? -(A) : (A))

#endif
