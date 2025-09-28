#include <stdlib.h>

#include "include/hyprlax.h"
#include "core/input/input_provider.h"
#include "core/monitor.h"
#include "include/log.h"

typedef struct workspace_provider_state {
    hyprlax_context_t *ctx;
} workspace_provider_state_t;

static int workspace_init(hyprlax_context_t *ctx, void **state_out) {
    if (!state_out) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    workspace_provider_state_t *state = calloc(1, sizeof(*state));
    if (!state) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    state->ctx = ctx;
    *state_out = state;
    return HYPRLAX_SUCCESS;
}

static void workspace_destroy(void *state) {
    free(state);
}

static int workspace_on_config(void *state, const config_t *cfg) {
    (void)state;
    (void)cfg;
    return HYPRLAX_SUCCESS;
}

static int workspace_start(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static int workspace_stop(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static bool workspace_tick(void *state,
                           const monitor_instance_t *monitor,
                           double now,
                           input_sample_t *out) {
    (void)now;
    if (!state || !out) {
        return false;
    }

    workspace_provider_state_t *provider = (workspace_provider_state_t*)state;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (monitor) {
        offset_x = monitor->parallax_offset_x;
        offset_y = monitor->parallax_offset_y;
    } else if (provider->ctx) {
        offset_x = provider->ctx->workspace_offset_x;
        offset_y = provider->ctx->workspace_offset_y;
    }

    out->x = offset_x;
    out->y = offset_y;
    out->valid = true;
    return true;
}

static const input_provider_ops_t g_workspace_provider_ops = {
    .name = "workspace",
    .init = workspace_init,
    .destroy = workspace_destroy,
    .on_config = workspace_on_config,
    .start = workspace_start,
    .stop = workspace_stop,
    .tick = workspace_tick,
};

const input_provider_ops_t* input_workspace_provider_ops(void) {
    return &g_workspace_provider_ops;
}
