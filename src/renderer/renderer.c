/*
 * renderer.c - Renderer management
 *
 * Handles creation and management of renderer backends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/renderer.h"
#include "../include/hyprlax_internal.h"

/* Create renderer instance */
int renderer_create(renderer_t **out_renderer, const char *backend_name) {
    if (!out_renderer) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    renderer_t *renderer = calloc(1, sizeof(renderer_t));
    if (!renderer) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Select backend based on name */
#ifdef ENABLE_GLES2
    if (!backend_name || strcmp(backend_name, "gles2") == 0) {
        /* Default to OpenGL ES 2.0 */
        renderer->ops = &renderer_gles2_ops;
    } else
#endif
    {
        /* Unknown backend or not compiled in */
        fprintf(stderr, "Error: Renderer backend '%s' not available in this build\n",
                backend_name ? backend_name : "default");
        free(renderer);
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    renderer->initialized = false;
    *out_renderer = renderer;

    return HYPRLAX_SUCCESS;
}

/* Destroy renderer instance */
void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;

    if (renderer->initialized && renderer->ops && renderer->ops->destroy) {
        renderer->ops->destroy();
    }

    free(renderer);
}