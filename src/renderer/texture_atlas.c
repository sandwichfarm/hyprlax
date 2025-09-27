/*
 * texture_atlas.c - Texture atlas implementation for optimization
 *
 * Combines multiple textures into a single atlas to reduce texture binding overhead
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/renderer.h"
#include "texture_atlas.h"

/* Atlas entry for tracking texture positions */
typedef struct atlas_entry {
    int texture_index;
    float u1, v1, u2, v2;  /* UV coordinates in atlas */
    int x, y, width, height;  /* Position in atlas */
} atlas_entry_t;

/* Texture atlas structure */
struct texture_atlas {
    texture_t *atlas_texture;
    atlas_entry_t *entries;
    int entry_count;
    int atlas_width;
    int atlas_height;
    bool enabled;
};

/* Calculate next power of two */
static int next_pow2(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

/* Create texture atlas from multiple textures */
texture_atlas_t* texture_atlas_create(texture_t **textures, int texture_count,
                                      const renderer_ops_t *ops, bool enabled) {
    if (!enabled || !textures || texture_count <= 0 || !ops) {
        return NULL;
    }

    texture_atlas_t *atlas = calloc(1, sizeof(texture_atlas_t));
    if (!atlas) return NULL;

    atlas->enabled = enabled;
    atlas->entry_count = texture_count;
    atlas->entries = calloc(texture_count, sizeof(atlas_entry_t));
    if (!atlas->entries) {
        free(atlas);
        return NULL;
    }

    /* Calculate atlas dimensions - simple grid layout */
    int grid_size = (int)ceil(sqrt(texture_count));
    int max_texture_width = 0;
    int max_texture_height = 0;

    for (int i = 0; i < texture_count; i++) {
        if (textures[i]->width > max_texture_width) {
            max_texture_width = textures[i]->width;
        }
        if (textures[i]->height > max_texture_height) {
            max_texture_height = textures[i]->height;
        }
    }

    /* Make atlas power of two for better GPU compatibility */
    atlas->atlas_width = next_pow2(grid_size * max_texture_width);
    atlas->atlas_height = next_pow2(grid_size * max_texture_height);

    /* Allocate atlas pixel data */
    size_t atlas_size = atlas->atlas_width * atlas->atlas_height * 4;  /* RGBA */
    unsigned char *atlas_data = calloc(atlas_size, 1);
    if (!atlas_data) {
        free(atlas->entries);
        free(atlas);
        return NULL;
    }

    /* Copy each texture into the atlas */
    int current_x = 0;
    int current_y = 0;

    for (int i = 0; i < texture_count; i++) {
        texture_t *tex = textures[i];

        /* Check if we need to move to next row */
        if (current_x + tex->width > atlas->atlas_width) {
            current_x = 0;
            current_y += max_texture_height;
        }

        /* Store entry information */
        atlas->entries[i].texture_index = i;
        atlas->entries[i].x = current_x;
        atlas->entries[i].y = current_y;
        atlas->entries[i].width = tex->width;
        atlas->entries[i].height = tex->height;

        /* Calculate UV coordinates */
        atlas->entries[i].u1 = (float)current_x / atlas->atlas_width;
        atlas->entries[i].v1 = (float)current_y / atlas->atlas_height;
        atlas->entries[i].u2 = (float)(current_x + tex->width) / atlas->atlas_width;
        atlas->entries[i].v2 = (float)(current_y + tex->height) / atlas->atlas_height;

        /* Note: We'd need access to the actual pixel data to copy it
         * For now, this is a placeholder structure */

        current_x += max_texture_width;
    }

    /* Create the atlas texture */
    atlas->atlas_texture = ops->create_texture(atlas_data, atlas->atlas_width,
                                               atlas->atlas_height, TEXTURE_FORMAT_RGBA);

    free(atlas_data);

    if (!atlas->atlas_texture) {
        free(atlas->entries);
        free(atlas);
        return NULL;
    }

    return atlas;
}

/* Destroy texture atlas */
void texture_atlas_destroy(texture_atlas_t *atlas, const renderer_ops_t *ops) {
    if (!atlas) return;

    if (atlas->atlas_texture && ops) {
        ops->destroy_texture(atlas->atlas_texture);
    }

    free(atlas->entries);
    free(atlas);
}

/* Get texture from atlas */
texture_t* texture_atlas_get_texture(texture_atlas_t *atlas) {
    if (!atlas || !atlas->enabled) return NULL;
    return atlas->atlas_texture;
}

/* Get UV coordinates for a texture in the atlas */
bool texture_atlas_get_uv(texture_atlas_t *atlas, int texture_index,
                          float *u1, float *v1, float *u2, float *v2) {
    if (!atlas || !atlas->enabled || texture_index < 0 ||
        texture_index >= atlas->entry_count) {
        return false;
    }

    atlas_entry_t *entry = &atlas->entries[texture_index];
    *u1 = entry->u1;
    *v1 = entry->v1;
    *u2 = entry->u2;
    *v2 = entry->v2;
    return true;
}

/* Check if atlas is enabled */
bool texture_atlas_is_enabled(texture_atlas_t *atlas) {
    return atlas && atlas->enabled;
}

/* Get atlas dimensions */
void texture_atlas_get_dimensions(texture_atlas_t *atlas, int *width, int *height) {
    if (!atlas) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    if (width) *width = atlas->atlas_width;
    if (height) *height = atlas->atlas_height;
}