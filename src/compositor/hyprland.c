/*
 * hyprland.c - Hyprland compositor adapter
 * 
 * Implements compositor interface for Hyprland, including
 * Hyprland-specific IPC communication and features.
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
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Hyprland IPC commands */
#define HYPRLAND_IPC_GET_WORKSPACES "j/workspaces"
#define HYPRLAND_IPC_GET_MONITORS "j/monitors"
#define HYPRLAND_IPC_GET_ACTIVE_WORKSPACE "j/activeworkspace"
#define HYPRLAND_IPC_GET_ACTIVE_WINDOW "j/activewindow"

/* Workspace-to-monitor mapping for detecting stealing */
#define MAX_WORKSPACES 32
typedef struct {
    int workspace_id;
    char monitor_name[64];
} workspace_monitor_map_t;

/* Hyprland private data */
typedef struct {
    int ipc_fd;           /* Command socket */
    int event_fd;         /* Event socket */
    char socket_path[256];
    char event_socket_path[256];
    bool connected;
    int current_workspace;
    int current_monitor;
    char current_monitor_name[64];  /* Track current monitor name */
    /* Workspace-to-monitor mapping for detecting stealing */
    workspace_monitor_map_t workspace_map[MAX_WORKSPACES];
    int workspace_map_count;
    /* Plugin detection */
    bool has_split_monitor_plugin;  /* split-monitor-workspaces changes behavior */
} hyprland_data_t;

/* Global instance (simplified for now) */
static hyprland_data_t *g_hyprland_data = NULL;

/* Helper: Find which monitor owns a workspace */
static const char* find_workspace_owner(int workspace_id) {
    if (!g_hyprland_data) return NULL;
    
    for (int i = 0; i < g_hyprland_data->workspace_map_count; i++) {
        if (g_hyprland_data->workspace_map[i].workspace_id == workspace_id) {
            return g_hyprland_data->workspace_map[i].monitor_name;
        }
    }
    return NULL;
}

/* Helper: Update workspace ownership */
static void update_workspace_owner(int workspace_id, const char *monitor_name) {
    if (!g_hyprland_data || !monitor_name) return;
    
    /* Check if workspace already mapped */
    for (int i = 0; i < g_hyprland_data->workspace_map_count; i++) {
        if (g_hyprland_data->workspace_map[i].workspace_id == workspace_id) {
            /* Update existing mapping */
            strncpy(g_hyprland_data->workspace_map[i].monitor_name, 
                   monitor_name, sizeof(g_hyprland_data->workspace_map[i].monitor_name) - 1);
            g_hyprland_data->workspace_map[i].monitor_name[sizeof(g_hyprland_data->workspace_map[i].monitor_name) - 1] = '\0';
            return;
        }
    }
    
    /* Add new mapping if space available */
    if (g_hyprland_data->workspace_map_count < MAX_WORKSPACES) {
        g_hyprland_data->workspace_map[g_hyprland_data->workspace_map_count].workspace_id = workspace_id;
        strncpy(g_hyprland_data->workspace_map[g_hyprland_data->workspace_map_count].monitor_name,
               monitor_name, sizeof(g_hyprland_data->workspace_map[0].monitor_name) - 1);
        g_hyprland_data->workspace_map[g_hyprland_data->workspace_map_count].monitor_name[sizeof(g_hyprland_data->workspace_map[0].monitor_name) - 1] = '\0';
        g_hyprland_data->workspace_map_count++;
    }
}

/* Forward declaration */
static int hyprland_send_command(const char *command, char *response, size_t response_size);

/* Detect split-monitor-workspaces plugin */
static bool detect_split_monitor_plugin(void) {
    char response[4096];
    
    /* Query hyprctl plugins to check if split-monitor-workspaces is loaded */
    if (hyprland_send_command("j/plugins", response, sizeof(response)) == HYPRLAX_SUCCESS) {
        /* Simple check for plugin name in JSON response */
        if (strstr(response, "split-monitor-workspaces") != NULL) {
            if (getenv("HYPRLAX_DEBUG")) {
                fprintf(stderr, "[DEBUG] Detected split-monitor-workspaces plugin\n");
            }
            return true;
        }
    }
    
    return false;
}

/* Get Hyprland socket paths */
static bool get_hyprland_socket_paths(char *cmd_path, char *event_path, size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *hyprland_instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    
    if (!runtime_dir || !hyprland_instance) {
        return false;
    }
    
    snprintf(cmd_path, size, 
             "%s/hypr/%s/.socket.sock", runtime_dir, hyprland_instance);
    snprintf(event_path, size,
             "%s/hypr/%s/.socket2.sock", runtime_dir, hyprland_instance);
    
    return true;
}

/* Initialize Hyprland adapter */
static int hyprland_init(void *platform_data) {
    (void)platform_data;  /* Not used for Hyprland */
    
    if (g_hyprland_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }
    
    g_hyprland_data = calloc(1, sizeof(hyprland_data_t));
    if (!g_hyprland_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    g_hyprland_data->ipc_fd = -1;
    g_hyprland_data->connected = false;
    g_hyprland_data->current_workspace = 1;
    g_hyprland_data->current_monitor = 0;
    g_hyprland_data->current_monitor_name[0] = '\0';
    g_hyprland_data->workspace_map_count = 0;
    
    return HYPRLAX_SUCCESS;
}

/* Destroy Hyprland adapter */
static void hyprland_destroy(void) {
    if (!g_hyprland_data) return;
    
    if (g_hyprland_data->ipc_fd >= 0) {
        close(g_hyprland_data->ipc_fd);
    }
    
    free(g_hyprland_data);
    g_hyprland_data = NULL;
}

/* Detect if running under Hyprland */
static bool hyprland_detect(void) {
    const char *hyprland_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    
    if (hyprland_sig && *hyprland_sig) {
        return true;
    }
    
    if (desktop && strstr(desktop, "Hyprland")) {
        return true;
    }
    
    return false;
}

/* Get compositor name */
static const char* hyprland_get_name(void) {
    return "Hyprland";
}

/* Create layer surface (uses wlr-layer-shell) */
static int hyprland_create_layer_surface(void *surface, 
                                        const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* This will be handled by the platform layer with wlr-layer-shell protocol */
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void hyprland_configure_layer_surface(void *layer_surface, 
                                            int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
    /* Handled by platform layer */
}

/* Destroy layer surface */
static void hyprland_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
    /* Handled by platform layer */
}

/* Get current workspace */
static int hyprland_get_current_workspace(void) {
    if (!g_hyprland_data) return 1;
    return g_hyprland_data->current_workspace;
}

/* Get workspace count */
static int hyprland_get_workspace_count(void) {
    /* Hyprland has dynamic workspaces, return a reasonable default */
    return 10;
}

/* List workspaces */
static int hyprland_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Simplified implementation - would parse IPC response */
    *count = 10;
    *workspaces = calloc(*count, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    for (int i = 0; i < *count; i++) {
        (*workspaces)[i].id = i + 1;
        snprintf((*workspaces)[i].name, sizeof((*workspaces)[i].name), "%d", i + 1);
        (*workspaces)[i].active = (i == 0);
        (*workspaces)[i].visible = (i == 0);
    }
    
    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int hyprland_get_current_monitor(void) {
    if (!g_hyprland_data) return 0;
    return g_hyprland_data->current_monitor;
}

/* List monitors */
static int hyprland_list_monitors(monitor_info_t **monitors, int *count) {
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
    strncpy((*monitors)[0].name, "eDP-1", sizeof((*monitors)[0].name));
    (*monitors)[0].x = 0;
    (*monitors)[0].y = 0;
    (*monitors)[0].width = 1920;
    (*monitors)[0].height = 1080;
    (*monitors)[0].scale = 1.0;
    (*monitors)[0].primary = true;
    
    return HYPRLAX_SUCCESS;
}

/* Simple socket connect for command socket (no retry needed) */
static int connect_simple_socket(const char *path) {
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
    
    return fd;
}

/* Connect to Hyprland IPC */
static int hyprland_connect_ipc(const char *socket_path) {
    (void)socket_path; /* Optional parameter, auto-detect if not provided */
    
    if (!g_hyprland_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (g_hyprland_data->connected) {
        return HYPRLAX_SUCCESS;
    }
    
    /* Get socket paths */
    if (!get_hyprland_socket_paths(g_hyprland_data->socket_path, 
                                   g_hyprland_data->event_socket_path,
                                   sizeof(g_hyprland_data->socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Note: Command socket is created per-command in hyprland_send_command */
    g_hyprland_data->ipc_fd = -1;  /* Not used for persistent connection */
    
    /* Connect event socket with retries (stays open for event monitoring) */
    g_hyprland_data->event_fd = compositor_connect_socket_with_retry(
        g_hyprland_data->event_socket_path,
        "Hyprland",
        30,    /* max_retries: 3 seconds total */
        100    /* retry_delay_ms */
    );
    
    if (g_hyprland_data->event_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Make event socket non-blocking */
    int flags = fcntl(g_hyprland_data->event_fd, F_GETFL, 0);
    fcntl(g_hyprland_data->event_fd, F_SETFL, flags | O_NONBLOCK);
    
    g_hyprland_data->connected = true;
    
    /* Detect plugins after connection established */
    g_hyprland_data->has_split_monitor_plugin = detect_split_monitor_plugin();
    
    /* Get initial workspace */
    char response[1024];
    if (hyprland_send_command(HYPRLAND_IPC_GET_ACTIVE_WORKSPACE, response, sizeof(response)) == HYPRLAX_SUCCESS) {
        /* Parse JSON response to get workspace ID */
        /* Simple parsing - look for "id": */
        char *id_str = strstr(response, "\"id\":");
        if (id_str) {
            g_hyprland_data->current_workspace = atoi(id_str + 6);
        }
    }
    
    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void hyprland_disconnect_ipc(void) {
    if (!g_hyprland_data) return;
    
    if (g_hyprland_data->ipc_fd >= 0) {
        close(g_hyprland_data->ipc_fd);
        g_hyprland_data->ipc_fd = -1;
    }
    
    if (g_hyprland_data->event_fd >= 0) {
        close(g_hyprland_data->event_fd);
        g_hyprland_data->event_fd = -1;
    }
    
    g_hyprland_data->connected = false;
}

/* Poll for events */
static int hyprland_poll_events(compositor_event_t *event) {
    if (!event || !g_hyprland_data || !g_hyprland_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Poll event socket */
    struct pollfd pfd = {
        .fd = g_hyprland_data->event_fd,
        .events = POLLIN
    };
    
    int poll_result = poll(&pfd, 1, 0);
    if (poll_result < 0) {
        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] Hyprland poll error: %s\n", strerror(errno));
        }
        return HYPRLAX_ERROR_NO_DATA;
    } else if (poll_result == 0) {
        /* No events available */
        return HYPRLAX_ERROR_NO_DATA;
    }
    
    /* Read event data */
    char buffer[4096];
    ssize_t n = read(g_hyprland_data->event_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        if (getenv("HYPRLAX_DEBUG") && n < 0) {
            fprintf(stderr, "[DEBUG] Hyprland read error: %s\n", strerror(errno));
        }
        return HYPRLAX_ERROR_NO_DATA;
    }
    buffer[n] = '\0';
    
    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Hyprland event received: %s\n", buffer);
    }
    
    /* Parse Hyprland events - multiple events may be in buffer separated by newlines */
    char *line = buffer;
    char *next_line;
    
    while (line && *line) {
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }
        
        /* Parse Hyprland event format: "event_name>>data" */
        if (strncmp(line, "workspace>>", 11) == 0) {
            int new_workspace = atoi(line + 11);
            if (new_workspace != g_hyprland_data->current_workspace) {
                event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                event->data.workspace.from_workspace = g_hyprland_data->current_workspace;
                event->data.workspace.to_workspace = new_workspace;
                /* Hyprland uses linear workspaces, set x/y to 0 */
                event->data.workspace.from_x = 0;
                event->data.workspace.from_y = 0;
                event->data.workspace.to_x = 0;
                event->data.workspace.to_y = 0;
                /* Use last known monitor name if we have one */
                if (g_hyprland_data->current_monitor_name[0] != '\0') {
                    strncpy(event->data.workspace.monitor_name, 
                           g_hyprland_data->current_monitor_name,
                           sizeof(event->data.workspace.monitor_name) - 1);
                    event->data.workspace.monitor_name[sizeof(event->data.workspace.monitor_name) - 1] = '\0';
                } else {
                    event->data.workspace.monitor_name[0] = '\0';
                }
                g_hyprland_data->current_workspace = new_workspace;
                if (getenv("HYPRLAX_DEBUG")) {
                    fprintf(stderr, "[DEBUG] Workspace change detected: %d -> %d\n",
                            event->data.workspace.from_workspace,
                            event->data.workspace.to_workspace);
                }
                return HYPRLAX_SUCCESS;
            }
        } else if (strncmp(line, "focusedmon>>", 12) == 0) {
            /* Parse monitor change: "focusedmon>>monitor_name,workspace_id" */
            char *comma = strchr(line + 12, ',');
            if (comma) {
                /* Extract monitor name */
                size_t monitor_name_len = comma - (line + 12);
                if (monitor_name_len > 0 && monitor_name_len < sizeof(event->data.workspace.monitor_name)) {
                    strncpy(event->data.workspace.monitor_name, line + 12, monitor_name_len);
                    event->data.workspace.monitor_name[monitor_name_len] = '\0';
                    
                    /* Save current monitor name for future workspace events */
                    strncpy(g_hyprland_data->current_monitor_name, line + 12, monitor_name_len);
                    g_hyprland_data->current_monitor_name[monitor_name_len] = '\0';
                    
                    int new_workspace = atoi(comma + 1);
                    
                    /* Check if workspace is being stolen from another monitor */
                    const char *previous_owner = find_workspace_owner(new_workspace);
                    bool is_steal = false;
                    if (previous_owner && strcmp(previous_owner, event->data.workspace.monitor_name) != 0) {
                        is_steal = true;
                        if (getenv("HYPRLAX_DEBUG")) {
                            fprintf(stderr, "[DEBUG] Workspace %d stolen from %s to %s\n",
                                   new_workspace, previous_owner, event->data.workspace.monitor_name);
                        }
                    }
                    
                    /* Update workspace ownership */
                    update_workspace_owner(new_workspace, event->data.workspace.monitor_name);
                    
                    /* Always send workspace change event with monitor info */
                    event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                    event->data.workspace.from_workspace = g_hyprland_data->current_workspace;
                    event->data.workspace.to_workspace = new_workspace;
                    event->data.workspace.from_x = 0;
                    event->data.workspace.from_y = 0;
                    event->data.workspace.to_x = 0;
                    event->data.workspace.to_y = 0;
                    g_hyprland_data->current_workspace = new_workspace;
                    
                    if (getenv("HYPRLAX_DEBUG")) {
                        fprintf(stderr, "[DEBUG] Workspace change on monitor %s: %d -> %d%s\n",
                                event->data.workspace.monitor_name,
                                event->data.workspace.from_workspace,
                                event->data.workspace.to_workspace,
                                is_steal ? " (stolen)" : "");
                    }
                    return HYPRLAX_SUCCESS;
                }
            }
        }
        
        line = next_line;
    }
    
    return HYPRLAX_ERROR_NO_DATA;
}

/* Send IPC command */
static int hyprland_send_command(const char *command, char *response, 
                                size_t response_size) {
    if (!g_hyprland_data) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Connect a new command socket for each command */
    int cmd_fd = connect_simple_socket(g_hyprland_data->socket_path);
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
        ssize_t n = read(cmd_fd, response, response_size - 1);
        if (n > 0) {
            response[n] = '\0';
        }
    }
    
    close(cmd_fd);
    return HYPRLAX_SUCCESS;
}

/* Check blur support */
static bool hyprland_supports_blur(void) {
    return true;  /* Hyprland supports blur */
}

/* Check transparency support */
static bool hyprland_supports_transparency(void) {
    return true;  /* Hyprland supports transparency */
}

/* Check animation support */
static bool hyprland_supports_animations(void) {
    return true;  /* Hyprland has excellent animation support */
}

/* Check if Hyprland has split-monitor-workspaces plugin */
bool hyprland_has_split_monitor_plugin(void) {
    return g_hyprland_data ? g_hyprland_data->has_split_monitor_plugin : false;
}

/* Set blur amount */
static int hyprland_set_blur(float amount) {
    if (!g_hyprland_data || !g_hyprland_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Would send Hyprland-specific blur command */
    char command[256];
    snprintf(command, sizeof(command), 
             "keyword decoration:blur:size %.0f", amount * 10);
    
    return hyprland_send_command(command, NULL, 0);
}

/* Set wallpaper offset for parallax */
static int hyprland_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* This would be handled through layer surface positioning */
    return HYPRLAX_SUCCESS;
}

/* Hyprland compositor operations */
const compositor_ops_t compositor_hyprland_ops = {
    .init = hyprland_init,
    .destroy = hyprland_destroy,
    .detect = hyprland_detect,
    .get_name = hyprland_get_name,
    .create_layer_surface = hyprland_create_layer_surface,
    .configure_layer_surface = hyprland_configure_layer_surface,
    .destroy_layer_surface = hyprland_destroy_layer_surface,
    .get_current_workspace = hyprland_get_current_workspace,
    .get_workspace_count = hyprland_get_workspace_count,
    .list_workspaces = hyprland_list_workspaces,
    .get_current_monitor = hyprland_get_current_monitor,
    .list_monitors = hyprland_list_monitors,
    .connect_ipc = hyprland_connect_ipc,
    .disconnect_ipc = hyprland_disconnect_ipc,
    .poll_events = hyprland_poll_events,
    .send_command = hyprland_send_command,
    .supports_blur = hyprland_supports_blur,
    .supports_transparency = hyprland_supports_transparency,
    .supports_animations = hyprland_supports_animations,
    .set_blur = hyprland_set_blur,
    .set_wallpaper_offset = hyprland_set_wallpaper_offset,
};