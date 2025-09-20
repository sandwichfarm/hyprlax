/*
 * generic_wayland.c - Generic Wayland compositor adapter
 *
 * Implements basic wlr-layer-shell support for compositors that
 * don't have specific IPC interfaces (River, Wayfire, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Generic Wayland private data */
typedef struct {
    int current_workspace;
    int current_monitor;
} generic_wayland_data_t;

static generic_wayland_data_t *g_generic_data = NULL;

/* Initialize generic Wayland adapter */
static int generic_wayland_init(void *platform_data) {
    (void)platform_data;

    if (g_generic_data) {
        return HYPRLAX_SUCCESS;
    }

    g_generic_data = calloc(1, sizeof(generic_wayland_data_t));
    if (!g_generic_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    g_generic_data->current_workspace = 1;
    g_generic_data->current_monitor = 0;

    return HYPRLAX_SUCCESS;
}

/* Destroy generic Wayland adapter */
static void generic_wayland_destroy(void) {
    if (g_generic_data) {
        free(g_generic_data);
        g_generic_data = NULL;
    }
}

/* Detect generic Wayland session */
static bool generic_wayland_detect(void) {
    /* This is a fallback for any Wayland compositor */
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    return (wayland_display && *wayland_display);
}

/* Get compositor name */
static const char* generic_wayland_get_name(void) {
    /* Try to detect specific compositor */
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (desktop) {
        return desktop;
    }
    return "Generic Wayland";
}

/* Create layer surface using wlr-layer-shell */
static int generic_wayland_create_layer_surface(void *surface,
                                               const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* Platform layer handles the actual protocol */
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void generic_wayland_configure_layer_surface(void *layer_surface,
                                                   int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
}

/* Destroy layer surface */
static void generic_wayland_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
}

/* Get current workspace - generic compositors may not expose this */
static int generic_wayland_get_current_workspace(void) {
    if (!g_generic_data) return 1;
    return g_generic_data->current_workspace;
}

/* Get workspace count */
static int generic_wayland_get_workspace_count(void) {
    return 1;  /* Assume single workspace without IPC */
}

/* List workspaces */
static int generic_wayland_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Single workspace for generic compositor */
    *count = 1;
    *workspaces = calloc(1, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    (*workspaces)[0].id = 1;
    strncpy((*workspaces)[0].name, "1", sizeof((*workspaces)[0].name));
    (*workspaces)[0].active = true;
    (*workspaces)[0].visible = true;

    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int generic_wayland_get_current_monitor(void) {
    if (!g_generic_data) return 0;
    return g_generic_data->current_monitor;
}

/* List monitors */
static int generic_wayland_list_monitors(monitor_info_t **monitors, int *count) {
    if (!monitors || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Single monitor assumption */
    *count = 1;
    *monitors = calloc(1, sizeof(monitor_info_t));
    if (!*monitors) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    (*monitors)[0].id = 0;
    strncpy((*monitors)[0].name, "default", sizeof((*monitors)[0].name));
    (*monitors)[0].x = 0;
    (*monitors)[0].y = 0;
    (*monitors)[0].width = 1920;  /* Default resolution */
    (*monitors)[0].height = 1080;
    (*monitors)[0].scale = 1.0;
    (*monitors)[0].primary = true;

    return HYPRLAX_SUCCESS;
}

/* No IPC for generic compositor */
static int generic_wayland_connect_ipc(const char *socket_path) {
    (void)socket_path;
    return HYPRLAX_SUCCESS;  /* No-op */
}

static void generic_wayland_disconnect_ipc(void) {
    /* No-op */
}

static int generic_wayland_poll_events(compositor_event_t *event) {
    if (!event) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    memset(event, 0, sizeof(*event));
    return HYPRLAX_SUCCESS;
}

static int generic_wayland_send_command(const char *command, char *response,
                                       size_t response_size) {
    (void)command;
    (void)response;
    (void)response_size;
    return HYPRLAX_ERROR_INVALID_ARGS;  /* No IPC */
}

/* Feature support - conservative defaults */
static bool generic_wayland_supports_blur(void) {
    return false;  /* Most compositors don't have blur */
}

static bool generic_wayland_supports_transparency(void) {
    return true;  /* Basic transparency usually works */
}

static bool generic_wayland_supports_animations(void) {
    return false;  /* No animation control without IPC */
}

static int generic_wayland_set_blur(float amount) {
    (void)amount;
    return HYPRLAX_ERROR_INVALID_ARGS;
}

static int generic_wayland_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* Would update layer surface position */
    return HYPRLAX_SUCCESS;
}

/* No event fd for generic compositor */
static int generic_wayland_get_event_fd(void) {
    return -1;
}

/* Generic Wayland compositor operations */
const compositor_ops_t compositor_generic_wayland_ops = {
    .init = generic_wayland_init,
    .destroy = generic_wayland_destroy,
    .detect = generic_wayland_detect,
    .get_name = generic_wayland_get_name,
    .create_layer_surface = generic_wayland_create_layer_surface,
    .configure_layer_surface = generic_wayland_configure_layer_surface,
    .destroy_layer_surface = generic_wayland_destroy_layer_surface,
    .get_current_workspace = generic_wayland_get_current_workspace,
    .get_workspace_count = generic_wayland_get_workspace_count,
    .list_workspaces = generic_wayland_list_workspaces,
    .get_current_monitor = generic_wayland_get_current_monitor,
    .list_monitors = generic_wayland_list_monitors,
    .connect_ipc = generic_wayland_connect_ipc,
    .disconnect_ipc = generic_wayland_disconnect_ipc,
    .poll_events = generic_wayland_poll_events,
    .send_command = generic_wayland_send_command,
    .get_event_fd = generic_wayland_get_event_fd,
    .supports_blur = generic_wayland_supports_blur,
    .supports_transparency = generic_wayland_supports_transparency,
    .supports_animations = generic_wayland_supports_animations,
    .set_blur = generic_wayland_set_blur,
    .set_wallpaper_offset = generic_wayland_set_wallpaper_offset,
};
