/*
 * renderer.h - Renderer abstraction interface
 *
 * Provides an abstraction layer for rendering operations, allowing
 * different rendering backends (OpenGL ES, OpenGL, Vulkan, etc.)
 */

#ifndef HYPRLAX_RENDERER_H
#define HYPRLAX_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include "hyprlax_internal.h"

/* Renderer capability flags */
typedef enum {
    RENDERER_CAP_BLUR = 1 << 0,
    RENDERER_CAP_VSYNC = 1 << 1,
    RENDERER_CAP_MULTISAMPLING = 1 << 2,
} renderer_capability_t;

/* Texture format */
typedef enum {
    TEXTURE_FORMAT_RGBA,
    TEXTURE_FORMAT_RGB,
    TEXTURE_FORMAT_BGRA,
    TEXTURE_FORMAT_BGR,
} texture_format_t;

/* Renderer configuration */
typedef struct {
    int width;
    int height;
    bool vsync;
    int target_fps;
    uint32_t capabilities;
} renderer_config_t;

/* Texture handle */
typedef struct {
    uint32_t id;
    int width;
    int height;
    texture_format_t format;
} texture_t;

/* Extended draw parameters */
typedef struct renderer_layer_params {
    int fit_mode;       /* matches layer_fit_mode_t */
    float content_scale;
    float align_x;      /* 0..1 left->right */
    float align_y;      /* 0..1 top->bottom */
    float base_uv_x;    /* additional UV shift */
    float base_uv_y;
    int overflow_mode;  /* 0=repeat_edge,1=repeat,2=repeat_x,3=repeat_y,4=none */
    float margin_px_x;  /* extra margins (pixels) to avoid edges when translating */
    float margin_px_y;
    int tile_x;         /* 1 = repeat in X regardless of overflow */
    int tile_y;         /* 1 = repeat in Y regardless of overflow */
    float auto_safe_norm_x; /* additional normalized shrink based on max offset */
    float auto_safe_norm_y;
    /* Per-layer tint */
    float tint_r;
    float tint_g;
    float tint_b;
    float tint_strength;
} renderer_layer_params_t;

/* Renderer operations interface */
typedef struct renderer_ops {
    /* Lifecycle */
    int (*init)(void *native_display, void *native_window, const renderer_config_t *config);
    void (*destroy)(void);

    /* Frame management */
    void (*begin_frame)(void);
    void (*end_frame)(void);
    void (*present)(void);

    /* Texture management */
    texture_t* (*create_texture)(const void *data, int width, int height, texture_format_t format);
    void (*destroy_texture)(texture_t *texture);
    void (*bind_texture)(const texture_t *texture, int unit);

    /* Drawing operations */
    void (*clear)(float r, float g, float b, float a);
    /* Optional: draw fullscreen color overlay with blending (for trails) */
    void (*fade_frame)(float r, float g, float b, float a);
    void (*draw_layer)(const texture_t *texture, float x, float y,
                      float opacity, float blur_amount);

    void (*draw_layer_ex)(const texture_t *texture, float x, float y,
                         float opacity, float blur_amount,
                         const renderer_layer_params_t *params);

    /* Configuration */
    void (*resize)(int width, int height);
    void (*set_vsync)(bool enabled);
    uint32_t (*get_capabilities)(void);

    /* Debug */
    const char* (*get_name)(void);
    const char* (*get_version)(void);
} renderer_ops_t;

/* Renderer instance */
typedef struct renderer {
    const renderer_ops_t *ops;
    renderer_config_t config;
    void *private_data;
    bool initialized;
} renderer_t;

/* Global renderer management */
int renderer_create(renderer_t **renderer, const char *backend_name);
void renderer_destroy(renderer_t *renderer);

/* Convenience macros for calling renderer operations */
#define RENDERER_INIT(r, display, window, config) \
    ((r)->ops->init((display), (window), (config)))
#define RENDERER_BEGIN_FRAME(r) \
    ((r)->ops->begin_frame())
#define RENDERER_END_FRAME(r) \
    ((r)->ops->end_frame())
#define RENDERER_PRESENT(r) \
    ((r)->ops->present())
#define RENDERER_CLEAR(r, red, green, blue, alpha) \
    ((r)->ops->clear((red), (green), (blue), (alpha)))
#define RENDERER_FADE(r, red, green, blue, alpha) \
    do { if ((r)->ops->fade_frame) (r)->ops->fade_frame((red), (green), (blue), (alpha)); } while(0)

/* Available renderer backends */
extern const renderer_ops_t renderer_gles2_ops;
/* Future: renderer_gl3_ops, renderer_vulkan_ops */

/* Multi-monitor support functions for GLES2 backend */
#ifdef __EGL_H__
EGLSurface gles2_create_monitor_surface(void *native_window);
int gles2_make_current(EGLSurface surface);
#else
/* Forward declaration for when EGL types aren't available */
void* gles2_create_monitor_surface(void *native_window);
int gles2_make_current(void *surface);
#endif

#endif /* HYPRLAX_RENDERER_H */
