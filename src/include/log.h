/*
 * log.h - Logging system for hyprlax
 */

#ifndef HYPRLAX_LOG_H
#define HYPRLAX_LOG_H

#include <stdio.h>
#include <stdbool.h>

/* Log levels */
typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
} log_level_t;

/* Initialize logging system */
void log_init(bool debug, const char *log_file);
/* Adjust minimum level for output (default LOG_WARN; LOG_DEBUG when debug=true) */
void log_set_level(log_level_t level);

/* Cleanup logging system */
void log_cleanup(void);

/* Log message with level */
void log_message(log_level_t level, const char *fmt, ...);

/* Convenience macros */
#define LOG_ERROR(...) log_message(LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_WARN, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) log_message(LOG_TRACE, __VA_ARGS__)

#endif /* HYPRLAX_LOG_H */
