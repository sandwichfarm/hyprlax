/*
 * config_toml.h - TOML configuration loader interface
 */

#ifndef HYPRLAX_CONFIG_TOML_H
#define HYPRLAX_CONFIG_TOML_H

#include "core.h"
#include "hyprlax.h"

/* Load [global] settings from a TOML file into cfg */
int config_load_toml(config_t *cfg, const char *path);

/* Apply TOML config to running context (globals + layers under [[global.layers]]) */
int config_apply_toml_to_context(hyprlax_context_t *ctx, const char *path);

#endif /* HYPRLAX_CONFIG_TOML_H */

