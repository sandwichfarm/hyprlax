/*
 * monitor.c - Multi-monitor management implementation
 */

#include "monitor.h"
#include "hyprlax.h"
#include "core.h"
#include "log.h"
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

    /* Initialize workspace context (default to numeric) */
    monitor->current_context.model = WS_MODEL_GLOBAL_NUMERIC;
    monitor->current_context.data.workspace_id = 1;
    monitor->previous_context = monitor->current_context;

    monitor->parallax_offset_x = 0.0f;
    monitor->parallax_offset_y = 0.0f;
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

/* Get primary monitor */
monitor_instance_t* monitor_list_get_primary(monitor_list_t *list) {
    if (!list) return NULL;

    monitor_instance_t *current = list->head;
    while (current) {
        if (current->is_primary) {
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

/* Handle workspace change for specific monitor (legacy - numeric only) */
void monitor_handle_workspace_change(hyprlax_context_t *ctx,
                                    monitor_instance_t *monitor,
                                    int new_workspace) {
    if (!monitor) return;

    /* Create workspace context for numeric workspace */
    workspace_context_t new_context = monitor->current_context;
    new_context.data.workspace_id = new_workspace;

    monitor_handle_workspace_context_change(ctx, monitor, &new_context);
}

/* Handle workspace context change (flexible model) */
void monitor_handle_workspace_context_change(hyprlax_context_t *ctx,
                                            monitor_instance_t *monitor,
                                            const workspace_context_t *new_context) {
    if (!monitor || !new_context) return;

    /* Check if context actually changed */
    if (workspace_context_equal(&monitor->current_context, new_context)) return;

    /* Calculate offset based on context change */
    float offset = workspace_calculate_offset(&monitor->current_context,
                                             new_context,
                                             monitor->config ? monitor->config->shift_pixels : 200.0f,
                                             NULL);

    /* Debug output - only visible with --debug flag */

    /* Update context */
    monitor->previous_context = monitor->current_context;
    monitor->current_context = *new_context;

    /* Start animation if offset changed */
    if (offset != 0.0f && ctx) {
        /* Calculate absolute workspace position based on workspace ID */
        int target_workspace = new_context->data.workspace_id;
        int base_workspace = 1; /* Assuming workspace 1 is at position 0 */
        float absolute_target = (target_workspace - base_workspace) *
                              (monitor->config ? monitor->config->shift_pixels : 200.0f);

        /* Update all layers with their absolute target positions */
        if (ctx->layers) {
            parallax_layer_t *layer = ctx->layers;

            while (layer) {
                /* Each layer moves at its own speed based on shift_multiplier */
                float layer_target_x = absolute_target * layer->shift_multiplier;
                float layer_target_y = 0.0f;

                layer_update_offset(layer, layer_target_x, layer_target_y,
                                  monitor->config ? monitor->config->animation_duration : 1.0,
                                  monitor->config ? monitor->config->default_easing : EASE_CUBIC_OUT);

                layer = layer->next;
            }
        }

        /* Update monitor's animation separately for tracking */
        monitor_start_parallax_animation_offset(ctx, monitor, offset);
    }
}

/* Start parallax animation for monitor (legacy - uses workspace delta) */
void monitor_start_parallax_animation(hyprlax_context_t *ctx,
                                     monitor_instance_t *monitor,
                                     int workspace_delta) {
    if (!monitor || !monitor->config) return;

    float shift_amount = monitor->config->shift_pixels * workspace_delta;
    monitor_start_parallax_animation_offset(ctx, monitor, shift_amount);
}

/* Start parallax animation with specific offset */
void monitor_start_parallax_animation_offset(hyprlax_context_t *ctx,
                                            monitor_instance_t *monitor,
                                            float offset) {
    if (!monitor) return;

    /* If already animating, use current animated position as start point */
    if (monitor->animating && ctx) {
        /* Calculate current position in the ongoing animation */
        double elapsed = ctx->last_frame_time - monitor->animation_start_time;
        double duration = monitor->config ? monitor->config->animation_duration * 1000.0 : 1000.0;
        double progress = (elapsed >= duration) ? 1.0 : (elapsed / duration);

        /* Apply easing to progress */
        double eased_progress = progress;
        if (monitor->config && monitor->config->default_easing) {
            eased_progress = apply_easing(progress, monitor->config->default_easing);
        }

        /* Calculate current animated position */
        monitor->animation_start_x = monitor->animation_start_x +
                                    (monitor->animation_target_x - monitor->animation_start_x) * eased_progress;
        monitor->animation_start_y = monitor->animation_start_y +
                                    (monitor->animation_target_y - monitor->animation_start_y) * eased_progress;
    } else {
        /* Not animating, use current offset as start */
        monitor->animation_start_x = monitor->parallax_offset_x;
        monitor->animation_start_y = monitor->parallax_offset_y;
    }

    /* Set animation targets - add offset to the starting position */
    monitor->animation_target_x = monitor->animation_start_x + offset;
    monitor->animation_target_y = monitor->animation_start_y;  /* No vertical shift for now */

    /* Start animation */
    monitor->animation_start_time = ctx ? ctx->last_frame_time : 0.0;
    monitor->animating = true;

    /* Debug output - only with --debug flag */
    if (ctx && ctx->config.debug) {
        LOG_DEBUG("Monitor %s: starting animation %.1f -> %.1f",
                monitor->name, monitor->animation_start_x, monitor->animation_target_x);
    }
}

/* Update animation state */
void monitor_update_animation(monitor_instance_t *monitor, double current_time) {
    if (!monitor || !monitor->animating || !monitor->config) return;

    double elapsed = current_time - monitor->animation_start_time;
    double duration = monitor->config->animation_duration * 1000.0;  /* Convert to ms */

    if (elapsed >= duration) {
        /* Animation complete */
        monitor->parallax_offset_x = monitor->animation_target_x;
        monitor->parallax_offset_y = monitor->animation_target_y;
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
    monitor->parallax_offset_x = monitor->animation_start_x +
        (monitor->animation_target_x - monitor->animation_start_x) * eased_progress;
    monitor->parallax_offset_y = monitor->animation_start_y +
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