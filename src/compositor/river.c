/*
 * river.c - River compositor adapter
 *
 * Implements compositor interface for River, a dynamic tiling Wayland compositor.
 * River uses a tag-based workspace system and custom IPC protocol.
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
#include <wayland-client.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"
#include "../../protocols/river-status-client-protocol.h"

/* River uses tags instead of workspaces */
#define RIVER_MAX_TAGS 32
#define RIVER_DEFAULT_TAGS 9

/* Tag animation policy */
typedef enum {
    TAG_POLICY_HIGHEST,    /* Use highest visible tag for offset */
    TAG_POLICY_LOWEST,     /* Use lowest visible tag for offset */
    TAG_POLICY_FIRST_SET,  /* Use first set tag for offset */
    TAG_POLICY_NO_PARALLAX /* Disable when multiple tags visible */
} river_tag_policy_t;

/* River private data */
typedef struct {
    /* Wayland connection for river-status protocol */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_output *output;
    /* River status protocol objects */
    struct zriver_status_manager_v1 *status_manager;
    struct zriver_seat_status_v1 *seat_status;
    struct zriver_output_status_v1 *output_status;
    /* Connection state */
    bool connected;
    bool status_connected;
    /* Tag state */
    uint32_t focused_tags;  /* Bitfield of focused tags */
    uint32_t occupied_tags; /* Bitfield of occupied tags */
    uint32_t previous_focused_tags; /* For detecting changes */
    uint32_t urgent_tags;   /* Tags with urgent windows */
    /* Output tracking */
    int current_output;
    char current_output_name[64];
    char focused_output[64];  /* Currently focused output */
    /* Configuration */
    int tag_count;
    river_tag_policy_t tag_policy;
    bool animate_on_tag_change;
    bool geometry_warned;
    /* Event queue for batching */
    bool tags_changed;
    uint32_t new_focused_tags;
} river_data_t;

/* Global instance */
static river_data_t *g_river_data = NULL;

/* Forward declarations for river-status protocol handlers */
static void seat_status_focused_output(void *data,
                                      struct zriver_seat_status_v1 *seat_status,
                                      struct wl_output *output);
static void seat_status_unfocused_output(void *data,
                                        struct zriver_seat_status_v1 *seat_status,
                                        struct wl_output *output);
static void seat_status_focused_view(void *data,
                                    struct zriver_seat_status_v1 *seat_status,
                                    const char *title);
static void seat_status_mode(void *data,
                            struct zriver_seat_status_v1 *seat_status,
                            const char *mode);

static void output_status_focused_tags(void *data,
                                      struct zriver_output_status_v1 *output_status,
                                      uint32_t tags);
static void output_status_view_tags(void *data,
                                   struct zriver_output_status_v1 *output_status,
                                   struct wl_array *tags);
static void output_status_urgent_tags(void *data,
                                     struct zriver_output_status_v1 *output_status,
                                     uint32_t tags);
static void output_status_layout_name(void *data,
                                     struct zriver_output_status_v1 *output_status,
                                     const char *name);
static void output_status_layout_name_clear(void *data,
                                           struct zriver_output_status_v1 *output_status);

/* River seat status listener */
static const struct zriver_seat_status_v1_listener seat_status_listener = {
    .focused_output = seat_status_focused_output,
    .unfocused_output = seat_status_unfocused_output,
    .focused_view = seat_status_focused_view,
    .mode = seat_status_mode,
};

/* River output status listener */
static const struct zriver_output_status_v1_listener output_status_listener = {
    .focused_tags = output_status_focused_tags,
    .view_tags = output_status_view_tags,
    .urgent_tags = output_status_urgent_tags,
    .layout_name = output_status_layout_name,
    .layout_name_clear = output_status_layout_name_clear,
};

/* Registry listener for Wayland globals */
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t name, const char *interface, uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *registry,
                                  uint32_t name);

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

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

/* Get River socket paths */
static bool get_river_socket_paths(char *control_path, char *status_path, size_t size) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");

    if (!runtime_dir || !wayland_display) {
        return false;
    }

    /* Control socket for riverctl commands */
    if (control_path) {
        snprintf(control_path, size, "%s/river.control.%s", runtime_dir, wayland_display);
    }

    /* Status socket for monitoring events - uses Wayland protocol extension */
    if (status_path) {
        snprintf(status_path, size, "%s/%s", runtime_dir, wayland_display);
    }

    return true;
}

/* Helper: Calculate primary tag based on policy */
static int get_primary_tag(uint32_t tags, river_tag_policy_t policy) {
    if (tags == 0) return 1;

    /* Check if only one tag is visible */
    if ((tags & (tags - 1)) == 0) {
        /* Single tag visible - use it regardless of policy */
        return get_first_tag(tags);
    }

    /* Multiple tags visible - apply policy */
    switch (policy) {
        case TAG_POLICY_HIGHEST: {
            /* Find highest set bit */
            int tag = RIVER_MAX_TAGS;
            uint32_t mask = 1u << (RIVER_MAX_TAGS - 1);
            while (mask && !(tags & mask)) {
                mask >>= 1;
                tag--;
            }
            return tag;
        }

        case TAG_POLICY_LOWEST:
        case TAG_POLICY_FIRST_SET:
            /* Find lowest set bit */
            return get_first_tag(tags);

        case TAG_POLICY_NO_PARALLAX:
            /* Return current tag to prevent animation */
            return -1;

        default:
            return get_first_tag(tags);
    }
}

/* Load River configuration from environment */
static void river_load_config(river_data_t *data) {
    if (!data) return;

    /* Check for tag policy configuration */
    const char *tag_policy = getenv("HYPRLAX_RIVER_TAG_POLICY");
    if (tag_policy) {
        if (strcasecmp(tag_policy, "highest") == 0) {
            data->tag_policy = TAG_POLICY_HIGHEST;
        } else if (strcasecmp(tag_policy, "lowest") == 0) {
            data->tag_policy = TAG_POLICY_LOWEST;
        } else if (strcasecmp(tag_policy, "first") == 0 ||
                   strcasecmp(tag_policy, "first_set") == 0) {
            data->tag_policy = TAG_POLICY_FIRST_SET;
        } else if (strcasecmp(tag_policy, "none") == 0 ||
                   strcasecmp(tag_policy, "no_parallax") == 0) {
            data->tag_policy = TAG_POLICY_NO_PARALLAX;
        }

        LOG_DEBUG("River tag policy set to: %s", tag_policy);
    }

    /* Check if animation should be disabled for multi-tag */
    const char *animate = getenv("HYPRLAX_RIVER_ANIMATE_TAGS");
    if (animate) {
        data->animate_on_tag_change = (strcasecmp(animate, "false") != 0 &&
                                      strcasecmp(animate, "0") != 0);
    }

    /* Tag count configuration */
    const char *tag_count = getenv("HYPRLAX_RIVER_TAG_COUNT");
    if (tag_count) {
        int count = atoi(tag_count);
        if (count > 0 && count <= RIVER_MAX_TAGS) {
            data->tag_count = count;
        }
    }
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

    g_river_data->display = NULL;
    g_river_data->registry = NULL;
    g_river_data->seat = NULL;
    g_river_data->output = NULL;
    g_river_data->status_manager = NULL;
    g_river_data->seat_status = NULL;
    g_river_data->output_status = NULL;
    g_river_data->connected = false;
    g_river_data->status_connected = false;
    g_river_data->focused_tags = 1;  /* Tag 1 by default */
    g_river_data->occupied_tags = 0;
    g_river_data->previous_focused_tags = 1;
    g_river_data->urgent_tags = 0;
    g_river_data->current_output = 0;
    g_river_data->current_output_name[0] = '\0';
    g_river_data->focused_output[0] = '\0';
    g_river_data->tag_count = RIVER_DEFAULT_TAGS;
    g_river_data->tag_policy = TAG_POLICY_LOWEST;  /* Default policy */
    g_river_data->animate_on_tag_change = true;
    g_river_data->geometry_warned = false;
    g_river_data->tags_changed = false;
    g_river_data->new_focused_tags = 1;

    /* Load configuration from environment */
    river_load_config(g_river_data);

    return HYPRLAX_SUCCESS;
}

/* Destroy River adapter */
static void river_destroy(void) {
    if (!g_river_data) return;

    /* Clean up river-status protocol objects */
    if (g_river_data->output_status) {
        zriver_output_status_v1_destroy(g_river_data->output_status);
    }
    if (g_river_data->seat_status) {
        zriver_seat_status_v1_destroy(g_river_data->seat_status);
    }
    if (g_river_data->status_manager) {
        zriver_status_manager_v1_destroy(g_river_data->status_manager);
    }

    /* Clean up Wayland objects */
    if (g_river_data->registry) {
        wl_registry_destroy(g_river_data->registry);
    }
    if (g_river_data->display) {
        wl_display_disconnect(g_river_data->display);
    }

    free(g_river_data);
    g_river_data = NULL;
}

/* Detect if running under River */
static bool river_detect(void) {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");

    if ((desktop && strcasecmp(desktop, "river") == 0) ||
        (session && strcasecmp(session, "river") == 0)) {
        return true;
    }

    /* Check if River control socket exists */
    char control_path[256];
    if (get_river_socket_paths(control_path, NULL, sizeof(control_path))) {
        if (access(control_path, F_OK) == 0) {
            return true;
        }
    }

    /* Check for riverctl command */
    return system("which riverctl > /dev/null 2>&1") == 0;
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

    /* Return primary tag based on policy */
    int primary = get_primary_tag(g_river_data->focused_tags, g_river_data->tag_policy);
    return (primary > 0) ? primary : get_first_tag(g_river_data->focused_tags);
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
    (void)socket_path; /* Not used - auto-detect */

    if (!g_river_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (g_river_data->connected) {
        return HYPRLAX_SUCCESS;
    }

    /* Connect to Wayland display */
    g_river_data->display = wl_display_connect(NULL);
    if (!g_river_data->display) {
        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] River: Failed to connect to Wayland display\n");
        }
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Get registry and bind globals */
    g_river_data->registry = wl_display_get_registry(g_river_data->display);
    wl_registry_add_listener(g_river_data->registry, &registry_listener, g_river_data);

    /* Roundtrip to get globals */
    wl_display_roundtrip(g_river_data->display);

    /* Check if river-status protocol is available */
    if (!g_river_data->status_manager) {
        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] River: river-status protocol not available\n");
        }
        /* Fall back to polling approach */
        g_river_data->connected = true;
        g_river_data->status_connected = false;
        return HYPRLAX_SUCCESS;
    }

    /* Create status objects if we have the required globals */
    if (g_river_data->seat && !g_river_data->seat_status) {
        g_river_data->seat_status = zriver_status_manager_v1_get_river_seat_status(
            g_river_data->status_manager, g_river_data->seat);
        zriver_seat_status_v1_add_listener(g_river_data->seat_status,
                                          &seat_status_listener, g_river_data);
    }

    if (g_river_data->output && !g_river_data->output_status) {
        g_river_data->output_status = zriver_status_manager_v1_get_river_output_status(
            g_river_data->status_manager, g_river_data->output);
        zriver_output_status_v1_add_listener(g_river_data->output_status,
                                            &output_status_listener, g_river_data);
    }

    /* Flush and get initial state */
    wl_display_flush(g_river_data->display);
    wl_display_roundtrip(g_river_data->display);

    g_river_data->connected = true;
    g_river_data->status_connected = (g_river_data->seat_status != NULL ||
                                     g_river_data->output_status != NULL);

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] River connected, status protocol: %s, focused tags: 0x%x\n",
                g_river_data->status_connected ? "yes" : "no",
                g_river_data->focused_tags);
    }

    return HYPRLAX_SUCCESS;
}

/* Disconnect from IPC */
static void river_disconnect_ipc(void) {
    if (!g_river_data) return;

    /* Clean up status objects */
    if (g_river_data->output_status) {
        zriver_output_status_v1_destroy(g_river_data->output_status);
        g_river_data->output_status = NULL;
    }
    if (g_river_data->seat_status) {
        zriver_seat_status_v1_destroy(g_river_data->seat_status);
        g_river_data->seat_status = NULL;
    }
    if (g_river_data->status_manager) {
        zriver_status_manager_v1_destroy(g_river_data->status_manager);
        g_river_data->status_manager = NULL;
    }

    if (g_river_data->registry) {
        wl_registry_destroy(g_river_data->registry);
        g_river_data->registry = NULL;
    }
    if (g_river_data->display) {
        wl_display_disconnect(g_river_data->display);
        g_river_data->display = NULL;
    }

    g_river_data->connected = false;
    g_river_data->status_connected = false;
}

/* Poll for events */
static int river_poll_events(compositor_event_t *event) {
    if (!event || !g_river_data || !g_river_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* If we have river-status protocol, use it */
    if (g_river_data->status_connected && g_river_data->display) {
        /* Process pending Wayland events */
        wl_display_dispatch_pending(g_river_data->display);

        /* Check if we have a tag change to report */
        if (g_river_data->tags_changed) {
            uint32_t new_tags = g_river_data->new_focused_tags;
            g_river_data->tags_changed = false;

            /* Process tag change */
            if (new_tags != g_river_data->focused_tags) {
        /* Calculate primary tags for animation */
        int old_primary = get_primary_tag(g_river_data->focused_tags, g_river_data->tag_policy);
        int new_primary = get_primary_tag(new_tags, g_river_data->tag_policy);

        /* Check for multi-tag scenarios */
        bool old_multi = (g_river_data->focused_tags & (g_river_data->focused_tags - 1)) != 0;
        bool new_multi = (new_tags & (new_tags - 1)) != 0;

        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] River tag state: old=0x%x (%s) new=0x%x (%s)\n",
                    g_river_data->focused_tags, old_multi ? "multi" : "single",
                    new_tags, new_multi ? "multi" : "single");
        }

        /* Determine if we should animate */
        bool should_animate = g_river_data->animate_on_tag_change;

        /* Apply multi-tag policy */
        if (new_multi && g_river_data->tag_policy == TAG_POLICY_NO_PARALLAX) {
            should_animate = false;
            if (getenv("HYPRLAX_DEBUG")) {
                fprintf(stderr, "[DEBUG] River: Disabling animation due to multi-tag with NO_PARALLAX policy\n");
            }
        }

        if (should_animate && old_primary > 0 && new_primary > 0 && old_primary != new_primary) {
            event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
            event->data.workspace.from_workspace = old_primary;
            event->data.workspace.to_workspace = new_primary;
            /* River doesn't have 2D workspaces */
            event->data.workspace.from_x = 0;
            event->data.workspace.from_y = 0;
            event->data.workspace.to_x = 0;
            event->data.workspace.to_y = 0;
            /* Store output name if we have it */
            strncpy(event->data.workspace.monitor_name,
                   g_river_data->current_output_name,
                   sizeof(event->data.workspace.monitor_name) - 1);
            event->data.workspace.monitor_name[sizeof(event->data.workspace.monitor_name) - 1] = '\0';

            g_river_data->previous_focused_tags = g_river_data->focused_tags;
            g_river_data->focused_tags = new_tags;

            if (getenv("HYPRLAX_DEBUG")) {
                fprintf(stderr, "[DEBUG] River tag change event: %d -> %d (tags: 0x%x -> 0x%x)\n",
                        old_primary, new_primary,
                        g_river_data->previous_focused_tags, new_tags);
            }

            return HYPRLAX_SUCCESS;
        }

                /* Update tags even if not animating */
                g_river_data->previous_focused_tags = g_river_data->focused_tags;
                g_river_data->focused_tags = new_tags;

                if (getenv("HYPRLAX_DEBUG") && !should_animate) {
                    fprintf(stderr, "[DEBUG] River tags updated without animation: 0x%x -> 0x%x\n",
                            g_river_data->previous_focused_tags, new_tags);
                }
            }
        }

        /* Check for more events */
        if (wl_display_prepare_read(g_river_data->display) == 0) {
            wl_display_read_events(g_river_data->display);
        }
    }

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

    /* River commands are sent via riverctl, not a persistent socket */
    char cmd_buffer[1024];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "riverctl %s 2>&1", command);

    FILE *fp = popen(cmd_buffer, "r");
    if (!fp) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Read response if buffer provided */
    if (response && response_size > 0) {
        size_t total = 0;
        while (total < response_size - 1 &&
               fgets(response + total, response_size - total, fp) != NULL) {
            total = strlen(response);
        }
        response[response_size - 1] = '\0';
    }

    int result = pclose(fp);
    return (result == 0) ? HYPRLAX_SUCCESS : HYPRLAX_ERROR_INVALID_ARGS;
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
    /* River itself has minimal animations, but we can animate tag changes */
    return g_river_data ? g_river_data->animate_on_tag_change : true;
}

/* Set blur */
static int river_set_blur(float amount) {
    (void)amount;
    return HYPRLAX_ERROR_INVALID_ARGS;  /* Not supported */
}

static int river_get_active_window_geometry(window_geometry_t *out) {
    if (!out) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    if (!g_river_data) {
        return HYPRLAX_ERROR_NO_DATA;
    }
    if (!g_river_data->geometry_warned) {
        LOG_WARN("river: active window geometry not available; window input source disabled");
        g_river_data->geometry_warned = true;
    }
    memset(out, 0, sizeof(*out));
    out->workspace_id = -1;
    out->monitor_id = g_river_data->current_output;
    if (g_river_data->current_output_name[0] != '\0') {
        strncpy(out->monitor_name, g_river_data->current_output_name, sizeof(out->monitor_name) - 1);
        out->monitor_name[sizeof(out->monitor_name) - 1] = '\0';
    }
    return HYPRLAX_ERROR_NO_DATA;
}

/* Set wallpaper offset */
static int river_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* Wallpaper offset is handled through layer surface positioning */
    /* This would be implemented in the platform layer */
    return HYPRLAX_SUCCESS;
}

/* Expose River status Wayland display FD (if connected) */
static int river_get_event_fd(void) {
    if (!g_river_data || !g_river_data->display) return -1;
    return wl_display_get_fd(g_river_data->display);
}

/* Get visible tag count for multi-tag handling */
static int river_get_visible_tag_count(void) {
    if (!g_river_data) return 0;
    return count_tags(g_river_data->focused_tags);
}

/* Check if multiple tags are visible */
static bool river_has_multiple_tags_visible(void) {
    if (!g_river_data) return false;
    /* Check if more than one bit is set */
    uint32_t tags = g_river_data->focused_tags;
    return (tags & (tags - 1)) != 0;
}

/* River-status protocol handlers */
static void seat_status_focused_output(void *data,
                                      struct zriver_seat_status_v1 *seat_status,
                                      struct wl_output *output) {
    (void)seat_status;
    river_data_t *river = (river_data_t *)data;
    if (!river) return;

    /* Store focused output for workspace events */
    river->output = output;

    LOG_DEBUG("River: Focused output changed");
}

static void seat_status_unfocused_output(void *data,
                                        struct zriver_seat_status_v1 *seat_status,
                                        struct wl_output *output) {
    (void)data;
    (void)seat_status;
    (void)output;
    /* Not needed for our purposes */
}

static void seat_status_focused_view(void *data,
                                    struct zriver_seat_status_v1 *seat_status,
                                    const char *title) {
    (void)data;
    (void)seat_status;
    (void)title;
    /* Not needed for parallax */
}

static void seat_status_mode(void *data,
                            struct zriver_seat_status_v1 *seat_status,
                            const char *mode) {
    (void)data;
    (void)seat_status;
    (void)mode;
    /* Not needed for parallax */
}

static void output_status_focused_tags(void *data,
                                      struct zriver_output_status_v1 *output_status,
                                      uint32_t tags) {
    (void)output_status;
    river_data_t *river = (river_data_t *)data;
    if (!river) return;

    /* Store the new tags - we'll process them in poll_events */
    if (tags != river->focused_tags) {
        river->tags_changed = true;
        river->new_focused_tags = tags;

        LOG_DEBUG("River: Tags changed from 0x%x to 0x%x", river->focused_tags, tags);
    }
}

static void output_status_view_tags(void *data,
                                   struct zriver_output_status_v1 *output_status,
                                   struct wl_array *tags) {
    (void)output_status;
    (void)tags;
    river_data_t *river = (river_data_t *)data;
    if (!river) return;

    /* This gives us occupied tags - which windows are on which tags */
    /* We could use this to optimize animations */
}

static void output_status_urgent_tags(void *data,
                                     struct zriver_output_status_v1 *output_status,
                                     uint32_t tags) {
    (void)output_status;
    river_data_t *river = (river_data_t *)data;
    if (!river) return;

    river->urgent_tags = tags;
}

static void output_status_layout_name(void *data,
                                     struct zriver_output_status_v1 *output_status,
                                     const char *name) {
    (void)output_status;
    (void)data;
    (void)name;
    /* Layout name not needed for parallax */
}

static void output_status_layout_name_clear(void *data,
                                           struct zriver_output_status_v1 *output_status) {
    (void)output_status;
    (void)data;
    /* Layout name not needed for parallax */
}

/* Wayland registry handler */
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t name, const char *interface, uint32_t version) {
    river_data_t *river = (river_data_t *)data;
    if (!river) return;

    if (strcmp(interface, "zriver_status_manager_v1") == 0) {
        river->status_manager = wl_registry_bind(registry, name,
                                                &zriver_status_manager_v1_interface,
                                                version < 4 ? version : 4);
        LOG_DEBUG("River: Found river-status protocol v%d", version);
    } else if (strcmp(interface, "wl_seat") == 0) {
        river->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0) {
        /* Bind first output - in multi-monitor setup, would need to track all */
        if (!river->output) {
            river->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                  uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
    /* Handle removal if needed */
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
    .get_event_fd = river_get_event_fd,
    .supports_blur = river_supports_blur,
    .supports_transparency = river_supports_transparency,
    .supports_animations = river_supports_animations,
    .set_blur = river_set_blur,
    .set_wallpaper_offset = river_set_wallpaper_offset,
    .get_active_window_geometry = river_get_active_window_geometry,
};
