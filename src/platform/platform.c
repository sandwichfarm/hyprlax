/*
 * platform.c - Platform management
 *
 * Handles creation and management of platform backends.
 */

#include <stdio.h>
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

static bool is_x11_session(void) {
    const char *display = getenv("DISPLAY");
    const char *xdg_session = getenv("XDG_SESSION_TYPE");

    if (display && *display) {
        return true;
    }

    if (xdg_session && strcmp(xdg_session, "x11") == 0) {
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

#ifdef ENABLE_X11
    if (is_x11_session()) {
        return PLATFORM_X11;
    }
#endif

    /* Default to first available platform */
#ifdef ENABLE_WAYLAND
    fprintf(stderr, "Warning: Could not detect platform, defaulting to Wayland\n");
    return PLATFORM_WAYLAND;
#elif defined(ENABLE_X11)
    fprintf(stderr, "Warning: Could not detect platform, defaulting to X11\n");
    return PLATFORM_X11;
#else
    fprintf(stderr, "Error: No platform backends enabled at compile time\n");
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
            break;
#endif

#ifdef ENABLE_X11
        case PLATFORM_X11:
            platform->ops = &platform_x11_ops;
            platform->type = PLATFORM_X11;
            break;
#endif

        default:
            fprintf(stderr, "Error: Platform type %d not available in this build\n", type);
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