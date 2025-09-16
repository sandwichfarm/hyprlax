/*
 * niri.c - Niri compositor adapter
 * 
 * Implements compositor interface for Niri, including
 * support for its scrollable workspace model with both
 * horizontal and vertical movement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <math.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Niri IPC commands */
#define NIRI_IPC_GET_WORKSPACES "\"action\": \"Workspaces\""
#define NIRI_IPC_GET_WINDOWS "\"action\": \"Windows\""
#define NIRI_IPC_GET_VERSION "\"action\": \"Version\""
#define NIRI_IPC_EVENT_WORKSPACE_ACTIVATED "WorkspaceActivated"
#define NIRI_IPC_EVENT_WORKSPACE_ACTIVE_WINDOW_CHANGED "WorkspaceActiveWindowChanged"

/* Niri private data */
typedef struct {
    int ipc_fd;           /* IPC socket */
    char socket_path[256];
    bool connected;
    /* Niri uses scrollable workspaces */
    int current_workspace_id;
    int current_column;   /* Current column position */
    int current_row;      /* Current row position (for vertical scrolling) */
    /* Workspace tracking */
    int num_workspaces;
    float scroll_offset_x;  /* Smooth scroll position */
    float scroll_offset_y;
} niri_data_t;

/* Global instance */
static niri_data_t *g_niri_data = NULL;

/* Forward declarations */
static int niri_send_command(const char *command, char *response, size_t response_size);

/* Get Niri socket path */
static bool get_niri_socket_path(char *path, size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    
    if (!runtime_dir) {
        return false;
    }
    
    /* Niri socket is at $XDG_RUNTIME_DIR/niri.wayland-<number>.sock */
    /* First try the environment variable */
    const char *niri_socket = getenv("NIRI_SOCKET");
    if (niri_socket) {
        strncpy(path, niri_socket, size);
        return true;
    }
    
    /* Try common socket paths */
    const char *socket_patterns[] = {
        "%s/niri.wayland-1.sock",
        "%s/niri.wayland-0.sock",
        "%s/niri.sock",
        NULL
    };
    
    for (int i = 0; socket_patterns[i]; i++) {
        snprintf(path, size, socket_patterns[i], runtime_dir);
        if (access(path, F_OK) == 0) {
            return true;
        }
    }
    
    return false;
}

/* Initialize Niri adapter */
static int niri_init(void *platform_data) {
    (void)platform_data;
    
    if (g_niri_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }
    
    g_niri_data = calloc(1, sizeof(niri_data_t));
    if (!g_niri_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    g_niri_data->ipc_fd = -1;
    g_niri_data->connected = false;
    g_niri_data->current_workspace_id = 0;
    g_niri_data->current_column = 0;
    g_niri_data->current_row = 0;
    g_niri_data->num_workspaces = 1;
    g_niri_data->scroll_offset_x = 0.0f;
    g_niri_data->scroll_offset_y = 0.0f;
    
    return HYPRLAX_SUCCESS;
}

/* Destroy Niri adapter */
static void niri_destroy(void) {
    if (!g_niri_data) return;
    
    if (g_niri_data->ipc_fd >= 0) {
        close(g_niri_data->ipc_fd);
    }
    
    free(g_niri_data);
    g_niri_data = NULL;
}

/* Detect if running under Niri */
static bool niri_detect(void) {
    /* Check environment variables */
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");
    const char *niri_socket = getenv("NIRI_SOCKET");
    
    if (niri_socket && *niri_socket) {
        return true;
    }
    
    if (desktop && strcasecmp(desktop, "niri") == 0) {
        return true;
    }
    
    if (session && strcasecmp(session, "niri") == 0) {
        return true;
    }
    
    /* Check for Niri-specific socket */
    char socket_path[256];
    if (get_niri_socket_path(socket_path, sizeof(socket_path))) {
        return true;
    }
    
    return false;
}

/* Get compositor name */
static const char* niri_get_name(void) {
    return "Niri";
}

/* Create layer surface (uses wlr-layer-shell) */
static int niri_create_layer_surface(void *surface, 
                                    const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* This will be handled by the platform layer with wlr-layer-shell protocol */
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void niri_configure_layer_surface(void *layer_surface, 
                                        int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
    /* Handled by platform layer */
}

/* Destroy layer surface */
static void niri_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
    /* Handled by platform layer */
}

/* Get current workspace */
static int niri_get_current_workspace(void) {
    if (!g_niri_data) return 0;
    return g_niri_data->current_workspace_id;
}

/* Get workspace count */
static int niri_get_workspace_count(void) {
    if (!g_niri_data) return 1;
    return g_niri_data->num_workspaces;
}

/* List workspaces */
static int niri_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (!g_niri_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Query actual workspaces via IPC */
    char response[4096];
    if (niri_send_command("{\"action\": \"Workspaces\"}", response, sizeof(response)) == HYPRLAX_SUCCESS) {
        /* Parse JSON response to count workspaces */
        /* Simplified - would need proper JSON parsing */
        g_niri_data->num_workspaces = 1; /* Default */
        
        /* Count occurrences of "id" in response */
        char *pos = response;
        int ws_count = 0;
        while ((pos = strstr(pos, "\"id\":")) != NULL) {
            ws_count++;
            pos += 5;
        }
        if (ws_count > 0) {
            g_niri_data->num_workspaces = ws_count;
        }
    }
    
    *count = g_niri_data->num_workspaces;
    *workspaces = calloc(*count, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    /* Niri workspaces are dynamically positioned */
    for (int i = 0; i < *count; i++) {
        (*workspaces)[i].id = i;
        snprintf((*workspaces)[i].name, sizeof((*workspaces)[i].name), 
                 "Workspace %d", i + 1);
        /* Niri arranges workspaces in columns and rows */
        (*workspaces)[i].x = i % 3;  /* Assuming 3 columns by default */
        (*workspaces)[i].y = i / 3;
        (*workspaces)[i].active = (i == g_niri_data->current_workspace_id);
        (*workspaces)[i].visible = (*workspaces)[i].active;
    }
    
    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int niri_get_current_monitor(void) {
    return 0;  /* Simplified for now */
}

/* List monitors */
static int niri_list_monitors(monitor_info_t **monitors, int *count) {
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
static int connect_niri_socket(const char *path) {
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

/* Connect to Niri IPC */
static int niri_connect_ipc(const char *socket_path) {
    (void)socket_path; /* Optional parameter */
    
    if (!g_niri_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (g_niri_data->connected) {
        return HYPRLAX_SUCCESS;
    }
    
    /* Get socket path */
    if (!get_niri_socket_path(g_niri_data->socket_path, 
                             sizeof(g_niri_data->socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Connect to IPC socket */
    g_niri_data->ipc_fd = connect_niri_socket(g_niri_data->socket_path);
    if (g_niri_data->ipc_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    g_niri_data->connected = true;
    
    /* Subscribe to events */
    const char *subscribe = "{\"action\": \"EventStream\"}";
    if (write(g_niri_data->ipc_fd, subscribe, strlen(subscribe)) < 0) {
        close(g_niri_data->ipc_fd);
        g_niri_data->ipc_fd = -1;
        g_niri_data->connected = false;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void niri_disconnect_ipc(void) {
    if (!g_niri_data) return;
    
    if (g_niri_data->ipc_fd >= 0) {
        close(g_niri_data->ipc_fd);
        g_niri_data->ipc_fd = -1;
    }
    
    g_niri_data->connected = false;
}

/* Parse workspace position from Niri's scrollable model */
static void parse_workspace_position(int workspace_id, int *x, int *y) {
    /* Niri arranges workspaces in a scrollable grid */
    /* This is a simplified model - actual implementation would need
     * to query Niri for the exact layout */
    
    /* Default to 3 columns layout */
    const int columns = 3;
    *x = workspace_id % columns;
    *y = workspace_id / columns;
}

/* Poll for events */
static int niri_poll_events(compositor_event_t *event) {
    if (!event || !g_niri_data || !g_niri_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Poll IPC socket */
    struct pollfd pfd = {
        .fd = g_niri_data->ipc_fd,
        .events = POLLIN
    };
    
    if (poll(&pfd, 1, 0) <= 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }
    
    /* Read event data */
    char buffer[4096];
    ssize_t n = read(g_niri_data->ipc_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }
    buffer[n] = '\0';
    
    /* Parse Niri events (JSON format) */
    if (strstr(buffer, NIRI_IPC_EVENT_WORKSPACE_ACTIVATED)) {
        /* Extract workspace ID from event */
        /* Format: {"WorkspaceActivated": {"id": 2, "idx": 1}} */
        char *id_str = strstr(buffer, "\"id\":");
        if (id_str) {
            int new_workspace_id = atoi(id_str + 5);
            
            if (new_workspace_id != g_niri_data->current_workspace_id) {
                /* Calculate old and new positions */
                int old_x, old_y, new_x, new_y;
                parse_workspace_position(g_niri_data->current_workspace_id, &old_x, &old_y);
                parse_workspace_position(new_workspace_id, &new_x, &new_y);
                
                event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                event->data.workspace.from_workspace = g_niri_data->current_workspace_id;
                event->data.workspace.to_workspace = new_workspace_id;
                event->data.workspace.from_x = old_x;
                event->data.workspace.from_y = old_y;
                event->data.workspace.to_x = new_x;
                event->data.workspace.to_y = new_y;
                
                g_niri_data->current_workspace_id = new_workspace_id;
                g_niri_data->current_column = new_x;
                g_niri_data->current_row = new_y;
                
                if (getenv("HYPRLAX_DEBUG")) {
                    fprintf(stderr, "[DEBUG] Niri workspace change: %d (%d,%d) -> %d (%d,%d)\n",
                            event->data.workspace.from_workspace,
                            old_x, old_y,
                            event->data.workspace.to_workspace,
                            new_x, new_y);
                }
                
                return HYPRLAX_SUCCESS;
            }
        }
    } else if (strstr(buffer, "\"Scroll\"")) {
        /* Niri also supports smooth scrolling events */
        /* Format: {"Scroll": {"dx": 0.0, "dy": -100.0}} */
        char *dx_str = strstr(buffer, "\"dx\":");
        char *dy_str = strstr(buffer, "\"dy\":");
        
        if (dx_str || dy_str) {
            float dx = 0.0f, dy = 0.0f;
            if (dx_str) {
                dx = atof(dx_str + 5);
            }
            if (dy_str) {
                dy = atof(dy_str + 5);
            }
            
            /* Update smooth scroll position */
            g_niri_data->scroll_offset_x += dx;
            g_niri_data->scroll_offset_y += dy;
            
            /* Convert scroll offset to workspace coordinates */
            /* Assuming each workspace is ~1000 pixels */
            const float workspace_size = 1000.0f;
            int new_x = (int)roundf(g_niri_data->scroll_offset_x / workspace_size);
            int new_y = (int)roundf(g_niri_data->scroll_offset_y / workspace_size);
            
            if (new_x != g_niri_data->current_column || new_y != g_niri_data->current_row) {
                event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                event->data.workspace.from_x = g_niri_data->current_column;
                event->data.workspace.from_y = g_niri_data->current_row;
                event->data.workspace.to_x = new_x;
                event->data.workspace.to_y = new_y;
                
                /* Calculate linear workspace IDs for compatibility */
                event->data.workspace.from_workspace = 
                    g_niri_data->current_row * 3 + g_niri_data->current_column;
                event->data.workspace.to_workspace = new_y * 3 + new_x;
                
                g_niri_data->current_column = new_x;
                g_niri_data->current_row = new_y;
                
                if (getenv("HYPRLAX_DEBUG")) {
                    fprintf(stderr, "[DEBUG] Niri scroll: (%.1f, %.1f) -> workspace (%d,%d)\n",
                            dx, dy, new_x, new_y);
                }
                
                return HYPRLAX_SUCCESS;
            }
        }
    }
    
    return HYPRLAX_ERROR_NO_DATA;
}

/* Send IPC command */
static int niri_send_command(const char *command, char *response, 
                            size_t response_size) {
    if (!g_niri_data || !g_niri_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Niri uses a separate socket per command */
    char socket_path[256];
    if (!get_niri_socket_path(socket_path, sizeof(socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    int cmd_fd = connect_niri_socket(socket_path);
    if (cmd_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Send command */
    if (write(cmd_fd, command, strlen(command)) < 0) {
        close(cmd_fd);
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Read response if buffer provided */
    if (response && response_size > 0) {
        ssize_t total = 0;
        while (total < (ssize_t)response_size - 1) {
            ssize_t n = read(cmd_fd, response + total, response_size - total - 1);
            if (n <= 0) break;
            total += n;
        }
        response[total] = '\0';
    }
    
    close(cmd_fd);
    return HYPRLAX_SUCCESS;
}

/* Check blur support */
static bool niri_supports_blur(void) {
    return true;  /* Niri supports blur through its rendering pipeline */
}

/* Check transparency support */
static bool niri_supports_transparency(void) {
    return true;
}

/* Check animation support */
static bool niri_supports_animations(void) {
    return true;  /* Niri has smooth animations */
}

/* Set blur amount */
static int niri_set_blur(float amount) {
    if (!g_niri_data || !g_niri_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Would send Niri-specific blur configuration */
    char command[256];
    snprintf(command, sizeof(command), 
             "{\"action\": \"SetConfig\", \"blur\": %.2f}", amount);
    
    return niri_send_command(command, NULL, 0);
}

/* Set wallpaper offset for parallax */
static int niri_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* This would be handled through layer surface positioning */
    return HYPRLAX_SUCCESS;
}

/* Niri compositor operations */
const compositor_ops_t compositor_niri_ops = {
    .init = niri_init,
    .destroy = niri_destroy,
    .detect = niri_detect,
    .get_name = niri_get_name,
    .create_layer_surface = niri_create_layer_surface,
    .configure_layer_surface = niri_configure_layer_surface,
    .destroy_layer_surface = niri_destroy_layer_surface,
    .get_current_workspace = niri_get_current_workspace,
    .get_workspace_count = niri_get_workspace_count,
    .list_workspaces = niri_list_workspaces,
    .get_current_monitor = niri_get_current_monitor,
    .list_monitors = niri_list_monitors,
    .connect_ipc = niri_connect_ipc,
    .disconnect_ipc = niri_disconnect_ipc,
    .poll_events = niri_poll_events,
    .send_command = niri_send_command,
    .supports_blur = niri_supports_blur,
    .supports_transparency = niri_supports_transparency,
    .supports_animations = niri_supports_animations,
    .set_blur = niri_set_blur,
    .set_wallpaper_offset = niri_set_wallpaper_offset,
};