/*
 * wayland.c - Wayland platform implementation
 * 
 * Implements the platform interface for Wayland compositors.
 * Handles Wayland-specific window creation and event management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <GLES2/gl2.h>
#include "../include/platform.h"
#include "../include/hyprlax_internal.h"
#include "../../protocols/wlr-layer-shell-client-protocol.h"

/* Wayland platform private data */
typedef struct {
    /* Core Wayland objects */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_egl_window *egl_window;
    struct wl_output *output;
    
    /* Layer shell protocol */
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    
    /* Window state */
    int width;
    int height;
    bool configured;
    bool running;
} wayland_data_t;

/* Global instance (simplified for now) */
static wayland_data_t *g_wayland_data = NULL;

/* Registry listener callbacks */
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t id, const char *interface, uint32_t version) {
    wayland_data_t *wl_data = (wayland_data_t *)data;
    
    if (strcmp(interface, "wl_compositor") == 0) {
        wl_data->compositor = wl_registry_bind(registry, id,
                                              &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0 && !wl_data->output) {
        wl_data->output = wl_registry_bind(registry, id,
                                          &wl_output_interface, 1);
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        wl_data->layer_shell = wl_registry_bind(registry, id,
                                               &zwlr_layer_shell_v1_interface, 1);
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
    
    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Layer surface configured: %ux%u\n", width, height);
    }
    
    wl_data->width = width;
    wl_data->height = height;
    wl_data->configured = true;
    
    /* Acknowledge the configure event */
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    
    /* Resize EGL window if it exists */
    if (wl_data->egl_window) {
        wl_egl_window_resize(wl_data->egl_window, width, height, 0, 0);
    }
    
    /* TODO: Notify renderer of viewport change through callback mechanism
     * Platform should not make direct OpenGL calls */
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
    
    /* Connect to Wayland display */
    g_wayland_data->display = wl_display_connect(display_name);
    if (!g_wayland_data->display) {
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
            /* Configure as fullscreen background */
            zwlr_layer_surface_v1_set_exclusive_zone(g_wayland_data->layer_surface, -1);
            zwlr_layer_surface_v1_set_anchor(g_wayland_data->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            
            /* Add listener with g_wayland_data as user data */
            zwlr_layer_surface_v1_add_listener(g_wayland_data->layer_surface,
                                              &layer_surface_listener,
                                              g_wayland_data);
            
            /* Commit to get configure event */
            wl_surface_commit(g_wayland_data->surface);
            wl_display_roundtrip(g_wayland_data->display);
        }
    }
    
    /* Create EGL window */
    g_wayland_data->width = config->width;
    g_wayland_data->height = config->height;
    g_wayland_data->egl_window = wl_egl_window_create(g_wayland_data->surface,
                                                      config->width, config->height);
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
    }
    
    event->type = PLATFORM_EVENT_NONE;
    
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

/* Get platform name */
static const char* wayland_get_name(void) {
    return "Wayland";
}

/* Get backend name */
static const char* wayland_get_backend_name(void) {
    /* Could query compositor name */
    return "wayland";
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
    .get_native_display = wayland_get_native_display,
    .get_native_window = wayland_get_native_window,
    .supports_transparency = wayland_supports_transparency,
    .supports_blur = wayland_supports_blur,
    .get_name = wayland_get_name,
    .get_backend_name = wayland_get_backend_name,
};