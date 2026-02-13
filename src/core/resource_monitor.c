/*
 * resource_monitor.c - Resource monitoring implementation
 *
 * Monitors system resources (FDs, memory, GPU) for leak detection.
 */

#include "../include/resource_monitor.h"
#include "../include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <GLES2/gl2.h>

resource_monitor_t* resource_monitor_create(double check_interval) {
    resource_monitor_t *monitor = calloc(1, sizeof(resource_monitor_t));
    if (!monitor) return NULL;

    /* Set defaults */
    monitor->check_interval = check_interval;
    monitor->fd_growth_threshold = 10;
    monitor->memory_growth_threshold_kb = 50000;  /* 50 MB */
    monitor->enabled = true;

    /* Check environment variables */
    const char *debug_env = getenv("HYPRLAX_RESOURCE_MONITOR_DEBUG");
    monitor->debug_output = (debug_env && strcmp(debug_env, "1") == 0);

    const char *interval_env = getenv("HYPRLAX_RESOURCE_MONITOR_INTERVAL");
    if (interval_env) {
        double interval = atof(interval_env);
        if (interval > 0) monitor->check_interval = interval;
    }

    const char *disable_env = getenv("HYPRLAX_RESOURCE_MONITOR_DISABLE");
    if (disable_env && strcmp(disable_env, "1") == 0) {
        monitor->enabled = false;
        LOG_INFO("Resource monitor disabled by environment variable");
        return monitor;
    }

    /* Take baseline measurements */
    monitor->fd_count_start = resource_monitor_get_fd_count();
    monitor->fd_count_current = monitor->fd_count_start;
    monitor->fd_count_max = monitor->fd_count_start;

    monitor->memory_rss_start_kb = resource_monitor_get_memory_rss_kb();
    monitor->memory_rss_current_kb = monitor->memory_rss_start_kb;
    monitor->memory_rss_max_kb = monitor->memory_rss_start_kb;

    monitor->memory_vms_start_kb = resource_monitor_get_memory_vms_kb();
    monitor->memory_vms_current_kb = monitor->memory_vms_start_kb;

    LOG_INFO("Resource monitor initialized (check interval: %.1fs)", check_interval);
    LOG_INFO("  Baseline - FDs: %d, RSS: %zu KB, VMS: %zu KB",
             monitor->fd_count_start,
             monitor->memory_rss_start_kb,
             monitor->memory_vms_start_kb);

    return monitor;
}

void resource_monitor_destroy(resource_monitor_t *monitor) {
    if (!monitor) return;

    /* Print final statistics */
    LOG_INFO("Resource monitor statistics:");
    LOG_INFO("  Checks performed: %lu", (unsigned long)monitor->check_count);
    LOG_INFO("  FD growth: %d (max: %d)",
             monitor->fd_count_current - monitor->fd_count_start,
             monitor->fd_count_max - monitor->fd_count_start);
    LOG_INFO("  Memory growth: %zd KB (max: %zu KB)",
             (ssize_t)(monitor->memory_rss_current_kb - monitor->memory_rss_start_kb),
             monitor->memory_rss_max_kb - monitor->memory_rss_start_kb);

    free(monitor);
}

int resource_monitor_get_fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);

    return count;
}

size_t resource_monitor_get_memory_rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;

    char line[256];
    size_t rss_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%zu", &rss_kb);
            break;
        }
    }

    fclose(f);
    return rss_kb;
}

size_t resource_monitor_get_memory_vms_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;

    char line[256];
    size_t vms_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            sscanf(line + 7, "%zu", &vms_kb);
            break;
        }
    }

    fclose(f);
    return vms_kb;
}

void resource_monitor_get_gl_counts(int *textures, int *buffers, int *fbos) {
    /* Note: These queries may not work on all GL implementations
     * Provide best-effort tracking */

    if (textures) {
        GLint max_textures = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_textures);
        *textures = max_textures;  /* Approximate */
    }

    if (buffers) *buffers = 0;  /* No direct query available */
    if (fbos) *fbos = 0;        /* No direct query available */
}

void resource_monitor_check(resource_monitor_t *monitor) {
    if (!monitor || !monitor->enabled) return;

    monitor->check_count++;

    /* Get current metrics */
    int fd_count = resource_monitor_get_fd_count();
    size_t rss_kb = resource_monitor_get_memory_rss_kb();
    size_t vms_kb = resource_monitor_get_memory_vms_kb();

    /* Update current values */
    monitor->fd_count_current = fd_count;
    monitor->memory_rss_current_kb = rss_kb;
    monitor->memory_vms_current_kb = vms_kb;

    /* Update maximums */
    if (fd_count > monitor->fd_count_max) {
        monitor->fd_count_max = fd_count;
    }
    if (rss_kb > monitor->memory_rss_max_kb) {
        monitor->memory_rss_max_kb = rss_kb;
    }

    /* Calculate growth */
    int fd_growth = fd_count - monitor->fd_count_start;
    ssize_t memory_growth_kb = rss_kb - monitor->memory_rss_start_kb;

    /* Check for warnings */
    if (fd_growth > monitor->fd_growth_threshold) {
        LOG_WARN("File descriptor growth detected: +%d FDs (current: %d, baseline: %d)",
                 fd_growth, fd_count, monitor->fd_count_start);
    }

    if (memory_growth_kb > (ssize_t)monitor->memory_growth_threshold_kb) {
        LOG_WARN("Memory growth detected: +%zd KB (current: %zu KB, baseline: %zu KB)",
                 memory_growth_kb, rss_kb, monitor->memory_rss_start_kb);
    }

    /* Debug output */
    if (monitor->debug_output) {
        LOG_DEBUG("Resource check #%lu: FDs=%d (+%d), RSS=%zu KB (+%zd KB)",
                  (unsigned long)monitor->check_count, fd_count, fd_growth,
                  rss_kb, memory_growth_kb);
    }
}

bool resource_monitor_should_check(resource_monitor_t *monitor, double current_time) {
    if (!monitor || !monitor->enabled) return false;

    if (current_time - monitor->last_check_time >= monitor->check_interval) {
        monitor->last_check_time = current_time;
        return true;
    }

    return false;
}

void resource_monitor_print_status(resource_monitor_t *monitor) {
    if (!monitor) return;

    printf("\n=== Resource Monitor Status ===\n");
    printf("Checks performed: %lu\n", (unsigned long)monitor->check_count);
    printf("Check interval: %.1f seconds\n", monitor->check_interval);
    printf("\nFile Descriptors:\n");
    printf("  Baseline: %d\n", monitor->fd_count_start);
    printf("  Current:  %d (+%d)\n", monitor->fd_count_current,
           monitor->fd_count_current - monitor->fd_count_start);
    printf("  Maximum:  %d (+%d)\n", monitor->fd_count_max,
           monitor->fd_count_max - monitor->fd_count_start);
    printf("\nMemory (RSS):\n");
    printf("  Baseline: %zu KB\n", monitor->memory_rss_start_kb);
    printf("  Current:  %zu KB (+%zd KB)\n", monitor->memory_rss_current_kb,
           (ssize_t)(monitor->memory_rss_current_kb - monitor->memory_rss_start_kb));
    printf("  Maximum:  %zu KB (+%zu KB)\n", monitor->memory_rss_max_kb,
           monitor->memory_rss_max_kb - monitor->memory_rss_start_kb);
    printf("\n");
}
