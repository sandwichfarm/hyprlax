#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "core/input/input_manager.h"
#include "include/log.h"
#include "core/monitor.h"
#include "include/defaults.h"

#ifndef INPUT_MANAGER_CLAMP01
#define INPUT_MANAGER_CLAMP01(v) ((v) < 0.0f ? 0.0f : ((v) > 1.0f ? 1.0f : (v)))
#endif

static const input_provider_ops_t *g_provider_registry[INPUT_MAX];

static void trim_whitespace(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static input_id_t input_id_from_name(const char *name) {
    if (!name) return INPUT_MAX;
    if (strcasecmp(name, "workspace") == 0) return INPUT_WORKSPACE;
    if (strcasecmp(name, "cursor") == 0) return INPUT_CURSOR;
    if (strcasecmp(name, "window") == 0) return INPUT_WINDOW;
    return INPUT_MAX;
}

void input_source_selection_init(input_source_selection_t *selection) {
    if (!selection) return;
    memset(selection, 0, sizeof(*selection));
}

static void selection_set_seen(input_source_selection_t *selection, input_id_t id, bool explicit_weight, float weight) {
    if (!selection || id < 0 || id >= INPUT_MAX) return;
    selection->seen[id] = true;
    if (explicit_weight) {
        if (weight < 0.0f) weight = 0.0f;
        if (weight > 1.0f) weight = 1.0f;
        selection->explicit_weight[id] = true;
        selection->weights[id] = weight;
    } else if (!selection->explicit_weight[id]) {
        selection->weights[id] = 0.0f;
    }
    selection->modified = true;
}

int input_source_selection_add_spec(input_source_selection_t *selection, const char *spec) {
    if (!selection || !spec) return HYPRLAX_ERROR_INVALID_ARGS;

    char *copy = strdup(spec);
    if (!copy) return HYPRLAX_ERROR_NO_MEMORY;

    char *saveptr = NULL;
    char *token = strtok_r(copy, ",", &saveptr);
    while (token) {
        trim_whitespace(token);
        if (*token != '\0') {
            char *colon = strchr(token, ':');
            float weight = 0.0f;
            bool has_weight = false;
            if (colon) {
                *colon = '\0';
                char *weight_str = colon + 1;
                trim_whitespace(weight_str);
                if (*weight_str) {
                    weight = strtof(weight_str, NULL);
                    has_weight = true;
                }
            }

            trim_whitespace(token);
            input_id_t id = input_id_from_name(token);
            if (id == INPUT_MAX) {
                LOG_WARN("input manager: unknown input source '%s'", token);
            } else {
                selection_set_seen(selection, id, has_weight, weight);
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(copy);
    return HYPRLAX_SUCCESS;
}

bool input_source_selection_modified(const input_source_selection_t *selection) {
    return selection && selection->modified;
}

void input_source_selection_commit(input_source_selection_t *selection, config_t *cfg) {
    if (!selection || !cfg || !selection->modified) return;

    float final_weights[INPUT_MAX] = {0.0f};
    bool any_seen = false;
    float sum_explicit = 0.0f;
    int unspecified_count = 0;

    for (int i = 0; i < INPUT_MAX; ++i) {
        if (selection->seen[i]) {
            any_seen = true;
            if (selection->explicit_weight[i]) {
                float w = selection->weights[i];
                if (w < 0.0f) w = 0.0f;
                if (w > 1.0f) w = 1.0f;
                final_weights[i] = w;
                sum_explicit += w;
            } else {
                unspecified_count++;
            }
        }
    }

    if (any_seen) {
        float remaining = 1.0f - sum_explicit;
        if (remaining < 0.0f) remaining = 0.0f;

        bool workspace_unspecified = selection->seen[INPUT_WORKSPACE] && !selection->explicit_weight[INPUT_WORKSPACE];
        bool cursor_unspecified = selection->seen[INPUT_CURSOR] && !selection->explicit_weight[INPUT_CURSOR];

        if (workspace_unspecified && cursor_unspecified && unspecified_count == 2) {
            final_weights[INPUT_WORKSPACE] = HYPRLAX_DEFAULT_HYBRID_WORKSPACE_WEIGHT;
            final_weights[INPUT_CURSOR] = HYPRLAX_DEFAULT_HYBRID_CURSOR_WEIGHT;
            remaining = 0.0f;
            unspecified_count = 0;
        }

        if (unspecified_count > 0) {
            float per = (unspecified_count > 0 && remaining > 0.0f) ? (remaining / (float)unspecified_count) : 0.0f;
            for (int i = 0; i < INPUT_MAX; ++i) {
                if (selection->seen[i] && !selection->explicit_weight[i]) {
                    final_weights[i] = per;
                }
            }
        }

        cfg->parallax_workspace_weight = selection->seen[INPUT_WORKSPACE] ? final_weights[INPUT_WORKSPACE] : 0.0f;
        cfg->parallax_cursor_weight = selection->seen[INPUT_CURSOR] ? final_weights[INPUT_CURSOR] : 0.0f;

        /* Legacy parallax modes removed; behavior derives purely from weights. */

        cfg->parallax_window_weight = selection->seen[INPUT_WINDOW] ? final_weights[INPUT_WINDOW] : 0.0f;
    }

    input_source_selection_init(selection);
}

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
    manager->weights[INPUT_WINDOW] = INPUT_MANAGER_CLAMP01(cfg->parallax_window_weight);

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
