/*
 * monitor.c - Multi-monitor management implementation
 */

#include "monitor.h"
#include "hyprlax.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Create a new monitor list */
monitor_list_t* monitor_list_create(void) {
    monitor_list_t *list = calloc(1, sizeof(monitor_list_t));
    if (!list) {
        fprintf(stderr, "Failed to allocate monitor list\n");
        return NULL;
    }
    list->next_id = 1;
    return list;
}

/* Destroy monitor list and all monitors */
void monitor_list_destroy(monitor_list_t *list) {
    if (!list) return;
    
    monitor_instance_t *current = list->head;
    while (current) {
        monitor_instance_t *next = current->next;
        monitor_instance_destroy(current);
        current = next;
    }
    
    free(list);
}

/* Create a new monitor instance */
monitor_instance_t* monitor_instance_create(const char *name) {
    monitor_instance_t *monitor = calloc(1, sizeof(monitor_instance_t));
    if (!monitor) {
        fprintf(stderr, "Failed to allocate monitor instance\n");
        return NULL;
    }
    
    /* Set defaults */
    strncpy(monitor->name, name ? name : "unknown", sizeof(monitor->name) - 1);
    monitor->scale = 1;
    monitor->refresh_rate = 60;
    monitor->current_workspace = 1;
    monitor->workspace_offset_x = 0.0f;
    monitor->workspace_offset_y = 0.0f;
    monitor->target_frame_time = 1000.0 / 60.0;  /* Default 60 Hz */
    
    return monitor;
}

/* Destroy a monitor instance */
void monitor_instance_destroy(monitor_instance_t *monitor) {
    if (!monitor) return;
    
    /* Free config if allocated */
    if (monitor->config) {
        free(monitor->config);
    }
    
    free(monitor);
}

/* Add monitor to list */
void monitor_list_add(monitor_list_t *list, monitor_instance_t *monitor) {
    if (!list || !monitor) return;
    
    monitor->id = list->next_id++;
    monitor->next = NULL;
    
    /* Add to end of list to maintain order */
    if (!list->head) {
        list->head = monitor;
        if (!list->primary) {
            list->primary = monitor;
            monitor->is_primary = true;
        }
    } else {
        monitor_instance_t *tail = list->head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = monitor;
    }
    
    list->count++;
    fprintf(stderr, "Added monitor %s (id=%u, total=%d)\n", 
            monitor->name, monitor->id, list->count);
}

/* Remove monitor from list */
void monitor_list_remove(monitor_list_t *list, monitor_instance_t *monitor) {
    if (!list || !monitor) return;
    
    monitor_instance_t *prev = NULL;
    monitor_instance_t *current = list->head;
    
    while (current) {
        if (current == monitor) {
            if (prev) {
                prev->next = current->next;
            } else {
                list->head = current->next;
            }
            
            /* Update primary if needed */
            if (list->primary == monitor) {
                list->primary = list->head;
                if (list->primary) {
                    list->primary->is_primary = true;
                }
            }
            
            list->count--;
            fprintf(stderr, "Removed monitor %s (id=%u, remaining=%d)\n",
                    monitor->name, monitor->id, list->count);
            break;
        }
        prev = current;
        current = current->next;
    }
}

/* Find monitor by name */
monitor_instance_t* monitor_list_find_by_name(monitor_list_t *list, const char *name) {
    if (!list || !name) return NULL;
    
    monitor_instance_t *current = list->head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* Find monitor by Wayland output */
monitor_instance_t* monitor_list_find_by_output(monitor_list_t *list, struct wl_output *output) {
    if (!list || !output) return NULL;
    
    monitor_instance_t *current = list->head;
    while (current) {
        if (current->wl_output == output) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* Find monitor by ID */
monitor_instance_t* monitor_list_find_by_id(monitor_list_t *list, uint32_t id) {
    if (!list) return NULL;
    
    monitor_instance_t *current = list->head;
    while (current) {
        if (current->id == id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* Resolve configuration for monitor */
config_t* monitor_resolve_config(monitor_instance_t *monitor, config_t *global_config) {
    if (!monitor || !global_config) return NULL;
    
    /* For Phase 0: Clone global config to each monitor */
    config_t *config = calloc(1, sizeof(config_t));
    if (!config) return NULL;
    
    /* Deep copy global config */
    *config = *global_config;
    
    /* In future phases, this will handle per-monitor overrides */
    /* TODO: Phase 2 - Check for monitor-specific config */
    /* TODO: Phase 2 - Apply overrides based on pattern matching */
    
    return config;
}

/* Apply configuration to monitor */
void monitor_apply_config(monitor_instance_t *monitor, config_t *config) {
    if (!monitor || !config) return;
    
    /* Free old config if exists */
    if (monitor->config && monitor->config != config) {
        free(monitor->config);
    }
    
    monitor->config = config;
    
    /* Update frame time based on configured FPS if specified */
    if (config->target_fps > 0) {
        monitor->target_frame_time = 1000.0 / config->target_fps;
    } else {
        /* Use monitor's native refresh rate */
        monitor->target_frame_time = 1000.0 / monitor->refresh_rate;
    }
}

/* Handle workspace change for specific monitor */
void monitor_handle_workspace_change(hyprlax_context_t *ctx,
                                    monitor_instance_t *monitor,
                                    int new_workspace) {
    if (!monitor) return;
    
    int workspace_delta = new_workspace - monitor->current_workspace;
    if (workspace_delta == 0) return;
    
    fprintf(stderr, "Monitor %s: workspace %d -> %d (delta=%d)\n",
            monitor->name, monitor->current_workspace, new_workspace, workspace_delta);
    
    monitor->current_workspace = new_workspace;
    monitor_start_parallax_animation(ctx, monitor, workspace_delta);
}

/* Start parallax animation for monitor */
void monitor_start_parallax_animation(hyprlax_context_t *ctx,
                                     monitor_instance_t *monitor,
                                     int workspace_delta) {
    if (!monitor || !monitor->config) return;
    
    float shift_amount = monitor->config->shift_pixels * workspace_delta;
    
    /* Store animation start state */
    monitor->animation_start_x = monitor->workspace_offset_x;
    monitor->animation_start_y = monitor->workspace_offset_y;
    
    /* Set animation targets */
    monitor->animation_target_x = monitor->workspace_offset_x + shift_amount;
    monitor->animation_target_y = monitor->workspace_offset_y;  /* No vertical shift for now */
    
    /* Start animation */
    monitor->animation_start_time = ctx ? ctx->last_frame_time : 0.0;
    monitor->animating = true;
    
    fprintf(stderr, "Monitor %s: starting animation %.1f -> %.1f\n",
            monitor->name, monitor->animation_start_x, monitor->animation_target_x);
}

/* Update animation state */
void monitor_update_animation(monitor_instance_t *monitor, double current_time) {
    if (!monitor || !monitor->animating || !monitor->config) return;
    
    double elapsed = current_time - monitor->animation_start_time;
    double duration = monitor->config->animation_duration * 1000.0;  /* Convert to ms */
    
    if (elapsed >= duration) {
        /* Animation complete */
        monitor->workspace_offset_x = monitor->animation_target_x;
        monitor->workspace_offset_y = monitor->animation_target_y;
        monitor->animating = false;
        return;
    }
    
    /* Calculate progress with easing */
    float progress = elapsed / duration;
    
    /* Apply easing function (for now, just exponential) */
    float eased_progress;
    switch (monitor->config->default_easing) {
        case EASE_EXPO_OUT:
            eased_progress = 1.0f - powf(2.0f, -10.0f * progress);
            break;
        case EASE_LINEAR:
        default:
            eased_progress = progress;
            break;
    }
    
    /* Update offsets */
    monitor->workspace_offset_x = monitor->animation_start_x +
        (monitor->animation_target_x - monitor->animation_start_x) * eased_progress;
    monitor->workspace_offset_y = monitor->animation_start_y +
        (monitor->animation_target_y - monitor->animation_start_y) * eased_progress;
}

/* Check if monitor should render a new frame */
bool monitor_should_render(monitor_instance_t *monitor, double current_time) {
    if (!monitor) return false;
    
    /* Always render if animating */
    if (monitor->animating) return true;
    
    /* Check if enough time has passed for next frame */
    double time_since_last = current_time - monitor->last_frame_time;
    return time_since_last >= monitor->target_frame_time;
}

/* Mark frame as pending */
void monitor_mark_frame_pending(monitor_instance_t *monitor) {
    if (monitor) {
        monitor->frame_pending = true;
    }
}

/* Frame callback done */
void monitor_frame_done(monitor_instance_t *monitor) {
    if (monitor) {
        monitor->frame_pending = false;
        /* Frame time will be updated by caller */
    }
}

/* Update monitor geometry */
void monitor_update_geometry(monitor_instance_t *monitor,
                            int width, int height,
                            int scale, int refresh_rate) {
    if (!monitor) return;
    
    monitor->width = width;
    monitor->height = height;
    monitor->scale = scale;
    monitor->refresh_rate = refresh_rate;
    
    /* Update target frame time */
    monitor->target_frame_time = 1000.0 / refresh_rate;
    
    fprintf(stderr, "Monitor %s geometry: %dx%d@%dHz scale=%d\n",
            monitor->name, width, height, refresh_rate, scale);
}

/* Set global position */
void monitor_set_global_position(monitor_instance_t *monitor, int x, int y) {
    if (monitor) {
        monitor->global_x = x;
        monitor->global_y = y;
    }
}

/* Get monitor name */
const char* monitor_get_name(monitor_instance_t *monitor) {
    return monitor ? monitor->name : NULL;
}

/* Check if monitor is active */
bool monitor_is_active(monitor_instance_t *monitor) {
    return monitor && monitor->wl_surface != NULL;
}