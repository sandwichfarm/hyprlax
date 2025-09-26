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
#include <time.h>

/* Get current time in seconds */
static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Create a new monitor list */
monitor_list_t* monitor_list_create(void) {
    monitor_list_t *list = calloc(1, sizeof(monitor_list_t));
    if (!list) {
        LOG_ERROR("Failed to allocate monitor list");
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
        LOG_ERROR("Failed to allocate monitor instance");
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
    LOG_INFO("Monitor added: %s (id=%u, total=%d)",
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
    LOG_INFO("Monitor removed: %s (id=%u, remaining=%d)",
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
    if (workspace_context_equal(&monitor->current_context, new_context)) {
        if (ctx && ctx->config.debug) {
            fprintf(stderr, "[DEBUG] monitor_handle_workspace_context_change: No change detected\n");
        }
        return;
    }

    /* Cursor-only mode: update context for bookkeeping, but do not drive parallax
       offsets or layer animations from workspace changes. Cursor input is the
       sole parallax source in this mode. */
    if (ctx && ctx->config.parallax_mode == PARALLAX_CURSOR) {
        monitor->previous_context = monitor->current_context;
        monitor->current_context = *new_context;
        if (ctx->config.debug) {
            fprintf(stderr, "[DEBUG] monitor_handle_workspace_context_change: cursor-only mode; skipping parallax update\n");
        }
        return;
    }

    if (ctx && ctx->config.debug) {
        fprintf(stderr, "[DEBUG] monitor_handle_workspace_context_change:\n");
        fprintf(stderr, "[DEBUG]   Monitor: %s\n", monitor->name);
        fprintf(stderr, "[DEBUG]   Model: %s\n", workspace_model_to_string(new_context->model));
        if (new_context->model == WS_MODEL_PER_OUTPUT_NUMERIC) {
            fprintf(stderr, "[DEBUG]   From workspace ID: %d\n", monitor->current_context.data.workspace_id);
            fprintf(stderr, "[DEBUG]   To workspace ID: %d\n", new_context->data.workspace_id);
        }
    }

    /* Check if this is a 2D workspace model */
    bool is_2d = (new_context->model == WS_MODEL_SET_BASED ||
                  new_context->model == WS_MODEL_PER_OUTPUT_NUMERIC);

    workspace_offset_t offset_2d = {0.0f, 0.0f};
    float offset_1d = 0.0f;

    if (is_2d) {
        /* Calculate 2D offset for 2D models */
        offset_2d = workspace_calculate_offset_2d(&monitor->current_context,
                                                 new_context,
                                                 monitor->config ? monitor->config->shift_pixels : 200.0f,
                                                 NULL);
        if (ctx && ctx->config.debug) {
            fprintf(stderr, "[DEBUG]   Using 2D offset calculation\n");
        }
    } else {
        /* Calculate 1D offset for linear models */
        offset_1d = workspace_calculate_offset(&monitor->current_context,
                                              new_context,
                                              monitor->config ? monitor->config->shift_pixels : 200.0f,
                                              NULL);
        offset_2d.x = offset_1d;
        offset_2d.y = 0.0f;
        if (ctx && ctx->config.debug) {
            fprintf(stderr, "[DEBUG]   Using 1D offset calculation\n");
        }
    }

    if (ctx && ctx->config.debug) {
        fprintf(stderr, "[DEBUG]   Offset: X=%.1f, Y=%.1f\n", offset_2d.x, offset_2d.y);
    }

    /* Keep a copy of the old context for correct delta calculations */
    workspace_context_t old_context = monitor->current_context;

    /* Start animation if offset changed */
    if ((offset_2d.x != 0.0f || offset_2d.y != 0.0f) && ctx) {
        /* Calculate absolute workspace position */
        float absolute_target_x, absolute_target_y;

        if (is_2d) {
            /* For 2D models, accumulate the offsets */
            /* If animating, accumulate from the target position, not current */
            float base_x = monitor->animating ? monitor->animation_target_x : monitor->parallax_offset_x;
            float base_y = monitor->animating ? monitor->animation_target_y : monitor->parallax_offset_y;

            if (ctx && ctx->config.debug) {
                LOG_DEBUG("  Base position: X=%.1f, Y=%.1f (animating=%d)", base_x, base_y, monitor->animating);
            }

            absolute_target_x = base_x + offset_2d.x;
            absolute_target_y = base_y + offset_2d.y;

            /* Update the accumulated position */
            monitor->parallax_offset_x = absolute_target_x;
            monitor->parallax_offset_y = absolute_target_y;
            if (ctx && ctx->config.debug) {
                LOG_DEBUG("  2D accumulative offset: X=%.1f, Y=%.1f", monitor->parallax_offset_x, monitor->parallax_offset_y);
            }
        } else {
            /* For 1D models, accumulate delta from previous context (legacy behavior) */
            int from_ws = old_context.data.workspace_id;
            int to_ws = new_context->data.workspace_id;
            int delta_ws = to_ws - from_ws;
            float step = (monitor->config ? monitor->config->shift_pixels : 200.0f);
            float base_x = monitor->animating ? monitor->animation_target_x : monitor->parallax_offset_x;
            absolute_target_x = base_x + delta_ws * step;
            absolute_target_y = 0.0f;
            if (ctx && ctx->config.debug) {
                LOG_DEBUG("  1D accumulative: from %d to %d (delta=%d) base=%.1f -> X=%.1f",
                          from_ws, to_ws, delta_ws, base_x, absolute_target_x);
            }
        }

        /* Update all layers with their absolute target positions */
        if (ctx->layers) {
            if (ctx && ctx->config.debug) {
                LOG_DEBUG("  Updating layers with target: X=%.1f, Y=%.1f", absolute_target_x, absolute_target_y);
            }

            parallax_layer_t *layer = ctx->layers;
            int layer_count = 0;

            while (layer) {
                parallax_layer_t *next_layer = layer->next; /* capture next to avoid surprises */
                /* Each layer moves at its own speed based on per-axis multipliers */
                float mx = layer->shift_multiplier_x;
                float my = layer->shift_multiplier_y;

                float layer_target_x = absolute_target_x * mx;

                /* Preserve legacy aspect ratio scaling if per-axis not customized */
                float layer_target_y;
                float debug_aspect = 1.0f;
                if (layer->shift_multiplier_x == layer->shift_multiplier &&
                    layer->shift_multiplier_y == layer->shift_multiplier) {
                    if (layer->texture_width > 0 && layer->texture_height > 0) {
                        debug_aspect = (float)layer->texture_height / (float)layer->texture_width;
                    }
                    layer_target_y = absolute_target_y * my * debug_aspect;
                } else {
                    layer_target_y = absolute_target_y * my;
                }

                if (ctx && ctx->config.debug) {
                    fprintf(stderr, "[DEBUG]     Layer %d: multiplier=%.2f, aspect=%.2f, target=(%.1f, %.1f)\n",
                            layer_count++, layer->shift_multiplier, debug_aspect,
                            layer_target_x, layer_target_y);
                }

                layer_update_offset(layer, layer_target_x, layer_target_y,
                                  monitor->config ? monitor->config->animation_duration : 1.0,
                                  monitor->config ? monitor->config->default_easing : EASE_CUBIC_OUT);
                /* Defensive: if any callee scribbled over the linked-list pointer, restore it */
                if (layer->next != next_layer) {
                    if (ctx && ctx->config.debug) {
                        fprintf(stderr, "[DEBUG]     WARNING: layer->next mutated (%p -> %p); restoring\n",
                                (void*)layer->next, (void*)next_layer);
                    }
                    layer->next = next_layer;
                }
                if (ctx && ctx->config.debug) {
                    fprintf(stderr, "[DEBUG]     Layer %d updated; next=%p\n", layer_count-1, (void*)next_layer);
                }
                if (next_layer == layer) {
                    /* Prevent infinite loop on corrupted list */
                    if (ctx && ctx->config.debug) fprintf(stderr, "[DEBUG]     Detected self-referential next; breaking.\n");
                    break;
                }
                layer = next_layer;
            }
        } else {
            if (ctx && ctx->config.debug) {
                fprintf(stderr, "[DEBUG]   WARNING: No layers to update!\n");
            }
        }

        /* Update monitor's animation separately for tracking */
        float dominant_offset = fabs(offset_2d.x) > fabs(offset_2d.y) ? offset_2d.x : offset_2d.y;
        monitor_start_parallax_animation_offset(ctx, monitor, dominant_offset);
    }

    /* Update context after computing offsets */
    monitor->previous_context = old_context;
    monitor->current_context = *new_context;
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
        double current_time = get_time();
        double elapsed = current_time - monitor->animation_start_time;
        double duration = monitor->config ? monitor->config->animation_duration : 1.0;
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
    monitor->animation_start_time = get_time();
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
    double duration = monitor->config->animation_duration;  /* Duration is in seconds */

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

    LOG_INFO("Monitor %s geometry: %dx%d@%dHz scale=%d",
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
