/*
 * core.h - Core module interface
 *
 * This module contains platform-agnostic parallax engine logic including
 * animation, easing functions, and layer management.
 */

#ifndef HYPRLAX_CORE_H
#define HYPRLAX_CORE_H

#include "hyprlax_internal.h"

/* Easing function types */
typedef enum {
    EASE_LINEAR,
    EASE_QUAD_OUT,
    EASE_CUBIC_OUT,
    EASE_QUART_OUT,
    EASE_QUINT_OUT,
    EASE_SINE_OUT,
    EASE_EXPO_OUT,
    EASE_CIRC_OUT,
    EASE_BACK_OUT,
    EASE_ELASTIC_OUT,
    EASE_BOUNCE_OUT,
    EASE_CUSTOM_SNAP,
    EASE_MAX
} easing_type_t;

/* Animation state - no allocations in evaluate path */
typedef struct animation_state {
    double start_time;
    double duration;
    float from_value;
    float to_value;
    easing_type_t easing;
    bool active;
    bool completed;
} animation_state_t;

/* Content scaling modes for layers */
typedef enum {
    LAYER_FIT_STRETCH = 0,   /* Default: stretch to fill viewport */
    LAYER_FIT_COVER,         /* Scale to cover viewport; crop excess */
    LAYER_FIT_CONTAIN,       /* Scale to contain; letterbox if needed */
    LAYER_FIT_WIDTH,         /* Fit width exactly; crop or letterbox vertically */
    LAYER_FIT_HEIGHT         /* Fit height exactly; crop or letterbox horizontally */
} layer_fit_mode_t;

/* Layer definition - temporarily named differently to avoid conflict with ipc.h */
typedef struct parallax_layer {
    uint32_t id;
    char *image_path;

    /* Parallax parameters */
    float shift_multiplier;           /* legacy scalar multiplier */
    float shift_multiplier_x;         /* per-axis multiplier (defaults to shift_multiplier) */
    float shift_multiplier_y;
    float opacity;
    float blur_amount;
    int z_index;

    /* Per-layer inversion flags */
    bool invert_workspace_x;
    bool invert_workspace_y;
    bool invert_cursor_x;
    bool invert_cursor_y;
    bool invert_window_x;
    bool invert_window_y;
    bool hidden;                /* dedicated visibility flag */

    /* Animation state */
    animation_state_t x_animation;
    animation_state_t y_animation;
    float current_x;
    float current_y;
    float offset_x;  /* Current parallax offset */
    float offset_y;

    /* OpenGL resources */
    uint32_t texture_id;
    int width;       /* Texture width */
    int height;      /* Texture height */
    int texture_width;
    int texture_height;

    layer_fit_mode_t fit_mode;
    float content_scale;          /* Additional scale multiplier (1.0 = no change) */
    bool  scale_is_custom;        /* true if set per-layer; false = inherits global */
    float align_x;                /* 0.0 left, 0.5 center, 1.0 right (for cover/crop alignment) */
    float align_y;                /* 0.0 top, 0.5 center, 1.0 bottom */
    float base_uv_x;              /* Initial UV pan offset (adds to parallax) */
    float base_uv_y;

    /* Rendering overflow/margins/tiling */
    int overflow_mode;            /* 0=repeat_edge, 1=repeat, 2=repeat_x, 3=repeat_y, 4=none; -1 means inherit */
    float margin_px_x;            /* safe margin in pixels (x) to avoid edges */
    float margin_px_y;            /* safe margin in pixels (y) to avoid edges */
    int tile_x;                   /* -1 inherit, 0 off, 1 on */
    int tile_y;                   /* -1 inherit, 0 off, 1 on */

    /* Per-layer tint */
    float tint_r;                 /* 0..1 */
    float tint_g;                 /* 0..1 */
    float tint_b;                 /* 0..1 */
    float tint_strength;          /* 0..1: 0=no tint, 1=full */

    /* Linked list */
    struct parallax_layer *next;
} parallax_layer_t;

/* Parallax mode */
typedef enum {
    PARALLAX_WORKSPACE = 0,
    PARALLAX_CURSOR = 1,
    PARALLAX_HYBRID = 2,
} parallax_mode_t;

/* Configuration structure */
typedef struct {
    /* Display settings */
    int target_fps;
    int max_fps;
    float scale_factor;
    bool vsync;  /* VSync enabled (default: false) */
    float idle_poll_rate;  /* Polling rate when idle in Hz (default: 2.0 = 500ms) */

    /* Animation settings */
    float shift_percent;        /* NEW: Shift as percentage of viewport width (0-100) */
    float shift_pixels;         /* DEPRECATED: Use shift_percent instead */
    double animation_duration;
    easing_type_t default_easing;

    /* Debug settings */
    bool debug;
    int log_level;             /* 0..4: ERROR..TRACE (see log_level_t) */
    bool dry_run;
    char *debug_log_path;

    /* Paths */
    char *config_path;
    char *socket_path;

    /* Feature flags */
    bool blur_enabled;
    bool ipc_enabled;

    /* Parallax configuration */
    parallax_mode_t parallax_mode;    /* workspace | cursor | hybrid */
    float parallax_workspace_weight;  /* 0..1 */
    float parallax_cursor_weight;     /* 0..1 */
    float parallax_window_weight;     /* 0..1 */
    bool invert_workspace_x;
    bool invert_workspace_y;
    bool invert_cursor_x;
    bool invert_cursor_y;
    bool invert_window_x;
    bool invert_window_y;
    float parallax_max_offset_x;      /* pixel clamp after blend */
    float parallax_max_offset_y;

    /* Render overflow defaults and margins */
    int render_overflow_mode;     /* default overflow mode for layers */
    float render_margin_px_x;     /* default margins in pixels */
    float render_margin_px_y;
    int render_tile_x;            /* default tiling flags */
    int render_tile_y;
    /* Trails/accumulation effect */
    bool render_accumulate;       /* if true, accumulate previous frames */
    float render_trail_strength;  /* 0..1 fade amount per frame when accumulating */

    /* Cursor input configuration */
    float cursor_sensitivity_x;       /* multiplier on normalized input */
    float cursor_sensitivity_y;
    float cursor_deadzone_px;         /* deadzone in screen pixels */
    float cursor_ema_alpha;           /* 0..1 smoothing factor */
    double cursor_anim_duration;      /* seconds; 0 disables easing */
    easing_type_t cursor_easing;      /* easing for cursor animation */
    bool cursor_follow_global;        /* true: animate even off background via compositor/global cursor */

    /* Window input configuration */
    float window_sensitivity_x;       /* multiplier on window-based offset */
    float window_sensitivity_y;
    float window_deadzone_px;         /* deadzone in screen pixels */
    float window_ema_alpha;           /* 0..1 smoothing factor */
} config_t;

/* Easing functions - pure math, no side effects */
float ease_linear(float t);
float ease_quad_out(float t);
float ease_cubic_out(float t);
float ease_quart_out(float t);
float ease_quint_out(float t);
float ease_sine_out(float t);
float ease_expo_out(float t);
float ease_circ_out(float t);
float ease_back_out(float t);
float ease_elastic_out(float t);
float ease_bounce_out(float t);
float ease_custom_snap(float t);

/* Apply easing function by type */
float apply_easing(float t, easing_type_t type);

/* Get easing type from string name */
easing_type_t easing_from_string(const char *name);

/* Get string name from easing type */
const char* easing_to_string(easing_type_t type);

/* Parallax helpers */
parallax_mode_t parallax_mode_from_string(const char *name);
const char* parallax_mode_to_string(parallax_mode_t mode);

/* Animation functions - no allocations in evaluate path */
void animation_start(animation_state_t *anim, float from, float to,
                    double duration, easing_type_t easing);
void animation_stop(animation_state_t *anim);
float animation_evaluate(animation_state_t *anim, double current_time);
bool animation_is_active(const animation_state_t *anim);
bool animation_is_complete(const animation_state_t *anim, double current_time);

/* Layer management */
parallax_layer_t* layer_create(const char *image_path, float shift_multiplier, float opacity);
void layer_destroy(parallax_layer_t *layer);
void layer_update_offset(parallax_layer_t *layer, float target_x, float target_y,
                        double duration, easing_type_t easing);
void layer_tick(parallax_layer_t *layer, double current_time);

/* Layer list management */
parallax_layer_t* layer_list_add(parallax_layer_t *head, parallax_layer_t *new_layer);
parallax_layer_t* layer_list_remove(parallax_layer_t *head, uint32_t layer_id);
parallax_layer_t* layer_list_find(parallax_layer_t *head, uint32_t layer_id);
void layer_list_destroy(parallax_layer_t *head);
int layer_list_count(parallax_layer_t *head);
parallax_layer_t* layer_list_sort_by_z(parallax_layer_t *head);

/* Configuration parsing */
int config_parse_args(config_t *cfg, int argc, char **argv);
int config_load_file(config_t *cfg, const char *path);
void config_set_defaults(config_t *cfg);
void config_cleanup(config_t *cfg);

#endif /* HYPRLAX_CORE_H */
