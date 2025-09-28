#ifndef HYPRLAX_INPUT_PROVIDER_H
#define HYPRLAX_INPUT_PROVIDER_H

#include <stdbool.h>
#include "include/core.h"

struct hyprlax_context;
struct monitor_instance;

typedef struct input_sample {
    float x;      /* pixel-space offset along X */
    float y;      /* pixel-space offset along Y */
    bool  valid;  /* false => treat as {0,0} */
} input_sample_t;

typedef struct input_provider_ops {
    const char *name;
    int  (*init)(struct hyprlax_context *ctx, void **state);
    void (*destroy)(void *state);
    int  (*on_config)(void *state, const config_t *cfg);
    int  (*start)(void *state);
    int  (*stop)(void *state);
    bool (*tick)(void *state,
                 const struct monitor_instance *monitor,
                 double now,
                 input_sample_t *out);
} input_provider_ops_t;

#endif /* HYPRLAX_INPUT_PROVIDER_H */
