/*
 * sway.c - Sway compositor adapter
 *
 * Implements compositor interface for Sway/i3-compatible IPC.
 * Sway uses i3-compatible IPC protocol with JSON messages.
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
#include "../include/log.h"

/* Sway IPC message types (i3-compatible) */
#define SWAY_IPC_COMMAND        0
#define SWAY_IPC_GET_WORKSPACES 1
#define SWAY_IPC_SUBSCRIBE      2
#define SWAY_IPC_GET_OUTPUTS    3
#define SWAY_IPC_GET_TREE       4
#define SWAY_IPC_GET_MARKS      5
#define SWAY_IPC_GET_BAR_CONFIG 6
#define SWAY_IPC_GET_VERSION    7
#define SWAY_IPC_GET_BINDING_MODES 8
#define SWAY_IPC_GET_CONFIG     9
#define SWAY_IPC_SEND_TICK      10

/* IPC response types for events */
#define SWAY_IPC_EVENT_WORKSPACE        0x80000000
#define SWAY_IPC_EVENT_OUTPUT           0x80000001
#define SWAY_IPC_EVENT_MODE             0x80000002
#define SWAY_IPC_EVENT_WINDOW           0x80000003
#define SWAY_IPC_EVENT_BARCONFIG_UPDATE 0x80000004
#define SWAY_IPC_EVENT_BINDING          0x80000005
#define SWAY_IPC_EVENT_SHUTDOWN         0x80000006
#define SWAY_IPC_EVENT_TICK             0x80000007

/* IPC magic string */
#define SWAY_IPC_MAGIC "i3-ipc"
#define SWAY_IPC_HEADER_SIZE 14  /* magic (6) + length (4) + type (4) */

/* Sway IPC header */
typedef struct {
    char magic[6];
    uint32_t length;
    uint32_t type;
} __attribute__((packed)) sway_ipc_header_t;

/* Sway private data */
typedef struct {
    int cmd_fd;           /* Command socket */
    int event_fd;         /* Event socket */
    char socket_path[256];
    bool connected;
    int current_workspace;
    int current_monitor;
    char *event_buffer;   /* Buffer for partial event data */
    size_t event_buffer_size;
    size_t event_buffer_used;
} sway_data_t;

/* Global instance */
static sway_data_t *g_sway_data = NULL;

/* Forward declarations */
static int sway_send_ipc_message(int fd, uint32_t type, const char *payload);
static int sway_recv_ipc_message(int fd, uint32_t *type, char **payload, size_t *payload_size);
static int parse_workspace_id_from_json(const char *json);
static int parse_workspace_count_from_json(const char *json);
static int connect_sway_socket(const char *path);

/* Get Sway socket path */
static bool get_sway_socket_path(char *path, size_t size) {
    const char *sway_sock = getenv("SWAYSOCK");
    const char *i3_sock = getenv("I3SOCK");

    if (sway_sock && *sway_sock) {
        strncpy(path, sway_sock, size - 1);
        path[size - 1] = '\0';
        return true;
    }

    if (i3_sock && *i3_sock) {
        strncpy(path, i3_sock, size - 1);
        path[size - 1] = '\0';
        return true;
    }

    return false;
}

/* Initialize Sway adapter */
static int sway_init(void *platform_data) {
    (void)platform_data;

    if (g_sway_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }

    g_sway_data = calloc(1, sizeof(sway_data_t));
    if (!g_sway_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    g_sway_data->cmd_fd = -1;
    g_sway_data->event_fd = -1;
    g_sway_data->connected = false;
    g_sway_data->current_workspace = 1;
    g_sway_data->current_monitor = 0;
    g_sway_data->event_buffer_size = 4096;
    g_sway_data->event_buffer = malloc(g_sway_data->event_buffer_size);
    if (!g_sway_data->event_buffer) {
        free(g_sway_data);
        g_sway_data = NULL;
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    g_sway_data->event_buffer_used = 0;

    return HYPRLAX_SUCCESS;
}

/* Destroy Sway adapter */
static void sway_destroy(void) {
    if (!g_sway_data) return;

    if (g_sway_data->cmd_fd >= 0) {
        close(g_sway_data->cmd_fd);
    }

    if (g_sway_data->event_fd >= 0) {
        close(g_sway_data->event_fd);
    }

    if (g_sway_data->event_buffer) {
        free(g_sway_data->event_buffer);
    }

    free(g_sway_data);
    g_sway_data = NULL;
}

/* Detect if running under Sway */
static bool sway_detect(void) {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");
    const char *sway_sock = getenv("SWAYSOCK");

    if (sway_sock && *sway_sock) {
        return true;
    }

    if (desktop && strstr(desktop, "sway")) {
        return true;
    }

    if (session && strstr(session, "sway")) {
        return true;
    }

    return false;
}

/* Get compositor name */
static const char* sway_get_name(void) {
    return "Sway";
}

/* Create layer surface (uses wlr-layer-shell) */
static int sway_create_layer_surface(void *surface,
                                    const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* This will be handled by the platform layer with wlr-layer-shell protocol */
    return HYPRLAX_SUCCESS;
}

static void sway_configure_layer_surface(void *layer_surface,
                                        int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
}

static void sway_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
}

static int sway_get_current_workspace(void) {
    if (!g_sway_data) return 1;
    return g_sway_data->current_workspace;
}

static int sway_get_workspace_count(void) {
    if (!g_sway_data || !g_sway_data->connected) {
        return 10;  /* Default */
    }

    /* Send IPC command to get workspaces */
    if (sway_send_ipc_message(g_sway_data->cmd_fd, SWAY_IPC_GET_WORKSPACES, "") < 0) {
        return 10;
    }

    /* Receive response */
    uint32_t response_type;
    char *response = NULL;
    size_t response_size;

    if (sway_recv_ipc_message(g_sway_data->cmd_fd, &response_type, &response, &response_size) < 0) {
        return 10;
    }

    /* Parse JSON response to count workspaces */
    int count = parse_workspace_count_from_json(response);
    free(response);

    return count > 0 ? count : 10;
}

static int sway_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_sway_data || !g_sway_data->connected) {
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Send IPC command to get workspaces */
    if (sway_send_ipc_message(g_sway_data->cmd_fd, SWAY_IPC_GET_WORKSPACES, "") < 0) {
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Receive response */
    uint32_t response_type;
    char *response = NULL;
    size_t response_size;

    if (sway_recv_ipc_message(g_sway_data->cmd_fd, &response_type, &response, &response_size) < 0) {
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Parse JSON response - simplified parsing */
    int workspace_count = parse_workspace_count_from_json(response);
    if (workspace_count <= 0) {
        free(response);
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_SUCCESS;
    }

    *workspaces = calloc(workspace_count, sizeof(workspace_info_t));
    if (!*workspaces) {
        free(response);
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Simple parsing - look for workspace numbers */
    *count = workspace_count;
    char *ptr = response;
    int idx = 0;

    while ((ptr = strstr(ptr, "\"num\":"))) {
        ptr += 7;
        int ws_num = atoi(ptr);
        if (idx < workspace_count) {
            (*workspaces)[idx].id = ws_num;
            snprintf((*workspaces)[idx].name, sizeof((*workspaces)[idx].name), "%d", ws_num);

            /* Check if visible */
            char *visible = strstr(ptr, "\"visible\"");
            if (visible && visible < ptr + 100) {
                visible += 10;
                (*workspaces)[idx].visible = (strncmp(visible, "true", 4) == 0);
                (*workspaces)[idx].active = (*workspaces)[idx].visible;
            }

            idx++;
        }
    }

    free(response);
    return HYPRLAX_SUCCESS;
}

static int sway_get_current_monitor(void) {
    return 0;
}

static int sway_list_monitors(monitor_info_t **monitors, int *count) {
    if (!monitors || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!g_sway_data || !g_sway_data->connected) {
        *count = 0;
        *monitors = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Send IPC command to get outputs */
    if (sway_send_ipc_message(g_sway_data->cmd_fd, SWAY_IPC_GET_OUTPUTS, "") < 0) {
        *count = 0;
        *monitors = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Receive response */
    uint32_t response_type;
    char *response = NULL;
    size_t response_size;

    if (sway_recv_ipc_message(g_sway_data->cmd_fd, &response_type, &response, &response_size) < 0) {
        *count = 0;
        *monitors = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Count outputs - simplified */
    int output_count = 0;
    char *ptr = response;
    while ((ptr = strstr(ptr, "\"name\":"))) {
        output_count++;
        ptr += 8;
    }

    if (output_count == 0) {
        free(response);
        *count = 0;
        *monitors = NULL;
        return HYPRLAX_SUCCESS;
    }

    *monitors = calloc(output_count, sizeof(monitor_info_t));
    if (!*monitors) {
        free(response);
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Parse outputs */
    *count = output_count;
    ptr = response;
    int idx = 0;

    while ((ptr = strstr(ptr, "\"name\":"))) {
        if (idx >= output_count) break;

        ptr += 8;
        char *end = strchr(ptr, '"');
        if (end) {
            size_t name_len = end - ptr;
            if (name_len >= sizeof((*monitors)[idx].name)) {
                name_len = sizeof((*monitors)[idx].name) - 1;
            }
            strncpy((*monitors)[idx].name, ptr, name_len);
            (*monitors)[idx].name[name_len] = '\0';
        }

        (*monitors)[idx].id = idx;

        /* Parse dimensions - look for rect */
        char *rect = strstr(ptr, "\"rect\"");
        if (rect && rect < ptr + 500) {
            rect = strstr(rect, "\"x\":");
            if (rect) {
                (*monitors)[idx].x = atoi(rect + 5);
            }
            rect = strstr(rect, "\"y\":");
            if (rect) {
                (*monitors)[idx].y = atoi(rect + 5);
            }
            rect = strstr(rect, "\"width\":");
            if (rect) {
                (*monitors)[idx].width = atoi(rect + 9);
            }
            rect = strstr(rect, "\"height\":");
            if (rect) {
                (*monitors)[idx].height = atoi(rect + 10);
            }
        }

        /* Check if primary */
        char *primary = strstr(ptr, "\"primary\"");
        if (primary && primary < ptr + 500) {
            primary += 11;
            (*monitors)[idx].primary = (strncmp(primary, "true", 4) == 0);
        }

        (*monitors)[idx].scale = 1.0;
        idx++;
    }

    free(response);
    return HYPRLAX_SUCCESS;
}

/* Connect socket helper */
static int connect_sway_socket(const char *path) {
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

static int sway_connect_ipc(const char *socket_path) {
    if (!g_sway_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (g_sway_data->connected) {
        return HYPRLAX_SUCCESS;
    }

    /* Get socket path */
    if (socket_path && *socket_path) {
        strncpy(g_sway_data->socket_path, socket_path, sizeof(g_sway_data->socket_path) - 1);
    } else if (!get_sway_socket_path(g_sway_data->socket_path, sizeof(g_sway_data->socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Connect command socket with retries for startup race condition */
    g_sway_data->cmd_fd = compositor_connect_socket_with_retry(
        g_sway_data->socket_path,
        "Sway",
        30,     /* max retries */
        500     /* delay ms */
    );
    if (g_sway_data->cmd_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Connect event socket for subscriptions */
    g_sway_data->event_fd = connect_sway_socket(g_sway_data->socket_path);
    if (g_sway_data->event_fd < 0) {
        close(g_sway_data->cmd_fd);
        g_sway_data->cmd_fd = -1;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Make event socket non-blocking */
    int flags = fcntl(g_sway_data->event_fd, F_GETFL, 0);
    fcntl(g_sway_data->event_fd, F_SETFL, flags | O_NONBLOCK);

    /* Subscribe to workspace events */
    const char *subscribe = "[\"workspace\"]";
    if (sway_send_ipc_message(g_sway_data->event_fd, SWAY_IPC_SUBSCRIBE, subscribe) < 0) {
        close(g_sway_data->cmd_fd);
        close(g_sway_data->event_fd);
        g_sway_data->cmd_fd = -1;
        g_sway_data->event_fd = -1;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Read subscription response */
    uint32_t response_type;
    char *response = NULL;
    size_t response_size;
    sway_recv_ipc_message(g_sway_data->event_fd, &response_type, &response, &response_size);
    if (response) free(response);

    g_sway_data->connected = true;

    /* Get initial workspace */
    if (sway_send_ipc_message(g_sway_data->cmd_fd, SWAY_IPC_GET_WORKSPACES, "") == 0) {
        if (sway_recv_ipc_message(g_sway_data->cmd_fd, &response_type, &response, &response_size) == 0) {
            /* Find focused workspace */
            char *focused = strstr(response, "\"focused\":true");
            if (focused) {
                /* Look backwards for the workspace number */
                char *num = focused;
                while (num > response && strncmp(num, "\"num\":", 6) != 0) {
                    num--;
                }
                if (strncmp(num, "\"num\":", 6) == 0) {
                    g_sway_data->current_workspace = atoi(num + 6);
                }
            }
            free(response);
        }
    }

    return HYPRLAX_SUCCESS;
}

static void sway_disconnect_ipc(void) {
    if (!g_sway_data) return;

    if (g_sway_data->cmd_fd >= 0) {
        close(g_sway_data->cmd_fd);
        g_sway_data->cmd_fd = -1;
    }

    if (g_sway_data->event_fd >= 0) {
        close(g_sway_data->event_fd);
        g_sway_data->event_fd = -1;
    }

    g_sway_data->connected = false;
}

static int sway_poll_events(compositor_event_t *event) {
    if (!event || !g_sway_data || !g_sway_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Poll event socket */
    struct pollfd pfd = {
        .fd = g_sway_data->event_fd,
        .events = POLLIN
    };

    int poll_result = poll(&pfd, 1, 0);
    if (poll_result < 0) {
        return HYPRLAX_ERROR_NO_DATA;
    } else if (poll_result == 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Read IPC message */
    uint32_t msg_type;
    char *payload = NULL;
    size_t payload_size;

    if (sway_recv_ipc_message(g_sway_data->event_fd, &msg_type, &payload, &payload_size) < 0) {
        return HYPRLAX_ERROR_NO_DATA;
    }

    /* Parse workspace change event */
    if ((msg_type & 0xFFFFFF00) == SWAY_IPC_EVENT_WORKSPACE) {
        /* Look for "change":"focus" */
        if (strstr(payload, "\"change\":\"focus\"")) {
            /* Parse current workspace */
            char *current = strstr(payload, "\"current\"");
            if (current) {
                char *num = strstr(current, "\"num\":");
                if (num) {
                    int new_workspace = atoi(num + 6);
                    if (new_workspace != g_sway_data->current_workspace) {
                        event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                        event->data.workspace.from_workspace = g_sway_data->current_workspace;
                        event->data.workspace.to_workspace = new_workspace;
                        event->data.workspace.from_x = 0;
                        event->data.workspace.from_y = 0;
                        event->data.workspace.to_x = 0;
                        event->data.workspace.to_y = 0;
                        g_sway_data->current_workspace = new_workspace;

                        LOG_DEBUG("Sway workspace change: %d -> %d",
                                  event->data.workspace.from_workspace,
                                  event->data.workspace.to_workspace);

                        free(payload);
                        return HYPRLAX_SUCCESS;
                    }
                }
            }
        }
    }

    free(payload);
    return HYPRLAX_ERROR_NO_DATA;
}

static int sway_send_command(const char *command, char *response,
                            size_t response_size) {
    if (!g_sway_data || !g_sway_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Send command as IPC message */
    if (sway_send_ipc_message(g_sway_data->cmd_fd, SWAY_IPC_COMMAND, command) < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Receive response */
    uint32_t response_type;
    char *ipc_response = NULL;
    size_t ipc_response_size;

    if (sway_recv_ipc_message(g_sway_data->cmd_fd, &response_type, &ipc_response, &ipc_response_size) < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Copy response if buffer provided */
    if (response && response_size > 0) {
        size_t copy_size = ipc_response_size < response_size - 1 ? ipc_response_size : response_size - 1;
        memcpy(response, ipc_response, copy_size);
        response[copy_size] = '\0';
    }

    free(ipc_response);
    return HYPRLAX_SUCCESS;
}

static bool sway_supports_blur(void) {
    return false;  /* Sway doesn't have built-in blur */
}

static bool sway_supports_transparency(void) {
    return true;
}

static bool sway_supports_animations(void) {
    return false;  /* Sway has minimal animations */
}

static int sway_set_blur(float amount) {
    (void)amount;
    return HYPRLAX_ERROR_INVALID_ARGS;  /* Not supported */
}

static int sway_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    return HYPRLAX_SUCCESS;
}

/* Expose Sway event socket FD for blocking waits */
static int sway_get_event_fd(void) {
    if (!g_sway_data || !g_sway_data->connected) return -1;
    return g_sway_data->event_fd;
}

/* IPC helper functions */
static int sway_send_ipc_message(int fd, uint32_t type, const char *payload) {
    size_t payload_len = payload ? strlen(payload) : 0;
    size_t total_len = SWAY_IPC_HEADER_SIZE + payload_len;

    char *buffer = malloc(total_len);
    if (!buffer) {
        return -1;
    }

    /* Build header */
    memcpy(buffer, SWAY_IPC_MAGIC, 6);
    *((uint32_t*)(buffer + 6)) = payload_len;
    *((uint32_t*)(buffer + 10)) = type;

    /* Add payload */
    if (payload_len > 0) {
        memcpy(buffer + SWAY_IPC_HEADER_SIZE, payload, payload_len);
    }

    /* Send message */
    ssize_t n = write(fd, buffer, total_len);
    free(buffer);

    return (n == (ssize_t)total_len) ? 0 : -1;
}

static int sway_recv_ipc_message(int fd, uint32_t *type, char **payload, size_t *payload_size) {
    sway_ipc_header_t header;

    /* Read header */
    ssize_t n = read(fd, &header, SWAY_IPC_HEADER_SIZE);
    if (n != SWAY_IPC_HEADER_SIZE) {
        return -1;
    }

    /* Verify magic */
    if (memcmp(header.magic, SWAY_IPC_MAGIC, 6) != 0) {
        return -1;
    }

    *type = header.type;
    *payload_size = header.length;

    /* Read payload */
    if (header.length > 0) {
        *payload = malloc(header.length + 1);
        if (!*payload) {
            return -1;
        }

        n = read(fd, *payload, header.length);
        if (n != (ssize_t)header.length) {
            free(*payload);
            *payload = NULL;
            return -1;
        }

        (*payload)[header.length] = '\0';
    } else {
        *payload = NULL;
    }

    return 0;
}

static int parse_workspace_id_from_json(const char *json) {
    if (!json) return 1;

    /* Find focused workspace */
    const char *focused = strstr(json, "\"focused\":true");
    if (!focused) return 1;

    /* Look backwards for workspace number */
    const char *ptr = focused;
    while (ptr > json) {
        if (strncmp(ptr, "\"num\":", 6) == 0) {
            return atoi(ptr + 6);
        }
        ptr--;
    }

    return 1;
}

static int parse_workspace_count_from_json(const char *json) {
    if (!json) return 0;

    int count = 0;
    const char *ptr = json;

    /* Count occurrences of "num": */
    while ((ptr = strstr(ptr, "\"num\":"))) {
        count++;
        ptr += 6;
    }

    return count;
}

/* Sway compositor operations */
const compositor_ops_t compositor_sway_ops = {
    .init = sway_init,
    .destroy = sway_destroy,
    .detect = sway_detect,
    .get_name = sway_get_name,
    .create_layer_surface = sway_create_layer_surface,
    .configure_layer_surface = sway_configure_layer_surface,
    .destroy_layer_surface = sway_destroy_layer_surface,
    .get_current_workspace = sway_get_current_workspace,
    .get_workspace_count = sway_get_workspace_count,
    .list_workspaces = sway_list_workspaces,
    .get_current_monitor = sway_get_current_monitor,
    .list_monitors = sway_list_monitors,
    .connect_ipc = sway_connect_ipc,
    .disconnect_ipc = sway_disconnect_ipc,
    .poll_events = sway_poll_events,
    .send_command = sway_send_command,
    .get_event_fd = sway_get_event_fd,
    .supports_blur = sway_supports_blur,
    .supports_transparency = sway_supports_transparency,
    .supports_animations = sway_supports_animations,
    .set_blur = sway_set_blur,
    .set_wallpaper_offset = sway_set_wallpaper_offset,
};
