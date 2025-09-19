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
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Niri configuration */
// TODO: Make column width configurable or auto-detect from actual tile sizes
static const float NIRI_COLUMN_WIDTH = 1900.0f;

/* Window info for tracking focus */
typedef struct {
    int id;
    int column;  /* Column position from pos_in_scrolling_layout */
    int row;     /* Row position from pos_in_scrolling_layout */
} niri_window_info_t;

/* Niri private data */
typedef struct {
    FILE *event_stream;   /* Event stream from niri msg */
    pid_t event_pid;      /* PID of niri msg process */
    bool connected;
    
    /* Current state */
    int current_workspace_id;
    int focused_window_id;
    int current_column;   /* Column of focused window */
    int current_row;      /* Row of focused window */
    
    /* Window tracking for column detection */
    niri_window_info_t *windows;
    int window_count;
    int window_capacity;
    
    /* Debug tracking */
    bool debug_enabled;
    
    /* JSON parsing buffer */
    char parse_buffer[8192];
} niri_data_t;

/* Global instance */
static niri_data_t *g_niri_data = NULL;

/* Forward declarations */
static int niri_send_command(const char *command, char *response, size_t response_size);
static void update_window_info(int id, int column, int row);
static int get_window_column(int window_id);

/* Update window info cache */
static void update_window_info(int id, int column, int row) {
    if (!g_niri_data) return;
    
    /* Find existing window or add new one */
    for (int i = 0; i < g_niri_data->window_count; i++) {
        if (g_niri_data->windows[i].id == id) {
            g_niri_data->windows[i].column = column;
            g_niri_data->windows[i].row = row;
            return;
        }
    }
    
    /* Add new window */
    if (g_niri_data->window_count >= g_niri_data->window_capacity) {
        int new_capacity = g_niri_data->window_capacity * 2;
        if (new_capacity < 32) new_capacity = 32;
        niri_window_info_t *new_windows = realloc(g_niri_data->windows, 
                                                  new_capacity * sizeof(niri_window_info_t));
        if (new_windows) {
            g_niri_data->windows = new_windows;
            g_niri_data->window_capacity = new_capacity;
        } else {
            return;  /* Failed to grow */
        }
    }
    
    g_niri_data->windows[g_niri_data->window_count].id = id;
    g_niri_data->windows[g_niri_data->window_count].column = column;
    g_niri_data->windows[g_niri_data->window_count].row = row;
    g_niri_data->window_count++;
}

/* Get window column by ID */
static int get_window_column(int window_id) {
    if (!g_niri_data) return -1;
    
    for (int i = 0; i < g_niri_data->window_count; i++) {
        if (g_niri_data->windows[i].id == window_id) {
            return g_niri_data->windows[i].column;
        }
    }
    return -1;
}

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

    g_niri_data->event_stream = NULL;
    g_niri_data->event_pid = 0;
    g_niri_data->connected = false;
    g_niri_data->current_workspace_id = 1;
    g_niri_data->focused_window_id = -1;
    g_niri_data->current_column = 0;
    g_niri_data->current_row = 0;
    g_niri_data->debug_enabled = getenv("HYPRLAX_DEBUG") != NULL;
    g_niri_data->windows = NULL;
    g_niri_data->window_count = 0;
    g_niri_data->window_capacity = 0;

    if (g_niri_data->debug_enabled) {
        fprintf(stderr, "[DEBUG] Niri adapter initialized\n");
    }

    return HYPRLAX_SUCCESS;
}

/* Destroy Niri adapter */
static void niri_destroy(void) {
    if (!g_niri_data) return;

    if (g_niri_data->event_stream) {
        fclose(g_niri_data->event_stream);
    }
    
    if (g_niri_data->event_pid > 0) {
        kill(g_niri_data->event_pid, SIGTERM);
        waitpid(g_niri_data->event_pid, NULL, 0);
    }
    
    if (g_niri_data->windows) {
        free(g_niri_data->windows);
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
    /* Niri has dynamic workspaces, return a default */
    return 10;
}

/* List workspaces */
static int niri_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_niri_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Niri has dynamic workspaces, return a default set */
    *count = 10;
    *workspaces = calloc(*count, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Create placeholder workspace info */
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

    /* Launch niri msg --json event-stream as subprocess */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    if (pid == 0) {
        /* Child process */
        close(pipefd[0]); /* Close read end */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        /* Redirect stderr to /dev/null to suppress "Started reading events" message */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        execlp("niri", "niri", "msg", "--json", "event-stream", NULL);
        /* If exec fails */
        _exit(1);
    }
    
    /* Parent process */
    close(pipefd[1]); /* Close write end */
    
    /* Make the read end non-blocking */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    
    g_niri_data->event_stream = fdopen(pipefd[0], "r");
    if (!g_niri_data->event_stream) {
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    g_niri_data->event_pid = pid;
    g_niri_data->connected = true;

    if (g_niri_data->debug_enabled) {
        fprintf(stderr, "[DEBUG] Connected to Niri event stream (PID %d)\n", pid);
    }

    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void niri_disconnect_ipc(void) {
    if (!g_niri_data) return;

    if (g_niri_data->event_stream) {
        fclose(g_niri_data->event_stream);
        g_niri_data->event_stream = NULL;
    }
    
    if (g_niri_data->event_pid > 0) {
        kill(g_niri_data->event_pid, SIGTERM);
        waitpid(g_niri_data->event_pid, NULL, 0);
        g_niri_data->event_pid = 0;
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
    
    if (g_niri_data && g_niri_data->debug_enabled) {
        fprintf(stderr, "[DEBUG] parse_workspace_position: ID %d -> (%d, %d)\n",
                workspace_id, *x, *y);
    }
}

/* Poll for events */
/* Poll for events from Niri */
static int niri_poll_events(compositor_event_t *event) {
    if (!event || !g_niri_data || !g_niri_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_niri_data->event_stream) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Check if data is available */
    int fd = fileno(g_niri_data->event_stream);
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN
    };

    if (poll(&pfd, 1, 0) <= 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Read a line of JSON */
    if (!fgets(g_niri_data->parse_buffer, sizeof(g_niri_data->parse_buffer), 
               g_niri_data->event_stream)) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Parse the JSON event */
    /* Check for WindowFocusChanged event */
    if (strstr(g_niri_data->parse_buffer, "\"WindowFocusChanged\"")) {
        /* Extract the window ID */
        /* Format: {"WindowFocusChanged":{"id":5}} or {"WindowFocusChanged":{"id":null}} */
        char *id_str = strstr(g_niri_data->parse_buffer, "\"id\":");
        if (id_str) {
            id_str += 5; /* Skip "id": */
            while (*id_str == ' ') id_str++;
            
            int new_window_id = -1;
            if (strncmp(id_str, "null", 4) != 0) {
                new_window_id = atoi(id_str);
            }
            
            if (new_window_id != g_niri_data->focused_window_id) {
                /* Window focus changed, check if column changed */
                int old_column = g_niri_data->current_column;
                int new_column = get_window_column(new_window_id);
                
                if (g_niri_data->debug_enabled) {
                    fprintf(stderr, "[DEBUG] Niri: Window focus changed %d -> %d, column %d -> %d\n",
                            g_niri_data->focused_window_id, new_window_id,
                            old_column, new_column);
                }
                
                g_niri_data->focused_window_id = new_window_id;
                
                if (new_column >= 0 && new_column != old_column) {
                    /* Column changed - generate workspace change event */
                    event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                    
                    /* Use column positions as workspace IDs for parallax */
                    event->data.workspace.from_workspace = old_column;
                    event->data.workspace.to_workspace = new_column;
                    
                    /* Provide X coordinate change for 2D handling */
                    event->data.workspace.from_x = old_column;
                    event->data.workspace.from_y = 0;
                    event->data.workspace.to_x = new_column;
                    event->data.workspace.to_y = 0;
                    
                    g_niri_data->current_column = new_column;
                    
                    if (g_niri_data->debug_enabled) {
                        fprintf(stderr, "[DEBUG] Niri: Generated workspace change event, column %d -> %d\n",
                                old_column, new_column);
                    }
                    
                    return HYPRLAX_SUCCESS;
                }
            }
        }
    }
    /* Check for WindowsChanged event to update window cache */
    else if (strstr(g_niri_data->parse_buffer, "\"WindowsChanged\"")) {
        /* Clear window cache */
        g_niri_data->window_count = 0;
        
        /* Parse all windows */
        /* Format: {"WindowsChanged":{"windows":[{...}]}} */
        char *windows_start = strstr(g_niri_data->parse_buffer, "\"windows\":[");
        if (windows_start) {
            windows_start += 11; /* Skip "windows":[ */
            
            /* Parse each window */
            char *window_ptr = windows_start;
            while ((window_ptr = strstr(window_ptr, "\"id\":"))) {
                window_ptr += 5;
                int window_id = atoi(window_ptr);
                
                /* Find pos_in_scrolling_layout for this window */
                char *layout_str = strstr(window_ptr, "\"pos_in_scrolling_layout\":");
                if (layout_str && layout_str < strstr(window_ptr, "},")) {
                    layout_str += 26; /* Skip to value */
                    
                    if (strncmp(layout_str, "null", 4) != 0) {
                        /* Parse [column,row] */
                        char *bracket = strchr(layout_str, '[');
                        if (bracket) {
                            int column = atoi(bracket + 1);
                            char *comma = strchr(bracket, ',');
                            int row = comma ? atoi(comma + 1) : 1;
                            
                            update_window_info(window_id, column, row);
                            
                            if (g_niri_data->debug_enabled) {
                                fprintf(stderr, "[DEBUG] Niri: Window %d at column %d, row %d\n",
                                        window_id, column, row);
                            }
                        }
                    }
                }
                
                /* Move to next window */
                window_ptr = strstr(window_ptr, "},{");
                if (!window_ptr) break;
            }
        }
    }
    /* Check for WindowOpenedOrChanged to update single window */
    else if (strstr(g_niri_data->parse_buffer, "\"WindowOpenedOrChanged\"")) {
        /* Parse single window update */
        char *id_str = strstr(g_niri_data->parse_buffer, "\"id\":");
        if (id_str) {
            id_str += 5;
            int window_id = atoi(id_str);
            
            /* Find pos_in_scrolling_layout */
            char *layout_str = strstr(g_niri_data->parse_buffer, "\"pos_in_scrolling_layout\":");
            if (layout_str) {
                layout_str += 26;
                
                if (strncmp(layout_str, "null", 4) != 0) {
                    char *bracket = strchr(layout_str, '[');
                    if (bracket) {
                        int column = atoi(bracket + 1);
                        char *comma = strchr(bracket, ',');
                        int row = comma ? atoi(comma + 1) : 1;
                        
                        update_window_info(window_id, column, row);
                        
                        if (g_niri_data->debug_enabled) {
                            fprintf(stderr, "[DEBUG] Niri: Window %d updated at column %d, row %d\n",
                                    window_id, column, row);
                        }
                    }
                }
            }
        }
    }
    /* Check for WorkspaceActivated for vertical workspace changes */
    else if (strstr(g_niri_data->parse_buffer, "\"WorkspaceActivated\"")) {
        char *id_str = strstr(g_niri_data->parse_buffer, "\"id\":");
        if (id_str) {
            id_str += 5;
            int new_workspace_id = atoi(id_str);
            
            if (new_workspace_id != g_niri_data->current_workspace_id) {
                /* Vertical workspace change */
                event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                event->data.workspace.from_workspace = g_niri_data->current_workspace_id;
                event->data.workspace.to_workspace = new_workspace_id;
                
                /* Use Y coordinate for vertical changes */
                event->data.workspace.from_x = 0;
                event->data.workspace.from_y = g_niri_data->current_workspace_id;
                event->data.workspace.to_x = 0;
                event->data.workspace.to_y = new_workspace_id;
                
                g_niri_data->current_workspace_id = new_workspace_id;
                
                if (g_niri_data->debug_enabled) {
                    fprintf(stderr, "[DEBUG] Niri: Workspace activated %d\n", new_workspace_id);
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