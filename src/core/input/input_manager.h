#ifndef HYPRLAX_INPUT_MANAGER_H
#define HYPRLAX_INPUT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/input/input_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum input_id {
    INPUT_WORKSPACE = 0,
    INPUT_CURSOR    = 1,
    INPUT_WINDOW    = 2,
    INPUT_MAX
} input_id_t;

#define INPUT_MANAGER_MAX_MONITORS 16

struct hyprlax_context;
typedef struct monitor_instance monitor_instance_t;

typedef struct input_monitor_cache_entry {
    bool occupied;
    uint32_t monitor_id;
    input_sample_t composite;
    bool composite_valid;
    input_sample_t sources[INPUT_MAX];
    bool source_valid[INPUT_MAX];
} input_monitor_cache_entry_t;

typedef struct input_source_selection {
    bool seen[INPUT_MAX];
    bool explicit_weight[INPUT_MAX];
    float weights[INPUT_MAX];
    bool modified;
} input_source_selection_t;

typedef struct input_manager {
    struct hyprlax_context *ctx;
    const config_t *config;
    uint32_t enabled_mask;
    float weights[INPUT_MAX];
    void *states[INPUT_MAX];
    const input_provider_ops_t *ops[INPUT_MAX];
    input_monitor_cache_entry_t monitor_cache[INPUT_MANAGER_MAX_MONITORS];
} input_manager_t;

int  input_register_provider(const input_provider_ops_t *ops, input_id_t id);
void input_clear_provider_registry(void);
void input_register_builtin_providers(void);

int  input_manager_init(struct hyprlax_context *ctx,
                        input_manager_t *manager,
                        const config_t *cfg);
void input_manager_destroy(input_manager_t *manager);
int  input_manager_apply_config(input_manager_t *manager, const config_t *cfg);
int  input_manager_set_enabled(input_manager_t *manager,
                               input_id_t id,
                               bool enabled,
                               float weight);
bool input_manager_tick(input_manager_t *manager,
                        monitor_instance_t *monitor,
                        double now,
                        float *out_px_x,
                        float *out_px_y);
void input_manager_reset_cache(input_manager_t *manager);
const input_monitor_cache_entry_t* input_manager_get_cache(const input_manager_t *manager,
                                                          const monitor_instance_t *monitor);
bool input_manager_last_source(const input_manager_t *manager,
                               const monitor_instance_t *monitor,
                               input_id_t id,
                               input_sample_t *out);

void input_source_selection_init(input_source_selection_t *selection);
int  input_source_selection_add_spec(input_source_selection_t *selection, const char *spec);
bool input_source_selection_modified(const input_source_selection_t *selection);
void input_source_selection_commit(input_source_selection_t *selection, config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* HYPRLAX_INPUT_MANAGER_H */
