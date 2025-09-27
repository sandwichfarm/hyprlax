/*
 * hyprlax_internal.h - Internal shared definitions
 *
 * This header contains definitions shared across modules but not exposed
 * to external users. All modules can include this header.
 */

#ifndef HYPRLAX_INTERNAL_H
#define HYPRLAX_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Version information */
/* Version is now defined at compile time via -DHYPRLAX_VERSION in Makefile */
#ifndef HYPRLAX_VERSION
#define HYPRLAX_VERSION "unknown"
#endif
#define HYPRLAX_ABI_VERSION 1

/* Common macros */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max) (MIN(MAX(x, min), max))

/* Debug logging - only enabled with DEBUG flag */
#ifdef DEBUG
    #define DEBUG_LOG(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define DEBUG_LOG(fmt, ...) ((void)0)
#endif

/* Error codes */
typedef enum {
    HYPRLAX_SUCCESS = 0,
    HYPRLAX_ERROR_INVALID_ARGS = -1,
    HYPRLAX_ERROR_NO_MEMORY = -2,
    HYPRLAX_ERROR_NO_DISPLAY = -3,
    HYPRLAX_ERROR_NO_COMPOSITOR = -4,
    HYPRLAX_ERROR_GL_INIT = -5,
    HYPRLAX_ERROR_FILE_NOT_FOUND = -6,
    HYPRLAX_ERROR_LOAD_FAILED = -7,
    HYPRLAX_ERROR_NO_DATA = -8,
    HYPRLAX_ERROR_ALREADY_RUNNING = -9,
} hyprlax_error_t;

/* Forward declarations for cross-module types */
// Note: layer_t is currently defined in ipc.h for IPC layer management
// The main hyprlax.c uses struct layer for actual rendering layers
// These will be unified in later phases of modularization
typedef struct animation_state animation_state_t;
typedef struct renderer renderer_t;
typedef struct platform_adapter platform_adapter_t;
typedef struct compositor_adapter compositor_adapter_t;

#endif /* HYPRLAX_INTERNAL_H */