/*
 * monitor.h - Multi-monitor management
 *
 * Handles monitor detection, lifecycle, and per-monitor state.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "core.h"

/* Forward declarations */
struct wl_output;
struct wl_surface;
struct zwlr_layer_surface_v1;
struct wl_callback;
typedef struct EGLSurface_* EGLSurface;
typedef struct hyprlax_context hyprlax_context_t;

/* Include workspace models for flexible workspace tracking */
#include "../compositor/workspace_models.h"

/* Multi-monitor modes */
typedef enum {
    MULTI_MON_ALL,        /* Use all monitors (DEFAULT) */
    MULTI_MON_PRIMARY,    /* Only primary monitor */
    MULTI_MON_SPECIFIC,   /* User-specified monitors */
} multi_monitor_mode_t;

/* Monitor instance - represents a single physical monitor */
typedef struct monitor_instance {
    /* Monitor identification */
    char name[64];                     /* e.g., "DP-1", "HDMI-2" */
    uint32_t id;                       /* Unique ID for this session */
    bool is_primary;

    /* Physical properties */
    int width, height;                 /* Resolution in pixels */
    int scale;                        /* Output scale factor */
    int refresh_rate;                 /* Hz */
    int transform;                    /* Rotation/flip */

    /* Position in global coordinate space */
    int global_x, global_y;

    /* Wayland objects */
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    void *wl_egl_window;              /* EGL window for this surface */

    /* EGL surface (shares context with others) */
    EGLSurface egl_surface;

    /* Frame scheduling */
    struct wl_callback *frame_callback;
    bool frame_pending;
    double last_frame_time;
    double target_frame_time;         /* Based on refresh rate */

    /* Cached GL state per monitor */
    int viewport_width;
    int viewport_height;

    /* Workspace tracking (flexible model support) */
    workspace_context_t current_context;  /* Current workspace/tag/set state */
    workspace_context_t previous_context; /* Previous state for comparison */
    float parallax_offset_x;             /* Calculated parallax offset */
    float parallax_offset_y;

    /* Compositor capabilities for this monitor */
    compositor_capabilities_t capabilities;

    /* Animation state */
    bool animating;
    double animation_start_time;
    float animation_target_x;
    float animation_target_y;
    float animation_start_x;
    float animation_start_y;

    /* Configuration (resolved for this monitor) */
    config_t *config;

    /* Linked list */
    struct monitor_instance *next;
} monitor_instance_t;

/* Monitor list management */
typedef struct monitor_list {
    monitor_instance_t *head;
    monitor_instance_t *primary;
    int count;
    uint32_t next_id;
} monitor_list_t;

/* Monitor management functions */
monitor_list_t* monitor_list_create(void);
void monitor_list_destroy(monitor_list_t *list);

/* Monitor instance operations */
monitor_instance_t* monitor_instance_create(const char *name);
void monitor_instance_destroy(monitor_instance_t *monitor);

/* List operations */
void monitor_list_add(monitor_list_t *list, monitor_instance_t *monitor);
void monitor_list_remove(monitor_list_t *list, monitor_instance_t *monitor);
monitor_instance_t* monitor_list_find_by_name(monitor_list_t *list, const char *name);
monitor_instance_t* monitor_list_find_by_output(monitor_list_t *list, struct wl_output *output);
monitor_instance_t* monitor_list_find_by_id(monitor_list_t *list, uint32_t id);
monitor_instance_t* monitor_list_get_primary(monitor_list_t *list);

/* Monitor configuration */
config_t* monitor_resolve_config(monitor_instance_t *monitor, config_t *global_config);
void monitor_apply_config(monitor_instance_t *monitor, config_t *config);

/* Workspace handling */
void monitor_handle_workspace_change(hyprlax_context_t *ctx,
                                    monitor_instance_t *monitor,
                                    int new_workspace);
void monitor_handle_workspace_context_change(hyprlax_context_t *ctx,
                                            monitor_instance_t *monitor,
                                            const workspace_context_t *new_context);
void monitor_start_parallax_animation(hyprlax_context_t *ctx,
                                     monitor_instance_t *monitor,
                                     int workspace_delta);
void monitor_start_parallax_animation_offset(hyprlax_context_t *ctx,
                                            monitor_instance_t *monitor,
                                            float offset);
void monitor_update_animation(monitor_instance_t *monitor, double current_time);

/* Frame management */
bool monitor_should_render(monitor_instance_t *monitor, double current_time);
void monitor_mark_frame_pending(monitor_instance_t *monitor);
void monitor_frame_done(monitor_instance_t *monitor);

/* Utility functions */
void monitor_update_geometry(monitor_instance_t *monitor,
                            int width, int height,
                            int scale, int refresh_rate);
void monitor_set_global_position(monitor_instance_t *monitor, int x, int y);
const char* monitor_get_name(monitor_instance_t *monitor);
bool monitor_is_active(monitor_instance_t *monitor);

#endif /* MONITOR_H */
