/*
 * log.c - Logging implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include "include/log.h"

static FILE *g_log_file = NULL;
static bool g_log_to_file = false;
static log_level_t g_min_level = LOG_WARN; /* default: warnings and errors only */

/* Initialize logging system */
void log_init(bool debug, const char *log_file) {
    /* Back-compat: map legacy debug flag to a minimum level */
    g_min_level = debug ? LOG_DEBUG : LOG_WARN;

    if (log_file) {
        g_log_file = fopen(log_file, "a");
        if (g_log_file) {
            g_log_to_file = true;
            /* Write header */
            time_t now = time(NULL);
            fprintf(g_log_file, "\n=== HYPRLAX LOG START: %s", ctime(&now));
            fprintf(g_log_file, "PID: %d\n", getpid());
            fprintf(g_log_file, "=====================================\n\n");
            fflush(g_log_file);
        }
    }
}

/* Cleanup logging system */
void log_cleanup(void) {
    if (g_log_file) {
        time_t now = time(NULL);
        fprintf(g_log_file, "\n=== HYPRLAX LOG END: %s", ctime(&now));
        fprintf(g_log_file, "=====================================\n");
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_to_file = false;
}

void log_set_level(log_level_t level) {
    if (level < LOG_ERROR) level = LOG_ERROR;
    if (level > LOG_TRACE) level = LOG_TRACE;
    g_min_level = level;
}

/* Log message with level (filtered by g_min_level) */
void log_message(log_level_t level, const char *fmt, ...) {
    /* Filter by configured minimum level */
    if (level > g_min_level) return;

    /* Get timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    /* Level prefix */
    const char *level_str = "";
    switch (level) {
        case LOG_ERROR: level_str = "[ERROR]"; break;
        case LOG_WARN:  level_str = "[WARN] "; break;
        case LOG_INFO:  level_str = "[INFO] "; break;
        case LOG_DEBUG: level_str = "[DEBUG]"; break;
        case LOG_TRACE: level_str = "[TRACE]"; break;
    }

    /* Format message */
    va_list args;
    char buffer[4096];

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Output to file if logging to file is enabled */
    if (g_log_to_file && g_log_file) {
        fprintf(g_log_file, "%s.%03ld %s %s\n",
                timestamp, tv.tv_usec / 1000, level_str, buffer);
        fflush(g_log_file);
    }

    /* Stderr policy:
     * - If logging to file, mirror only WARN/ERROR to stderr to avoid spam
     * - Otherwise, print all messages that passed the level filter */
    if (g_log_to_file) {
        if (level == LOG_ERROR || level == LOG_WARN) {
            fprintf(stderr, "%s %s\n", level_str, buffer);
        }
    } else {
        fprintf(stderr, "%s %s\n", level_str, buffer);
    }
}
