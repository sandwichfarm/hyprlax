#include <string.h>
#include "include/hyprlax.h"

/* Minimal stub for load_texture used by runtime property tests. */
unsigned int load_texture(const char *path, int *width, int *height) {
    (void)path;
    if (width) *width = 32;
    if (height) *height = 32;
    return 1; /* non-zero fake texture id */
}

/* Stubs for the render_core layer loader (render_core.c is not linked into
 * these tests). They record the last call so tests can check routing. */
const parallax_layer_t *stub_reload_last_layer = NULL;
char stub_reload_last_path[256] = {0};
int stub_reload_calls = 0;
int stub_reload_result = 0;

int hyprlax_load_layer_texture(hyprlax_context_t *ctx, parallax_layer_t *layer) {
    (void)ctx;
    if (!layer) return 0;
    layer->texture_id = 1;
    layer->width = layer->height = 32;
    layer->texture_width = layer->texture_height = 32;
    return 1;
}

int hyprlax_reload_layer_texture(hyprlax_context_t *ctx, parallax_layer_t *layer, const char *new_path) {
    (void)ctx;
    stub_reload_calls++;
    stub_reload_last_layer = layer;
    stub_reload_last_path[0] = '\0';
    if (new_path) {
        strncpy(stub_reload_last_path, new_path, sizeof(stub_reload_last_path) - 1);
    }
    return stub_reload_result;
}

void hyprlax_release_layer_textures(parallax_layer_t *layer) {
    if (!layer) return;
    layer->texture_id = 0;
    layer->is_gif = false;
    layer->frame_count = 0;
}

void gles2_destroy_monitor_surface(void *surface) {
    (void)surface;
}

typedef struct gd_GIF gd_GIF;
void gd_close_gif(gd_GIF *gif) {
    (void)gif;
}
