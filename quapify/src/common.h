#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define MAX_VARS ((1u << 31) - 1)

/* The parser parses into the functions defined in this file!
 */

/** @brief Add a quantifier from the optional prefix.
    @param lit Negative for existential, positive for universal
 */
void
add_quantifier(int lit);

/** @brief Add a literal.
    @param lit The literal that is added. 0 ends a clause.
 */
void
add_lit(int lit);

extern bool option_verbose;

void
message(const char* fmt, ...);

#endif
