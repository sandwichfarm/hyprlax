#include "include/hyprlax.h"

/* Minimal stub for load_texture used by runtime property tests. */
unsigned int load_texture(const char *path, int *width, int *height) {
    (void)path;
    if (width) *width = 32;
    if (height) *height = 32;
    return 1; /* non-zero fake texture id */
}

void gles2_destroy_monitor_surface(void *surface) {
    (void)surface;
}

typedef struct gd_GIF gd_GIF;
void gd_close_gif(gd_GIF *gif) {
    (void)gif;
}
