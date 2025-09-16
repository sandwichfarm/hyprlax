/*
 * hyprlax.h - Main application interface
 * 
 * Ties together all modules and provides the main application context.
 */

#ifndef HYPRLAX_H
#define HYPRLAX_H

#include <stdbool.h>
#include <stdint.h>
#include "hyprlax_internal.h"
#include "core.h"
#include "renderer.h"
#include "platform.h"
#include "compositor.h"

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
    const char *platform_backend;    /* "wayland", "x11", "auto" */
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
    
    /* Application state */
    app_state_t state;
    bool running;
    
    /* Layers */
    parallax_layer_t *layers;
    int layer_count;
    
    /* Timing */
    double last_frame_time;
    double delta_time;
    double fps;
    
    /* Current workspace/monitor */
    int current_workspace;
    int current_monitor;
    float workspace_offset_x;
    float workspace_offset_y;
    
    /* IPC context (legacy, will be removed) */
    void *ipc_ctx;
    
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
void hyprlax_handle_resize(hyprlax_context_t *ctx, int width, int height);

/* Rendering */
void hyprlax_render_frame(hyprlax_context_t *ctx);

/* Control interface */
int hyprlax_ctl_main(int argc, char **argv);

#endif /* HYPRLAX_H */