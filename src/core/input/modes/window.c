#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "include/hyprlax.h"
#include "core/input/input_provider.h"
#include "include/compositor.h"
#include "include/log.h"

typedef struct window_provider_state {
    hyprlax_context_t *ctx;
    float ema_x;
    float ema_y;
    bool ema_valid;
    window_geometry_t cached_geom;
    bool cache_valid;
    double cache_time;
    bool capability_warned;
} window_provider_state_t;

static int window_init(hyprlax_context_t *ctx, void **state_out) {
    if (!state_out) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    window_provider_state_t *state = calloc(1, sizeof(*state));
    if (!state) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    state->ctx = ctx;
    state->ema_x = 0.0f;
    state->ema_y = 0.0f;
    state->ema_valid = false;
    state->cache_valid = false;
    state->cache_time = 0.0;
    state->capability_warned = false;
    *state_out = state;
    return HYPRLAX_SUCCESS;
}

static void window_destroy(void *state) {
    free(state);
}

static int window_on_config(void *state, const config_t *cfg) {
    (void)cfg;
    window_provider_state_t *provider = (window_provider_state_t*)state;
    if (!provider) return HYPRLAX_SUCCESS;
    provider->ema_valid = false;
    return HYPRLAX_SUCCESS;
}

static int window_start(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static int window_stop(void *state) {
    (void)state;
    return HYPRLAX_SUCCESS;
}

static bool point_in_monitor(const monitor_instance_t *monitor, double x, double y) {
    if (!monitor) return false;
    double left = monitor->global_x;
    double top = monitor->global_y;
    double right = left + monitor->width;
    double bottom = top + monitor->height;
    return x >= left && x <= right && y >= top && y <= bottom;
}

static float apply_deadzone(float value, float deadzone) {
    float absval = fabsf(value);
    if (absval <= deadzone) {
        return 0.0f;
    }
    return value;
}

static bool window_tick(void *state,
                        const monitor_instance_t *monitor,
                        double now,
                        input_sample_t *out) {
    if (!state || !monitor || !out) {
        return false;
    }
    window_provider_state_t *provider = (window_provider_state_t*)state;
    hyprlax_context_t *ctx = provider->ctx;
    if (!ctx || !ctx->compositor || !ctx->compositor->ops) {
        return false;
    }
    const compositor_ops_t *ops = ctx->compositor->ops;
    if (!ops->get_active_window_geometry) {
        if (!provider->capability_warned) {
            LOG_WARN("window input: compositor does not expose active window geometry; window source disabled");
            provider->capability_warned = true;
        }
        return false;
    }
    provider->capability_warned = false;

    bool refresh = true;
    if (provider->cache_valid) {
        if (fabs(now - provider->cache_time) < 0.0005) {
            refresh = false;
        }
    }

    if (refresh) {
        window_geometry_t geom;
        if (ops->get_active_window_geometry(&geom) == HYPRLAX_SUCCESS && geom.width > 0.0 && geom.height > 0.0) {
            provider->cached_geom = geom;
            provider->cache_valid = true;
        } else {
            provider->cache_valid = false;
        }
        provider->cache_time = now;
    }

    if (!provider->cache_valid) {
        provider->ema_valid = false;
        return false;
    }

    window_geometry_t *geom = &provider->cached_geom;
    double window_cx = geom->x + geom->width * 0.5;
    double window_cy = geom->y + geom->height * 0.5;

    if (!point_in_monitor(monitor, window_cx, window_cy)) {
        provider->ema_valid = false;
        return false;
    }

    double monitor_center_x = monitor->global_x + (double)monitor->width * 0.5;
    double monitor_center_y = monitor->global_y + (double)monitor->height * 0.5;

    float dx = (float)(window_cx - monitor_center_x);
    float dy = (float)(window_cy - monitor_center_y);

    float sx = ctx->config.window_sensitivity_x;
    float sy = ctx->config.window_sensitivity_y;
    float deadzone = ctx->config.window_deadzone_px;
    float ema_alpha = ctx->config.window_ema_alpha;

    float adj_x = apply_deadzone(dx * sx, deadzone);
    float adj_y = apply_deadzone(dy * sy, deadzone);

    if (ema_alpha > 0.0f && ema_alpha < 1.0f) {
        if (!provider->ema_valid) {
            provider->ema_x = adj_x;
            provider->ema_y = adj_y;
            provider->ema_valid = true;
        } else {
            provider->ema_x = provider->ema_x + ema_alpha * (adj_x - provider->ema_x);
            provider->ema_y = provider->ema_y + ema_alpha * (adj_y - provider->ema_y);
        }
        adj_x = provider->ema_x;
        adj_y = provider->ema_y;
    } else {
        provider->ema_valid = false;
    }

    out->x = adj_x;
    out->y = adj_y;
    out->valid = true;
    return true;
}

static const input_provider_ops_t g_window_provider_ops = {
    .name = "window",
    .init = window_init,
    .destroy = window_destroy,
    .on_config = window_on_config,
    .start = window_start,
    .stop = window_stop,
    .tick = window_tick,
};

const input_provider_ops_t* input_window_provider_ops(void) {
    return &g_window_provider_ops;
}
