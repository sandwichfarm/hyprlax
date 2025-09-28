#include <stdlib.h>

#include "include/hyprlax.h"
#include "core/input/input_provider.h"
#include "core/monitor.h"
#include "include/log.h"

typedef struct cursor_provider_state {
    hyprlax_context_t *ctx;
} cursor_provider_state_t;

static int cursor_init(hyprlax_context_t *ctx, void **state_out) {
    if (!state_out) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    cursor_provider_state_t *state = calloc(1, sizeof(*state));
    if (!state) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    state->ctx = ctx;
    *state_out = state;
    return HYPRLAX_SUCCESS;
}

static void cursor_destroy(void *state) {
    free(state);
}

static int cursor_on_config(void *state, const config_t *cfg) {
    (void)state;
    (void)cfg;
    return HYPRLAX_SUCCESS;
}

static int cursor_start(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static int cursor_stop(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static bool cursor_tick(void *state,
                        const monitor_instance_t *monitor,
                        double now,
                        input_sample_t *out) {
    (void)monitor;
    (void)now;
    if (!state || !out) {
        return false;
    }

    cursor_provider_state_t *provider = (cursor_provider_state_t*)state;
    hyprlax_context_t *ctx = provider->ctx;
    if (!ctx) {
        return false;
    }

    if (!ctx->cursor_supported) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->valid = false;
        return false;
    }

    float px_x = ctx->cursor_eased_x * ctx->config.parallax_max_offset_x;
    float px_y = ctx->cursor_eased_y * ctx->config.parallax_max_offset_y;

    out->x = px_x;
    out->y = px_y;
    out->valid = true;
    return true;
}

static const input_provider_ops_t g_cursor_provider_ops = {
    .name = "cursor",
    .init = cursor_init,
    .destroy = cursor_destroy,
    .on_config = cursor_on_config,
    .start = cursor_start,
    .stop = cursor_stop,
    .tick = cursor_tick,
};

const input_provider_ops_t* input_cursor_provider_ops(void) {
    return &g_cursor_provider_ops;
}
