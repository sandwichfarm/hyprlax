/*
 * time_utils.c - Time utility implementation
 *
 * Provides overflow-safe timestamp operations using 64-bit milliseconds.
 */

#include "../include/time_utils.h"
#include <stdbool.h>

/* Get current time in milliseconds since system boot */
timestamp_ms_t time_get_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1;  /* Error */
    }

    /* Convert to milliseconds using 64-bit arithmetic to prevent overflow */
    timestamp_ms_t ms = (timestamp_ms_t)ts.tv_sec * 1000LL +
                        (timestamp_ms_t)ts.tv_nsec / 1000000LL;
    return ms;
}

/* Calculate elapsed time between two timestamps */
int64_t time_elapsed_ms(timestamp_ms_t start, timestamp_ms_t end) {
    /* Simple subtraction works for 64-bit (no wraparound for 292 million years) */
    return end - start;
}

/* Convert milliseconds to seconds */
double time_ms_to_seconds(timestamp_ms_t ms) {
    return (double)ms / 1000.0;
}

/* Convert seconds to milliseconds */
timestamp_ms_t time_seconds_to_ms(double seconds) {
    return (timestamp_ms_t)(seconds * 1000.0);
}

/* Validate timestamp is reasonable */
bool time_is_timestamp_valid(timestamp_ms_t ts) {
    if (ts < 0) {
        return false;
    }

    timestamp_ms_t now = time_get_monotonic_ms();
    if (now < 0) {
        /* Can't validate if we can't get current time */
        return false;
    }

    /* Not more than 1 second in future (allow for minor clock skew) */
    if (ts > now + 1000) {
        return false;
    }

    /* Not more than 7 days old (604800000 ms = 7 * 24 * 60 * 60 * 1000) */
    if (time_elapsed_ms(ts, now) > 604800000LL) {
        return false;
    }

    return true;
}
