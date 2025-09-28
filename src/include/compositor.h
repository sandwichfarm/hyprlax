/*
 * compositor.h - Compositor adapter interface
 *
 * Provides an abstraction layer for compositor-specific features,
 * allowing support for different Wayland compositors (Hyprland, Sway, etc.)
 */

#ifndef HYPRLAX_COMPOSITOR_H
#define HYPRLAX_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "hyprlax_internal.h"

/* Compositor types */
typedef enum {
    COMPOSITOR_HYPRLAND,
    COMPOSITOR_WAYFIRE,          /* Wayfire (2D workspace grid) */
    COMPOSITOR_NIRI,             /* Niri (scrollable workspaces) */
    COMPOSITOR_SWAY,
    COMPOSITOR_RIVER,            /* River (tag-based workspaces) */
    COMPOSITOR_GENERIC_WAYLAND,  /* Generic wlr-layer-shell */
    COMPOSITOR_AUTO,             /* Auto-detect */
} compositor_type_t;

/* Compositor capability flags */
typedef enum {
    C_CAP_GLOBAL_CURSOR            = 1 << 0,  /* Provides global cursor position */
    C_CAP_WS_GLOBAL_NUMERIC        = 1 << 1,  /* Global numeric workspaces */
    C_CAP_WS_PER_OUTPUT_NUMERIC    = 1 << 2,  /* Per-output numeric workspaces */
    C_CAP_WS_TAG_BASED             = 1 << 3,  /* Tag-based workspace model */
    C_CAP_WS_SET_BASED             = 1 << 4,  /* Wayfire workspace sets */
} compositor_caps_t;

/* Layer positions for layer-shell */
typedef enum {
    LAYER_BACKGROUND,
    LAYER_BOTTOM,
    LAYER_TOP,
    LAYER_OVERLAY,
} layer_position_t;

/* Anchor flags for positioning */
typedef enum {
    ANCHOR_TOP = 1 << 0,
    ANCHOR_BOTTOM = 1 << 1,
    ANCHOR_LEFT = 1 << 2,
    ANCHOR_RIGHT = 1 << 3,
} anchor_flags_t;

/* Workspace information */
typedef struct {
    int id;
    char name[64];
    bool active;
    bool visible;
    bool occupied;  /* Whether workspace has windows */
    int x, y;  /* Grid position for 2D workspace layouts */
} workspace_info_t;

typedef struct {
    double x;
    double y;
    double width;
    double height;
    int workspace_id;
    int monitor_id;
    char monitor_name[64];
    bool floating;
} window_geometry_t;

/* Monitor information */
typedef struct {
    int id;
    char name[64];
    int x, y;
    int width, height;
    float scale;
    bool primary;
} monitor_info_t;

/* Compositor event */
typedef enum {
    COMPOSITOR_EVENT_WORKSPACE_CHANGE,
    COMPOSITOR_EVENT_MONITOR_CHANGE,
    COMPOSITOR_EVENT_FOCUS_CHANGE,
    COMPOSITOR_EVENT_BLUR_CHANGE,
} compositor_event_type_t;

typedef struct {
    compositor_event_type_t type;
    union {
        struct {
            int from_workspace;
            int to_workspace;
            /* 2D workspace grid support */
            int from_x, from_y;
            int to_x, to_y;
            /* Monitor association */
            char monitor_name[64];  /* Which monitor this workspace change affects */
        } workspace;
        struct {
            int monitor_id;
        } monitor;
        struct {
            bool focused;
        } focus;
    } data;
} compositor_event_t;

/* Layer surface configuration */
typedef struct {
    layer_position_t layer;
    anchor_flags_t anchor;
    int exclusive_zone;  /* -1 for exclusive, 0 for non-exclusive */
    int margin_top, margin_bottom, margin_left, margin_right;
    bool keyboard_interactive;
    bool accept_input;
} layer_surface_config_t;

/* Compositor operations interface */
typedef struct compositor_ops {
    /* Lifecycle */
    int (*init)(void *platform_data);
    void (*destroy)(void);

    /* Detection */
    bool (*detect)(void);
    const char* (*get_name)(void);

    /* Layer surface management */
    int (*create_layer_surface)(void *surface, const layer_surface_config_t *config);
    void (*configure_layer_surface)(void *layer_surface, int width, int height);
    void (*destroy_layer_surface)(void *layer_surface);

    /* Workspace management */
    int (*get_current_workspace)(void);
    int (*get_workspace_count)(void);
    int (*list_workspaces)(workspace_info_t **workspaces, int *count);

    /* Monitor management */
    int (*get_current_monitor)(void);
    int (*list_monitors)(monitor_info_t **monitors, int *count);

    /* IPC/Events */
    int (*connect_ipc)(const char *socket_path);
    void (*disconnect_ipc)(void);
    int (*poll_events)(compositor_event_t *event);
    int (*send_command)(const char *command, char *response, size_t response_size);

    /* Optional: expose event socket FD for blocking waits */
    int (*get_event_fd)(void);

    /* Compositor-specific features */
    bool (*supports_blur)(void);
    bool (*supports_transparency)(void);
    bool (*supports_animations)(void);
    int (*set_blur)(float amount);
    int (*set_wallpaper_offset)(float x, float y);
    /* Optional global cursor provider */
    int (*get_cursor_position)(double *x, double *y);
    int (*get_active_window_geometry)(window_geometry_t *out);
} compositor_ops_t;

/* Compositor adapter instance */
typedef struct compositor_adapter {
    const compositor_ops_t *ops;
    compositor_type_t type;
    uint64_t caps; /* compositor_caps_t bits */
    void *private_data;
    bool initialized;
    bool connected;
} compositor_adapter_t;

/* Global compositor management */
int compositor_create(compositor_adapter_t **adapter, compositor_type_t type);
void compositor_destroy(compositor_adapter_t *adapter);
/* Name-based creation (keeps name mapping inside compositor module) */
int compositor_create_by_name(compositor_adapter_t **adapter, const char *name);

/* Utility function for socket connection with retry (for startup readiness) */
int compositor_connect_socket_with_retry(const char *socket_path,
                                         const char *compositor_name,
                                         int max_retries,
                                         int retry_delay_ms);

/* Auto-detect compositor */
compositor_type_t compositor_detect(void);

/* Convenience macros */
#define COMPOSITOR_INIT(c, platform) \
    ((c)->ops->init(platform))
#define COMPOSITOR_GET_WORKSPACE(c) \
    ((c)->ops->get_current_workspace())
#define COMPOSITOR_SUPPORTS_BLUR(c) \
    ((c)->ops->supports_blur ? (c)->ops->supports_blur() : false)

/* Available compositor adapters */
extern const compositor_ops_t compositor_hyprland_ops;
extern const compositor_ops_t compositor_wayfire_ops;
extern const compositor_ops_t compositor_niri_ops;
extern const compositor_ops_t compositor_sway_ops;
extern const compositor_ops_t compositor_river_ops;
extern const compositor_ops_t compositor_generic_wayland_ops;

#endif /* HYPRLAX_COMPOSITOR_H */
