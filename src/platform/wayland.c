/*
 * wayland.c - Wayland platform implementation
 *
 * Implements the platform interface for Wayland compositors.
 * Handles Wayland-specific window creation and event management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <poll.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <GLES2/gl2.h>
#include "../include/platform.h"
#include "../compositor/workspace_models.h"
#include "../include/hyprlax_internal.h"
#include "../include/log.h"
#include "../include/defaults.h"
#include "../include/renderer.h"
#include "../../protocols/wlr-layer-shell-client-protocol.h"
#include "../include/hyprlax.h"
#include "../core/monitor.h"
#include "../include/wayland_api.h"

/* Output info tracking */
typedef struct output_info {
    struct wl_output *output;
    uint32_t global_id;
    char name[64];
    int width, height;
    int refresh_rate;
    int scale;
    int transform;
    int global_x, global_y;
    struct output_info *next;
} output_info_t;

/* Wayland platform private data */
typedef struct {
    /* Core Wayland objects */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;          /* Legacy single surface */
    struct wl_egl_window *egl_window;    /* Legacy single window */
    struct wl_output *output;            /* Legacy primary output */

    /* Multi-monitor support */
    output_info_t *outputs;              /* All detected outputs */
    int output_count;
    hyprlax_context_t *ctx;              /* Back reference to context */

    /* Layer shell protocol */
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;  /* Legacy single surface */

    /* Window state */
    int width;
    int height;
    bool configured;
    bool running;

    /* Pending resize event */
    bool has_pending_resize;
    int pending_width;
    int pending_height;

    /* Input tracking */
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    double pointer_global_x;
    double pointer_global_y;
    bool pointer_valid;
    struct wl_surface *pointer_surface; /* last focused surface for motion mapping */
} wayland_data_t;

/* Seat & pointer listeners forward declarations */
static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps);
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name);
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void pointer_handle_enter(void *data, struct wl_pointer *ptr, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy);
static void pointer_handle_leave(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *surface);
static void pointer_handle_motion(void *data, struct wl_pointer *ptr, uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
static void pointer_handle_button(void *data, struct wl_pointer *ptr, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void pointer_handle_axis(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis, wl_fixed_t value);
static void pointer_handle_frame(void *data, struct wl_pointer *ptr);
static void pointer_handle_axis_source(void *data, struct wl_pointer *ptr, uint32_t axis_source);
static void pointer_handle_axis_stop(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis);
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *ptr, uint32_t axis, int32_t discrete);
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

/* Global instance (simplified for now) */
static wayland_data_t *g_wayland_data = NULL;

/* Frame callback handling for pacing */
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    monitor_instance_t *monitor = (monitor_instance_t *)data;
    if (monitor) {
        monitor_frame_done(monitor);
        /* time is in ms; store seconds for consistency if needed elsewhere */
        monitor->last_frame_time = time / 1000.0;
    }
    if (cb) {
        wl_callback_destroy(cb);
    }
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

/* Forward for monitor surface creation */
int wayland_create_monitor_surface(monitor_instance_t *monitor);

/* Store context for monitor detection - set by platform init */
void wayland_set_context(hyprlax_context_t *ctx) {
    if (!g_wayland_data) return;
    g_wayland_data->ctx = ctx;

    /* If outputs were discovered before context was set (common when we
       roundtrip during connect), realize them now so rendering can start. */
    if (g_wayland_data->ctx && g_wayland_data->ctx->monitors) {
        output_info_t *info = g_wayland_data->outputs;
        while (info) {
            /* Skip if already added */
            monitor_instance_t *mon = monitor_list_find_by_output(g_wayland_data->ctx->monitors, info->output);
            if (!mon && info->width > 0 && info->height > 0) {
                mon = monitor_instance_create(info->name);
                if (mon) {
                    mon->wl_output = info->output;
                    monitor_update_geometry(mon, info->width, info->height,
                                            info->scale, info->refresh_rate);
                    monitor_set_global_position(mon, info->global_x, info->global_y);

                    config_t *config = monitor_resolve_config(mon, &g_wayland_data->ctx->config);
                    monitor_apply_config(mon, config);

                    if (g_wayland_data->ctx->compositor) {
                        workspace_model_t model = workspace_detect_model_for_adapter(g_wayland_data->ctx->compositor);
                        mon->current_context.model = model;
                        mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(g_wayland_data->ctx->compositor);
                        mon->previous_context = mon->current_context;
                    }

                    monitor_list_add(g_wayland_data->ctx->monitors, mon);

                    /* Defer surface creation until size is valid (we're only creating when size>0 here),
                       but safe to create now as well */
                    int ret = wayland_create_monitor_surface(mon);
                    if (ret == HYPRLAX_SUCCESS) {
                        LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                    } else {
                        LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                    }
                }
            }
            info = info->next;
        }
    }
}

/* Forward declarations */
static void output_handle_geometry(void *data, struct wl_output *output,
                                  int32_t x, int32_t y,
                                  int32_t physical_width, int32_t physical_height,
                                  int32_t subpixel, const char *make, const char *model,
                                  int32_t transform);
static void output_handle_mode(void *data, struct wl_output *output,
                              uint32_t flags, int32_t width, int32_t height,
                              int32_t refresh);
static void output_handle_done(void *data, struct wl_output *output);
static void output_handle_scale(void *data, struct wl_output *output, int32_t scale);
static void output_handle_name(void *data, struct wl_output *output, const char *name);
static void output_handle_description(void *data, struct wl_output *output, const char *description);

/* Output listener */
static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
};

/* Registry listener callbacks */
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t id, const char *interface, uint32_t version) {
    wayland_data_t *wl_data = (wayland_data_t *)data;

    if (strcmp(interface, "wl_compositor") == 0) {
        wl_data->compositor = wl_registry_bind(registry, id,
                                              &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0) {
        /* Bind to ALL outputs for multi-monitor support */
        struct wl_output *output = wl_registry_bind(registry, id,
                                                   &wl_output_interface, 4);

        /* Create output info */
        output_info_t *info = calloc(1, sizeof(output_info_t));
        if (info) {
            info->output = output;
            info->global_id = id;
            info->scale = 1;  /* Default scale */
            snprintf(info->name, sizeof(info->name), "output-%u", id);

            /* Add listener to get output details */
            wl_output_add_listener(output, &output_listener, info);

            /* Add to list */
            info->next = wl_data->outputs;
            wl_data->outputs = info;
            wl_data->output_count++;

            /* Keep first as primary for legacy */
            if (!wl_data->output) {
                wl_data->output = output;
            }

            LOG_DEBUG("Detected output %u (total: %d)", id, wl_data->output_count);
        }
} else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
    wl_data->layer_shell = wl_registry_bind(registry, id,
                                           &zwlr_layer_shell_v1_interface, 1);
} else if (strcmp(interface, "wl_seat") == 0) {
    wl_data->seat = wl_registry_bind(registry, id, &wl_seat_interface, 5);
    if (wl_data->seat) {
        wl_seat_add_listener(wl_data->seat, &seat_listener, wl_data);
    }
}
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                  uint32_t id) {
    /* Handle removal if needed */
    (void)data;
    (void)registry;
    (void)id;
}

/* Layer surface listener callbacks */
static void layer_surface_configure(void *data,
                                   struct zwlr_layer_surface_v1 *layer_surface,
                                   uint32_t serial,
                                   uint32_t width, uint32_t height) {
    wayland_data_t *wl_data = (wayland_data_t *)data;

    LOG_DEBUG("Layer surface configure called: %ux%u", width, height);

    if (width > 0 && height > 0) {
        wl_data->width = width;
        wl_data->height = height;
        wl_data->configured = true;
        LOG_DEBUG("Layer surface dimensions set: %ux%u", width, height);
    } else {
        LOG_WARN("Layer surface configure with invalid dimensions: %ux%u", width, height);
    }

    /* Acknowledge the configure event */
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    /* Resize EGL window if it exists */
    if (wl_data->egl_window) {
        wl_egl_window_resize(wl_data->egl_window, width, height, 0, 0);

        /* Store pending resize event to be picked up in poll_events */
        wl_data->has_pending_resize = true;
        wl_data->pending_width = width;
        wl_data->pending_height = height;
        LOG_DEBUG("Pending resize event stored: %dx%d", width, height);
    }

    /* Ensure monitors are realized once we have dimensions */
    if (wl_data && wl_data->ctx && wl_data->ctx->monitors && wl_data->ctx->monitors->count == 0) {
        LOG_DEBUG("Realizing monitors after layer-surface configure");
        output_info_t *info = wl_data->outputs;
        while (info) {
            if (info->width > 0 && info->height > 0) {
                monitor_instance_t *mon = monitor_list_find_by_output(wl_data->ctx->monitors, info->output);
                if (!mon) {
                    mon = monitor_instance_create(info->name);
                    if (mon) {
                        mon->wl_output = info->output;
                        monitor_update_geometry(mon, info->width, info->height,
                                                info->scale, info->refresh_rate);
                        monitor_set_global_position(mon, info->global_x, info->global_y);
                        config_t *config = monitor_resolve_config(mon, &wl_data->ctx->config);
                        monitor_apply_config(mon, config);
                        if (wl_data->ctx->compositor) {
                            workspace_model_t model = workspace_detect_model_for_adapter(wl_data->ctx->compositor);
                            mon->current_context.model = model;
                            mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(wl_data->ctx->compositor);
                            mon->previous_context = mon->current_context;
                        }
                        monitor_list_add(wl_data->ctx->monitors, mon);
                        int ret2 = wayland_create_monitor_surface(mon);
                        if (ret2 == HYPRLAX_SUCCESS) {
                            LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                        } else {
                            LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                        }
                    }
                }
            }
            info = info->next;
        }
    }
}

static void layer_surface_closed(void *data,
                                struct zwlr_layer_surface_v1 *layer_surface) {
    wayland_data_t *wl_data = (wayland_data_t *)data;
    wl_data->running = false;
    (void)layer_surface;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* Initialize Wayland platform */
static int wayland_init(void) {
    /* Platform-wide initialization if needed */
    return HYPRLAX_SUCCESS;
}

/* Destroy Wayland platform */
static void wayland_destroy(void) {
    /* Platform-wide cleanup if needed */
}

/* Connect to Wayland display */
static int wayland_connect(const char *display_name) {
    if (g_wayland_data) {
        return HYPRLAX_SUCCESS;  /* Already connected */
    }

    g_wayland_data = calloc(1, sizeof(wayland_data_t));
    if (!g_wayland_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Try to connect to Wayland display with retries for startup race condition */
    int max_retries = WAYLAND_CONNECT_MAX_RETRIES;  /* retries */
    int retry_delay_ms = WAYLAND_CONNECT_RETRY_MS;  /* delay ms */
    bool first_attempt = true;

    for (int i = 0; i < max_retries; i++) {
        g_wayland_data->display = wl_display_connect(display_name);
        if (g_wayland_data->display) {
            break;  /* Successfully connected */
        }

        if (first_attempt) {
            LOG_INFO("Waiting for Wayland display to be ready...");
            first_attempt = false;
        }

        /* Sleep before retry using nanosleep for consistency */
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = retry_delay_ms * 1000000L;  /* Convert ms to ns */
        nanosleep(&ts, NULL);
    }

    if (!g_wayland_data->display) {
        LOG_ERROR("Failed to connect to Wayland display after %d attempts", max_retries);
        free(g_wayland_data);
        g_wayland_data = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Get registry and listen for globals */
    g_wayland_data->registry = wl_display_get_registry(g_wayland_data->display);
    wl_registry_add_listener(g_wayland_data->registry, &registry_listener, g_wayland_data);

    /* Roundtrip to receive globals */
    wl_display_roundtrip(g_wayland_data->display);

    if (!g_wayland_data->compositor) {
        wl_display_disconnect(g_wayland_data->display);
        free(g_wayland_data);
        g_wayland_data = NULL;
        return HYPRLAX_ERROR_NO_COMPOSITOR;
    }

    return HYPRLAX_SUCCESS;
}

/* Disconnect from Wayland display */
static void wayland_disconnect(void) {
    if (!g_wayland_data) return;

    /* Destroy per-monitor Wayland resources if context is present */
    if (g_wayland_data->ctx && g_wayland_data->ctx->monitors) {
        monitor_instance_t *mon = g_wayland_data->ctx->monitors->head;
        while (mon) {
            if (mon->wl_egl_window) {
                wl_egl_window_destroy(mon->wl_egl_window);
                mon->wl_egl_window = NULL;
            }
            if (mon->layer_surface) {
                zwlr_layer_surface_v1_destroy(mon->layer_surface);
                mon->layer_surface = NULL;
            }
            if (mon->wl_surface) {
                wl_surface_destroy(mon->wl_surface);
                mon->wl_surface = NULL;
            }
            mon = mon->next;
        }
    }

    if (g_wayland_data->egl_window) {
        wl_egl_window_destroy(g_wayland_data->egl_window);
        g_wayland_data->egl_window = NULL;
    }

    if (g_wayland_data->layer_surface) {
        zwlr_layer_surface_v1_destroy(g_wayland_data->layer_surface);
        g_wayland_data->layer_surface = NULL;
    }

    if (g_wayland_data->surface) {
        wl_surface_destroy(g_wayland_data->surface);
        g_wayland_data->surface = NULL;
    }

    if (g_wayland_data->layer_shell) {
        zwlr_layer_shell_v1_destroy(g_wayland_data->layer_shell);
    }

    if (g_wayland_data->compositor) {
        wl_compositor_destroy(g_wayland_data->compositor);
    }

    if (g_wayland_data->registry) {
        wl_registry_destroy(g_wayland_data->registry);
    }

    if (g_wayland_data->display) {
        wl_display_disconnect(g_wayland_data->display);
    }

    free(g_wayland_data);
    g_wayland_data = NULL;
}

/* Check if connected */
static bool wayland_is_connected(void) {
    return g_wayland_data && g_wayland_data->display;
}

/* Create window */
static int wayland_create_window(const window_config_t *config) {
    if (!config || !g_wayland_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Create surface */
    g_wayland_data->surface = wl_compositor_create_surface(g_wayland_data->compositor);
    if (!g_wayland_data->surface) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Create layer surface if layer shell is available */
    if (g_wayland_data->layer_shell) {
        g_wayland_data->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            g_wayland_data->layer_shell,
            g_wayland_data->surface,
            g_wayland_data->output,  /* NULL means default output */
            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
            "hyprlax");

        if (g_wayland_data->layer_surface) {
            /* Configure as fullscreen background (input-transparent, non-interactive) */
            zwlr_layer_surface_v1_set_exclusive_zone(g_wayland_data->layer_surface, WAYLAND_EXCLUSIVE_ZONE_BACKGROUND);
            zwlr_layer_surface_v1_set_anchor(g_wayland_data->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

            /* Do not accept keyboard focus on wallpaper */
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                g_wayland_data->layer_surface,
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

            /* Make the background surface input-transparent */
            if (g_wayland_data->compositor && g_wayland_data->surface) {
                struct wl_region *empty = wl_compositor_create_region(g_wayland_data->compositor);
                if (empty) {
                    wl_surface_set_input_region(g_wayland_data->surface, empty);
                    wl_region_destroy(empty);
                }
            }

            /* Add listener with g_wayland_data as user data */
            zwlr_layer_surface_v1_add_listener(g_wayland_data->layer_surface,
                                              &layer_surface_listener,
                                              g_wayland_data);

            /* First commit and roundtrip to let compositor know about the layer surface */
            wl_surface_commit(g_wayland_data->surface);
            wl_display_roundtrip(g_wayland_data->display);

            /* Now create EGL window with initial 1x1 dimensions (will be resized on configure) */
            g_wayland_data->egl_window = wl_egl_window_create(g_wayland_data->surface, WAYLAND_EGL_INITIAL_W, WAYLAND_EGL_INITIAL_H);
            if (!g_wayland_data->egl_window) {
                if (g_wayland_data->layer_surface) {
                    zwlr_layer_surface_v1_destroy(g_wayland_data->layer_surface);
                }
                wl_surface_destroy(g_wayland_data->surface);
                g_wayland_data->surface = NULL;
                return HYPRLAX_ERROR_NO_MEMORY;
            }

            /* Commit again to trigger configuration with the EGL window */
            wl_surface_commit(g_wayland_data->surface);
            wl_display_flush(g_wayland_data->display);

            /* Wait for initial configuration - the configure callback will resize the EGL window */
            while (!g_wayland_data->configured) {
                wl_display_dispatch(g_wayland_data->display);
            }

            LOG_DEBUG("Configure event received with dimensions: %dx%d",
                    g_wayland_data->width, g_wayland_data->height);

            return HYPRLAX_SUCCESS;
        }
    }

    /* Non-layer-shell path - create EGL window with config dimensions */
    g_wayland_data->egl_window = wl_egl_window_create(g_wayland_data->surface,
                                                      config->width,
                                                      config->height);
    if (!g_wayland_data->egl_window) {
        if (g_wayland_data->layer_surface) {
            zwlr_layer_surface_v1_destroy(g_wayland_data->layer_surface);
        }
        wl_surface_destroy(g_wayland_data->surface);
        g_wayland_data->surface = NULL;
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Commit the surface */
    wl_surface_commit(g_wayland_data->surface);

    return HYPRLAX_SUCCESS;
}

/* Destroy window */
static void wayland_destroy_window(void) {
    if (!g_wayland_data) return;

    if (g_wayland_data->egl_window) {
        wl_egl_window_destroy(g_wayland_data->egl_window);
        g_wayland_data->egl_window = NULL;
    }

    if (g_wayland_data->layer_surface) {
        zwlr_layer_surface_v1_destroy(g_wayland_data->layer_surface);
        g_wayland_data->layer_surface = NULL;
    }

    if (g_wayland_data->surface) {
        wl_surface_destroy(g_wayland_data->surface);
        g_wayland_data->surface = NULL;
    }
}

/* Show window */
static void wayland_show_window(void) {
    if (g_wayland_data && g_wayland_data->surface) {
        wl_surface_commit(g_wayland_data->surface);
    }
}

/* Create surface for a specific monitor */
int wayland_create_monitor_surface(monitor_instance_t *monitor) {
    if (!monitor || !g_wayland_data || !g_wayland_data->compositor) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Create surface for this monitor */
    monitor->wl_surface = wl_compositor_create_surface(g_wayland_data->compositor);
    if (!monitor->wl_surface) {
        LOG_ERROR("Failed to create surface for monitor %s", monitor->name);
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Create layer surface bound to specific output */
    if (g_wayland_data->layer_shell && monitor->wl_output) {
        monitor->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            g_wayland_data->layer_shell,
            monitor->wl_surface,
            monitor->wl_output,  /* Bind to specific output */
            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
            "hyprlax");

        if (monitor->layer_surface) {
            /* Configure as fullscreen background (input-transparent, non-interactive) */
            zwlr_layer_surface_v1_set_exclusive_zone(monitor->layer_surface, WAYLAND_EXCLUSIVE_ZONE_BACKGROUND);
            zwlr_layer_surface_v1_set_anchor(monitor->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

            /* Do not accept keyboard focus on wallpaper */
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                monitor->layer_surface,
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

            /* Make this monitor's background surface input-transparent */
            if (g_wayland_data->compositor && monitor->wl_surface) {
                struct wl_region *empty = wl_compositor_create_region(g_wayland_data->compositor);
                if (empty) {
                    wl_surface_set_input_region(monitor->wl_surface, empty);
                    wl_region_destroy(empty);
                }
            }

            /* Set size to 0,0 to let compositor decide */
            zwlr_layer_surface_v1_set_size(monitor->layer_surface, 0, 0);

            /* Add listener for this monitor's layer surface */
            zwlr_layer_surface_v1_add_listener(monitor->layer_surface,
                                              &layer_surface_listener,
                                              g_wayland_data);

            /* Commit to get configure event */
            wl_surface_commit(monitor->wl_surface);

            LOG_DEBUG("Created layer surface for monitor %s", monitor->name);
        } else {
            LOG_ERROR("Failed to create layer surface for monitor %s", monitor->name);
            wl_surface_destroy(monitor->wl_surface);
            monitor->wl_surface = NULL;
            return HYPRLAX_ERROR_NO_MEMORY;
        }
    }

    /* Create EGL window for this surface */
    if (monitor->wl_surface) {
        monitor->wl_egl_window = wl_egl_window_create(
            monitor->wl_surface,
            monitor->width * monitor->scale,
            monitor->height * monitor->scale);

        if (!monitor->wl_egl_window) {
            LOG_ERROR("Failed to create EGL window for monitor %s", monitor->name);
            /* Clean up */
            if (monitor->layer_surface) {
                zwlr_layer_surface_v1_destroy(monitor->layer_surface);
                monitor->layer_surface = NULL;
            }
            wl_surface_destroy(monitor->wl_surface);
            monitor->wl_surface = NULL;
            return HYPRLAX_ERROR_NO_MEMORY;
        }

        /* Create EGL surface for this monitor if we have a renderer context */
        if (g_wayland_data->ctx && g_wayland_data->ctx->renderer) {
            /* Get the EGL surface creation function from renderer */
            monitor->egl_surface = gles2_create_monitor_surface(monitor->wl_egl_window);

            if (monitor->egl_surface) {
                LOG_DEBUG("Created EGL surface for monitor %s", monitor->name);
            } else {
                LOG_WARN("Failed to create EGL surface for monitor %s", monitor->name);
            }
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Hide window */
static void wayland_hide_window(void) {
    /* Hiding is typically done by unmapping, but for layer-shell
       we'll just leave it to the compositor */
}

/* Poll for events */
static int wayland_poll_events(platform_event_t *event) {
    if (!event) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Dispatch pending Wayland events */
    if (g_wayland_data && g_wayland_data->display) {
        /* First dispatch any pending events */
        wl_display_dispatch_pending(g_wayland_data->display);
        /* Then flush any pending requests */
        wl_display_flush(g_wayland_data->display);

        /* Opportunistically drain readable Wayland events without blocking */
        int fd = wl_display_get_fd(g_wayland_data->display);
        if (wl_display_prepare_read(g_wayland_data->display) == 0) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
            int pret = poll(&pfd, 1, 0);
            if (pret > 0) {
                wl_display_read_events(g_wayland_data->display);
            } else {
                wl_display_cancel_read(g_wayland_data->display);
            }
        } else {
            wl_display_dispatch_pending(g_wayland_data->display);
        }
    }

    /* Check for pending resize event */
    if (g_wayland_data && g_wayland_data->has_pending_resize) {
        event->type = PLATFORM_EVENT_RESIZE;
        event->data.resize.width = g_wayland_data->pending_width;
        event->data.resize.height = g_wayland_data->pending_height;
        g_wayland_data->has_pending_resize = false;
        LOG_DEBUG("Returning resize event: %dx%d",
                g_wayland_data->pending_width, g_wayland_data->pending_height);
        return HYPRLAX_SUCCESS;
    }

    event->type = PLATFORM_EVENT_NONE;

    /* Fallback: if no monitors realized yet but outputs are known with size,
       realize them opportunistically during polling. */
    if (g_wayland_data && g_wayland_data->ctx && g_wayland_data->ctx->monitors &&
        g_wayland_data->ctx->monitors->count == 0) {
        output_info_t *info = g_wayland_data->outputs;
        int realized = 0;
        while (info) {
            if (info->width > 0 && info->height > 0) {
                monitor_instance_t *mon = monitor_list_find_by_output(g_wayland_data->ctx->monitors, info->output);
                if (!mon) {
                    mon = monitor_instance_create(info->name);
                    if (mon) {
                        mon->wl_output = info->output;
                        monitor_update_geometry(mon, info->width, info->height,
                                                info->scale, info->refresh_rate);
                        monitor_set_global_position(mon, info->global_x, info->global_y);
                        config_t *config = monitor_resolve_config(mon, &g_wayland_data->ctx->config);
                        monitor_apply_config(mon, config);
                        if (g_wayland_data->ctx->compositor) {
                            workspace_model_t model = workspace_detect_model_for_adapter(g_wayland_data->ctx->compositor);
                            mon->current_context.model = model;
                            mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(g_wayland_data->ctx->compositor);
                            mon->previous_context = mon->current_context;
                        }
                        monitor_list_add(g_wayland_data->ctx->monitors, mon);
                        int ret2 = wayland_create_monitor_surface(mon);
                        if (ret2 == HYPRLAX_SUCCESS) {
                            LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                        } else {
                            LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                        }
                        realized++;
                    }
                }
            }
            info = info->next;
        }
        if (realized > 0) {
            LOG_INFO("Realized %d monitor(s) during poll", realized);
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Wait for events with timeout */
static int wayland_wait_events(platform_event_t *event, int timeout_ms) {
    if (!event) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* In real implementation, use poll() on Wayland fd */
    event->type = PLATFORM_EVENT_NONE;

    return HYPRLAX_SUCCESS;
}

/* Flush pending events */
static void wayland_flush_events(void) {
    if (g_wayland_data && g_wayland_data->display) {
        /* Commit the surface to show rendered content */
        if (g_wayland_data->surface) {
            wl_surface_commit(g_wayland_data->surface);
        }
        /* Flush display to send all pending requests */
        wl_display_flush(g_wayland_data->display);
    }
}

/* Return Wayland display FD for blocking waits */
static int wayland_get_event_fd(void) {
    if (g_wayland_data && g_wayland_data->display) {
        return wl_display_get_fd(g_wayland_data->display);
    }
    return -1;
}

/* Public helper to realize monitors immediately if not present */
void wayland_realize_monitors_now(void) {
    wayland_data_t *wl_data = g_wayland_data;
    if (!wl_data || !wl_data->ctx || !wl_data->ctx->monitors) return;
    if (wl_data->ctx->monitors->count > 0) return;
    output_info_t *info = wl_data->outputs;
    int realized = 0;
    while (info) {
        if (info->width > 0 && info->height > 0) {
            monitor_instance_t *mon = monitor_list_find_by_output(wl_data->ctx->monitors, info->output);
            if (!mon) {
                mon = monitor_instance_create(info->name);
                if (mon) {
                    mon->wl_output = info->output;
                    monitor_update_geometry(mon, info->width, info->height,
                                            info->scale, info->refresh_rate);
                    monitor_set_global_position(mon, info->global_x, info->global_y);
                    config_t *config = monitor_resolve_config(mon, &wl_data->ctx->config);
                    monitor_apply_config(mon, config);
                    if (wl_data->ctx->compositor) {
                        workspace_model_t model = workspace_detect_model_for_adapter(wl_data->ctx->compositor);
                        mon->current_context.model = model;
                        mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(wl_data->ctx->compositor);
                        mon->previous_context = mon->current_context;
                    }
                    monitor_list_add(wl_data->ctx->monitors, mon);
                    int ret2 = wayland_create_monitor_surface(mon);
                    if (ret2 == HYPRLAX_SUCCESS) {
                        LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                    } else {
                        LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                    }
                    realized++;
                }
            }
        }
        info = info->next;
    }
    if (realized > 0) {
        LOG_INFO("Realized %d monitor(s) on demand", realized);
    }
}

/* Get native display handle */
static void* wayland_get_native_display(void) {
    if (g_wayland_data) {
        return g_wayland_data->display;
    }
    return NULL;
}

/* Get native window handle */
static void* wayland_get_native_window(void) {
    if (g_wayland_data) {
        return g_wayland_data->egl_window;
    }
    return NULL;
}

/* Get window dimensions - global helper */
void wayland_get_window_size(int *width, int *height) {
    if (g_wayland_data) {
        if (width) *width = g_wayland_data->width;
        if (height) *height = g_wayland_data->height;
    } else {
        if (width) *width = 1920;
        if (height) *height = 1080;
    }
}

/* Check transparency support */
static bool wayland_supports_transparency(void) {
    return true;  /* Wayland supports transparency */
}

/* Check blur support */
static bool wayland_supports_blur(void) {
    /* Depends on compositor, but generally supported */
    return true;
}

/* Output listener callbacks */
static void output_handle_geometry(void *data, struct wl_output *output,
                                  int32_t x, int32_t y,
                                  int32_t physical_width, int32_t physical_height,
                                  int32_t subpixel, const char *make, const char *model,
                                  int32_t transform) {
    output_info_t *info = (output_info_t *)data;
    if (info) {
        info->global_x = x;
        info->global_y = y;
        info->transform = transform;
        LOG_TRACE("Output geometry: %s at (%d,%d) transform=%d",
                info->name, x, y, transform);
    }
}

static void output_handle_mode(void *data, struct wl_output *output,
                              uint32_t flags, int32_t width, int32_t height,
                              int32_t refresh) {
    output_info_t *info = (output_info_t *)data;
    if (info && (flags & WL_OUTPUT_MODE_CURRENT)) {
        info->width = width;
        info->height = height;
        info->refresh_rate = refresh / 1000;  /* mHz to Hz */
        LOG_TRACE("Output mode: %s %dx%d@%dHz",
                info->name, width, height, info->refresh_rate);

        /* If context is available and monitor not yet created, realize it now */
        if (g_wayland_data && g_wayland_data->ctx && g_wayland_data->ctx->monitors) {
            monitor_instance_t *mon = monitor_list_find_by_output(g_wayland_data->ctx->monitors, output);
            if (!mon && width > 0 && height > 0) {
                mon = monitor_instance_create(info->name);
                if (mon) {
                    mon->wl_output = output;
                    monitor_update_geometry(mon, info->width, info->height,
                                            info->scale, info->refresh_rate);
                    monitor_set_global_position(mon, info->global_x, info->global_y);
                    config_t *config = monitor_resolve_config(mon, &g_wayland_data->ctx->config);
                    monitor_apply_config(mon, config);
                    if (g_wayland_data->ctx->compositor) {
                        workspace_model_t model = workspace_detect_model_for_adapter(g_wayland_data->ctx->compositor);
                        mon->current_context.model = model;
                        mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(g_wayland_data->ctx->compositor);
                        mon->previous_context = mon->current_context;
                    }
                    monitor_list_add(g_wayland_data->ctx->monitors, mon);
                    int ret = wayland_create_monitor_surface(mon);
                    if (ret == HYPRLAX_SUCCESS) {
                        LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                    } else {
                        LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                    }
                }
            }
        }
    }
}

static void output_handle_done(void *data, struct wl_output *output) {
    output_info_t *info = (output_info_t *)data;
    if (!info || !g_wayland_data) return;

    /* When output configuration is complete, create monitor instance if context exists */
    if (g_wayland_data->ctx && g_wayland_data->ctx->monitors) {
        /* Check if monitor already exists */
        monitor_instance_t *mon = monitor_list_find_by_output(g_wayland_data->ctx->monitors, output);
        if (!mon) {
            /* Create new monitor instance */
            mon = monitor_instance_create(info->name);
            if (mon) {
                mon->wl_output = output;
                monitor_update_geometry(mon, info->width, info->height,
                                      info->scale, info->refresh_rate);
                monitor_set_global_position(mon, info->global_x, info->global_y);

                /* Resolve and apply config */
                config_t *config = monitor_resolve_config(mon, &g_wayland_data->ctx->config);
                monitor_apply_config(mon, config);

                /* Set initial workspace context from compositor */
                if (g_wayland_data->ctx->compositor) {
                    workspace_model_t model = workspace_detect_model_for_adapter(g_wayland_data->ctx->compositor);
                    mon->current_context.model = model;
                    mon->current_context.data.workspace_id = COMPOSITOR_GET_WORKSPACE(g_wayland_data->ctx->compositor);
                    mon->previous_context = mon->current_context;
                }

                /* Add to monitor list */
                monitor_list_add(g_wayland_data->ctx->monitors, mon);

                /* Create surface for this monitor if in multi-monitor mode */
                if (g_wayland_data->ctx->monitor_mode == MULTI_MON_ALL ||
                    (g_wayland_data->ctx->monitor_mode == MULTI_MON_PRIMARY && mon->is_primary)) {
                    /* Create surface for this monitor */
                    int ret = wayland_create_monitor_surface(mon);
                    if (ret == HYPRLAX_SUCCESS) {
                        LOG_DEBUG("Successfully created surface for monitor %s", mon->name);
                    } else {
                        LOG_ERROR("Failed to create surface for monitor %s", mon->name);
                    }
                }
            }
        }
    }
}

static void output_handle_scale(void *data, struct wl_output *output, int32_t scale) {
    output_info_t *info = (output_info_t *)data;
    if (info) {
        info->scale = scale;
        LOG_TRACE("Output scale: %s scale=%d", info->name, scale);
    }
}

static void output_handle_name(void *data, struct wl_output *output, const char *name) {
    output_info_t *info = (output_info_t *)data;
    if (info && name) {
        strncpy(info->name, name, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        LOG_DEBUG("Output name: %s", info->name);
    }
}

/* Seat & pointer listener impl */
static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    wayland_data_t *wl_data = (wayland_data_t *)data;
    if (!wl_data) return;
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        if (!wl_data->pointer) {
            wl_data->pointer = wl_seat_get_pointer(seat);
            if (wl_data->pointer) wl_pointer_add_listener(wl_data->pointer, &pointer_listener, wl_data);
        }
    } else {
        if (wl_data->pointer) {
            wl_pointer_destroy(wl_data->pointer);
            wl_data->pointer = NULL;
            wl_data->pointer_valid = false;
        }
    }
}
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static void pointer_update_global_from_surface(wayland_data_t *wl_data, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    if (!wl_data || !wl_data->ctx || !wl_data->ctx->monitors) return;
    monitor_instance_t *mon = wl_data->ctx->monitors->head;
    while (mon) {
        if (mon->wl_surface == surface) {
            wl_data->pointer_global_x = mon->global_x + wl_fixed_to_double(sx);
            wl_data->pointer_global_y = mon->global_y + wl_fixed_to_double(sy);
            wl_data->pointer_valid = true;
            return;
        }
        mon = mon->next;
    }
}

static void pointer_handle_enter(void *data, struct wl_pointer *ptr, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)ptr; (void)serial;
    wayland_data_t *wl_data = (wayland_data_t*)data;
    if (!wl_data) return;
    wl_data->pointer_surface = surface;
    pointer_update_global_from_surface(wl_data, surface, sx, sy);
}
static void pointer_handle_leave(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *surface) {
    wayland_data_t *wl_data = (wayland_data_t*)data;
    (void)ptr; (void)serial; (void)surface;
    if (wl_data) {
        wl_data->pointer_surface = NULL;
        /* If configured to animate only on background, clear validity on leave */
        if (wl_data->ctx && !wl_data->ctx->config.cursor_follow_global) {
            wl_data->pointer_valid = false;
        }
    }
}
static void pointer_handle_motion(void *data, struct wl_pointer *ptr, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    wayland_data_t *wl_data = (wayland_data_t*)data;
    (void)ptr; (void)time;
    if (!wl_data || !wl_data->pointer_surface) return;
    pointer_update_global_from_surface(wl_data, wl_data->pointer_surface, sx, sy);
}
static void pointer_handle_button(void *data, struct wl_pointer *ptr, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)data; (void)ptr; (void)serial; (void)time; (void)button; (void)state;
}
static void pointer_handle_axis(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)ptr; (void)time; (void)axis; (void)value;
}

static void pointer_handle_frame(void *data, struct wl_pointer *ptr) {
    (void)data; (void)ptr;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *ptr, uint32_t axis_source) {
    (void)data; (void)ptr; (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis) {
    (void)data; (void)ptr; (void)time; (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *ptr, uint32_t axis, int32_t discrete) {
    (void)data; (void)ptr; (void)axis; (void)discrete;
}

bool wayland_get_cursor_global(double *x, double *y) {
    if (!g_wayland_data || !g_wayland_data->pointer_valid) return false;
    if (x) *x = g_wayland_data->pointer_global_x;
    if (y) *y = g_wayland_data->pointer_global_y;
    return true;
}

static void output_handle_description(void *data, struct wl_output *output, const char *description) {
    /* Optional: Store description if needed */
    (void)data;
    (void)output;
    (void)description;
}

/* Get platform name */
static const char* wayland_get_name(void) {
    return "Wayland";
}

/* Get backend name */
static const char* wayland_get_backend_name(void) {
    /* Could query compositor name */
    return "wayland";
}

/* Commit a monitor's Wayland surface */
void wayland_commit_monitor_surface(monitor_instance_t *monitor) {
    if (monitor && monitor->wl_surface) {
        /* Request a frame callback to pace the next frame if not already pending */
        const char *use_fc = getenv("HYPRLAX_FRAME_CALLBACK");
        if (use_fc && *use_fc && !monitor->frame_pending) {
            struct wl_callback *cb = wl_surface_frame(monitor->wl_surface);
            if (cb) {
                monitor->frame_callback = cb;
                wl_callback_add_listener(cb, &frame_listener, monitor);
                monitor_mark_frame_pending(monitor);
            }
        }
        wl_surface_commit(monitor->wl_surface);
        if (g_wayland_data && g_wayland_data->display) {
            wl_display_flush(g_wayland_data->display);
        }
    }
}

/* Wayland platform operations */
const platform_ops_t platform_wayland_ops = {
    .init = wayland_init,
    .destroy = wayland_destroy,
    .connect = wayland_connect,
    .disconnect = wayland_disconnect,
    .is_connected = wayland_is_connected,
    .create_window = wayland_create_window,
    .destroy_window = wayland_destroy_window,
    .show_window = wayland_show_window,
    .hide_window = wayland_hide_window,
    .poll_events = wayland_poll_events,
    .wait_events = wayland_wait_events,
    .flush_events = wayland_flush_events,
    .get_event_fd = wayland_get_event_fd,
    .get_native_display = wayland_get_native_display,
    .get_native_window = wayland_get_native_window,
    .get_window_size = wayland_get_window_size,
    .commit_monitor_surface = wayland_commit_monitor_surface,
    .get_cursor_global = wayland_get_cursor_global,
    .realize_monitors = wayland_realize_monitors_now,
    .set_context = wayland_set_context,
    .supports_transparency = wayland_supports_transparency,
    .supports_blur = wayland_supports_blur,
    .get_name = wayland_get_name,
    .get_backend_name = wayland_get_backend_name,
};
