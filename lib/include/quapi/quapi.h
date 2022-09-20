#ifndef QUAPI_H
#define QUAPI_H

#include <quapi/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Return the name and the version of the incremental SAT solving library.
 */
const char*
quapi_signature();

/**
 * Construct a new solver and return a pointer to it. Use the returned pointer
 * as the first parameter in each of the following functions.
 *
 * Required state: N/A
 * State after: INPUT
 */
quapi_solver*
quapi_init(const char* path,
           const char** argv,
           const char** envp,
           int litcount,
           int clausecount,
           int prefixdepth,
           const char* SAT_regex,
           const char* UNSAT_regex);

/**
 * Release the solver, i.e., all its resoruces and allocated memory
 * (destructor). The solver pointer cannot be used for any purposes after this
 * call.
 *
 * Required state: INPUT or INPUT_LITERALS or SAT or UNSAT
 * State after: undefined
 */
void
quapi_release(quapi_solver* solver);

/**
 * Add the given literal into the currently added clause or finalize the clause
 * with a 0.
 *
 * Required state: INPUT or INPUT_LITERALS
 * State after: INPUT_LITERALS
 *
 * Literals are encoded as (non-zero) integers as in the DIMACS formats. They
 * have to be smaller or equal to INT32_MAX and strictly larger than INT32_MIN
 * (to avoid negation overflow). This applies to all the literal arguments in
 * API functions.
 */
void
quapi_add(quapi_solver* solver, int32_t lit_or_zero);

/** Extend the quantifier with the given literal. Positive literals are
 * existentially quantified, negative literals are universally quantified.
 *
 * Required state: INPUT
 * State after: INPUT
 */
void
quapi_quantify(quapi_solver* solver, int32_t lit_or_zero);

/**
 * Add an assumption for the next SAT search (the next call of quapi_solve).
 * After calling quapi_solve all the previously added assumptions are cleared.
 *
 * Internally appends another unit clause to the formula.
 *
 * Required state: INPUT_LITERALS | INPUT_ASSUMPTIONS
 * State after: INPUT_ASSUMPTIONS
 *
 * Returns false if some error happened.
 */
bool
quapi_assume(quapi_solver* solver, int32_t lit_or_zero);

/**
 * Solve the formula with specified clauses under the specified assumptions. If
 * the formula is satisfiable the function returns 10 and the state of the
 * solver is reset to INPUT_LITERALS. If the formula is unsatisfiable the
 * function returns 20 and the state of the solver is changed to INPUT_LITERALS.
 * If the search is interrupted the function returns 0 and the state of the
 * solver is changed to INPUT_LITERALS. This function can be called in any
 * defined state of the solver. Note that the state of the solver _during_
 * execution of 'quapi_solve' is WORKING.
 *
 * If no assumptions have been applied prior to calling solve, a fork is
 * executed to be able to re-use the same solver later. This is similar to
 * having an empty assumption set.
 *
 * Required state: INPUT_LITERALS | INPUT_ASSUMPTIONS
 * State after: INPUT_LITERALS
 */
int
quapi_solve(quapi_solver* solver);

/**
 * Terminate a running solver from a different thread. The solver must already
 * be WORKING.
 */
void
quapi_terminate(quapi_solver* solver);

void
quapi_reset_assumptions(quapi_solver* solver);

quapi_state
quapi_get_state(quapi_solver* solver);

/** @brief Sets a callback function with userdata to process STDOUT.
 *
 * Once the callback function returns != 0, STDOUT handling is stopped (similar
 * to the SAT and UNSAT regex).
 *
 * The userdata is given to the callback function without other modification and
 * can be used to pass arbitrary data. This function works regardlessly of the
 * PCRE2 library status. When not parsing output, no STDOUT will be read from
 * the process, in turn not costing extra performance.
 *
 * Also look at the "stdout callback function" in tests/test_stdout_cb.cpp for a
 * usage example.
 */
void
quapi_set_stdout_cb(quapi_solver* solver,
                    quapi_stdout_cb stdout_cb,
                    void* userdata);

#ifdef __cplusplus
}
#include <memory>

struct QuAPIDeleter {
  void operator()(quapi_solver* s) { quapi_release(s); }
};

using QuAPISolver = std::unique_ptr<quapi_solver, QuAPIDeleter>;
#endif

#endif
