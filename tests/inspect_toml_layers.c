#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/hyprlax.h"
#include "include/config_toml.h"
#include "include/core.h"

/* Provide minimal context create/destroy stubs */
hyprlax_context_t* hyprlax_create(void) { 
    hyprlax_context_t *ctx = calloc(1, sizeof(hyprlax_context_t));
    config_set_defaults(&ctx->config);
    return ctx; 
}
void hyprlax_destroy(hyprlax_context_t *ctx) { if (ctx) { layer_list_destroy(ctx->layers); free(ctx); } }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.toml\n", argv[0]); return 2; }
    hyprlax_context_t *ctx = hyprlax_create();
    if (!ctx) return 3;
    int rc = config_apply_toml_to_context(ctx, argv[1]);
    if (rc != 0) { fprintf(stderr, "apply rc=%d\n", rc); return 1; }
    printf("global.scale_factor=%.3f\n", ctx->config.scale_factor);
    printf("global.shift_percent=%.4f\n", ctx->config.shift_percent);
    int i=0; for (parallax_layer_t *l = ctx->layers; l; l=l->next) {
        printf("layer[%d].content_scale=%.3f shift=%.2f fit=%d\n", i, l->content_scale, l->shift_multiplier, l->fit_mode);
        i++;
    }
    hyprlax_destroy(ctx);
    return 0;
}
