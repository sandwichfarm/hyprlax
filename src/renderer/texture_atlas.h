/*
 * texture_atlas.h - Texture atlas interface
 *
 * Combines multiple textures into a single atlas to reduce texture binding overhead
 */

#ifndef HYPRLAX_TEXTURE_ATLAS_H
#define HYPRLAX_TEXTURE_ATLAS_H

#include <stdbool.h>
#include "../include/renderer.h"

/* Forward declaration */
typedef struct texture_atlas texture_atlas_t;

/* Create texture atlas from multiple textures */
texture_atlas_t* texture_atlas_create(texture_t **textures, int texture_count,
                                      const renderer_ops_t *ops, bool enabled);

/* Destroy texture atlas */
void texture_atlas_destroy(texture_atlas_t *atlas, const renderer_ops_t *ops);

/* Get the combined atlas texture */
texture_t* texture_atlas_get_texture(texture_atlas_t *atlas);

/* Get UV coordinates for a texture in the atlas */
bool texture_atlas_get_uv(texture_atlas_t *atlas, int texture_index,
                          float *u1, float *v1, float *u2, float *v2);

/* Check if atlas is enabled */
bool texture_atlas_is_enabled(texture_atlas_t *atlas);

/* Get atlas dimensions */
void texture_atlas_get_dimensions(texture_atlas_t *atlas, int *width, int *height);

#endif /* HYPRLAX_TEXTURE_ATLAS_H */