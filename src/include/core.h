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

/* Layer definition - temporarily named differently to avoid conflict with ipc.h */
typedef struct parallax_layer {
    uint32_t id;
    char *image_path;

    /* Parallax parameters */
    float shift_multiplier;
    float opacity;
    float blur_amount;
    int z_index;

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

    /* Linked list */
    struct parallax_layer *next;
} parallax_layer_t;

/* Configuration structure */
typedef struct {
    /* Display settings */
    int target_fps;
    int max_fps;
    float scale_factor;

    /* Animation settings */
    float shift_pixels;
    double animation_duration;
    easing_type_t default_easing;

    /* Debug settings */
    bool debug;
    bool dry_run;
    char *debug_log_path;

    /* Paths */
    char *config_path;
    char *socket_path;

    /* Feature flags */
    bool blur_enabled;
    bool ipc_enabled;
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

/* Configuration parsing */
int config_parse_args(config_t *cfg, int argc, char **argv);
int config_load_file(config_t *cfg, const char *path);
void config_set_defaults(config_t *cfg);
void config_cleanup(config_t *cfg);

#endif /* HYPRLAX_CORE_H */