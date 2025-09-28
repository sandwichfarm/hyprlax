/*
 * compositor.c - Compositor adapter management
 *
 * Handles creation and management of compositor adapters.
 */

#include <stdio.h>
#include "../include/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Utility function for connecting to Unix socket with retries
 * Used by all compositors to wait for compositor readiness at startup
 */
int compositor_connect_socket_with_retry(const char *socket_path,
                                         const char *compositor_name,
                                         int max_retries,
                                         int retry_delay_ms) {
    if (!socket_path) return -1;

    bool first_attempt = true;

    for (int i = 0; i < max_retries; i++) {
        /* Try to connect */
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (i < max_retries - 1) {
                struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = retry_delay_ms * 1000000L;
            nanosleep(&ts, NULL);
                continue;
            }
            return -1;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            /* Success */
            if (!first_attempt && compositor_name) {
                LOG_INFO("Connected to %s after %d retries", compositor_name, i);
            }
            return fd;
        }

        /* Connection failed */
        close(fd);

        if (first_attempt && compositor_name) {
            LOG_INFO("Waiting for %s to be ready...", compositor_name);
            first_attempt = false;
        }

        if (i < max_retries - 1) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = retry_delay_ms * 1000000L;
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

/* Detect compositor type */
compositor_type_t compositor_detect(void) {
    /* Check in order of specificity */

#ifdef ENABLE_HYPRLAND
    /* Hyprland */
    if (compositor_hyprland_ops.detect && compositor_hyprland_ops.detect()) {
        DEBUG_LOG("Detected Hyprland compositor");
        return COMPOSITOR_HYPRLAND;
    }
#endif

#ifdef ENABLE_WAYFIRE
    /* Wayfire (2D workspace grid) */
    if (compositor_wayfire_ops.detect && compositor_wayfire_ops.detect()) {
        DEBUG_LOG("Detected Wayfire compositor");
        return COMPOSITOR_WAYFIRE;
    }
#endif

#ifdef ENABLE_NIRI
    /* Niri (scrollable workspaces) */
    if (compositor_niri_ops.detect && compositor_niri_ops.detect()) {
        DEBUG_LOG("Detected Niri compositor");
        return COMPOSITOR_NIRI;
    }
#endif

#ifdef ENABLE_SWAY
    /* Sway */
    if (compositor_sway_ops.detect && compositor_sway_ops.detect()) {
        DEBUG_LOG("Detected Sway compositor");
        return COMPOSITOR_SWAY;
    }
#endif

#ifdef ENABLE_RIVER
    /* River */
    if (compositor_river_ops.detect && compositor_river_ops.detect()) {
        DEBUG_LOG("Detected River compositor");
        return COMPOSITOR_RIVER;
    }
#endif

#ifdef ENABLE_GENERIC_WAYLAND
    /* Generic Wayland (fallback) */
    if (compositor_generic_wayland_ops.detect && compositor_generic_wayland_ops.detect()) {
        DEBUG_LOG("Detected generic Wayland compositor");
        return COMPOSITOR_GENERIC_WAYLAND;
    }
#endif

    LOG_WARN("Could not detect compositor type");
#ifdef ENABLE_GENERIC_WAYLAND
    return COMPOSITOR_GENERIC_WAYLAND;
#else
    return COMPOSITOR_AUTO; /* Will fail in compositor_create */
#endif
}

/* Create compositor adapter instance */
int compositor_create(compositor_adapter_t **out_adapter, compositor_type_t type) {
    if (!out_adapter) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    compositor_adapter_t *adapter = calloc(1, sizeof(compositor_adapter_t));
    if (!adapter) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Auto-detect if requested */
    if (type == COMPOSITOR_AUTO) {
        type = compositor_detect();
    }

    /* Select adapter based on type */
    switch (type) {
#ifdef ENABLE_HYPRLAND
        case COMPOSITOR_HYPRLAND:
            adapter->ops = &compositor_hyprland_ops;
            adapter->type = COMPOSITOR_HYPRLAND;
            adapter->caps = C_CAP_GLOBAL_CURSOR | C_CAP_WS_GLOBAL_NUMERIC;
            break;
#endif

#ifdef ENABLE_WAYFIRE
        case COMPOSITOR_WAYFIRE:
            adapter->ops = &compositor_wayfire_ops;
            adapter->type = COMPOSITOR_WAYFIRE;
            adapter->caps = C_CAP_WS_SET_BASED;
            break;
#endif

#ifdef ENABLE_NIRI
        case COMPOSITOR_NIRI:
            adapter->ops = &compositor_niri_ops;
            adapter->type = COMPOSITOR_NIRI;
            adapter->caps = C_CAP_WS_PER_OUTPUT_NUMERIC;
            break;
#endif

#ifdef ENABLE_SWAY
        case COMPOSITOR_SWAY:
            adapter->ops = &compositor_sway_ops;
            adapter->type = COMPOSITOR_SWAY;
            adapter->caps = C_CAP_WS_GLOBAL_NUMERIC;
            break;
#endif

#ifdef ENABLE_RIVER
        case COMPOSITOR_RIVER:
            adapter->ops = &compositor_river_ops;
            adapter->type = COMPOSITOR_RIVER;
            adapter->caps = C_CAP_WS_TAG_BASED;
            break;
#endif

#ifdef ENABLE_GENERIC_WAYLAND
        case COMPOSITOR_GENERIC_WAYLAND:
            adapter->ops = &compositor_generic_wayland_ops;
            adapter->type = COMPOSITOR_GENERIC_WAYLAND;
            adapter->caps = C_CAP_WS_GLOBAL_NUMERIC;
            break;
#endif

        default:
            LOG_ERROR("Compositor type %d not available in this build", type);
            free(adapter);
            return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Normalize capability bits based on ops presence */
    if (adapter->ops && adapter->ops->get_cursor_position) {
        adapter->caps |= C_CAP_GLOBAL_CURSOR;
    }

    adapter->initialized = false;
    adapter->connected = false;
    *out_adapter = adapter;

    DEBUG_LOG("Created compositor adapter for %s",
              adapter->ops->get_name ? adapter->ops->get_name() : "unknown");

    return HYPRLAX_SUCCESS;
}

/* Destroy compositor adapter instance */
void compositor_destroy(compositor_adapter_t *adapter) {
    if (!adapter) return;

    if (adapter->connected && adapter->ops && adapter->ops->disconnect_ipc) {
        adapter->ops->disconnect_ipc();
    }

    if (adapter->initialized && adapter->ops && adapter->ops->destroy) {
        adapter->ops->destroy();
    }

    free(adapter);
}

/* Name-based creation: map names to types within compositor module */
int compositor_create_by_name(compositor_adapter_t **out_adapter, const char *name) {
    if (!name || strcmp(name, "auto") == 0) {
        return compositor_create(out_adapter, COMPOSITOR_AUTO);
    }
#ifdef ENABLE_HYPRLAND
    if (strcasecmp(name, "hyprland") == 0) return compositor_create(out_adapter, COMPOSITOR_HYPRLAND);
#endif
#ifdef ENABLE_SWAY
    if (strcasecmp(name, "sway") == 0) return compositor_create(out_adapter, COMPOSITOR_SWAY);
#endif
#ifdef ENABLE_WAYFIRE
    if (strcasecmp(name, "wayfire") == 0) return compositor_create(out_adapter, COMPOSITOR_WAYFIRE);
#endif
#ifdef ENABLE_NIRI
    if (strcasecmp(name, "niri") == 0) return compositor_create(out_adapter, COMPOSITOR_NIRI);
#endif
#ifdef ENABLE_RIVER
    if (strcasecmp(name, "river") == 0) return compositor_create(out_adapter, COMPOSITOR_RIVER);
#endif
#ifdef ENABLE_GENERIC_WAYLAND
    if (strcasecmp(name, "generic") == 0 || strcasecmp(name, "generic-wayland") == 0 || strcasecmp(name, "wayland") == 0)
        return compositor_create(out_adapter, COMPOSITOR_GENERIC_WAYLAND);
#endif
    LOG_ERROR("Unknown compositor backend: %s", name);
    return HYPRLAX_ERROR_INVALID_ARGS;
}
