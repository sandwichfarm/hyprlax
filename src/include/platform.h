/*
 * platform.h - Platform abstraction interface
 * 
 * Provides an abstraction layer for windowing system operations,
 * allowing support for different platforms
 */

#ifndef HYPRLAX_PLATFORM_H
#define HYPRLAX_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "hyprlax_internal.h"

/* Platform types */
typedef enum {
    PLATFORM_WAYLAND,
    PLATFORM_AUTO,  /* Auto-detect */
} platform_type_t;

/* Platform events */
typedef enum {
    PLATFORM_EVENT_NONE,
    PLATFORM_EVENT_RESIZE,
    PLATFORM_EVENT_CLOSE,
    PLATFORM_EVENT_FOCUS_IN,
    PLATFORM_EVENT_FOCUS_OUT,
    PLATFORM_EVENT_CONFIGURE,
} platform_event_type_t;

/* Platform event data */
typedef struct {
    platform_event_type_t type;
    union {
        struct {
            int width;
            int height;
        } resize;
        struct {
            int x;
            int y;
        } position;
    } data;
} platform_event_t;

/* Window configuration */
typedef struct {
    int width;
    int height;
    int x;
    int y;
    bool fullscreen;
    bool borderless;
    const char *title;
    const char *app_id;
} window_config_t;

/* Platform operations interface */
typedef struct platform_ops {
    /* Lifecycle */
    int (*init)(void);
    void (*destroy)(void);
    
    /* Connection management */
    int (*connect)(const char *display_name);
    void (*disconnect)(void);
    bool (*is_connected)(void);
    
    /* Window management */
    int (*create_window)(const window_config_t *config);
    void (*destroy_window)(void);
    void (*show_window)(void);
    void (*hide_window)(void);
    
    /* Event handling */
    int (*poll_events)(platform_event_t *event);
    int (*wait_events)(platform_event_t *event, int timeout_ms);
    void (*flush_events)(void);
    
    /* Native handles for renderer */
    void* (*get_native_display)(void);
    void* (*get_native_window)(void);
    
    /* Platform-specific features */
    bool (*supports_transparency)(void);
    bool (*supports_blur)(void);
    
    /* Debug */
    const char* (*get_name)(void);
    const char* (*get_backend_name)(void);
} platform_ops_t;

/* Platform instance */
typedef struct platform {
    const platform_ops_t *ops;
    platform_type_t type;
    void *private_data;
    bool initialized;
    bool connected;
} platform_t;

/* Global platform management */
int platform_create(platform_t **platform, platform_type_t type);
void platform_destroy(platform_t *platform);

/* Auto-detect best platform */
platform_type_t platform_detect(void);

/* Convenience macros */
#define PLATFORM_INIT(p) \
    ((p)->ops->init())
#define PLATFORM_CONNECT(p, display) \
    ((p)->ops->connect(display))
#define PLATFORM_CREATE_WINDOW(p, config) \
    ((p)->ops->create_window(config))
#define PLATFORM_POLL_EVENTS(p, event) \
    ((p)->ops->poll_events(event))
#define PLATFORM_GET_NATIVE_DISPLAY(p) \
    ((p)->ops->get_native_display())
#define PLATFORM_GET_NATIVE_WINDOW(p) \
    ((p)->ops->get_native_window())

/* Available platform backends */
extern const platform_ops_t platform_wayland_ops;

#endif /* HYPRLAX_PLATFORM_H */