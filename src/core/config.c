/*
 * config.c - Configuration parsing and management
 *
 * Handles command-line arguments and configuration files.
 */

#include <stdio.h>
#include "../include/log.h"
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "../include/core.h"
#include "../include/log.h"
#include "../include/defaults.h"

/* Set default configuration values */
void config_set_defaults(config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(config_t));

    cfg->target_fps = HYPRLAX_DEFAULT_FPS;
    cfg->max_fps = 144;
    /* Default to auto (computed from scale + workspaces). A non-zero
     * shift_percent or shift_pixels from CLI/TOML/IPC overrides this. */
    cfg->shift_percent = 0.0f;     /* 0 => auto */
    cfg->shift_pixels = 0.0f;      /* 0 => auto */
    cfg->scale_factor = HYPRLAX_DEFAULT_SCALE_FACTOR;    /* margin for parallax */
    cfg->animation_duration = HYPRLAX_DEFAULT_ANIM_DURATION;
    cfg->default_easing = EASE_CUBIC_OUT;
    cfg->vsync = false;  /* Default off to prevent GPU blocking when idle */
    cfg->idle_poll_rate = HYPRLAX_IDLE_POLL_RATE_DEFAULT;  /* Hz */
    cfg->debug = false;
    cfg->log_level = -1; /* -1 means not explicitly set, will use LOG_WARN or LOG_DEBUG based on debug flag */
    cfg->dry_run = false;
    cfg->blur_enabled = true;
    cfg->ipc_enabled = true;
    cfg->config_path = NULL;
    cfg->socket_path = NULL;

    /* Parallax defaults */
    cfg->parallax_mode = PARALLAX_WORKSPACE;
    cfg->parallax_workspace_weight = 1.0f;
    cfg->parallax_cursor_weight = 0.0f;
    cfg->parallax_window_weight = 0.0f;
    cfg->invert_workspace_x = false; /* Default: match legacy hyprlax scroll */
    cfg->invert_workspace_y = false;
    cfg->invert_cursor_x = false;
    cfg->invert_cursor_y = false;
    cfg->invert_window_x = false;
    cfg->invert_window_y = false;
    cfg->parallax_max_offset_x = HYPRLAX_DEFAULT_MAX_OFFSET_PX; /* effectively no clamp by default */
    cfg->parallax_max_offset_y = HYPRLAX_DEFAULT_MAX_OFFSET_PX;
    /* Render overflow defaults */
    /* For simple single-layer wallpapers, avoid visible edge smearing by default.
     * We keep repeat_edge (0) for back-compatibility, but set a slightly larger
     * default scale_factor to provide overscan, and rely on absolute positioning
     * to prevent unbounded drift. If smearing still occurs in custom configs,
     * users can set render.overflow to 'repeat' or increase content_scale. */
    cfg->render_overflow_mode = 0; /* repeat_edge */
    cfg->render_margin_px_x = 0.0f;
    cfg->render_margin_px_y = 0.0f;
    cfg->render_tile_x = 0;
    cfg->render_tile_y = 0;
    cfg->render_accumulate = false;
    cfg->render_trail_strength = HYPRLAX_DEFAULT_TRAIL_STRENGTH; /* per-frame fade when accumulating */
    cfg->cursor_sensitivity_x = 1.0f;
    cfg->cursor_sensitivity_y = 1.0f;
    cfg->cursor_deadzone_px = 4.0f;
    cfg->cursor_ema_alpha = 0.25f;
    cfg->cursor_anim_duration = 0.0; /* disabled by default (low-latency) */
    cfg->cursor_easing = EASE_CUBIC_OUT;
    cfg->cursor_follow_global = true; /* default: animate even when cursor isn’t over background */
    cfg->window_sensitivity_x = 1.0f;
    cfg->window_sensitivity_y = 1.0f;
    cfg->window_deadzone_px = 6.0f;
    cfg->window_ema_alpha = 0.25f;
}

/* Parse command-line arguments */
int config_parse_args(config_t *cfg, int argc, char **argv) {
    if (!cfg || !argv) return HYPRLAX_ERROR_INVALID_ARGS;

    // Set defaults first
    config_set_defaults(cfg);

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"fps", required_argument, 0, 'f'},
        {"shift", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'd'},
        {"easing", required_argument, 0, 'e'},
        {"config", required_argument, 0, 'c'},
        {"debug", no_argument, 0, 'D'},
        {"dry-run", no_argument, 0, 'n'},
        {"no-blur", no_argument, 0, 'B'},
        {"no-ipc", no_argument, 0, 'I'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hvf:s:d:e:c:DnBI",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                // Help will be handled by main
                return HYPRLAX_ERROR_INVALID_ARGS;

            case 'v':
                printf("hyprlax %s\n", HYPRLAX_VERSION);
                exit(0);

            case 'f':
                cfg->target_fps = atoi(optarg);
                if (cfg->target_fps <= 0 || cfg->target_fps > HYPRLAX_MAX_ALLOWED_FPS) {
                    LOG_WARN("Invalid FPS value: %s", optarg);
                    return HYPRLAX_ERROR_INVALID_ARGS;
                }
                break;

            case 's': {
                float value = atof(optarg);
                if (value < 0) {
                    LOG_WARN("Invalid shift value: %s", optarg);
                    return HYPRLAX_ERROR_INVALID_ARGS;
                }
                /* Auto-detect percentage vs pixels:
                 * Values <= 10 are treated as percentages
                 * Values > 10 are treated as pixels (deprecated) */
                if (value <= 10.0f) {
                    cfg->shift_percent = value;
                    cfg->shift_pixels = 0.0f;  /* Clear deprecated field */
                } else {
                    /* Backwards compatibility: treat as pixels */
                    cfg->shift_pixels = value;
                    cfg->shift_percent = 0.0f;  /* Will be converted at runtime */
                    LOG_WARN("Using deprecated pixel-based shift (%.0f px). Consider using percentage (0-10)", value);
                }
                break;
            }

            case 'd':
                cfg->animation_duration = atof(optarg);
                if (cfg->animation_duration <= 0) {
                    LOG_WARN("Invalid duration value: %s", optarg);
                    return HYPRLAX_ERROR_INVALID_ARGS;
                }
                break;

            case 'e':
                cfg->default_easing = easing_from_string(optarg);
                break;

            case 'c':
                cfg->config_path = strdup(optarg);
                break;

            case 'D':
                cfg->debug = true;
                break;

            case 'n':
                cfg->dry_run = true;
                break;

            case 'B':
                cfg->blur_enabled = false;
                break;

            case 'I':
                cfg->ipc_enabled = false;
                break;

            default:
                return HYPRLAX_ERROR_INVALID_ARGS;
        }
    }

    return HYPRLAX_SUCCESS;
}

/* Load configuration from file */
int config_load_file(config_t *cfg, const char *path) {
    if (!cfg || !path) return HYPRLAX_ERROR_INVALID_ARGS;

    FILE *file = fopen(path, "r");
    if (!file) {
        return HYPRLAX_ERROR_FILE_NOT_FOUND;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Parse key = value pairs
        char key[64], value[192];
        if (sscanf(line, "%63s = %191s", key, value) == 2) {
            if (strcmp(key, "fps") == 0) {
                cfg->target_fps = atoi(value);
            } else if (strcmp(key, "shift") == 0) {
                cfg->shift_pixels = atof(value);
            } else if (strcmp(key, "duration") == 0) {
                cfg->animation_duration = atof(value);
            } else if (strcmp(key, "easing") == 0) {
                cfg->default_easing = easing_from_string(value);
            } else if (strcmp(key, "debug") == 0) {
                cfg->debug = (strcmp(value, "true") == 0);
            }
        }
    }

    fclose(file);
    return HYPRLAX_SUCCESS;
}

/* Clean up configuration */
void config_cleanup(config_t *cfg) {
    if (!cfg) return;

    if (cfg->config_path) {
        free(cfg->config_path);
        cfg->config_path = NULL;
    }

    if (cfg->socket_path) {
        free(cfg->socket_path);
        cfg->socket_path = NULL;
    }
}
