/*
 * x11_ewmh.c - X11 EWMH compositor adapter
 * 
 * Implements compositor interface for X11 window managers with EWMH support.
 * Supports i3, bspwm, awesome, xmonad, dwm, and other EWMH-compliant WMs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* EWMH atom names */
#define EWMH_SUPPORTED "_NET_SUPPORTED"
#define EWMH_SUPPORTING_WM_CHECK "_NET_SUPPORTING_WM_CHECK"
#define EWMH_WM_NAME "_NET_WM_NAME"
#define EWMH_CURRENT_DESKTOP "_NET_CURRENT_DESKTOP"
#define EWMH_NUMBER_OF_DESKTOPS "_NET_NUMBER_OF_DESKTOPS"
#define EWMH_DESKTOP_NAMES "_NET_DESKTOP_NAMES"
#define EWMH_DESKTOP_GEOMETRY "_NET_DESKTOP_GEOMETRY"
#define EWMH_DESKTOP_VIEWPORT "_NET_DESKTOP_VIEWPORT"
#define EWMH_WORKAREA "_NET_WORKAREA"
#define EWMH_WM_STATE "_NET_WM_STATE"
#define EWMH_WM_STATE_BELOW "_NET_WM_STATE_BELOW"
#define EWMH_WM_STATE_STICKY "_NET_WM_STATE_STICKY"
#define EWMH_WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"
#define EWMH_WM_WINDOW_TYPE "_NET_WM_WINDOW_TYPE"
#define EWMH_WM_WINDOW_TYPE_DESKTOP "_NET_WM_WINDOW_TYPE_DESKTOP"

/* X11 EWMH private data */
typedef struct {
    Display *display;
    int screen;
    Window root_window;
    Window desktop_window;
    
    /* EWMH atoms */
    Atom atom_net_supported;
    Atom atom_net_supporting_wm_check;
    Atom atom_net_wm_name;
    Atom atom_net_current_desktop;
    Atom atom_net_number_of_desktops;
    Atom atom_net_desktop_names;
    Atom atom_net_desktop_geometry;
    Atom atom_net_desktop_viewport;
    Atom atom_net_workarea;
    Atom atom_net_wm_state;
    Atom atom_net_wm_state_below;
    Atom atom_net_wm_state_sticky;
    Atom atom_net_wm_state_fullscreen;
    Atom atom_net_wm_window_type;
    Atom atom_net_wm_window_type_desktop;
    
    bool connected;
    int current_workspace;
    int workspace_count;
    char wm_name[256];
} x11_ewmh_data_t;

/* Global instance */
static x11_ewmh_data_t *g_x11_data = NULL;

/* Helper: Get X11 property */
static bool get_x11_property(Display *display, Window window, Atom property,
                             Atom type, unsigned char **data, unsigned long *nitems) {
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;
    
    if (XGetWindowProperty(display, window, property, 0, ~0L, False, type,
                          &actual_type, &actual_format, nitems, &bytes_after, data) != Success) {
        return false;
    }
    
    if (actual_type != type) {
        if (*data) XFree(*data);
        return false;
    }
    
    return true;
}

/* Helper: Get current desktop */
static int get_current_desktop(x11_ewmh_data_t *data) {
    unsigned char *prop_data = NULL;
    unsigned long nitems;
    
    if (get_x11_property(data->display, data->root_window, 
                        data->atom_net_current_desktop, XA_CARDINAL, 
                        &prop_data, &nitems)) {
        if (nitems > 0) {
            int desktop = *((long*)prop_data);
            XFree(prop_data);
            return desktop + 1;  /* Convert to 1-based */
        }
    }
    
    return 1;
}

/* Helper: Get number of desktops */
static int get_desktop_count(x11_ewmh_data_t *data) {
    unsigned char *prop_data = NULL;
    unsigned long nitems;
    
    if (get_x11_property(data->display, data->root_window,
                        data->atom_net_number_of_desktops, XA_CARDINAL,
                        &prop_data, &nitems)) {
        if (nitems > 0) {
            int count = *((long*)prop_data);
            XFree(prop_data);
            return count;
        }
    }
    
    return 4;  /* Default */
}

/* Helper: Detect WM name */
static void detect_wm_name(x11_ewmh_data_t *data) {
    unsigned char *prop_data = NULL;
    unsigned long nitems;
    Window wm_check_window = None;
    
    /* Get supporting WM window */
    if (get_x11_property(data->display, data->root_window,
                        data->atom_net_supporting_wm_check, XA_WINDOW,
                        &prop_data, &nitems)) {
        if (nitems > 0) {
            wm_check_window = *((Window*)prop_data);
            XFree(prop_data);
        }
    }
    
    /* Get WM name from the WM window */
    if (wm_check_window != None) {
        if (get_x11_property(data->display, wm_check_window,
                            data->atom_net_wm_name, XInternAtom(data->display, "UTF8_STRING", False),
                            &prop_data, &nitems)) {
            if (nitems > 0) {
                strncpy(data->wm_name, (char*)prop_data, sizeof(data->wm_name) - 1);
                data->wm_name[sizeof(data->wm_name) - 1] = '\0';
                XFree(prop_data);
            }
        }
    }
    
    /* Fallback detection */
    if (strlen(data->wm_name) == 0) {
        /* Check for i3 */
        if (getenv("I3SOCK")) {
            strcpy(data->wm_name, "i3");
        }
        /* Check for bspwm */
        else if (getenv("BSPWM_SOCKET")) {
            strcpy(data->wm_name, "bspwm");
        }
        /* Default */
        else {
            strcpy(data->wm_name, "Unknown X11 WM");
        }
    }
}

/* Initialize X11 EWMH adapter */
static int x11_ewmh_init(void *platform_data) {
    (void)platform_data;
    
    if (g_x11_data) {
        return HYPRLAX_SUCCESS;  /* Already initialized */
    }
    
    g_x11_data = calloc(1, sizeof(x11_ewmh_data_t));
    if (!g_x11_data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    /* Open X11 display */
    g_x11_data->display = XOpenDisplay(NULL);
    if (!g_x11_data->display) {
        free(g_x11_data);
        g_x11_data = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    g_x11_data->screen = DefaultScreen(g_x11_data->display);
    g_x11_data->root_window = RootWindow(g_x11_data->display, g_x11_data->screen);
    
    /* Initialize EWMH atoms */
    g_x11_data->atom_net_supported = XInternAtom(g_x11_data->display, EWMH_SUPPORTED, False);
    g_x11_data->atom_net_supporting_wm_check = XInternAtom(g_x11_data->display, EWMH_SUPPORTING_WM_CHECK, False);
    g_x11_data->atom_net_wm_name = XInternAtom(g_x11_data->display, EWMH_WM_NAME, False);
    g_x11_data->atom_net_current_desktop = XInternAtom(g_x11_data->display, EWMH_CURRENT_DESKTOP, False);
    g_x11_data->atom_net_number_of_desktops = XInternAtom(g_x11_data->display, EWMH_NUMBER_OF_DESKTOPS, False);
    g_x11_data->atom_net_desktop_names = XInternAtom(g_x11_data->display, EWMH_DESKTOP_NAMES, False);
    g_x11_data->atom_net_desktop_geometry = XInternAtom(g_x11_data->display, EWMH_DESKTOP_GEOMETRY, False);
    g_x11_data->atom_net_desktop_viewport = XInternAtom(g_x11_data->display, EWMH_DESKTOP_VIEWPORT, False);
    g_x11_data->atom_net_workarea = XInternAtom(g_x11_data->display, EWMH_WORKAREA, False);
    g_x11_data->atom_net_wm_state = XInternAtom(g_x11_data->display, EWMH_WM_STATE, False);
    g_x11_data->atom_net_wm_state_below = XInternAtom(g_x11_data->display, EWMH_WM_STATE_BELOW, False);
    g_x11_data->atom_net_wm_state_sticky = XInternAtom(g_x11_data->display, EWMH_WM_STATE_STICKY, False);
    g_x11_data->atom_net_wm_state_fullscreen = XInternAtom(g_x11_data->display, EWMH_WM_STATE_FULLSCREEN, False);
    g_x11_data->atom_net_wm_window_type = XInternAtom(g_x11_data->display, EWMH_WM_WINDOW_TYPE, False);
    g_x11_data->atom_net_wm_window_type_desktop = XInternAtom(g_x11_data->display, EWMH_WM_WINDOW_TYPE_DESKTOP, False);
    
    /* Detect WM name */
    detect_wm_name(g_x11_data);
    
    /* Get initial workspace info */
    g_x11_data->current_workspace = get_current_desktop(g_x11_data);
    g_x11_data->workspace_count = get_desktop_count(g_x11_data);
    
    /* Select events for workspace changes */
    XSelectInput(g_x11_data->display, g_x11_data->root_window, PropertyChangeMask);
    
    g_x11_data->connected = true;
    
    return HYPRLAX_SUCCESS;
}

/* Destroy X11 EWMH adapter */
static void x11_ewmh_destroy(void) {
    if (!g_x11_data) return;
    
    if (g_x11_data->desktop_window) {
        XDestroyWindow(g_x11_data->display, g_x11_data->desktop_window);
    }
    
    if (g_x11_data->display) {
        XCloseDisplay(g_x11_data->display);
    }
    
    free(g_x11_data);
    g_x11_data = NULL;
}

/* Detect if running under X11 */
static bool x11_ewmh_detect(void) {
    const char *display = getenv("DISPLAY");
    const char *session = getenv("XDG_SESSION_TYPE");
    
    if (session && strcmp(session, "x11") == 0) {
        return true;
    }
    
    return (display && *display);
}

/* Get compositor name */
static const char* x11_ewmh_get_name(void) {
    if (g_x11_data && strlen(g_x11_data->wm_name) > 0) {
        return g_x11_data->wm_name;
    }
    return "X11/EWMH";
}

/* Create layer surface (X11 desktop window) */
static int x11_ewmh_create_layer_surface(void *surface, 
                                        const layer_surface_config_t *config) {
    (void)config;
    
    if (!g_x11_data || !g_x11_data->connected) {
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* The platform layer (X11) will create the actual window */
    /* Here we just need to ensure it has desktop window properties */
    if (surface) {
        Window *window = (Window*)surface;
        
        /* Set window type to desktop */
        XChangeProperty(g_x11_data->display, *window,
                       g_x11_data->atom_net_wm_window_type, XA_ATOM, 32,
                       PropModeReplace, (unsigned char*)&g_x11_data->atom_net_wm_window_type_desktop, 1);
        
        /* Set window state to below and sticky */
        Atom states[2] = { g_x11_data->atom_net_wm_state_below, g_x11_data->atom_net_wm_state_sticky };
        XChangeProperty(g_x11_data->display, *window,
                       g_x11_data->atom_net_wm_state, XA_ATOM, 32,
                       PropModeReplace, (unsigned char*)states, 2);
        
        /* Store for later use */
        g_x11_data->desktop_window = *window;
    }
    
    return HYPRLAX_SUCCESS;
}

/* Configure layer surface */
static void x11_ewmh_configure_layer_surface(void *layer_surface, 
                                            int width, int height) {
    (void)layer_surface;
    (void)width;
    (void)height;
    /* Handled by platform layer */
}

/* Destroy layer surface */
static void x11_ewmh_destroy_layer_surface(void *layer_surface) {
    (void)layer_surface;
    if (g_x11_data) {
        g_x11_data->desktop_window = None;
    }
}

/* Get current workspace */
static int x11_ewmh_get_current_workspace(void) {
    if (!g_x11_data) return 1;
    return g_x11_data->current_workspace;
}

/* Get workspace count */
static int x11_ewmh_get_workspace_count(void) {
    if (!g_x11_data) return 4;
    
    g_x11_data->workspace_count = get_desktop_count(g_x11_data);
    return g_x11_data->workspace_count;
}

/* List workspaces */
static int x11_ewmh_list_workspaces(workspace_info_t **workspaces, int *count) {
    if (!workspaces || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (!g_x11_data || !g_x11_data->connected) {
        *count = 0;
        *workspaces = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    int desktop_count = get_desktop_count(g_x11_data);
    int current_desktop = get_current_desktop(g_x11_data);
    
    *workspaces = calloc(desktop_count, sizeof(workspace_info_t));
    if (!*workspaces) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    *count = desktop_count;
    
    /* Get desktop names if available */
    unsigned char *names_data = NULL;
    unsigned long names_nitems;
    char *names = NULL;
    
    if (get_x11_property(g_x11_data->display, g_x11_data->root_window,
                        g_x11_data->atom_net_desktop_names, 
                        XInternAtom(g_x11_data->display, "UTF8_STRING", False),
                        &names_data, &names_nitems)) {
        names = (char*)names_data;
    }
    
    for (int i = 0; i < desktop_count; i++) {
        (*workspaces)[i].id = i + 1;
        
        /* Use desktop names if available */
        if (names) {
            strncpy((*workspaces)[i].name, names, sizeof((*workspaces)[i].name) - 1);
            names += strlen(names) + 1;
        } else {
            snprintf((*workspaces)[i].name, sizeof((*workspaces)[i].name), "%d", i + 1);
        }
        
        (*workspaces)[i].active = (i + 1 == current_desktop);
        (*workspaces)[i].visible = (*workspaces)[i].active;
    }
    
    if (names_data) XFree(names_data);
    
    return HYPRLAX_SUCCESS;
}

/* Get current monitor */
static int x11_ewmh_get_current_monitor(void) {
    return 0;  /* X11 doesn't have a standard way to get current monitor */
}

/* List monitors */
static int x11_ewmh_list_monitors(monitor_info_t **monitors, int *count) {
    if (!monitors || !count) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (!g_x11_data || !g_x11_data->connected) {
        *count = 0;
        *monitors = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* For simplicity, return primary monitor info */
    *count = 1;
    *monitors = calloc(1, sizeof(monitor_info_t));
    if (!*monitors) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    (*monitors)[0].id = 0;
    strncpy((*monitors)[0].name, "Primary", sizeof((*monitors)[0].name));
    (*monitors)[0].x = 0;
    (*monitors)[0].y = 0;
    (*monitors)[0].width = DisplayWidth(g_x11_data->display, g_x11_data->screen);
    (*monitors)[0].height = DisplayHeight(g_x11_data->display, g_x11_data->screen);
    (*monitors)[0].scale = 1.0;
    (*monitors)[0].primary = true;
    
    return HYPRLAX_SUCCESS;
}

/* Connect IPC (X11 uses property events) */
static int x11_ewmh_connect_ipc(const char *socket_path) {
    (void)socket_path;
    
    if (!g_x11_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Already connected in init */
    return HYPRLAX_SUCCESS;
}

/* Disconnect IPC */
static void x11_ewmh_disconnect_ipc(void) {
    /* Nothing to do - handled in destroy */
}

/* Poll for events */
static int x11_ewmh_poll_events(compositor_event_t *event) {
    if (!event || !g_x11_data || !g_x11_data->connected) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    /* Check for pending X11 events */
    if (XPending(g_x11_data->display) > 0) {
        XEvent xevent;
        XNextEvent(g_x11_data->display, &xevent);
        
        if (xevent.type == PropertyNotify) {
            /* Check if current desktop changed */
            if (xevent.xproperty.atom == g_x11_data->atom_net_current_desktop) {
                int new_workspace = get_current_desktop(g_x11_data);
                
                if (new_workspace != g_x11_data->current_workspace) {
                    event->type = COMPOSITOR_EVENT_WORKSPACE_CHANGE;
                    event->data.workspace.from_workspace = g_x11_data->current_workspace;
                    event->data.workspace.to_workspace = new_workspace;
                    event->data.workspace.from_x = 0;
                    event->data.workspace.from_y = 0;
                    event->data.workspace.to_x = 0;
                    event->data.workspace.to_y = 0;
                    
                    g_x11_data->current_workspace = new_workspace;
                    
                    if (getenv("HYPRLAX_DEBUG")) {
                        fprintf(stderr, "[DEBUG] X11 EWMH workspace change: %d -> %d\n",
                                event->data.workspace.from_workspace,
                                event->data.workspace.to_workspace);
                    }
                    
                    return HYPRLAX_SUCCESS;
                }
            }
        }
    }
    
    return HYPRLAX_ERROR_NO_DATA;
}

/* Send command (not applicable for X11 EWMH) */
static int x11_ewmh_send_command(const char *command, char *response, 
                                size_t response_size) {
    (void)command;
    (void)response;
    (void)response_size;
    
    /* X11 EWMH doesn't have a command interface */
    return HYPRLAX_ERROR_INVALID_ARGS;
}

/* Check blur support */
static bool x11_ewmh_supports_blur(void) {
    /* Some compositors like picom support blur */
    return false;  /* Conservative default */
}

/* Check transparency support */
static bool x11_ewmh_supports_transparency(void) {
    /* Most X11 compositors support transparency */
    return true;
}

/* Check animation support */
static bool x11_ewmh_supports_animations(void) {
    return false;  /* X11 WMs typically don't have smooth animations */
}

/* Set blur */
static int x11_ewmh_set_blur(float amount) {
    (void)amount;
    /* Would need to communicate with compositor like picom */
    return HYPRLAX_ERROR_INVALID_ARGS;
}

/* Set wallpaper offset */
static int x11_ewmh_set_wallpaper_offset(float x, float y) {
    (void)x;
    (void)y;
    /* Not directly supported by EWMH */
    return HYPRLAX_SUCCESS;
}

/* X11 EWMH compositor operations */
const compositor_ops_t compositor_x11_ewmh_ops = {
    .init = x11_ewmh_init,
    .destroy = x11_ewmh_destroy,
    .detect = x11_ewmh_detect,
    .get_name = x11_ewmh_get_name,
    .create_layer_surface = x11_ewmh_create_layer_surface,
    .configure_layer_surface = x11_ewmh_configure_layer_surface,
    .destroy_layer_surface = x11_ewmh_destroy_layer_surface,
    .get_current_workspace = x11_ewmh_get_current_workspace,
    .get_workspace_count = x11_ewmh_get_workspace_count,
    .list_workspaces = x11_ewmh_list_workspaces,
    .get_current_monitor = x11_ewmh_get_current_monitor,
    .list_monitors = x11_ewmh_list_monitors,
    .connect_ipc = x11_ewmh_connect_ipc,
    .disconnect_ipc = x11_ewmh_disconnect_ipc,
    .poll_events = x11_ewmh_poll_events,
    .send_command = x11_ewmh_send_command,
    .supports_blur = x11_ewmh_supports_blur,
    .supports_transparency = x11_ewmh_supports_transparency,
    .supports_animations = x11_ewmh_supports_animations,
    .set_blur = x11_ewmh_set_blur,
    .set_wallpaper_offset = x11_ewmh_set_wallpaper_offset,
};