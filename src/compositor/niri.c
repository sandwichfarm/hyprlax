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
#include <time.h>
#include <inttypes.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"
#include "../include/log.h"

#define NIRI_CACHE_BUFFER 16384
#define NIRI_GEOM_CACHE_TTL 0.05
#define NIRI_METADATA_CACHE_TTL 0.25

/* Niri configuration */

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

    /* Window tracking for column detection */
    niri_window_info_t *windows;
    int window_count;
    int window_capacity;

    /* Debug tracking */
    bool debug_enabled;

    /* Geometry and metadata caches */
    window_geometry_t geometry_cache;
    double geometry_cache_time;
    bool geometry_cache_valid;
    char outputs_cache[NIRI_CACHE_BUFFER];
    double outputs_cache_time;
    bool outputs_cache_valid;
    char workspaces_cache[NIRI_CACHE_BUFFER];
    double workspaces_cache_time;
    bool workspaces_cache_valid;

    /* JSON parsing buffer */
    char parse_buffer[8192];
} niri_data_t;

/* Global instance */
static niri_data_t *g_niri_data = NULL;

/* Forward declarations */
static int niri_send_command(const char *command, char *response, size_t response_size);
static void update_window_info(int id, int column, int row);

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
    g_niri_data->debug_enabled = getenv("HYPRLAX_DEBUG") != NULL; /* legacy */
    g_niri_data->windows = NULL;
    g_niri_data->window_count = 0;
    g_niri_data->window_capacity = 0;
    g_niri_data->geometry_cache_valid = false;
    g_niri_data->outputs_cache_valid = false;
    g_niri_data->workspaces_cache_valid = false;

    LOG_DEBUG("Niri adapter initialized");

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

    /* Check if niri command is available */
    FILE *pipe = popen("niri msg --json version 2>/dev/null", "r");
    if (!pipe) {
        return false;
    }

    char response[256];
    bool has_output = (fgets(response, sizeof(response), pipe) != NULL);
    int status = pclose(pipe);

    return (status == 0 && has_output);
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
    /* Query current state from Niri */
    char response[4096];
    if (niri_send_command("msg --json focused-window", response, sizeof(response)) != 0) {
        return 0;  /* Default if query fails */
    }

    int workspace_id = 1;
    int column = 0;

    /* Parse workspace ID */
    char *ws_str = strstr(response, "\"workspace_id\":");
    if (ws_str) {
        ws_str += 15;
        workspace_id = atoi(ws_str);
    }

    /* Parse column position */
    char *pos_str = strstr(response, "\"pos_in_scrolling_layout\":");
    if (pos_str) {
        pos_str += 26;
        if (strncmp(pos_str, "null", 4) != 0) {
            char *bracket = strchr(pos_str, '[');
            if (bracket) {
                column = atoi(bracket + 1);
            }
        }
    }

    /* Return encoded position for 2D tracking */
    return workspace_id * 1000 + column;
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
    /* Query current workspace to determine active one */
    int current_encoded = niri_get_current_workspace();
    int current_ws = current_encoded / 1000;

    for (int i = 0; i < *count; i++) {
        (*workspaces)[i].id = i;
        snprintf((*workspaces)[i].name, sizeof((*workspaces)[i].name),
                 "Workspace %d", i + 1);
        /* Niri arranges workspaces in columns and rows */
        (*workspaces)[i].x = i % 3;  /* Assuming 3 columns by default */
        (*workspaces)[i].y = i / 3;
        (*workspaces)[i].active = (i == current_ws);
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
    /* Avoid stdio buffering pitfalls with poll()+fgets() combo */
    setvbuf(g_niri_data->event_stream, NULL, _IONBF, 0);

    g_niri_data->event_pid = pid;
    g_niri_data->connected = true;

    LOG_DEBUG("Connected to Niri event stream (PID %d)", pid);

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

/* (removed unused parse_workspace_position helper) */

/* Poll for events */
/* Poll for events from Niri */
static int niri_poll_events(compositor_event_t *event) {
    if (!event || !g_niri_data || !g_niri_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_niri_data->event_stream) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Read a line of JSON (non-blocking FD; may return NULL if no data) */
    errno = 0;
    if (!fgets(g_niri_data->parse_buffer, sizeof(g_niri_data->parse_buffer),
               g_niri_data->event_stream)) {
        clearerr(g_niri_data->event_stream); /* clear EAGAIN on non-blocking */
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

            if (new_window_id >= 0) {
                /* Get column and row for this window */
                int column = -1, row = -1;
                for (int i = 0; i < g_niri_data->window_count; i++) {
                    if (g_niri_data->windows[i].id == new_window_id) {
                        column = g_niri_data->windows[i].column;
                        row = g_niri_data->windows[i].row;
                        break;
                    }
                }

                if (column >= 0 && row >= 0) {
                    /* Report the position where focus moved to */
                    /* The monitor context will handle "from" state */
                    event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;

                    /* Only report the "to" position - let monitor track "from" */
                    event->data.workspace.to_workspace = row * 1000 + column;
                    event->data.workspace.from_workspace = -1; /* Indicate unknown */

                    /* Provide 2D coordinates */
                    event->data.workspace.to_x = column;
                    event->data.workspace.to_y = row;
                    event->data.workspace.from_x = -1;  /* Unknown, let monitor handle */
                    event->data.workspace.from_y = -1;  /* Unknown, let monitor handle */

                    LOG_TRACE("Niri: Focus moved to window %d at column %d, row %d",
                              new_window_id, column, row);

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
                if (layout_str) {
                    layout_str += 26; /* Skip to value */

                    if (strncmp(layout_str, "null", 4) != 0) {
                        /* Parse [column,row] */
                        char *bracket = strchr(layout_str, '[');
                        if (bracket) {
                            int column = atoi(bracket + 1);
                            char *comma = strchr(bracket, ',');
                            int row = comma ? atoi(comma + 1) : 1;

                            update_window_info(window_id, column, row);
                            LOG_TRACE("Niri: Window %d at column %d, row %d",
                                      window_id, column, row);
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
                        LOG_TRACE("Niri: Window %d updated at column %d, row %d",
                                  window_id, column, row);
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

            /* Report workspace change - let monitor handle "from" state */
            event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;

            /* We don't know the column; under tests we use a shim. */
            int column = 0;
#ifdef UNIT_TEST
            extern int niri_test_get_current_column(void);
            column = niri_test_get_current_column();
            if (column < 0) {
                column = 0;
            }
#else
            int current_encoded = niri_get_current_workspace();
            column = current_encoded % 1000;
#endif

            event->data.workspace.to_workspace = new_workspace_id * 1000 + column;
            event->data.workspace.from_workspace = -1; /* Unknown, let monitor handle */

            /* Use Y coordinate for vertical changes */
                /* Provide 2D coordinates */
                event->data.workspace.to_x = column;
                event->data.workspace.to_y = new_workspace_id;
                event->data.workspace.from_x = -1;  /* Unknown, let monitor handle */
                event->data.workspace.from_y = -1;  /* Unknown, let monitor handle */

                LOG_DEBUG("Niri: Workspace activated %d at column %d",
                          new_workspace_id, column);

                return HYPRLAX_SUCCESS;
        }
    }

    return HYPRLAX_ERROR_NO_DATA;
}
static double niri_monotonic_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *find_matching_brace(const char *start) {
    if (!start || *start != '{') {
        return NULL;
    }
    int depth = 0;
    const char *p = start;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p - 1) != '\\') {
                    break;
                }
                p++;
            }
        }
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
        if (*p == '\0') {
            break;
        }
        p++;
    }
    return NULL;
}

static const char *find_matching_bracket(const char *start) {
    if (!start || *start != '[') {
        return NULL;
    }
    int depth = 0;
    const char *p = start;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p - 1) != '\\') {
                    break;
                }
                p++;
            }
        }
        if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
        if (*p == '\0') {
            break;
        }
        p++;
    }
    return NULL;
}

static bool parse_double_field(const char *json, const char *key, double *out) {
    if (!json || !key || !out) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return false;
    }
    pos++;
    pos = skip_ws(pos);
    if (!pos || *pos == '\0' || strncmp(pos, "null", 4) == 0) {
        return false;
    }
    char *endptr = NULL;
    double value = strtod(pos, &endptr);
    if (endptr == pos) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_uint64_field(const char *json, const char *key, uint64_t *out) {
    if (!json || !key || !out) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return false;
    }
    pos++;
    pos = skip_ws(pos);
    if (!pos || *pos == '\0' || strncmp(pos, "null", 4) == 0) {
        return false;
    }
    char *endptr = NULL;
    unsigned long long value = strtoull(pos, &endptr, 10);
    if (endptr == pos) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool parse_bool_field(const char *json, const char *key, bool *out) {
    if (!json || !key || !out) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return false;
    }
    pos++;
    pos = skip_ws(pos);
    if (!pos) {
        return false;
    }
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_string_field(const char *json, const char *key, char *out, size_t out_sz) {
    if (!json || !key || !out || out_sz == 0) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return false;
    }
    pos++;
    pos = skip_ws(pos);
    if (!pos || *pos == '\0' || strncmp(pos, "null", 4) == 0) {
        return false;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;
    size_t i = 0;
    while (*pos) {
        if (*pos == '"' && *(pos - 1) != '\\') {
            break;
        }
        if (*pos == '\\' && *(pos + 1) != '\0') {
            pos++;
        }
        if (i + 1 < out_sz) {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
    return true;
}

static bool parse_double_pair(const char *json, const char *key, double *out_x, double *out_y) {
    if (!json || !key || !out_x || !out_y) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return false;
    }
    pos++;
    pos = skip_ws(pos);
    if (!pos || *pos == '\0' || strncmp(pos, "null", 4) == 0) {
        return false;
    }
    if (*pos != '[') {
        return false;
    }
    const char *end = find_matching_bracket(pos);
    if (!end) {
        return false;
    }
    char buffer[256];
    size_t len = (size_t)(end - pos + 1);
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, pos, len);
    buffer[len] = '\0';
    char *ptr = buffer + 1;
    ptr = (char *)skip_ws(ptr);
    char *local_end = NULL;
    double first = strtod(ptr, &local_end);
    if (local_end == ptr) {
        return false;
    }
    ptr = (char *)skip_ws(local_end);
    if (*ptr != ',') {
        return false;
    }
    ptr++;
    ptr = (char *)skip_ws(ptr);
    double second = strtod(ptr, &local_end);
    if (local_end == ptr) {
        return false;
    }
    *out_x = first;
    *out_y = second;
    return true;
}

static bool parse_int_pair(const char *json, const char *key, int *out_x, int *out_y) {
    if (!out_x || !out_y) {
        return false;
    }
    double dx = 0.0;
    double dy = 0.0;
    if (!parse_double_pair(json, key, &dx, &dy)) {
        return false;
    }
    *out_x = (int)lround(dx);
    *out_y = (int)lround(dy);
    return true;
}

static bool json_extract_object(const char *json, const char *key_with_quotes, char *out, size_t out_sz) {
    if (!json || !key_with_quotes || !out || out_sz == 0) {
        return false;
    }
    const char *pos = strstr(json, key_with_quotes);
    if (!pos) {
        return false;
    }
    pos = strchr(pos, '{');
    if (!pos) {
        return false;
    }
    const char *end = find_matching_brace(pos);
    if (!end) {
        return false;
    }
    size_t len = (size_t)(end - pos + 1);
    if (len >= out_sz) {
        len = out_sz - 1;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
    return true;
}

static const char *niri_get_outputs_json(double now) {
    if (!g_niri_data) {
        return NULL;
    }
    if (g_niri_data->outputs_cache_valid &&
        fabs(now - g_niri_data->outputs_cache_time) < NIRI_METADATA_CACHE_TTL) {
        return g_niri_data->outputs_cache;
    }
    if (niri_send_command("msg --json outputs", g_niri_data->outputs_cache,
                          sizeof(g_niri_data->outputs_cache)) != HYPRLAX_SUCCESS) {
        g_niri_data->outputs_cache_valid = false;
        return NULL;
    }
    g_niri_data->outputs_cache_valid = true;
    g_niri_data->outputs_cache_time = now;
    return g_niri_data->outputs_cache;
}

static const char *niri_get_workspaces_json(double now) {
    if (!g_niri_data) {
        return NULL;
    }
    if (g_niri_data->workspaces_cache_valid &&
        fabs(now - g_niri_data->workspaces_cache_time) < NIRI_METADATA_CACHE_TTL) {
        return g_niri_data->workspaces_cache;
    }
    if (niri_send_command("msg --json workspaces", g_niri_data->workspaces_cache,
                          sizeof(g_niri_data->workspaces_cache)) != HYPRLAX_SUCCESS) {
        g_niri_data->workspaces_cache_valid = false;
        return NULL;
    }
    g_niri_data->workspaces_cache_valid = true;
    g_niri_data->workspaces_cache_time = now;
    return g_niri_data->workspaces_cache;
}

static bool niri_find_output_geometry(const char *json,
                                      const char *output_name,
                                      double *out_x,
                                      double *out_y,
                                      double *out_w,
                                      double *out_h,
                                      int *out_index) {
    if (!json || !output_name || !*output_name) {
        return false;
    }
    const char *p = json;
    int index = 0;
    size_t name_len = strlen(output_name);
    while ((p = strchr(p, '"')) != NULL) {
        const char *key_start = p + 1;
        const char *key_end = key_start;
        while (*key_end && !(*key_end == '"' && *(key_end - 1) != '\\')) {
            key_end++;
        }
        if (*key_end == '\0') {
            break;
        }
        size_t key_len = (size_t)(key_end - key_start);
        const char *after_key = skip_ws(key_end + 1);
        if (!after_key || *after_key != ':') {
            p = key_end + 1;
            continue;
        }
        after_key++;
        after_key = skip_ws(after_key);
        if (!after_key || *after_key != '{') {
            p = after_key;
            continue;
        }
        const char *obj_start = after_key;
        const char *obj_end = find_matching_brace(obj_start);
        if (!obj_end) {
            break;
        }
        obj_end++;
        bool matched = (key_len == name_len && strncmp(key_start, output_name, name_len) == 0);
        if (matched) {
            char object_buf[2048];
            size_t len = (size_t)(obj_end - obj_start);
            if (len >= sizeof(object_buf)) {
                len = sizeof(object_buf) - 1;
            }
            memcpy(object_buf, obj_start, len);
            object_buf[len] = '\0';
            char logical_buf[1024];
            if (!json_extract_object(object_buf, "\"logical\"", logical_buf, sizeof(logical_buf))) {
                return false;
            }
            double lx = 0.0, ly = 0.0, lw = 0.0, lh = 0.0;
            parse_double_field(logical_buf, "x", &lx);
            parse_double_field(logical_buf, "y", &ly);
            parse_double_field(logical_buf, "width", &lw);
            parse_double_field(logical_buf, "height", &lh);
            if (out_x) *out_x = lx;
            if (out_y) *out_y = ly;
            if (out_w) *out_w = lw;
            if (out_h) *out_h = lh;
            if (out_index) *out_index = index;
            return true;
        }
        index++;
        p = obj_end;
    }
    return false;
}

static bool niri_pick_first_output(const char *json,
                                   double *out_x,
                                   double *out_y,
                                   double *out_w,
                                   double *out_h,
                                   int *out_index,
                                   char *out_name,
                                   size_t out_name_sz) {
    if (!json) {
        return false;
    }
    const char *p = strchr(json, '"');
    if (!p) {
        return false;
    }
    const char *key_start = p + 1;
    const char *key_end = key_start;
    while (*key_end && !(*key_end == '"' && *(key_end - 1) != '\\')) {
        key_end++;
    }
    if (*key_end == '\0') {
        return false;
    }
    size_t key_len = (size_t)(key_end - key_start);
    if (out_name && out_name_sz > 0) {
        size_t copy_len = key_len < (out_name_sz - 1) ? key_len : (out_name_sz - 1);
        memcpy(out_name, key_start, copy_len);
        out_name[copy_len] = '\0';
    }
    const char *after_key = skip_ws(key_end + 1);
    if (!after_key || *after_key != ':') {
        return false;
    }
    after_key++;
    after_key = skip_ws(after_key);
    if (!after_key || *after_key != '{') {
        return false;
    }
    const char *obj_start = after_key;
    const char *obj_end = find_matching_brace(obj_start);
    if (!obj_end) {
        return false;
    }
    obj_end++;
    char object_buf[2048];
    size_t len = (size_t)(obj_end - obj_start);
    if (len >= sizeof(object_buf)) {
        len = sizeof(object_buf) - 1;
    }
    memcpy(object_buf, obj_start, len);
    object_buf[len] = '\0';
    char logical_buf[1024];
    if (!json_extract_object(object_buf, "\"logical\"", logical_buf, sizeof(logical_buf))) {
        return false;
    }
    double lx = 0.0, ly = 0.0, lw = 0.0, lh = 0.0;
    parse_double_field(logical_buf, "x", &lx);
    parse_double_field(logical_buf, "y", &ly);
    parse_double_field(logical_buf, "width", &lw);
    parse_double_field(logical_buf, "height", &lh);
    if (out_x) *out_x = lx;
    if (out_y) *out_y = ly;
    if (out_w) *out_w = lw;
    if (out_h) *out_h = lh;
    if (out_index) *out_index = 0;
    return true;
}

static bool niri_workspace_output(uint64_t workspace_id,
                                  double now,
                                  char *out,
                                  size_t out_sz) {
    if (!out || out_sz == 0) {
        return false;
    }
    const char *json = niri_get_workspaces_json(now);
    if (!json) {
        return false;
    }
    const char *p = json;
    while ((p = strchr(p, '{')) != NULL) {
        const char *obj_start = p;
        const char *obj_end = find_matching_brace(obj_start);
        if (!obj_end) {
            break;
        }
        obj_end++;
        char object_buf[1024];
        size_t len = (size_t)(obj_end - obj_start);
        if (len >= sizeof(object_buf)) {
            len = sizeof(object_buf) - 1;
        }
        memcpy(object_buf, obj_start, len);
        object_buf[len] = '\0';
        uint64_t id = 0;
        if (parse_uint64_field(object_buf, "id", &id) && id == workspace_id) {
            if (parse_string_field(object_buf, "output", out, out_sz)) {
                return true;
            }
            return false;
        }
        p = obj_end;
    }
    return false;
}

static int niri_get_active_window_geometry(window_geometry_t *out) {
    if (!out) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    if (!g_niri_data) {
        if (niri_init(NULL) != HYPRLAX_SUCCESS || !g_niri_data) {
            return HYPRLAX_ERROR_NO_DATA;
        }
    }
    double now = niri_monotonic_time();
    if (g_niri_data->geometry_cache_valid &&
        fabs(now - g_niri_data->geometry_cache_time) < NIRI_GEOM_CACHE_TTL) {
        *out = g_niri_data->geometry_cache;
        return HYPRLAX_SUCCESS;
    }

    char response[8192];
    if (niri_send_command("msg --json focused-window", response, sizeof(response)) != HYPRLAX_SUCCESS) {
        g_niri_data->geometry_cache_valid = false;
        return HYPRLAX_ERROR_NO_DATA;
    }
    if (response[0] == '\0' || strstr(response, "\"id\"") == NULL) {
        g_niri_data->geometry_cache_valid = false;
        return HYPRLAX_ERROR_NO_DATA;
    }

    bool is_floating = false;
    parse_bool_field(response, "is_floating", &is_floating);

    uint64_t workspace_id = 0;
    bool has_workspace = parse_uint64_field(response, "workspace_id", &workspace_id);

    double window_w = 0.0;
    double window_h = 0.0;
    if (!parse_double_pair(response, "window_size", &window_w, &window_h) ||
        window_w <= 0.0 || window_h <= 0.0) {
        g_niri_data->geometry_cache_valid = false;
        return HYPRLAX_ERROR_NO_DATA;
    }

    double window_off_x = 0.0;
    double window_off_y = 0.0;
    parse_double_pair(response, "window_offset_in_tile", &window_off_x, &window_off_y);

    double tile_pos_x = 0.0;
    double tile_pos_y = 0.0;
    bool has_tile_pos = parse_double_pair(response, "tile_pos_in_workspace_view", &tile_pos_x, &tile_pos_y);

    double tile_size_x = 0.0;
    double tile_size_y = 0.0;
    parse_double_pair(response, "tile_size", &tile_size_x, &tile_size_y);

    int col_idx = 0;
    int row_idx = 0;
    bool has_indices = parse_int_pair(response, "pos_in_scrolling_layout", &col_idx, &row_idx);

    char output_name[64];
    output_name[0] = '\0';
    if (has_workspace) {
        if (!niri_workspace_output(workspace_id, now, output_name, sizeof(output_name))) {
            output_name[0] = '\0';
        }
    }

    double monitor_x = 0.0;
    double monitor_y = 0.0;
    double monitor_w = 0.0;
    double monitor_h = 0.0;
    int monitor_index = 0;
    const char *outputs_json = niri_get_outputs_json(now);
    bool have_monitor = false;
    if (outputs_json) {
        if (output_name[0] != '\0') {
            have_monitor = niri_find_output_geometry(outputs_json, output_name,
                                                     &monitor_x, &monitor_y,
                                                     &monitor_w, &monitor_h,
                                                     &monitor_index);
        }
        if (!have_monitor) {
            if (niri_pick_first_output(outputs_json, &monitor_x, &monitor_y,
                                       &monitor_w, &monitor_h, &monitor_index,
                                       output_name, sizeof(output_name))) {
                have_monitor = true;
            }
        }
    }
    if (!have_monitor || monitor_w <= 0.0 || monitor_h <= 0.0) {
        monitor_x = 0.0;
        monitor_y = 0.0;
        monitor_w = 1920.0;
        monitor_h = 1080.0;
        monitor_index = 0;
        if (output_name[0] == '\0') {
            strncpy(output_name, "default", sizeof(output_name) - 1);
            output_name[sizeof(output_name) - 1] = '\0';
        }
    }

    double tile_origin_x = 0.0;
    double tile_origin_y = 0.0;
    if (has_tile_pos) {
        tile_origin_x = tile_pos_x;
        tile_origin_y = tile_pos_y;
    } else if (has_indices) {
        if (tile_size_x > 0.0) {
            tile_origin_x = (double)(col_idx - 1) * tile_size_x;
        }
        if (tile_size_y > 0.0) {
            tile_origin_y = (double)(row_idx - 1) * tile_size_y;
        }
    } else {
        tile_origin_x = (monitor_w - window_w) * 0.5;
        tile_origin_y = (monitor_h - window_h) * 0.5;
    }

    double top_left_x = tile_origin_x + window_off_x;
    double top_left_y = tile_origin_y + window_off_y;

    double global_x = monitor_x + top_left_x;
    double global_y = monitor_y + top_left_y;

    double monitor_right = monitor_x + monitor_w;
    double monitor_bottom = monitor_y + monitor_h;
    if (global_x + window_w > monitor_right) {
        global_x = monitor_right - window_w;
    }
    if (global_y + window_h > monitor_bottom) {
        global_y = monitor_bottom - window_h;
    }
    if (global_x < monitor_x) {
        global_x = monitor_x;
    }
    if (global_y < monitor_y) {
        global_y = monitor_y;
    }

    memset(out, 0, sizeof(*out));
    out->x = global_x;
    out->y = global_y;
    out->width = window_w;
    out->height = window_h;
    out->workspace_id = has_workspace ? (int)workspace_id : -1;
    out->monitor_id = monitor_index;
    if (output_name[0] != '\0') {
        strncpy(out->monitor_name, output_name, sizeof(out->monitor_name) - 1);
        out->monitor_name[sizeof(out->monitor_name) - 1] = '\0';
    }
    out->floating = is_floating;

    g_niri_data->geometry_cache = *out;
    g_niri_data->geometry_cache_time = now;
    g_niri_data->geometry_cache_valid = true;

    return HYPRLAX_SUCCESS;
}

/* Send IPC command via niri msg */
static int niri_send_command(const char *command, char *response,
                            size_t response_size) {
    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Build full command with 'niri' prefix */
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "niri %s", command);

    /* Execute command and capture output */
    FILE *pipe = popen(full_cmd, "r");
    if (!pipe) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Read response if buffer provided */
    if (response && response_size > 0) {
        size_t total = 0;
        while (total < response_size - 1) {
            if (!fgets(response + total, response_size - total, pipe)) {
                break;
            }
            total = strlen(response);
        }
        response[response_size - 1] = '\0';
    }

    int status = pclose(pipe);
    return (status == 0) ? HYPRLAX_SUCCESS : HYPRLAX_ERROR_INVALID_ARGS;
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

/* Expose Niri event stream FD for blocking waits */
static int niri_get_event_fd(void) {
    if (!g_niri_data || !g_niri_data->connected || !g_niri_data->event_stream) return -1;
    return fileno(g_niri_data->event_stream);
}

#ifdef UNIT_TEST
/*
 * Test helpers (compiled only for unit tests)
 * Allow tests to inject a fake event stream so we can exercise
 * niri_poll_events() without running a real Niri instance.
 */
static int s_test_current_column = -1;
int niri_test_get_current_column(void) { return s_test_current_column; }
void niri_test_set_current_column(int column) { s_test_current_column = column; }
void niri_test_setup_stream(int read_fd) {
    if (!g_niri_data) {
        niri_init(NULL);
    }
    if (!g_niri_data) return;
    /* Adopt caller-provided read FD as a FILE* stream */
    int flags = fcntl(read_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
    }
    FILE *f = fdopen(read_fd, "r");
    g_niri_data->event_stream = f;
    g_niri_data->event_pid = 0;
    g_niri_data->connected = true;
    if (g_niri_data->event_stream) {
        setvbuf(g_niri_data->event_stream, NULL, _IONBF, 0);
    }
}

void niri_test_reset(void) {
    niri_destroy();
}
#endif /* UNIT_TEST */

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
    .get_event_fd = niri_get_event_fd,
    .supports_blur = niri_supports_blur,
    .supports_transparency = niri_supports_transparency,
    .supports_animations = niri_supports_animations,
    .set_blur = niri_set_blur,
    .set_wallpaper_offset = niri_set_wallpaper_offset,
    .get_active_window_geometry = niri_get_active_window_geometry,
};
