/*
 * hyprlax.h - Main application interface
 *
 * Ties together all modules and provides the main application context.
 */

#ifndef HYPRLAX_H
#define HYPRLAX_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "hyprlax_internal.h"
#include "core.h"
#include "core/input/input_manager.h"
#include "renderer.h"
#include "platform.h"
#include "compositor.h"
#include "resource_monitor.h"
#include "../core/monitor.h"

/* Application state */
typedef enum {
    APP_STATE_INITIALIZING,
    APP_STATE_RUNNING,
    APP_STATE_PAUSED,
    APP_STATE_SHUTTING_DOWN,
} app_state_t;

/* Backend selection */
typedef struct {
    const char *renderer_backend;    /* "gles2", "gl3", "vulkan", "auto" */
    const char *platform_backend;    /* "wayland", "auto" */
    const char *compositor_backend;  /* "hyprland", "sway", "generic", "auto" */
} backend_config_t;

/* Main application context */
typedef struct hyprlax_context {
    /* Configuration */
    config_t config;
    backend_config_t backends;

    /* Module instances */
    renderer_t *renderer;
    platform_t *platform;
    compositor_adapter_t *compositor;
    input_manager_t input;

    /* Application state */
    app_state_t state;
    bool running;

    /* Layers */
    parallax_layer_t *layers;
    int layer_count;
    pthread_mutex_t layer_mutex;  /* Protects layer list operations */

    /* Timing */
    double last_frame_time;
    double delta_time;
    double fps;

    /* Multi-monitor support */
    monitor_list_t *monitors;           /* All active monitors */
    multi_monitor_mode_t monitor_mode;  /* ALL, PRIMARY, SPECIFIC */
    char **specific_monitors;           /* For SPECIFIC mode */
    int specific_monitor_count;

    /* Legacy single-monitor fields (kept for compatibility) */
    int current_workspace;
    int current_monitor;
    float workspace_offset_x;
    float workspace_offset_y;

    /* Cursor input state (for cursor/hybrid parallax) */
    int cursor_event_fd;      /* provider event fd, -1 if none */
    float cursor_norm_x;      /* normalized -1..1 after smoothing */
    float cursor_norm_y;      /* normalized -1..1 after smoothing */
    float cursor_eased_x;     /* normalized after easing animation */
    float cursor_eased_y;
    bool cursor_ease_initialized; /* eased values initialized */
    float cursor_ema_x;       /* smoothing accumulator */
    float cursor_ema_y;       /* smoothing accumulator */
    double cursor_last_time;  /* last update time */
    bool cursor_supported;    /* provider available */

    /* Cursor easing animation state */
    animation_state_t cursor_anim_x;
    animation_state_t cursor_anim_y;

    /* IPC context (legacy, will be removed) */
    void *ipc_ctx;

    /* Resource monitoring */
    resource_monitor_t *resource_monitor;

    /* Event-driven loop (Linux) */
    int epoll_fd;              /* epoll instance for unified waits */
    int frame_timer_fd;        /* timerfd for frame pacing */
    int debounce_timer_fd;     /* timerfd for coalescing compositor bursts */
    int platform_event_fd;     /* cached platform event fd */
    int compositor_event_fd;   /* cached compositor event fd */
    int ipc_event_fd;          /* IPC server socket fd */
    bool frame_timer_armed;    /* frame timer currently armed */
    bool debounce_pending;     /* debounce timer armed */
    compositor_event_t pending_event; /* last compositor event to apply after debounce */

    /* Internal: request an immediate retry render (e.g., pending texture load) */
    bool deferred_render_needed;

    /* Screen lock state tracking (Phase 2: hyprlock integration) */
    bool screen_locked;           /* Is screen currently locked? */
    double lock_time;             /* When was screen locked (for metrics) */
    uint32_t lock_cycle_count;    /* How many lock/unlock cycles */

} hyprlax_context_t;

/* Main application functions */
hyprlax_context_t* hyprlax_create(void);
void hyprlax_destroy(hyprlax_context_t *ctx);

int hyprlax_init(hyprlax_context_t *ctx, int argc, char **argv);
int hyprlax_run(hyprlax_context_t *ctx);
void hyprlax_shutdown(hyprlax_context_t *ctx);

/* Module initialization */
int hyprlax_init_platform(hyprlax_context_t *ctx);
int hyprlax_init_compositor(hyprlax_context_t *ctx);
int hyprlax_init_renderer(hyprlax_context_t *ctx);

/* Layer management */
int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path,
                     float shift_multiplier, float opacity, float blur);
void hyprlax_remove_layer(hyprlax_context_t *ctx, uint32_t layer_id);
void hyprlax_update_layers(hyprlax_context_t *ctx, double current_time);

/* Event handling */
void hyprlax_handle_workspace_change(hyprlax_context_t *ctx, int new_workspace);
void hyprlax_handle_monitor_workspace_change(hyprlax_context_t *ctx,
                                            const char *monitor_name,
                                            int new_workspace);
void hyprlax_handle_resize(hyprlax_context_t *ctx, int width, int height);

/* Rendering */
void hyprlax_render_frame(hyprlax_context_t *ctx);
int hyprlax_load_layer_textures(hyprlax_context_t *ctx);
/* Texture loading helper */
unsigned int load_texture(const char *path, int *width, int *height);

/* Control interface */
int hyprlax_ctl_main(int argc, char **argv);

/* Runtime property control (IPC bridge) */
int hyprlax_runtime_set_property(hyprlax_context_t *ctx, const char *property, const char *value);
int hyprlax_runtime_get_property(hyprlax_context_t *ctx, const char *property, char *out, size_t out_size);

/* Reload configuration (TOML only). Clears and re-applies layers. */
int hyprlax_reload_config(hyprlax_context_t *ctx);

/* Cursor input processing */
bool hyprlax_cursor_tick(hyprlax_context_t *ctx);

/* Event loop helpers */
int create_timerfd_monotonic(void);
void disarm_timerfd(int fd);
void arm_timerfd_ms(int fd, int initial_ms, int interval_ms);
int epoll_add_fd(int epfd, int fd, uint32_t events);
int epoll_del_fd(int epfd, int fd);
void hyprlax_setup_epoll(hyprlax_context_t *ctx);
void hyprlax_arm_frame_timer(hyprlax_context_t *ctx, int fps);
void hyprlax_disarm_frame_timer(hyprlax_context_t *ctx);
void hyprlax_arm_debounce(hyprlax_context_t *ctx, int debounce_ms);
void hyprlax_clear_timerfd(int fd);

#endif /* HYPRLAX_H */
