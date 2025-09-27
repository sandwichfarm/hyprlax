/*
 * platform.c - Platform management
 *
 * Handles creation and management of platform backends.
 */

#include <stdio.h>
#include "include/log.h"
#include <stdlib.h>
#include <string.h>
#include "../include/platform.h"
#include "../include/hyprlax_internal.h"

/* Environment variable checks for platform detection */
static bool is_wayland_session(void) {
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *xdg_session = getenv("XDG_SESSION_TYPE");

    if (wayland_display && *wayland_display) {
        return true;
    }

    if (xdg_session && strcmp(xdg_session, "wayland") == 0) {
        return true;
    }

    return false;
}

/* Auto-detect best platform */
platform_type_t platform_detect(void) {
#ifdef ENABLE_WAYLAND
    /* Prefer Wayland if available */
    if (is_wayland_session()) {
        return PLATFORM_WAYLAND;
    }
#endif

    /* Default to first available platform */
#ifdef ENABLE_WAYLAND
    LOG_WARN("Could not detect platform, defaulting to Wayland");
    return PLATFORM_WAYLAND;
#else
    LOG_ERROR("No platform backends enabled at compile time");
    return PLATFORM_AUTO; /* Will fail in platform_create */
#endif
}

/* Create platform instance */
int platform_create(platform_t **out_platform, platform_type_t type) {
    if (!out_platform) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    platform_t *platform = calloc(1, sizeof(platform_t));
    if (!platform) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Auto-detect if requested */
    if (type == PLATFORM_AUTO) {
        type = platform_detect();
    }

    /* Select backend based on type */
    switch (type) {
#ifdef ENABLE_WAYLAND
        case PLATFORM_WAYLAND:
            platform->ops = &platform_wayland_ops;
            platform->type = PLATFORM_WAYLAND;
            platform->caps = P_CAP_LAYER_SHELL | P_CAP_MULTI_OUTPUT | P_CAP_EVENT_FD |
                             P_CAP_WINDOW_SIZE_QUERY | P_CAP_SURFACE_COMMIT |
                             P_CAP_GLOBAL_CURSOR | P_CAP_REALIZE_MONITORS |
                             P_CAP_SET_CONTEXT;
            break;
#endif

        default:
            LOG_ERROR("Platform type %d not available in this build", type);
            free(platform);
            return HYPRLAX_ERROR_INVALID_ARGS;
    }

    platform->initialized = false;
    platform->connected = false;
    *out_platform = platform;

    return HYPRLAX_SUCCESS;
}

/* Destroy platform instance */
void platform_destroy(platform_t *platform) {
    if (!platform) return;

    if (platform->connected && platform->ops && platform->ops->disconnect) {
        platform->ops->disconnect();
    }

    if (platform->initialized && platform->ops && platform->ops->destroy) {
        platform->ops->destroy();
    }

    free(platform);
}

/* Name-based creation (keeps name mapping out of core) */
int platform_create_by_name(platform_t **out_platform, const char *name) {
    if (!name || strcmp(name, "auto") == 0) {
        return platform_create(out_platform, PLATFORM_AUTO);
    }
    /* Map simple known names to types */
#ifdef ENABLE_WAYLAND
    if (strcasecmp(name, "wayland") == 0) {
        return platform_create(out_platform, PLATFORM_WAYLAND);
    }
#endif
    LOG_ERROR("Unknown platform backend: %s", name);
    return HYPRLAX_ERROR_INVALID_ARGS;
}
