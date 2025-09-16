/*
 * river.c - River compositor adapter
 * 
 * Implements compositor interface for River, a dynamic tiling Wayland compositor.
 * River uses a tag-based workspace system and custom IPC protocol.
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

/* River uses tags instead of workspaces */
#define RIVER_MAX_TAGS 32
#define RIVER_DEFAULT_TAGS 9

/* River private data */
typedef struct {
    int control_fd;       /* River control socket */
    int status_fd;        /* River status socket */
    char socket_path[256];
    bool connected;
    uint32_t focused_tags;  /* Bitfield of focused tags */
    uint32_t occupied_tags; /* Bitfield of occupied tags */
    int current_output;
    int tag_count;
} river_data_t;

/* Global instance */
static river_data_t *g_river_data = NULL;

/* Helper: Count set bits (tags) */
static int count_tags(uint32_t tags) {
    int count = 0;
    while (tags) {
        count += tags & 1;
        tags >>= 1;
    }
    return count;
}

/* Helper: Get first set tag */
static int get_first_tag(uint32_t tags) {
    if (tags == 0) return 1;
    
    int tag = 1;
    while ((tags & 1) == 0) {
        tags >>= 1;
        tag++;
    }
    return tag;
}

/* Helper: Convert tag number to bitmask */
static uint32_t tag_to_mask(int tag) {
    if (tag < 1 || tag > RIVER_MAX_TAGS) return 1;
    return 1u << (tag - 1);
}

/* Get River socket path */
static bool get_river_socket_path(char *path, size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    
    if (!runtime_dir || !wayland_display) {
        return false;
    }
    
    snprintf(path, size, "%s/%s.control", runtime_dir, wayland_display);
    return true;
}

/* Initialize River adapter */
static int river_init(void *platform_data) {
    (void)platform_data;
    
    if (g_river_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }
    
    g_river_data = calloc(1, sizeof(river_data_t));
    if (!g_river_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    g_river_data->control_fd = -1;
    g_river_data->status_fd = -1;
    g_river_data->connected = false;
    g_river_data->focused_tags = 1;  /* Tag 1 by default */
    g_river_data->occupied_tags = 0;
    g_river_data->current_output = 0;
    g_river_data->tag_count = RIVER_DEFAULT_TAGS;
    
    return HYPRLAX_SUCCESS;
}

/* Destroy River adapter */
static void river_destroy(void) {
    if (!g_river_data) return;
    
    if (g_river_data->control_fd >= 0) {
        close(g_river_data->control_fd);
    }
    
    if (g_river_data->status_fd >= 0) {
        close(g_river_data->status_fd);
    }
    
    free(g_river_data);
    g_river_data = NULL;
}

/* Detect if running under River */
static bool river_detect(void) {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    
    if (desktop && strcasecmp(desktop, "river") == 0) {
        return true;
    }
    
    /* Check if River control socket exists */
    char socket_path[256];
    if (get_river_socket_path(socket_path, sizeof(socket_path))) {
        if (access(socket_path, F_OK) == 0) {
            return true;
        }
    }
    
    return false;
}

/* Get compositor name */
static const char* river_get_name(void) {
    return "River";
}

/* Create layer surface (uses wlr-layer-shell) */
static int river_create_layer_surface(void *surface, 
                                     const layer_surface_config_t *config) {
    (void)surface;
    (void)config;
    /* This will be handled by the platform layer with wlr-layer-shell protocol */
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void river_configure_layer_surface(void *layer_surface, 
                                         int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
    /* Handled by platform layer */
}

/* Destroy layer surface */
static void river_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
    /* Handled by platform layer */
}

/* Get current workspace (tag) */
static int river_get_current_workspace(void) {
    if (!g_river_data) return 1;
    
    /* Return the first focused tag */
    return get_first_tag(g_river_data->focused_tags);
}

/* Get workspace count (tag count) */
static int river_get_workspace_count(void) {
    if (!g_river_data) return RIVER_DEFAULT_TAGS;
    return g_river_data->tag_count;
}

/* List workspaces (tags) */
static int river_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (!g_river_data) {
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    *count = g_river_data->tag_count;
    *workspaces = calloc(g_river_data->tag_count, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    for (int i = 0; i < g_river_data->tag_count; i++) {
        (*workspaces)[i].id = i + 1;
        snprintf((*workspaces)[i].name, sizeof((*workspaces)[i].name), "%d", i + 1);
        
        uint32_t tag_mask = tag_to_mask(i + 1);
        (*workspaces)[i].active = (g_river_data->focused_tags & tag_mask) != 0;
        (*workspaces)[i].visible = (*workspaces)[i].active;
        (*workspaces)[i].occupied = (g_river_data->occupied_tags & tag_mask) != 0;
    }
    
    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int river_get_current_monitor(void) {
    if (!g_river_data) return 0;
    return g_river_data->current_output;
}

/* List monitors */
static int river_list_monitors(monitor_info_t **monitors, int *count) {
    if (!monitors || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Simplified implementation - would need to query River for actual outputs */
    *count = 1;
    *monitors = calloc(1, sizeof(monitor_info_t));
    if (!*monitors) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    (*monitors)[0].id = 0;
    strncpy((*monitors)[0].name, "Primary", sizeof((*monitors)[0].name));
    (*monitors)[0].x = 0;
    (*monitors)[0].y = 0;
    (*monitors)[0].width = 1920;
    (*monitors)[0].height = 1080;
    (*monitors)[0].scale = 1.0;
    (*monitors)[0].primary = true;
    
    return HYPRLAX_SUCCESS;
}

/* Connect socket helper */
static int connect_river_socket(const char *path) {
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
    
    /* Make non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    return fd;
}

/* Connect to River IPC */
static int river_connect_ipc(const char *socket_path) {
    if (!g_river_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (g_river_data->connected) {
        return HYPRLAX_SUCCESS;
    }
    
    /* Get socket path */
    if (socket_path && *socket_path) {
        strncpy(g_river_data->socket_path, socket_path, sizeof(g_river_data->socket_path) - 1);
    } else if (!get_river_socket_path(g_river_data->socket_path, sizeof(g_river_data->socket_path))) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Connect control socket */
    g_river_data->control_fd = connect_river_socket(g_river_data->socket_path);
    if (g_river_data->control_fd < 0) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* River status monitoring would require a separate status protocol */
    /* For now, we'll poll the control socket for updates */
    
    g_river_data->connected = true;
    
    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void river_disconnect_ipc(void) {
    if (!g_river_data) return;
    
    if (g_river_data->control_fd >= 0) {
        close(g_river_data->control_fd);
        g_river_data->control_fd = -1;
    }
    
    if (g_river_data->status_fd >= 0) {
        close(g_river_data->status_fd);
        g_river_data->status_fd = -1;
    }
    
    g_river_data->connected = false;
}

/* Poll for events */
static int river_poll_events(compositor_event_t *event) {
    if (!event || !g_river_data || !g_river_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* River doesn't have a standard event protocol like Hyprland */
    /* Would need to implement River status protocol parsing */
    /* For now, return no data */
    
    memset(event, 0, sizeof(*event));
    return HYPRLAX_ERROR_NO_DATA;
}

/* Send command */
static int river_send_command(const char *command, char *response, 
                             size_t response_size) {
    if (!g_river_data || !g_river_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    if (!command) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Send command to River control socket */
    size_t cmd_len = strlen(command);
    if (write(g_river_data->control_fd, command, cmd_len) != (ssize_t)cmd_len) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* River control commands typically don't return responses */
    if (response && response_size > 0) {
        response[0] = '\0';
    }
    
    return HYPRLAX_SUCCESS;
}

/* Check blur support */
static bool river_supports_blur(void) {
    return false;  /* River doesn't have built-in blur */
}

/* Check transparency support */
static bool river_supports_transparency(void) {
    return true;
}

/* Check animation support */
static bool river_supports_animations(void) {
    return false;  /* River has minimal animations */
}

/* Set blur */
static int river_set_blur(float amount) {
    (void)amount;
    return HYPRLAX_ERROR_INVALID_ARGS;  /* Not supported */
}

/* Set wallpaper offset */
static int river_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    return HYPRLAX_SUCCESS;
}

/* River compositor operations */
const compositor_ops_t compositor_river_ops = {
    .init = river_init,
    .destroy = river_destroy,
    .detect = river_detect,
    .get_name = river_get_name,
    .create_layer_surface = river_create_layer_surface,
    .configure_layer_surface = river_configure_layer_surface,
    .destroy_layer_surface = river_destroy_layer_surface,
    .get_current_workspace = river_get_current_workspace,
    .get_workspace_count = river_get_workspace_count,
    .list_workspaces = river_list_workspaces,
    .get_current_monitor = river_get_current_monitor,
    .list_monitors = river_list_monitors,
    .connect_ipc = river_connect_ipc,
    .disconnect_ipc = river_disconnect_ipc,
    .poll_events = river_poll_events,
    .send_command = river_send_command,
    .supports_blur = river_supports_blur,
    .supports_transparency = river_supports_transparency,
    .supports_animations = river_supports_animations,
    .set_blur = river_set_blur,
    .set_wallpaper_offset = river_set_wallpaper_offset,
};