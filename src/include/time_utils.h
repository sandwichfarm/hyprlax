/*
 * time_utils.h - Time utility functions for overflow-safe timestamps
 *
 * Provides 64-bit millisecond timestamps to prevent overflow issues
 * that occur with double-precision or 32-bit millisecond counters.
 */

#ifndef HYPRLAX_TIME_UTILS_H
#define HYPRLAX_TIME_UTILS_H

#include <stdint.h>
#include <time.h>

/* Use 64-bit milliseconds for timestamps (wraps after 292 million years) */
typedef int64_t timestamp_ms_t;

/* Get current time in milliseconds since system boot (CLOCK_MONOTONIC) */
timestamp_ms_t time_get_monotonic_ms(void);

/* Calculate elapsed time between two timestamps (handles wraparound) */
int64_t time_elapsed_ms(timestamp_ms_t start, timestamp_ms_t end);

/* Convert milliseconds to seconds (for display/compatibility) */
double time_ms_to_seconds(timestamp_ms_t ms);

/* Convert seconds to milliseconds (for compatibility) */
timestamp_ms_t time_seconds_to_ms(double seconds);

/* Validate timestamp is reasonable (not in future, not too old) */
bool time_is_timestamp_valid(timestamp_ms_t ts);

#endif /* HYPRLAX_TIME_UTILS_H */
