/*
 * x11.c - X11 platform implementation
 *
 * Implements the platform interface for X11/Xorg window managers.
 * Handles X11-specific window creation, EGL setup, and event management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xext.h>
#include <EGL/egl.h>
#include "../include/platform.h"
#include "../include/hyprlax_internal.h"

/* X11 platform private data */
typedef struct {
    /* X11 objects */
    Display *display;
    int screen;
    Window root_window;
    Window window;

    /* Window state */
    int width;
    int height;
    bool window_created;
    bool connected;
} x11_data_t;

/* Global instance */
static x11_data_t *g_x11_data = NULL;

static int x11_init(void) {
    if (g_x11_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }

    g_x11_data = calloc(1, sizeof(x11_data_t));
    if (!g_x11_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 platform initialized\n");
    }

    return HYPRLAX_SUCCESS;
}

static void x11_destroy(void) {
    if (!g_x11_data) return;

    if (g_x11_data->window && g_x11_data->display) {
        XDestroyWindow(g_x11_data->display, g_x11_data->window);
    }

    if (g_x11_data->display) {
        XCloseDisplay(g_x11_data->display);
    }

    free(g_x11_data);
    g_x11_data = NULL;

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 platform destroyed\n");
    }
}

static int x11_connect(const char *display_name) {
    if (!g_x11_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (g_x11_data->connected) {
        return HYPRLAX_SUCCESS;  /* Already connected */
    }

    /* Open X11 display */
    g_x11_data->display = XOpenDisplay(display_name);
    if (!g_x11_data->display) {
        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] Failed to open X11 display: %s\n",
                    display_name ? display_name : ":0");
        }
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    g_x11_data->screen = DefaultScreen(g_x11_data->display);
    g_x11_data->root_window = RootWindow(g_x11_data->display, g_x11_data->screen);
    g_x11_data->connected = true;

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 connected to display: %s\n",
                XDisplayName(display_name));
    }

    return HYPRLAX_SUCCESS;
}

static void x11_disconnect(void) {
    if (!g_x11_data) return;

    if (g_x11_data->window && g_x11_data->display) {
        XDestroyWindow(g_x11_data->display, g_x11_data->window);
        g_x11_data->window = None;
        g_x11_data->window_created = false;
    }

    if (g_x11_data->display) {
        XCloseDisplay(g_x11_data->display);
        g_x11_data->display = NULL;
    }

    g_x11_data->connected = false;

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 disconnected\n");
    }
}

static bool x11_is_connected(void) {
    return g_x11_data && g_x11_data->connected && g_x11_data->display;
}

static int x11_create_window(const window_config_t *config) {
    if (!g_x11_data || !g_x11_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    if (g_x11_data->window_created) {
        return HYPRLAX_SUCCESS;  /* Already created */
    }

    /* Set default window dimensions */
    int width = config ? config->width : DisplayWidth(g_x11_data->display, g_x11_data->screen);
    int height = config ? config->height : DisplayHeight(g_x11_data->display, g_x11_data->screen);
    int x = config ? config->x : 0;
    int y = config ? config->y : 0;

    g_x11_data->width = width;
    g_x11_data->height = height;

    /* Create window attributes */
    XSetWindowAttributes attrs;
    attrs.background_pixel = BlackPixel(g_x11_data->display, g_x11_data->screen);
    attrs.border_pixel = 0;
    attrs.colormap = DefaultColormap(g_x11_data->display, g_x11_data->screen);
    attrs.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask | PropertyChangeMask;
    attrs.override_redirect = True;  /* Bypass window manager for desktop window */

    unsigned long mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask | CWOverrideRedirect;

    /* Create the window */
    g_x11_data->window = XCreateWindow(
        g_x11_data->display,
        g_x11_data->root_window,
        x, y, width, height,
        0,  /* border_width */
        DefaultDepth(g_x11_data->display, g_x11_data->screen),
        InputOutput,
        DefaultVisual(g_x11_data->display, g_x11_data->screen),
        mask,
        &attrs
    );

    if (!g_x11_data->window) {
        if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] Failed to create X11 window\n");
        }
        return HYPRLAX_ERROR_NO_DISPLAY;
    }

    /* Set window title */
    const char *title = (config && config->title) ? config->title : "hyprlax";
    XStoreName(g_x11_data->display, g_x11_data->window, title);

    /* Set window properties for desktop window behavior */
    Atom wm_state = XInternAtom(g_x11_data->display, "_NET_WM_STATE", False);
    Atom wm_state_below = XInternAtom(g_x11_data->display, "_NET_WM_STATE_BELOW", False);
    Atom wm_state_sticky = XInternAtom(g_x11_data->display, "_NET_WM_STATE_STICKY", False);
    Atom wm_window_type = XInternAtom(g_x11_data->display, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_window_type_desktop = XInternAtom(g_x11_data->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    /* Set window type to desktop */
    XChangeProperty(g_x11_data->display, g_x11_data->window,
                   wm_window_type, XA_ATOM, 32, PropModeReplace,
                   (unsigned char*)&wm_window_type_desktop, 1);

    /* Set window state to below and sticky */
    Atom states[2] = { wm_state_below, wm_state_sticky };
    XChangeProperty(g_x11_data->display, g_x11_data->window,
                   wm_state, XA_ATOM, 32, PropModeReplace,
                   (unsigned char*)states, 2);

    g_x11_data->window_created = true;

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 window created: %dx%d at (%d,%d)\n",
                width, height, x, y);
    }

    return HYPRLAX_SUCCESS;
}

static void x11_destroy_window(void) {
    if (!g_x11_data || !g_x11_data->window) return;

    XDestroyWindow(g_x11_data->display, g_x11_data->window);
    g_x11_data->window = None;
    g_x11_data->window_created = false;

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 window destroyed\n");
    }
}

static void x11_show_window(void) {
    if (!g_x11_data || !g_x11_data->window || !g_x11_data->display) return;

    XMapWindow(g_x11_data->display, g_x11_data->window);
    XLowerWindow(g_x11_data->display, g_x11_data->window);  /* Send to back */
    XFlush(g_x11_data->display);

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 window shown\n");
    }
}

static void x11_hide_window(void) {
    if (!g_x11_data || !g_x11_data->window || !g_x11_data->display) return;

    XUnmapWindow(g_x11_data->display, g_x11_data->window);
    XFlush(g_x11_data->display);

    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] X11 window hidden\n");
    }
}

static int x11_poll_events(platform_event_t *event) {
    if (!event || !g_x11_data || !g_x11_data->display) {
        if (event) event->type = PLATFORM_EVENT_NONE;
        return HYPRLAX_SUCCESS;
    }

    event->type = PLATFORM_EVENT_NONE;

    /* Check for pending X11 events */
    if (XPending(g_x11_data->display) > 0) {
        XEvent xevent;
        XNextEvent(g_x11_data->display, &xevent);

        switch (xevent.type) {
            case ConfigureNotify:
                if (xevent.xconfigure.width != g_x11_data->width ||
                    xevent.xconfigure.height != g_x11_data->height) {

                    event->type = PLATFORM_EVENT_RESIZE;
                    event->data.resize.width = xevent.xconfigure.width;
                    event->data.resize.height = xevent.xconfigure.height;

                    g_x11_data->width = xevent.xconfigure.width;
                    g_x11_data->height = xevent.xconfigure.height;

                    if (getenv("HYPRLAX_DEBUG")) {
                        fprintf(stderr, "[DEBUG] X11 window resized: %dx%d\n",
                                g_x11_data->width, g_x11_data->height);
                    }
                }
                break;

            case Expose:
                if (getenv("HYPRLAX_DEBUG")) {
                    fprintf(stderr, "[DEBUG] X11 expose event\n");
                }
                break;

            case DestroyNotify:
                event->type = PLATFORM_EVENT_CLOSE;
                if (getenv("HYPRLAX_DEBUG")) {
                    fprintf(stderr, "[DEBUG] X11 window close event\n");
                }
                break;

            default:
                /* Other events not handled */
                break;
        }
    }

    return HYPRLAX_SUCCESS;
}

static int x11_wait_events(platform_event_t *event, int timeout_ms) {
    if (!event || !g_x11_data || !g_x11_data->display) {
        if (event) event->type = PLATFORM_EVENT_NONE;
        return HYPRLAX_SUCCESS;
    }

    /* For simplicity, just poll - X11 select-based waiting could be added */
    (void)timeout_ms;
    return x11_poll_events(event);
}

static void x11_flush_events(void) {
    if (!g_x11_data || !g_x11_data->display) return;

    XFlush(g_x11_data->display);
}

static void* x11_get_native_display(void) {
    return g_x11_data ? g_x11_data->display : NULL;
}

static void* x11_get_native_window(void) {
    if (!g_x11_data || !g_x11_data->window_created) {
        return NULL;
    }
    /* Return pointer to the Window ID for EGL */
    return &g_x11_data->window;
}

static bool x11_supports_transparency(void) {
    /* Most X11 compositors support transparency */
    return true;
}

static bool x11_supports_blur(void) {
    /* Some compositors like picom support blur - conservative default */
    return false;
}

static const char* x11_get_name(void) {
    return "X11";
}

static const char* x11_get_backend_name(void) {
    return "x11";
}

/* X11 platform operations */
const platform_ops_t platform_x11_ops = {
    .init = x11_init,
    .destroy = x11_destroy,
    .connect = x11_connect,
    .disconnect = x11_disconnect,
    .is_connected = x11_is_connected,
    .create_window = x11_create_window,
    .destroy_window = x11_destroy_window,
    .show_window = x11_show_window,
    .hide_window = x11_hide_window,
    .poll_events = x11_poll_events,
    .wait_events = x11_wait_events,
    .flush_events = x11_flush_events,
    .get_native_display = x11_get_native_display,
    .get_native_window = x11_get_native_window,
    .supports_transparency = x11_supports_transparency,
    .supports_blur = x11_supports_blur,
    .get_name = x11_get_name,
    .get_backend_name = x11_get_backend_name,
};