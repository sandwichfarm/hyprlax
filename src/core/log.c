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
static bool g_debug_enabled = false;
static bool g_log_to_file = false;

/* Initialize logging system */
void log_init(bool debug, const char *log_file) {
    g_debug_enabled = debug;
    
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

/* Log message with level */
void log_message(log_level_t level, const char *fmt, ...) {
    /* Skip debug/trace messages if not in debug mode */
    if (!g_debug_enabled && (level == LOG_DEBUG || level == LOG_TRACE)) {
        return;
    }
    
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
    
    /* Output to stderr ONLY if:
     * 1. We're NOT logging to file (i.e., normal debug mode), OR
     * 2. It's an ERROR or WARN message (always show these)
     * 
     * When logging to file, suppress INFO/DEBUG/TRACE from stderr */
    if (g_log_to_file) {
        /* When logging to file, only show ERROR and WARN on stderr */
        if (level == LOG_ERROR || level == LOG_WARN) {
            fprintf(stderr, "%s %s\n", level_str, buffer);
        }
    } else if (g_debug_enabled) {
        /* Normal debug mode (no file) - show everything on stderr */
        fprintf(stderr, "%s %s\n", level_str, buffer);
    } else {
        /* Not in debug mode and not logging to file - only show ERROR and WARN */
        if (level == LOG_ERROR || level == LOG_WARN) {
            fprintf(stderr, "%s %s\n", level_str, buffer);
        }
    }
}