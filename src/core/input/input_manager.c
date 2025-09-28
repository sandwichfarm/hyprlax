#include <math.h>
#include <stddef.h>
#include <string.h>

#include "core/input/input_manager.h"
#include "include/log.h"
#include "core/monitor.h"

#ifndef INPUT_MANAGER_CLAMP01
#define INPUT_MANAGER_CLAMP01(v) ((v) < 0.0f ? 0.0f : ((v) > 1.0f ? 1.0f : (v)))
#endif

static const input_provider_ops_t *g_provider_registry[INPUT_MAX];

void input_clear_provider_registry(void) {
    memset(g_provider_registry, 0, sizeof(g_provider_registry));
}

int input_register_provider(const input_provider_ops_t *ops, input_id_t id) {
    if (!ops) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    if (id < 0 || id >= INPUT_MAX) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    g_provider_registry[id] = ops;
    return HYPRLAX_SUCCESS;
}

static input_monitor_cache_entry_t *find_cache_slot(input_manager_t *manager, uint32_t monitor_id) {
    if (!manager) return NULL;

    int free_idx = -1;
    for (int i = 0; i < INPUT_MANAGER_MAX_MONITORS; ++i) {
        input_monitor_cache_entry_t *entry = &manager->monitor_cache[i];
        if (entry->occupied && entry->monitor_id == monitor_id) {
            return entry;
        }
        if (!entry->occupied && free_idx == -1) {
            free_idx = i;
        }
    }

    if (free_idx >= 0) {
        input_monitor_cache_entry_t *entry = &manager->monitor_cache[free_idx];
        entry->occupied = true;
        entry->monitor_id = monitor_id;
        entry->composite.x = 0.0f;
        entry->composite.y = 0.0f;
        entry->composite.valid = false;
        entry->composite_valid = false;
        for (int i = 0; i < INPUT_MAX; ++i) {
            entry->sources[i].x = 0.0f;
            entry->sources[i].y = 0.0f;
            entry->sources[i].valid = false;
            entry->source_valid[i] = false;
        }
        return entry;
    }

    /* Cache full: overwrite the first slot */
    input_monitor_cache_entry_t *entry = &manager->monitor_cache[0];
    entry->occupied = true;
    entry->monitor_id = monitor_id;
    entry->composite.x = 0.0f;
    entry->composite.y = 0.0f;
    entry->composite.valid = false;
    entry->composite_valid = false;
    for (int i = 0; i < INPUT_MAX; ++i) {
        entry->sources[i].x = 0.0f;
        entry->sources[i].y = 0.0f;
        entry->sources[i].valid = false;
        entry->source_valid[i] = false;
    }
    return entry;
}

static void prime_weights_from_config(input_manager_t *manager, const config_t *cfg) {
    if (!manager || !cfg) return;

    manager->weights[INPUT_WORKSPACE] = INPUT_MANAGER_CLAMP01(cfg->parallax_workspace_weight);
    manager->weights[INPUT_CURSOR] = INPUT_MANAGER_CLAMP01(cfg->parallax_cursor_weight);
    manager->weights[INPUT_WINDOW] = 0.0f; /* default off until configured */

    manager->enabled_mask = 0;
    if (manager->weights[INPUT_WORKSPACE] > 0.0f) {
        manager->enabled_mask |= (1u << INPUT_WORKSPACE);
    }
    if (manager->weights[INPUT_CURSOR] > 0.0f) {
        manager->enabled_mask |= (1u << INPUT_CURSOR);
    }
    if (manager->weights[INPUT_WINDOW] > 0.0f) {
        manager->enabled_mask |= (1u << INPUT_WINDOW);
    }
}

int input_manager_init(struct hyprlax_context *ctx,
                       input_manager_t *manager,
                       const config_t *cfg) {
    if (!manager) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ctx = ctx;
    manager->config = cfg;
    input_manager_reset_cache(manager);

    prime_weights_from_config(manager, cfg);

    for (int i = 0; i < INPUT_MAX; ++i) {
        manager->ops[i] = g_provider_registry[i];
        manager->states[i] = NULL;
        if (manager->ops[i] && manager->ops[i]->init) {
            if (manager->ops[i]->init(ctx, &manager->states[i]) != HYPRLAX_SUCCESS) {
                LOG_WARN("input_manager: init failed for provider %s", manager->ops[i]->name);
                manager->states[i] = NULL;
            }
        }
    }

    return HYPRLAX_SUCCESS;
}

void input_manager_destroy(input_manager_t *manager) {
    if (!manager) return;

    for (int i = 0; i < INPUT_MAX; ++i) {
        if (manager->ops[i] && manager->ops[i]->stop && manager->states[i]) {
            manager->ops[i]->stop(manager->states[i]);
        }
        if (manager->ops[i] && manager->ops[i]->destroy && manager->states[i]) {
            manager->ops[i]->destroy(manager->states[i]);
        }
        manager->states[i] = NULL;
    }

    manager->enabled_mask = 0;
    manager->ctx = NULL;
    manager->config = NULL;
    input_manager_reset_cache(manager);
}

int input_manager_apply_config(input_manager_t *manager, const config_t *cfg) {
    if (!manager) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    manager->config = cfg;
    if (cfg) {
        prime_weights_from_config(manager, cfg);
        for (int i = 0; i < INPUT_MAX; ++i) {
            if (manager->ops[i] && manager->ops[i]->on_config && manager->states[i]) {
                manager->ops[i]->on_config(manager->states[i], cfg);
            }
        }
    }

    input_manager_reset_cache(manager);
    return HYPRLAX_SUCCESS;
}

int input_manager_set_enabled(input_manager_t *manager,
                              input_id_t id,
                              bool enabled,
                              float weight) {
    if (!manager || id < 0 || id >= INPUT_MAX) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    float clamped = INPUT_MANAGER_CLAMP01(weight);
    manager->weights[id] = enabled ? clamped : 0.0f;
    if (enabled && clamped > 0.0f) {
        manager->enabled_mask |= (1u << id);
    } else {
        manager->enabled_mask &= ~(1u << id);
    }

    input_manager_reset_cache(manager);
    return HYPRLAX_SUCCESS;
}

void input_manager_reset_cache(input_manager_t *manager) {
    if (!manager) return;
    for (int i = 0; i < INPUT_MANAGER_MAX_MONITORS; ++i) {
        manager->monitor_cache[i].occupied = false;
        manager->monitor_cache[i].monitor_id = 0;
        manager->monitor_cache[i].composite.x = 0.0f;
        manager->monitor_cache[i].composite.y = 0.0f;
        manager->monitor_cache[i].composite.valid = false;
        manager->monitor_cache[i].composite_valid = false;
        for (int j = 0; j < INPUT_MAX; ++j) {
            manager->monitor_cache[i].sources[j].x = 0.0f;
            manager->monitor_cache[i].sources[j].y = 0.0f;
            manager->monitor_cache[i].sources[j].valid = false;
            manager->monitor_cache[i].source_valid[j] = false;
        }
    }
}

static float clamp_axis(float value, float limit) {
    if (limit <= 0.0f) {
        return value;
    }
    if (value > limit) return limit;
    if (value < -limit) return -limit;
    return value;
}

bool input_manager_tick(input_manager_t *manager,
                        monitor_instance_t *monitor,
                        double now,
                        float *out_px_x,
                        float *out_px_y) {
    if (!manager) {
        return false;
    }

    float accum_x = 0.0f;
    float accum_y = 0.0f;
    bool any_valid = false;
    input_sample_t source_samples[INPUT_MAX];
    bool source_valid[INPUT_MAX];

    for (int i = 0; i < INPUT_MAX; ++i) {
        source_samples[i].x = 0.0f;
        source_samples[i].y = 0.0f;
        source_samples[i].valid = false;
        source_valid[i] = false;
    }

    for (int i = 0; i < INPUT_MAX; ++i) {
        if (!(manager->enabled_mask & (1u << i))) {
            continue;
        }
        const input_provider_ops_t *ops = manager->ops[i];
        if (!ops || !ops->tick || !manager->states[i]) {
            continue;
        }

        input_sample_t sample = { .x = 0.0f, .y = 0.0f, .valid = false };
        bool produced = ops->tick(manager->states[i], monitor, now, &sample);
        if (!produced || !sample.valid) {
            continue;
        }

        source_samples[i] = sample;
        source_valid[i] = true;
        accum_x += sample.x * manager->weights[i];
        accum_y += sample.y * manager->weights[i];
        any_valid = true;
    }

    uint32_t monitor_id = monitor ? monitor->id : 0;

    if (!any_valid) {
        if (out_px_x) *out_px_x = 0.0f;
        if (out_px_y) *out_px_y = 0.0f;

        input_monitor_cache_entry_t *entry = find_cache_slot(manager, monitor_id);
        if (entry) {
            entry->composite.x = 0.0f;
            entry->composite.y = 0.0f;
            entry->composite.valid = false;
            entry->composite_valid = false;
            for (int i = 0; i < INPUT_MAX; ++i) {
                entry->sources[i].x = 0.0f;
                entry->sources[i].y = 0.0f;
                entry->sources[i].valid = false;
                entry->source_valid[i] = false;
            }
        }
    } else {
        float limit_x = manager->config ? manager->config->parallax_max_offset_x : 0.0f;
        float limit_y = manager->config ? manager->config->parallax_max_offset_y : 0.0f;
        float clamped_x = clamp_axis(accum_x, limit_x);
        float clamped_y = clamp_axis(accum_y, limit_y);
        if (out_px_x) *out_px_x = clamped_x;
        if (out_px_y) *out_px_y = clamped_y;

        input_monitor_cache_entry_t *entry = find_cache_slot(manager, monitor_id);
        if (entry) {
            entry->composite.x = clamped_x;
            entry->composite.y = clamped_y;
            entry->composite.valid = true;
            entry->composite_valid = true;
            for (int i = 0; i < INPUT_MAX; ++i) {
                entry->sources[i] = source_samples[i];
                entry->source_valid[i] = source_valid[i];
            }
        }
    }

    return any_valid;
}

const input_monitor_cache_entry_t* input_manager_get_cache(const input_manager_t *manager,
                                                          const monitor_instance_t *monitor) {
    if (!manager) {
        return NULL;
    }

    uint32_t monitor_id = monitor ? monitor->id : 0;
    for (int i = 0; i < INPUT_MANAGER_MAX_MONITORS; ++i) {
        const input_monitor_cache_entry_t *entry = &manager->monitor_cache[i];
        if (entry->occupied && entry->monitor_id == monitor_id) {
            return entry;
        }
    }

    return NULL;
}

bool input_manager_last_source(const input_manager_t *manager,
                               const monitor_instance_t *monitor,
                               input_id_t id,
                               input_sample_t *out) {
    if (!manager || id < 0 || id >= INPUT_MAX || !out) {
        return false;
    }

    const input_monitor_cache_entry_t *entry = input_manager_get_cache(manager, monitor);
    if (!entry) {
        return false;
    }

    if (!entry->source_valid[id]) {
        return false;
    }

    *out = entry->sources[id];
    return out->valid;
}
