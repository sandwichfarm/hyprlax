/*
 * resource_monitor.h - Resource monitoring for proactive leak detection
 *
 * Tracks file descriptors, memory usage, and GPU resources to detect
 * potential leaks in production environments.
 */

#ifndef HYPRLAX_RESOURCE_MONITOR_H
#define HYPRLAX_RESOURCE_MONITOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Baseline measurements (at start) */
    int fd_count_start;
    size_t memory_rss_start_kb;
    size_t memory_vms_start_kb;

    /* Current measurements */
    int fd_count_current;
    size_t memory_rss_current_kb;
    size_t memory_vms_current_kb;

    /* Growth tracking */
    int fd_count_max;
    size_t memory_rss_max_kb;

    /* GPU resources (if available) */
    int gl_texture_count;
    int gl_buffer_count;
    int gl_framebuffer_count;

    /* Timing */
    double check_interval;      /* How often to check (seconds) */
    double last_check_time;     /* When was last check */
    uint64_t check_count;       /* How many checks performed */

    /* Thresholds for warnings */
    int fd_growth_threshold;        /* Warn if FDs grow by this much */
    size_t memory_growth_threshold_kb;  /* Warn if memory grows by this much */

    /* Flags */
    bool enabled;
    bool debug_output;

} resource_monitor_t;

/* Initialize resource monitor */
resource_monitor_t* resource_monitor_create(double check_interval);

/* Destroy resource monitor */
void resource_monitor_destroy(resource_monitor_t *monitor);

/* Perform a health check (called periodically) */
void resource_monitor_check(resource_monitor_t *monitor);

/* Get current FD count */
int resource_monitor_get_fd_count(void);

/* Get current memory usage (RSS in KB) */
size_t resource_monitor_get_memory_rss_kb(void);

/* Get current memory usage (VMS in KB) */
size_t resource_monitor_get_memory_vms_kb(void);

/* Get GL resource counts (requires GL context) */
void resource_monitor_get_gl_counts(int *textures, int *buffers, int *fbos);

/* Print current status (debug) */
void resource_monitor_print_status(resource_monitor_t *monitor);

/* Check if it's time for a health check */
bool resource_monitor_should_check(resource_monitor_t *monitor, double current_time);

#endif /* HYPRLAX_RESOURCE_MONITOR_H */
