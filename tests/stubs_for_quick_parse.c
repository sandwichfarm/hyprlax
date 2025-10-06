#include <stdint.h>
#include <stdbool.h>
#include "include/hyprlax.h"
#include "core/input/input_manager.h"

/* Minimal hyprlax_add_layer for tests: create a layer and push to list */
int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path, float shift_multiplier, float opacity, float blur) {
    if (!ctx || !image_path) return -1;
    parallax_layer_t *l = layer_create(image_path, shift_multiplier, opacity);
    if (!l) return -1;
    /* Apply global default content scale */
    l->content_scale = ctx->config.scale_factor;
    l->blur_amount = blur;
    ctx->layers = layer_list_add(ctx->layers, l);
    return 0;
}

void input_source_selection_init(input_source_selection_t *selection) {
    if (selection) { 
        selection->modified = false;
        for (int i=0;i<INPUT_MAX;i++){ 
            selection->seen[i] = false;
            selection->explicit_weight[i] = false;
            selection->weights[i] = -1.0f;
        }
    }
}

int input_source_selection_add_spec(input_source_selection_t *selection, const char *spec) {
    (void)selection; (void)spec; return 0;
}

bool input_source_selection_modified(const input_source_selection_t *selection) {
    (void)selection; return false;
}

void input_source_selection_commit(input_source_selection_t *selection, config_t *cfg) {
    (void)selection; (void)cfg;
}
