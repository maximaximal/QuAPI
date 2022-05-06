#ifndef QUAPI_TIMING_H_
#define QUAPI_TIMING_H_

/** @file
 *
 * Provide some time-tracking utilities for analyzing the overhead of solver
 * startup. Timing may be enabled using the QUAPI_TIMING environment variable.
 */

void
quapi_timing_construct();

void
quapi_timing_firstread();

void
quapi_timing_header();

void
quapi_timing_msg_after_header();

#endif
