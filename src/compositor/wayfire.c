/*
 * wayfire.c - Wayfire compositor adapter
 *
 * Implements compositor interface for Wayfire, including
 * support for 2D workspace grid navigation.
 */

#include <stdio.h>
#include "../include/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Wayfire IPC commands */
#define WAYFIRE_IPC_GET_WORKSPACES "list-workspaces"
#define WAYFIRE_IPC_GET_CURRENT_WORKSPACE "get-current-workspace"
#define WAYFIRE_IPC_GET_OUTPUTS "list-outputs"

/* Wayfire private data */
typedef struct {
    int ipc_fd;           /* IPC socket */
    char socket_path[256];
    bool connected;
    /* 2D workspace grid tracking */
    int current_x;
    int current_y;
    int grid_width;      /* Number of workspaces horizontally */
    int grid_height;     /* Number of workspaces vertically */
} wayfire_data_t;

/* Global instance */
static wayfire_data_t *g_wayfire_data = NULL;

/* Forward declarations */
static int wayfire_send_command(const char *command, char *response, size_t response_size);

/* Get Wayfire socket path */
static bool get_wayfire_socket_path(char *path, size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");

    if (!runtime_dir || !wayland_display) {
        return false;
    }

    /* Wayfire socket is typically at $XDG_RUNTIME_DIR/wayfire-$WAYLAND_DISPLAY.sock */
    snprintf(path, size, "%s/wayfire-%s.sock", runtime_dir, wayland_display);

    /* Check if socket exists */
    if (access(path, F_OK) != 0) {
        /* Try alternate path */
        snprintf(path, size, "%s/wayfire.sock", runtime_dir);
        if (access(path, F_OK) != 0) {
            return false;
        }
    }

    return true;
}

/* Initialize Wayfire adapter */
static int wayfire_init(void *platform_data) {
    (void)platform_data;

    if (g_wayfire_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }

    g_wayfire_data = calloc(1, sizeof(wayfire_data_t));
    if (!g_wayfire_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    g_wayfire_data->ipc_fd = -1;
    g_wayfire_data->connected = false;
    g_wayfire_data->current_x = 0;
    g_wayfire_data->current_y = 0;
    g_wayfire_data->grid_width = 3;   /* Default 3x3 grid */
    g_wayfire_data->grid_height = 3;

    return HYPRLAX_SUCCESS;
}

/* Destroy Wayfire adapter */
static void wayfire_destroy(void) {
    if (!g_wayfire_data) return;

    if (g_wayfire_data->ipc_fd >= 0) {
        close(g_wayfire_data->ipc_fd);
    }

    free(g_wayfire_data);
    g_wayfire_data = NULL;
}

/* Detect if running under Wayfire */
static bool wayfire_detect(void) {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");

    if (desktop && strcasecmp(desktop, "wayfire") == 0) {
        return true;
    }

    if (session && strcasecmp(session, "wayfire") == 0) {
        return true;
    }

    /* Check for Wayfire-specific socket */
    char socket_path[256];
    if (get_wayfire_socket_path(socket_path, sizeof(socket_path))) {
        return true;
    }

    return false;
}

/* Get compositor name */
static const char* wayfire_get_name(void) {
    return "Wayfire";
}

/* Create layer surface (uses wlr-layer-shell) */
static int wayfire_create_layer_surface(void *surface,
                                       const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* This will be handled by the platform layer with wlr-layer-shell protocol */
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void wayfire_configure_layer_surface(void *layer_surface,
                                           int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
    /* Handled by platform layer */
}

/* Destroy layer surface */
static void wayfire_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
    /* Handled by platform layer */
}

/* Get current workspace (returns linear ID) */
static int wayfire_get_current_workspace(void) {
    if (!g_wayfire_data) return 0;

    /* Convert 2D coordinates to linear ID */
    return g_wayfire_data->current_y * g_wayfire_data->grid_width + g_wayfire_data->current_x;
}

/* Get workspace count */
static int wayfire_get_workspace_count(void) {
    if (!g_wayfire_data) return 9;

    return g_wayfire_data->grid_width * g_wayfire_data->grid_height;
}

/* List workspaces */
static int wayfire_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_wayfire_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    int total = g_wayfire_data->grid_width * g_wayfire_data->grid_height;
    *count = total;
    *workspaces = calloc(total, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Create workspace entries for the grid */
    for (int y = 0; y < g_wayfire_data->grid_height; y++) {
        for (int x = 0; x < g_wayfire_data->grid_width; x++) {
            int idx = y * g_wayfire_data->grid_width + x;
            (*workspaces)[idx].id = idx;
            snprintf((*workspaces)[idx].name, sizeof((*workspaces)[idx].name),
                     "%d,%d", x, y);
            (*workspaces)[idx].x = x;
            (*workspaces)[idx].y = y;
            (*workspaces)[idx].active = (x == g_wayfire_data->current_x &&
                                      y == g_wayfire_data->current_y);
            (*workspaces)[idx].visible = (*workspaces)[idx].active;
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int wayfire_get_current_monitor(void) {
    return 0;  /* Simplified for now */
}

/* List monitors */
static int wayfire_list_monitors(monitor_info_t **monitors, int *count) {
    if (!monitors || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Simplified implementation */
    *count = 1;
    *monitors = calloc(1, sizeof(monitor_info_t));
    if (!*monitors) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    (*monitors)[0].id = 0;
    strncpy((*monitors)[0].name, "default", sizeof((*monitors)[0].name));
    (*monitors)[0].x = 0;
    (*monitors)[0].y = 0;
    (*monitors)[0].width = 1920;
    (*monitors)[0].height = 1080;
    (*monitors)[0].scale = 1.0;
    (*monitors)[0].primary = true;

    return HYPRLAX_SUCCESS;
}

/* Connect socket helper */
static int connect_wayfire_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Make socket non-blocking for event polling */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

/* Connect to Wayfire IPC */
static int wayfire_connect_ipc(const char *socket_path) {
    (void)socket_path; /* Optional parameter */

    if (!g_wayfire_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (g_wayfire_data->connected) {
        return HYPRLAX_SUCCESS;
    }

    /* Get socket path */
    if (!get_wayfire_socket_path(g_wayfire_data->socket_path,
                                sizeof(g_wayfire_data->socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Connect to IPC socket with retries for startup race condition */
    g_wayfire_data->ipc_fd = compositor_connect_socket_with_retry(
        g_wayfire_data->socket_path,
        "Wayfire",
        30,     /* max retries */
        500     /* delay ms */
    );
    if (g_wayfire_data->ipc_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    g_wayfire_data->connected = true;

    /* Query initial workspace configuration */
    char response[1024];
    if (wayfire_send_command(WAYFIRE_IPC_GET_CURRENT_WORKSPACE, response,
                            sizeof(response)) == HYPRLAX_SUCCESS) {
        /* Parse response to get current x,y position */
        /* Format expected: {"x": 0, "y": 0} */
        char *x_str = strstr(response, "\"x\":");
        char *y_str = strstr(response, "\"y\":");
        if (x_str) {
            g_wayfire_data->current_x = atoi(x_str + 4);
        }
        if (y_str) {
            g_wayfire_data->current_y = atoi(y_str + 4);
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void wayfire_disconnect_ipc(void) {
    if (!g_wayfire_data) return;

    if (g_wayfire_data->ipc_fd >= 0) {
        close(g_wayfire_data->ipc_fd);
        g_wayfire_data->ipc_fd = -1;
    }

    g_wayfire_data->connected = false;
}

/* Poll for events */
static int wayfire_poll_events(compositor_event_t *event) {
    if (!event || !g_wayfire_data || !g_wayfire_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Poll IPC socket */
    struct pollfd pfd = {
        .fd = g_wayfire_data->ipc_fd,
        .events = POLLIN
    };

    if (poll(&pfd, 1, 0) <= 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Read event data */
    char buffer[1024];
    ssize_t n = read(g_wayfire_data->ipc_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }
    buffer[n] = '\0';

    /* Parse Wayfire event */
    /* Expected format: {"event": "workspace-changed", "x": 1, "y": 0} */
    if (strstr(buffer, "workspace-changed")) {
        int new_x = g_wayfire_data->current_x;
        int new_y = g_wayfire_data->current_y;

        char *x_str = strstr(buffer, "\"x\":");
        char *y_str = strstr(buffer, "\"y\":");

        if (x_str) {
            new_x = atoi(x_str + 4);
        }
        if (y_str) {
            new_y = atoi(y_str + 4);
        }

        if (new_x != g_wayfire_data->current_x || new_y != g_wayfire_data->current_y) {
            event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
            event->data.workspace.from_x = g_wayfire_data->current_x;
            event->data.workspace.from_y = g_wayfire_data->current_y;
            event->data.workspace.to_x = new_x;
            event->data.workspace.to_y = new_y;

            /* Also set linear IDs for compatibility */
            event->data.workspace.from_workspace =
                g_wayfire_data->current_y * g_wayfire_data->grid_width +
                g_wayfire_data->current_x;
            event->data.workspace.to_workspace =
                new_y * g_wayfire_data->grid_width + new_x;

            g_wayfire_data->current_x = new_x;
            g_wayfire_data->current_y = new_y;

            LOG_DEBUG("Wayfire workspace change: (%d,%d) -> (%d,%d)",
                      event->data.workspace.from_x, event->data.workspace.from_y,
                      event->data.workspace.to_x, event->data.workspace.to_y);

            return HYPRLAX_SUCCESS;
        }
    }

    return HYPRLAX_ERROR_NO_DATA;
}

/* Send IPC command */
static int wayfire_send_command(const char *command, char *response,
                               size_t response_size) {
    if (!g_wayfire_data || !g_wayfire_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Send command */
    if (write(g_wayfire_data->ipc_fd, command, strlen(command)) < 0) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Read response if buffer provided */
    if (response && response_size > 0) {
        ssize_t n = read(g_wayfire_data->ipc_fd, response, response_size - 1);
        if (n > 0) {
            response[n] = '\0';
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Check blur support */
static bool wayfire_supports_blur(void) {
    return true;  /* Wayfire supports blur through plugins */
}

/* Check transparency support */
static bool wayfire_supports_transparency(void) {
    return true;
}

/* Check animation support */
static bool wayfire_supports_animations(void) {
    return true;  /* Wayfire has good animation support */
}

/* Set blur amount */
static int wayfire_set_blur(float amount) {
    if (!g_wayfire_data || !g_wayfire_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Would send Wayfire-specific blur command */
    char command[256];
    snprintf(command, sizeof(command),
             "set-option blur_amount %.2f", amount);

    return wayfire_send_command(command, NULL, 0);
}

/* Set wallpaper offset for parallax */
static int wayfire_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* This would be handled through layer surface positioning */
    return HYPRLAX_SUCCESS;
}

/* Expose Wayfire IPC event fd for blocking waits */
static int wayfire_get_event_fd(void) {
    if (!g_wayfire_data || g_wayfire_data->ipc_fd < 0) return -1;
    return g_wayfire_data->ipc_fd;
}

/* Wayfire compositor operations */
const compositor_ops_t compositor_wayfire_ops = {
    .init = wayfire_init,
    .destroy = wayfire_destroy,
    .detect = wayfire_detect,
    .get_name = wayfire_get_name,
    .create_layer_surface = wayfire_create_layer_surface,
    .configure_layer_surface = wayfire_configure_layer_surface,
    .destroy_layer_surface = wayfire_destroy_layer_surface,
    .get_current_workspace = wayfire_get_current_workspace,
    .get_workspace_count = wayfire_get_workspace_count,
    .list_workspaces = wayfire_list_workspaces,
    .get_current_monitor = wayfire_get_current_monitor,
    .list_monitors = wayfire_list_monitors,
    .connect_ipc = wayfire_connect_ipc,
    .disconnect_ipc = wayfire_disconnect_ipc,
    .poll_events = wayfire_poll_events,
    .send_command = wayfire_send_command,
    .get_event_fd = wayfire_get_event_fd,
    .supports_blur = wayfire_supports_blur,
    .supports_transparency = wayfire_supports_transparency,
    .supports_animations = wayfire_supports_animations,
    .set_blur = wayfire_set_blur,
    .set_wallpaper_offset = wayfire_set_wallpaper_offset,
};
