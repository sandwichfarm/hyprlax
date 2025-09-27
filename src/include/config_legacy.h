/*
 * config_legacy.h - Legacy config reader and TOML converter
 */

#ifndef HYPRLAX_CONFIG_LEGACY_H
#define HYPRLAX_CONFIG_LEGACY_H

#include <stddef.h>

typedef struct legacy_layer_cfg {
    char *path;              /* absolute path after resolution */
    float shift_multiplier;  /* default 1.0 */
    float opacity;           /* default 1.0 */
    float blur;              /* default 0.0 */
} legacy_layer_cfg_t;

typedef struct legacy_cfg {
    /* globals (set == 0 means not specified in legacy) */
    int have_duration;  double duration;
    int have_shift;     float shift;
    int have_fps;       int fps;
    int have_vsync;     int vsync;
    int have_easing;    char easing[32];
    int have_idle;      float idle_hz;
    int have_scale;     float scale;

    /* layers */
    legacy_layer_cfg_t *layers;
    int layers_count;
    int layers_cap;

    /* source file path for relative resolution */
    char *source_path;
} legacy_cfg_t;

/* Parse legacy config file into structure (resolves relative paths). */
int legacy_config_read(const char *legacy_path, legacy_cfg_t *out_cfg, char *err, size_t err_sz);

/* Write a TOML config to dst_path from legacy config content. */
int legacy_config_write_toml(const legacy_cfg_t *cfg, const char *dst_path, char *err, size_t err_sz);

/* Free any allocations inside legacy_cfg_t */
void legacy_config_free(legacy_cfg_t *cfg);

/* Utility: compute suggested default legacy and toml paths under $HOME. */
int legacy_paths_default(char *legacy_path, size_t lsz, char *toml_path, size_t tsz);

#endif /* HYPRLAX_CONFIG_LEGACY_H */

