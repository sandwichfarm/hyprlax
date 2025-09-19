/*
 * config.c - Configuration parsing and management
 *
 * Handles command-line arguments and configuration files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "../include/core.h"

/* Set default configuration values */
void config_set_defaults(config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(config_t));

    cfg->target_fps = 60;
    cfg->max_fps = 144;
    cfg->shift_pixels = 150.0f;
    cfg->scale_factor = 1.5f;
    cfg->animation_duration = 1.0;
    cfg->default_easing = EASE_CUBIC_OUT;
    cfg->vsync = false;  /* Default off to prevent GPU blocking when idle */
    cfg->idle_poll_rate = 2.0f;  /* Default 2 Hz = 500ms polling when idle */
    cfg->debug = false;
    cfg->dry_run = false;
    cfg->blur_enabled = true;
    cfg->ipc_enabled = true;
    cfg->config_path = NULL;
    cfg->socket_path = NULL;
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
                if (cfg->target_fps <= 0 || cfg->target_fps > 240) {
                    fprintf(stderr, "Invalid FPS value: %s\n", optarg);
                    return HYPRLAX_ERROR_INVALID_ARGS;
                }
                break;

            case 's':
                cfg->shift_pixels = atof(optarg);
                if (cfg->shift_pixels < 0) {
                    fprintf(stderr, "Invalid shift value: %s\n", optarg);
                    return HYPRLAX_ERROR_INVALID_ARGS;
                }
                break;

            case 'd':
                cfg->animation_duration = atof(optarg);
                if (cfg->animation_duration <= 0) {
                    fprintf(stderr, "Invalid duration value: %s\n", optarg);
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