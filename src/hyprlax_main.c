/*
 * hyprlax_main.c - Main application integration
 *
 * Ties together all modules and manages the application lifecycle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "include/hyprlax.h"
#include "include/hyprlax_internal.h"
#include "include/log.h"
#include "include/renderer.h"
#include "include/compositor.h"
#include "include/config_toml.h"
#include "include/wayland_api.h"
#include "include/defaults.h"
#include "core/monitor.h"
#include "ipc.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GLES2/gl2.h>

#define MAX_CONFIG_LINE_SIZE 512

/* Forward declarations */
/* moved: hyprlax_load_layer_textures in core/render_core.c */
/* Forward declarations for handlers used before definition */
void hyprlax_handle_workspace_change_2d(hyprlax_context_t *ctx,
                                       int from_x, int from_y,
                                       int to_x, int to_y);
/* Helper prototype: parse either --opt=value or --opt value */
static inline const char* arg_get_val_local(const char *arg, const char *next);

/* Linux event/timer helpers moved to core/event_loop.c */

static void hyprlax_update_cursor_provider(hyprlax_context_t *ctx) {
    if (!ctx) return;

    bool need_cursor = (ctx->input.enabled_mask & (1u << INPUT_CURSOR)) &&
                       (ctx->input.weights[INPUT_CURSOR] > 0.0f);

    if (need_cursor) {
        bool created = false;
        if (ctx->cursor_event_fd < 0) {
            ctx->cursor_event_fd = create_timerfd_monotonic();
            if (ctx->cursor_event_fd < 0) {
                LOG_WARN("Failed to create cursor timerfd");
                ctx->cursor_supported = false;
                return;
            }
            created = true;
        }

        int fps = ctx->config.target_fps > 0 ? ctx->config.target_fps : HYPRLAX_DEFAULT_FPS;
        int interval_ms = fps > 0 ? (int)(1000.0 / (double)fps) : (int)(1000.0 / (double)HYPRLAX_DEFAULT_FPS);
        arm_timerfd_ms(ctx->cursor_event_fd, interval_ms, interval_ms);
        ctx->cursor_supported = true;

        /* If epoll is already initialized and this is a new timerfd, register it */
        if (created && ctx->epoll_fd >= 0) {
            epoll_add_fd(ctx->epoll_fd, ctx->cursor_event_fd, EPOLLIN);
        }

        /* Ensure an immediate frame to prime input caches after enabling */
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
    } else {
        if (ctx->cursor_event_fd >= 0) {
            if (ctx->epoll_fd >= 0) {
                epoll_del_fd(ctx->epoll_fd, ctx->cursor_event_fd);
            }
            close(ctx->cursor_event_fd);
            ctx->cursor_event_fd = -1;
        }
        ctx->cursor_supported = false;

        /* Kick a frame so renderer applies new weights immediately */
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
    }
}

static void warn_legacy_parallax_usage(const char *source) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    LOG_WARN("Legacy %s parallax spec detected; consider using --input / HYPRLAX_PARALLAX_INPUT / parallax.input instead", source ? source : "parallax.mode");
}

/* --- Local helpers (refactored from nested functions) --- */
static bool parse_bool_local(const char *v) {
    if (!v) return false;
    return (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 ||
            strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0);
}

static int overflow_from_string_local(const char *s) {
    if (!s) return -1;
    if (!strcmp(s, "inherit")) return -1;
    if (!strcmp(s, "repeat_edge") || !strcmp(s, "clamp")) return 0;
    if (!strcmp(s, "repeat") || !strcmp(s, "tile")) return 1;
    if (!strcmp(s, "repeat_x") || !strcmp(s, "tilex")) return 2;
    if (!strcmp(s, "repeat_y") || !strcmp(s, "tiley")) return 3;
    if (!strcmp(s, "none") || !strcmp(s, "off")) return 4;
    return -2;
}

static const char* overflow_to_string_local(int m) {
    switch (m) {
        case 0: return "repeat_edge";
        case 1: return "repeat";
        case 2: return "repeat_x";
        case 3: return "repeat_y";
        case 4: return "none";
        case -1: default: return "inherit";
    }
}

static int fit_from_string_local(const char *s) {
    if (!s) return -1;
    if (!strcmp(s, "stretch")) return LAYER_FIT_STRETCH;
    if (!strcmp(s, "cover")) return LAYER_FIT_COVER;
    if (!strcmp(s, "contain")) return LAYER_FIT_CONTAIN;
    if (!strcmp(s, "fit_width")) return LAYER_FIT_WIDTH;
    if (!strcmp(s, "fit_height")) return LAYER_FIT_HEIGHT;
    return -1;
}

static const char* fit_to_string_local(int m) {
    switch (m) {
        case LAYER_FIT_STRETCH: return "stretch";
        case LAYER_FIT_COVER: return "cover";
        case LAYER_FIT_CONTAIN: return "contain";
        case LAYER_FIT_WIDTH: return "fit_width";
        case LAYER_FIT_HEIGHT: return "fit_height";
        default: return "stretch";
    }
}

/* disarm_timerfd moved */

/* arm_timerfd_ms moved */

/* epoll_add_fd moved */

/* hyprlax_setup_epoll moved */

/* hyprlax_arm_frame_timer moved */

/* hyprlax_disarm_frame_timer moved */

/* hyprlax_arm_debounce moved */

/* hyprlax_clear_timerfd moved */

/* cursor smoothing moved to core/cursor.c */

/* cursor events handled in core/event_loop.c via hyprlax_cursor_tick */

/* Apply a compositor workspace event (shared by immediate and debounced paths) */
void process_workspace_event(hyprlax_context_t *ctx, const compositor_event_t *comp_event) {
    if (!ctx || !comp_event) return;
    /* If workspace input is disabled (weight == 0), ignore workspace events. */
    if (ctx->input.weights[INPUT_WORKSPACE] <= 0.0f) {
        LOG_TRACE("Ignoring workspace event: workspace input weight disabled");
        return;
    }

    /* Find target monitor */
    monitor_instance_t *target_monitor = NULL;
    if (ctx->monitors && comp_event->data.workspace.monitor_name[0] != '\0') {
        target_monitor = monitor_list_find_by_name(ctx->monitors,
                                                  comp_event->data.workspace.monitor_name);
        if (target_monitor) {
            LOG_TRACE("Workspace event for monitor: %s",
                      comp_event->data.workspace.monitor_name);
        }
    }
    if (!target_monitor && ctx->monitors) {
        target_monitor = monitor_list_get_primary(ctx->monitors);
        if (!target_monitor) target_monitor = ctx->monitors->head;
    }

    /* 2D vs linear */
    if (comp_event->data.workspace.from_x != 0 ||
        comp_event->data.workspace.from_y != 0 ||
        comp_event->data.workspace.to_x != 0 ||
        comp_event->data.workspace.to_y != 0) {
        if (ctx->config.debug) {
            LOG_DEBUG("Debounced 2D Workspace: (%d,%d)->(%d,%d)",
                      comp_event->data.workspace.from_x,
                      comp_event->data.workspace.from_y,
                      comp_event->data.workspace.to_x,
                      comp_event->data.workspace.to_y);
        }

        if (target_monitor) {
            workspace_model_t model = workspace_detect_model_for_adapter(ctx->compositor);
            workspace_context_t new_context;
            new_context.model = model;
            if (model == WS_MODEL_PER_OUTPUT_NUMERIC) {
                new_context.data.workspace_id = comp_event->data.workspace.to_y * 1000 +
                                                comp_event->data.workspace.to_x;
            } else if (model == WS_MODEL_SET_BASED) {
                new_context.data.wayfire_set.set_id = comp_event->data.workspace.to_y;
                new_context.data.wayfire_set.workspace_id = comp_event->data.workspace.to_x;
            } else {
                new_context.model = WS_MODEL_SET_BASED;
                new_context.data.wayfire_set.set_id = comp_event->data.workspace.to_y;
                new_context.data.wayfire_set.workspace_id = comp_event->data.workspace.to_x;
            }
            monitor_handle_workspace_context_change(ctx, target_monitor, &new_context);
        } else {
            hyprlax_handle_workspace_change_2d(ctx,
                                               comp_event->data.workspace.from_x,
                                               comp_event->data.workspace.from_y,
                                               comp_event->data.workspace.to_x,
                                               comp_event->data.workspace.to_y);
        }
    } else {
        if (target_monitor) {
            workspace_context_t new_context = {
                .model = WS_MODEL_GLOBAL_NUMERIC,
                .data.workspace_id = comp_event->data.workspace.to_workspace
            };
            monitor_handle_workspace_context_change(ctx, target_monitor, &new_context);
        } else {
            hyprlax_handle_workspace_change(ctx,
                                            comp_event->data.workspace.to_workspace);
        }
    }
}

/* Create application context */
hyprlax_context_t* hyprlax_create(void) {
    hyprlax_context_t *ctx = calloc(1, sizeof(hyprlax_context_t));
    if (!ctx) {
        LOG_ERROR("Failed to allocate application context");
        return NULL;
    }

    ctx->state = APP_STATE_INITIALIZING;
    ctx->running = false;
    ctx->current_workspace = 1;
    ctx->current_monitor = 0;
    ctx->workspace_offset_x = 0.0f;
    ctx->workspace_offset_y = 0.0f;

    /* Cursor state defaults */
    ctx->cursor_event_fd = -1;
    ctx->cursor_norm_x = 0.0f;
    ctx->cursor_norm_y = 0.0f;
    ctx->cursor_ema_x = 0.0f;
    ctx->cursor_ema_y = 0.0f;
    ctx->cursor_last_time = 0.0;
    ctx->cursor_supported = false;

    /* Set default configuration */
    config_set_defaults(&ctx->config);

    input_clear_provider_registry();
    input_register_builtin_providers();
    if (input_manager_init(ctx, &ctx->input, &ctx->config) != HYPRLAX_SUCCESS) {
        LOG_WARN("Failed to initialize input manager scaffolding");
    }

    /* Set default backends */
    ctx->backends.renderer_backend = "auto";
    ctx->backends.platform_backend = "auto";
    ctx->backends.compositor_backend = "auto";

    /* Event loop defaults */
    ctx->epoll_fd = -1;
    ctx->frame_timer_fd = -1;
    ctx->debounce_timer_fd = -1;
    ctx->platform_event_fd = -1;
    ctx->compositor_event_fd = -1;
    ctx->ipc_event_fd = -1;
    ctx->frame_timer_armed = false;
    ctx->debounce_pending = false;

    return ctx;
}

/* Destroy application context */
void hyprlax_destroy(hyprlax_context_t *ctx) {
    if (!ctx) return;

    hyprlax_shutdown(ctx);

    /* Clean up configuration */
    config_cleanup(&ctx->config);

    free(ctx);
}

/* Legacy config parser removed (TOML-only runtime) */

/* Reload configuration (TOML only) and re-apply layers */
int hyprlax_reload_config(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;
    const char *path = ctx->config.config_path;
    char fallback_path[PATH_MAX];
    if (!path || !*path) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(fallback_path, sizeof(fallback_path), "%s/.config/hyprlax/parallax.conf", home);
            if (access(fallback_path, R_OK) == 0) {
                path = fallback_path;
            }
        }
    }
    if (!path || !*path) {
        return HYPRLAX_ERROR_FILE_NOT_FOUND;
    }
    /* Clear layers */
    while (ctx->layers) {
        uint32_t id = ctx->layers->id;
        hyprlax_remove_layer(ctx, id);
    }
    /* TOML only */
    const char *ext = strrchr(path, '.');
    if (ext && strcasecmp(ext, ".toml") == 0) {
        int rc = config_apply_toml_to_context(ctx, path);
        if (rc == HYPRLAX_SUCCESS) {
            input_manager_apply_config(&ctx->input, &ctx->config);
            hyprlax_update_cursor_provider(ctx);
            if (ctx->frame_timer_fd >= 0) {
                arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
            }
            return HYPRLAX_SUCCESS;
        }
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    LOG_ERROR("Legacy config detected (%s). Please convert: hyprlax ctl convert-config %s ~/.config/hyprlax/hyprlax.toml --yes", path, path);
    return HYPRLAX_ERROR_INVALID_ARGS;
}

/* Parse command-line arguments */
static int parse_arguments(hyprlax_context_t *ctx, int argc, char **argv) {
    int init_trace = getenv("HYPRLAX_INIT_TRACE") && *getenv("HYPRLAX_INIT_TRACE") ? 1 : 0;
    if (init_trace) fprintf(stderr, "[INIT_TRACE] parse_arguments: begin argc=%d\n", argc);
    /* Honor environment variables for log levels */
    const char *trace_env = getenv("HYPRLAX_TRACE");
    const char *dbg_env = getenv("HYPRLAX_DEBUG");
    const char *verb_env = getenv("HYPRLAX_VERBOSE"); /* optional numeric override 0..4 */
    if (verb_env && *verb_env) {
        int v = atoi(verb_env);
        if (v < 0) v = 0;
        if (v > 4) v = 4;
        ctx->config.log_level = v;
        if (v >= 3) ctx->config.debug = true; /* map DEBUG/TRACE to debug behavior */
    } else if (trace_env && *trace_env && strcmp(trace_env, "0") != 0 && strcasecmp(trace_env, "false") != 0) {
        ctx->config.debug = true;
        ctx->config.log_level = 4; /* LOG_TRACE */
    } else if (dbg_env && *dbg_env && strcmp(dbg_env, "0") != 0 && strcasecmp(dbg_env, "false") != 0) {
        ctx->config.debug = true;
        ctx->config.log_level = 3; /* LOG_DEBUG */
    }
    input_source_selection_t cli_input_selection;
    input_source_selection_init(&cli_input_selection);
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"fps", required_argument, 0, 'f'},
        {"shift", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'd'},
        {"easing", required_argument, 0, 'e'},
        {"config", required_argument, 0, 'c'},
        {"debug", no_argument, 0, 'D'},
        {"debug-log", optional_argument, 0, 'L'},
        {"trace", no_argument, 0, 'T'},
        {"renderer", required_argument, 0, 'r'},
        {"platform", required_argument, 0, 'p'},
        {"compositor", required_argument, 0, 'C'},
        {"vsync", no_argument, 0, 'V'},
        {"verbose", required_argument, 0, 1020},
        {"idle-poll-rate", required_argument, 0, 1003},
        {"overflow", required_argument, 0, 1010},
        {"tile-x", no_argument, 0, 1011},
        {"tile-y", no_argument, 0, 1012},
        {"no-tile-x", no_argument, 0, 1013},
        {"no-tile-y", no_argument, 0, 1014},
        {"margin-px-x", required_argument, 0, 1015},
        {"margin-px-y", required_argument, 0, 1016},
        {"parallax", required_argument, 0, 1005},
        {"mouse-weight", required_argument, 0, 1006},
        {"workspace-weight", required_argument, 0, 1007},
        {"input", required_argument, 0, 1050},
        {"accumulate", no_argument, 0, 1040},
        {"trail-strength", required_argument, 0, 1041},
        {"non-interactive", no_argument, 0, 1030},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hvf:s:d:e:c:DL::r:p:C:VT",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [OPTIONS] [--layer <image:shift:opacity:blur[:#RRGGBB[:strength]]>...]\n", argv[0]);
                printf("\nOptions:\n");
                printf("  -h, --help                Show this help message\n");
                printf("  -v, --version             Show version information\n");
                printf("  -f, --fps <rate>          Target FPS (default: 60)\n");
                printf("  -s, --shift <pixels>      Shift amount per workspace (default: 50)\n");
                printf("  -d, --duration <seconds>  Animation duration (default: 1.0)\n");
                printf("  -e, --easing <type>       Easing function (default: cubic)\n");
                printf("  -c, --config <file>       Load configuration from file\n");
                printf("  -D, --debug               Enable debug output (INFO/DEBUG)\n");
                printf("  -L, --debug-log[=FILE]    Write debug output to file (default: /tmp/hyprlax-PID.log)\n");
                printf("      --trace               Enable trace output (most verbose)\n");
                printf("  -r, --renderer <backend>  Renderer backend (gles2, auto)\n");
                printf("  -p, --platform <backend>  Platform backend (wayland, auto)\n");
                printf("  -C, --compositor <backend> Compositor (hyprland, sway, generic, auto)\n");
                printf("  -V, --vsync               Enable VSync (default: off)\n");
                printf("      --verbose <level>     Log level: error|warn|info|debug|trace or 0..4\n");
                printf("      --input <spec>        Enable inputs, e.g. workspace,cursor:0.3\n");
                printf("      --mouse-weight <w>    Weight of cursor source (0..1)\n");
                printf("      --workspace-weight <w> Weight of workspace source (0..1)\n");
                printf("      --accumulate          Enable trails effect (accumulate frames)\n");
                printf("      --trail-strength <a>  Trail fade per frame (0..1, default: %.2f)\n", 0.12f);
                printf("  --idle-poll-rate <hz>     Polling rate when idle (default: 2.0 Hz)\n");
                printf("\nRender options:\n");
                printf("      --overflow <mode>     repeat_edge|repeat|repeat_x|repeat_y|none\n");
                printf("      --tile-x/--tile-y     Enable tiling per axis (overrides overflow on that axis)\n");
                printf("      --no-tile-x/--no-tile-y  Disable tiling per axis\n");
                printf("      --margin-px-x <px>    Extra horizontal safe margin (pixels)\n");
                printf("      --margin-px-y <px>    Extra vertical safe margin (pixels)\n");
                printf("\nEasing types:\n");
                printf("  linear, quad, cubic, quart, quint, sine, expo, circ,\n");
                printf("  back, elastic, bounce, snap\n");
                exit(0);  /* Exit successfully for help */

            case 'v':
                printf("hyprlax %s\n", HYPRLAX_VERSION);
                printf("Buttery-smooth parallax wallpaper daemon with support for multiple compositors, platforms and renderers\n");
                exit(0);  /* Exit successfully for version */

            case 'f':
                ctx->config.target_fps = atoi(optarg);
                break;

            case 's': {
                float value = atof(optarg);
                /* Auto-detect percentage vs pixels */
                if (value <= 10.0f) {
                    ctx->config.shift_percent = value;
                    ctx->config.shift_pixels = 0.0f;
                } else {
                    ctx->config.shift_pixels = value;
                    ctx->config.shift_percent = 0.0f;
                    LOG_WARN("Using deprecated pixel shift (%.0f). Use percentage (0-10)", value);
                }
                break;
            }

            case 'd':
                ctx->config.animation_duration = atof(optarg);
                break;

            case 'e':
                ctx->config.default_easing = easing_from_string(optarg);
                break;

            case 'c': {
                ctx->config.config_path = strdup(optarg);
                if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] parse_arguments: --config %s\n", optarg);
                const char *ext = strrchr(optarg, '.');
                if (!(ext && (strcasecmp(ext, ".toml") == 0))) {
                    LOG_ERROR("Legacy config detected: %s. Convert with: hyprlax ctl convert-config %s ~/.config/hyprlax/hyprlax.toml --yes", optarg, optarg);
                    return -1;
                }
                if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] parse_arguments: loading TOML\n");
                if (config_apply_toml_to_context(ctx, ctx->config.config_path) != HYPRLAX_SUCCESS) {
                    LOG_ERROR("Failed to load TOML config: %s", optarg);
                    return -1;
                }
                if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] parse_arguments: TOML loaded\n");
                break; }

            case 'D':
                ctx->config.debug = true;
                /* Propagate to components that check the env var */
                setenv("HYPRLAX_DEBUG", "1", 1);
                ctx->config.log_level = 3; /* LOG_DEBUG */
                break;

            case 'L':
                ctx->config.debug = true;  /* Debug log implies debug mode */
                setenv("HYPRLAX_DEBUG", "1", 1);
                if (optarg) {
                    ctx->config.debug_log_path = strdup(optarg);
                } else {
                    /* Default log file with timestamp */
                    char log_file[256];
                    snprintf(log_file, sizeof(log_file), HYPRLAX_DEBUG_LOG_TEMPLATE, getpid());
                    ctx->config.debug_log_path = strdup(log_file);
                }
                if (ctx->config.log_level < 3) ctx->config.log_level = 3; /* ensure LOG_DEBUG */
                break;

            case 'T': /* --trace */
                ctx->config.debug = true; /* trace implies debug behavior */
                setenv("HYPRLAX_DEBUG", "1", 1); /* enable legacy debug gating for adapters */
                setenv("HYPRLAX_TRACE", "1", 1);
                ctx->config.log_level = 4; /* LOG_TRACE */
                break;

            case 'r':
                ctx->backends.renderer_backend = optarg;
                break;

            case 'p':
                ctx->backends.platform_backend = optarg;
                break;

            case 'C':
                ctx->backends.compositor_backend = optarg;
                break;

            case 'V':
                ctx->config.vsync = true;
                break;

            case 1020: { /* --verbose */
                int lvl = -1;
                if (!strcmp(optarg, "error")) lvl = 0;
                else if (!strcmp(optarg, "warn") || !strcmp(optarg, "warning")) lvl = 1;
                else if (!strcmp(optarg, "info")) lvl = 2;
                else if (!strcmp(optarg, "debug")) lvl = 3;
                else if (!strcmp(optarg, "trace")) lvl = 4;
                else { lvl = atoi(optarg); if (lvl < 0) lvl = 0; if (lvl > 4) lvl = 4; }
                ctx->config.log_level = lvl;
                if (lvl >= 3) {
                    ctx->config.debug = true;
                    setenv("HYPRLAX_DEBUG", "1", 1);
                }
                if (lvl == 4) setenv("HYPRLAX_TRACE", "1", 1);
                break; }

            case 1001:  /* --primary-only */
                ctx->monitor_mode = MULTI_MON_PRIMARY;
                break;

            case 1002:  /* --monitor */
                /* TODO: Add specific monitor to list */
                ctx->monitor_mode = MULTI_MON_SPECIFIC;
                LOG_DEBUG("Monitor selection: %s", optarg);
                break;

            case 1003:  /* --idle-poll-rate */
                ctx->config.idle_poll_rate = atof(optarg);
                if (ctx->config.idle_poll_rate < HYPRLAX_IDLE_POLL_RATE_MIN || ctx->config.idle_poll_rate > HYPRLAX_IDLE_POLL_RATE_MAX) {
                    LOG_WARN("Invalid idle poll rate: %.1f, using default %.1f Hz", ctx->config.idle_poll_rate, HYPRLAX_IDLE_POLL_RATE_DEFAULT);
                    ctx->config.idle_poll_rate = HYPRLAX_IDLE_POLL_RATE_DEFAULT;
                }
                break;

            case 1005:  /* --parallax (legacy) */
                warn_legacy_parallax_usage("--parallax");
                /* No-op: legacy mode removed. Use --input or weights. */
                break;

            case 1006:  /* --mouse-weight */
                ctx->config.parallax_cursor_weight = atof(optarg);
                if (ctx->config.parallax_cursor_weight < 0.0f) ctx->config.parallax_cursor_weight = 0.0f;
                if (ctx->config.parallax_cursor_weight > 1.0f) ctx->config.parallax_cursor_weight = 1.0f;
                break;

            case 1007:  /* --workspace-weight */
                ctx->config.parallax_workspace_weight = atof(optarg);
                if (ctx->config.parallax_workspace_weight < 0.0f) ctx->config.parallax_workspace_weight = 0.0f;
                if (ctx->config.parallax_workspace_weight > 1.0f) ctx->config.parallax_workspace_weight = 1.0f;
                break;

            case 1050:  /* --input */
                if (input_source_selection_add_spec(&cli_input_selection, optarg) != HYPRLAX_SUCCESS) {
                    LOG_WARN("Invalid input specification: %s", optarg);
                }
                break;

            case 1040: /* --accumulate */
                ctx->config.render_accumulate = true;
                break;
            case 1041: /* --trail-strength */ {
                float v = atof(optarg); if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
                ctx->config.render_trail_strength = v; }
                break;

            case 1010: { /* --overflow */
                const char *s = optarg;
                int m = -1;
                if (!strcmp(s, "repeat_edge") || !strcmp(s, "clamp")) m = 0;
                else if (!strcmp(s, "repeat") || !strcmp(s, "tile")) m = 1;
                else if (!strcmp(s, "repeat_x") || !strcmp(s, "tilex")) m = 2;
                else if (!strcmp(s, "repeat_y") || !strcmp(s, "tiley")) m = 3;
                else if (!strcmp(s, "none") || !strcmp(s, "off")) m = 4;
                if (m >= 0) ctx->config.render_overflow_mode = m;
                else LOG_WARN("Unknown overflow mode: %s", s);
                break; }
            case 1011: /* --tile-x */
                ctx->config.render_tile_x = 1; break;
            case 1012: /* --tile-y */
                ctx->config.render_tile_y = 1; break;
            case 1013: /* --no-tile-x */
                ctx->config.render_tile_x = 0; break;
            case 1014: /* --no-tile-y */
                ctx->config.render_tile_y = 0; break;
            case 1015: /* --margin-px-x */
                ctx->config.render_margin_px_x = atof(optarg); break;
            case 1016: /* --margin-px-y */
                ctx->config.render_margin_px_y = atof(optarg); break;

            case 1004:  /* --disable-monitor */
                /* TODO: Add monitor to exclusion list */
                LOG_DEBUG("Excluding monitor: %s", optarg);
                break;

            case 1030: /* --non-interactive: handled in early main, ignore here */
                break;
            default:
                return -1;
        }
    }
    if (input_source_selection_modified(&cli_input_selection)) {
        input_source_selection_commit(&cli_input_selection, &ctx->config);
    }
    if (init_trace) fprintf(stderr, "[INIT_TRACE] parse_arguments: after getopt optind=%d argc=%d\n", optind, argc);

    /* Parse layer arguments */
    for (int i = optind; i < argc; i++) {
        if (init_trace) fprintf(stderr, "[INIT_TRACE] parse_arguments: tail arg[%d]=%s\n", i, argv[i]);
        if (strcmp(argv[i], "--layer") == 0 && i + 1 < argc) {
            /* Parse layer specification: image:shift:opacity:blur */
            char *layer_spec = argv[++i];
            char *image = strtok(layer_spec, ":");
            char *shift_str = strtok(NULL, ":");
            char *opacity_str = strtok(NULL, ":");
            char *blur_str = strtok(NULL, ":");
            char *tint_str = strtok(NULL, ":");
            char *tint_strength_str = strtok(NULL, ":");

            if (image) {
                float shift = shift_str ? atof(shift_str) : 1.0f;
                float opacity = opacity_str ? atof(opacity_str) : 1.0f;
                float blur = blur_str ? atof(blur_str) : 0.0f;

                hyprlax_add_layer(ctx, image, shift, opacity, blur);

                /* Apply tint if provided */
                if (tint_str) {
                    parallax_layer_t *last = ctx->layers;
                    while (last && last->next) last = last->next;
                    if (last) {
                        if (strcmp(tint_str, "none") == 0) {
                            last->tint_r = last->tint_g = last->tint_b = 1.0f;
                            last->tint_strength = 0.0f;
                        } else if (tint_str[0] == '#' && strlen(tint_str) == 7) {
                            char r[3] = {tint_str[1], tint_str[2], 0};
                            char g[3] = {tint_str[3], tint_str[4], 0};
                            char b[3] = {tint_str[5], tint_str[6], 0};
                            char *end = NULL;
                            long rv = strtol(r, &end, 16); if (end && *end) rv = 255;
                            long gv = strtol(g, &end, 16); if (end && *end) gv = 255;
                            long bv = strtol(b, &end, 16); if (end && *end) bv = 255;
                            last->tint_r = (float)rv / 255.0f;
                            last->tint_g = (float)gv / 255.0f;
                            last->tint_b = (float)bv / 255.0f;
                            float ts = 1.0f;
                            if (tint_strength_str && *tint_strength_str) {
                                ts = (float)atof(tint_strength_str);
                                if (ts < 0.0f) ts = 0.0f;
                                if (ts > 1.0f) ts = 1.0f;
                            }
                            last->tint_strength = ts;
                        }
                    }
                }
            }
        } else {
            /* Legacy: treat as image path */
            /* Check if file exists first */
            if (access(argv[i], F_OK) != 0) {
                LOG_ERROR("Image file not found: %s", argv[i]);
                return -1;
            }
            hyprlax_add_layer(ctx, argv[i], 1.0f, 1.0f, 0.0f);
        }
    }
    if (init_trace) fprintf(stderr, "[INIT_TRACE] parse_arguments: end\n");

    return 0;
}

/* Initialize platform module */
int hyprlax_init_platform(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Create platform instance via name mapping inside platform module */
    int ret = platform_create_by_name(&ctx->platform, ctx->backends.platform_backend);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to create platform adapter");
        return ret;
    }

    /* Initialize platform */
    ret = PLATFORM_INIT(ctx->platform);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to initialize platform");
        platform_destroy(ctx->platform);
        ctx->platform = NULL;
        return ret;
    }

    /* Connect to display */
    ret = PLATFORM_CONNECT(ctx->platform, NULL);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to connect to display");
        platform_destroy(ctx->platform);
        ctx->platform = NULL;
        return ret;
    }

    /* Share context with platform for monitor detection */
    if (ctx->platform->ops && ctx->platform->ops->set_context) {
        ctx->platform->ops->set_context(ctx);
    }

    LOG_DEBUG("Platform: %s", ctx->platform->ops->get_name());

    return HYPRLAX_SUCCESS;
}

/* Initialize compositor module */
int hyprlax_init_compositor(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Create compositor adapter via name mapping inside compositor module */
    int ret = compositor_create_by_name(&ctx->compositor, ctx->backends.compositor_backend);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to create compositor adapter");
        return ret;
    }

    /* Initialize compositor */
    ret = COMPOSITOR_INIT(ctx->compositor, ctx->platform);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to initialize compositor");
        compositor_destroy(ctx->compositor);
        ctx->compositor = NULL;
        return ret;
    }

    /* Connect IPC if available */
    if (ctx->compositor->ops->connect_ipc) {
        int ret = ctx->compositor->ops->connect_ipc(NULL);
        if (ret == HYPRLAX_SUCCESS && ctx->config.debug) {
            LOG_DEBUG("  IPC connected");
        }
    }

    if (ctx->config.debug) {
        LOG_INFO("Compositor: %s", ctx->compositor->ops->get_name());
        LOG_INFO("  Blur support: %s",
                COMPOSITOR_SUPPORTS_BLUR(ctx->compositor) ? "yes" : "no");
    }

    return HYPRLAX_SUCCESS;
}

/* Initialize renderer module */
int hyprlax_init_renderer(hyprlax_context_t *ctx) {
    if (!ctx || !ctx->platform) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Determine renderer backend */
    const char *backend = ctx->backends.renderer_backend;
    if (strcmp(backend, "auto") == 0) {
        backend = "gles2";  /* Default to OpenGL ES 2.0 */
    }

    /* Create renderer instance */
    int ret = renderer_create(&ctx->renderer, backend);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to create renderer");
        return ret;
    }

    /* Get actual window dimensions from platform */
    int actual_width = 1920;   /* Default fallback */
    int actual_height = 1080;

    if (ctx->platform && ctx->platform->ops && ctx->platform->ops->get_window_size) {
        ctx->platform->ops->get_window_size(&actual_width, &actual_height);
        LOG_DEBUG("[INIT] Window size: %dx%d", actual_width, actual_height);
    }

    /* Initialize renderer with native handles */
    renderer_config_t render_config = {
        .width = actual_width,
        .height = actual_height,
        .vsync = ctx->config.vsync,  /* Use config setting (default: off) */
        .target_fps = ctx->config.target_fps,
        .capabilities = 0,
    };

    void *native_display = PLATFORM_GET_NATIVE_DISPLAY(ctx->platform);
    void *native_window = PLATFORM_GET_NATIVE_WINDOW(ctx->platform);

    ret = RENDERER_INIT(ctx->renderer, native_display, native_window, &render_config);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to initialize renderer");
        renderer_destroy(ctx->renderer);
        ctx->renderer = NULL;
        return ret;
    }

    /* Mark renderer as initialized so runtime paths (IPC add, lazy loads)
     * can create textures. Previously this flag was never set, which
     * prevented textures for IPC-added layers from being loaded. */
    ctx->renderer->initialized = true;

    LOG_DEBUG("Renderer: %s", ctx->renderer->ops->get_name());

    return HYPRLAX_SUCCESS;
}

/* Initialize IPC server */
static int hyprlax_init_ipc(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    ctx->ipc_ctx = ipc_init();
    if (!ctx->ipc_ctx) {
        /* Check if failure was due to another instance running */
        /* The ipc_init() function already printed the error message */
        return HYPRLAX_ERROR_ALREADY_RUNNING;
    }

    /* Link IPC context to main context for runtime settings */
    ((ipc_context_t*)ctx->ipc_ctx)->app_context = ctx;

    if (ctx->config.debug) {
        LOG_INFO("IPC server initialized");
    }

    return HYPRLAX_SUCCESS;
}

/* Initialize application */
int hyprlax_init(hyprlax_context_t *ctx, int argc, char **argv) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;
    int init_trace = getenv("HYPRLAX_INIT_TRACE") && *getenv("HYPRLAX_INIT_TRACE") ? 1 : 0;
    if (init_trace) fprintf(stderr, "[INIT_TRACE] start\n");

    /* Parse arguments */
    if (parse_arguments(ctx, argc, argv) < 0) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    if (init_trace) fprintf(stderr, "[INIT_TRACE] after parse_arguments\n");

    /* Apply environment overrides (CLI > ENV > Config > Defaults overall). We read ENV now,
     * and reapply CLI overrides right after to ensure CLI wins. */
    {
        if (init_trace) fprintf(stderr, "[INIT_TRACE] before env overrides\n");
        const char *v;
        input_source_selection_t env_input_selection;
        input_source_selection_init(&env_input_selection);
        v = getenv("HYPRLAX_RENDER_FPS");
        if (v && *v) {
            int iv = atoi(v); if (iv > 0 && iv <= 240) ctx->config.target_fps = iv;
        }
        v = getenv("HYPRLAX_PARALLAX_SHIFT_PIXELS");
        if (v && *v) {
            float f = atof(v);
            if (f >= 0.0f) {
                /* Keep as pixels for backwards compatibility with env var */
                ctx->config.shift_pixels = f;
            }
        }
        v = getenv("HYPRLAX_PARALLAX_SHIFT_PERCENT");
        if (v && *v) {
            float f = atof(v);
            if (f >= 0.0f && f <= 100.0f) {
                ctx->config.shift_percent = f;
                ctx->config.shift_pixels = 0.0f;  /* Percent takes precedence */
            }
        }
        v = getenv("HYPRLAX_ANIMATION_DURATION");
        if (v && *v) {
            float f = atof(v); if (f > 0.0f) ctx->config.animation_duration = f;
        }
        v = getenv("HYPRLAX_ANIMATION_EASING");
        if (v && *v) {
            ctx->config.default_easing = easing_from_string(v);
        }
        v = getenv("HYPRLAX_RENDER_VSYNC");
        if (v && *v) {
            if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on")) ctx->config.vsync = true;
            else if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "off")) ctx->config.vsync = false;
        }
        v = getenv("HYPRLAX_RENDER_TILE_X");
        if (v && *v) {
            if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on")) ctx->config.render_tile_x = 1;
            else if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "off")) ctx->config.render_tile_x = 0;
        }
        v = getenv("HYPRLAX_RENDER_TILE_Y");
        if (v && *v) {
            if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on")) ctx->config.render_tile_y = 1;
            else if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "off")) ctx->config.render_tile_y = 0;
        }
        v = getenv("HYPRLAX_RENDER_MARGIN_PX_X");
        if (v && *v) {
            float f = atof(v); if (f >= 0.0f) ctx->config.render_margin_px_x = f;
        }
        v = getenv("HYPRLAX_RENDER_MARGIN_PX_Y");
        if (v && *v) {
            float f = atof(v); if (f >= 0.0f) ctx->config.render_margin_px_y = f;
        }
        /* Legacy HYPRLAX_PARALLAX_MODE removed; use HYPRLAX_PARALLAX_INPUT or sources weights. */
        v = getenv("HYPRLAX_PARALLAX_INPUT");
        if (v && *v) {
            if (input_source_selection_add_spec(&env_input_selection, v) == HYPRLAX_SUCCESS) {
                input_source_selection_commit(&env_input_selection, &ctx->config);
            }
        }
        v = getenv("HYPRLAX_PARALLAX_SOURCES_CURSOR_WEIGHT");
        if (v && *v) {
            float f = atof(v); if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f; ctx->config.parallax_cursor_weight = f;
        }
        v = getenv("HYPRLAX_PARALLAX_SOURCES_WORKSPACE_WEIGHT");
        if (v && *v) {
            float f = atof(v); if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f; ctx->config.parallax_workspace_weight = f;
        }
        v = getenv("HYPRLAX_PARALLAX_SOURCES_WINDOW_WEIGHT");
        if (v && *v) {
            float f = atof(v); if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f; ctx->config.parallax_window_weight = f;
        }
        v = getenv("HYPRLAX_RENDER_OVERFLOW");
        if (v && *v) {
            /* Map to overflow mode if recognized */
            extern int overflow_from_string_local(const char *s);
            int m = overflow_from_string_local(v);
            if (m != -2) ctx->config.render_overflow_mode = m;
        }
    }
    if (init_trace) fprintf(stderr, "[INIT_TRACE] after env overrides\n");

    /* Reapply CLI overrides for keys that may be overridden by ENV above */
    {
        if (init_trace) fprintf(stderr, "[INIT_TRACE] before CLI reapply\n");
        input_source_selection_t cli_reapply_selection;
        input_source_selection_init(&cli_reapply_selection);
        for (int i = 1; i < argc; i++) {
            const char *arg = argv[i];
            const char *valeq = NULL;
            const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
            if (!arg) continue;
            if (!strcmp(arg, "-f") || !strcmp(arg, "--fps") || !strncmp(arg, "--fps=", 6)) {
                valeq = arg_get_val_local(arg, next);
                if (valeq) { int iv = atoi(valeq); if (iv > 0 && iv <= 240) ctx->config.target_fps = iv; }
                if (!strncmp(arg, "--fps=", 6)) { /* inline consumed */ }
                else if (next) i++;
            } else if (!strcmp(arg, "-s") || !strcmp(arg, "--shift") || !strncmp(arg, "--shift=", 8)) {
                valeq = arg_get_val_local(arg, next);
                if (valeq) {
                    float f = atof(valeq);
                    if (f >= 0.0f) {
                        if (f <= 10.0f) {
                            ctx->config.shift_percent = f;
                            ctx->config.shift_pixels = 0.0f;
                        } else {
                            ctx->config.shift_pixels = f;
                            ctx->config.shift_percent = 0.0f;
                        }
                    }
                }
                if (!strncmp(arg, "--shift=", 8)) { } else if (next) i++;
            } else if (!strcmp(arg, "-d") || !strcmp(arg, "--duration") || !strncmp(arg, "--duration=", 11)) {
                valeq = arg_get_val_local(arg, next);
                if (valeq) { float f = atof(valeq); if (f > 0.0f) ctx->config.animation_duration = f; }
                if (!strncmp(arg, "--duration=", 11)) { } else if (next) i++;
            } else if (!strcmp(arg, "-e") || !strcmp(arg, "--easing") || !strncmp(arg, "--easing=", 10)) {
                valeq = arg_get_val_local(arg, next);
                if (valeq) { ctx->config.default_easing = easing_from_string(valeq); }
                if (!strncmp(arg, "--easing=", 10)) { } else if (next) i++;
            } else if (!strcmp(arg, "-V") || !strcmp(arg, "--vsync")) {
                ctx->config.vsync = true;
            } else if (!strncmp(arg, "--overflow=", 12) || !strcmp(arg, "--overflow")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) {
                    extern int overflow_from_string_local(const char *s);
                    int m = overflow_from_string_local(v2);
                    if (m != -2) ctx->config.render_overflow_mode = m;
                }
                if (!strchr(arg, '=') && next) i++;
            } else if (!strcmp(arg, "--tile-x")) {
                ctx->config.render_tile_x = 1;
            } else if (!strcmp(arg, "--tile-y")) {
                ctx->config.render_tile_y = 1;
            } else if (!strcmp(arg, "--no-tile-x")) {
                ctx->config.render_tile_x = 0;
            } else if (!strcmp(arg, "--no-tile-y")) {
                ctx->config.render_tile_y = 0;
            } else if (!strncmp(arg, "--margin-px-x=", 14) || !strcmp(arg, "--margin-px-x")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) { float f = atof(v2); if (f >= 0.0f) ctx->config.render_margin_px_x = f; }
                if (!strchr(arg, '=') && next) i++;
            } else if (!strncmp(arg, "--margin-px-y=", 14) || !strcmp(arg, "--margin-px-y")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) { float f = atof(v2); if (f >= 0.0f) ctx->config.render_margin_px_y = f; }
                if (!strchr(arg, '=') && next) i++;
            } else if (!strncmp(arg, "--parallax=", 11) || !strcmp(arg, "--parallax")) {
                warn_legacy_parallax_usage("--parallax");
                if (!strchr(arg, '=') && next) i++;
            } else if (!strncmp(arg, "--mouse-weight=", 15) || !strcmp(arg, "--mouse-weight")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) { float f = atof(v2); if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f; ctx->config.parallax_cursor_weight = f; }
                if (!strchr(arg, '=') && next) i++;
            } else if (!strncmp(arg, "--workspace-weight=", 20) || !strcmp(arg, "--workspace-weight")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) { float f = atof(v2); if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f; ctx->config.parallax_workspace_weight = f; }
                if (!strchr(arg, '=') && next) i++;
            } else if (!strncmp(arg, "--input=", 8) || !strcmp(arg, "--input")) {
                const char *v2 = strchr(arg, '=') ? strchr(arg, '=') + 1 : next;
                if (v2) {
                    if (input_source_selection_add_spec(&cli_reapply_selection, v2) != HYPRLAX_SUCCESS) {
                        LOG_WARN("Invalid input specification: %s", v2);
                    }
                }
                if (!strchr(arg, '=') && next) i++;
            }
        }
        if (input_source_selection_modified(&cli_reapply_selection)) {
            input_source_selection_commit(&cli_reapply_selection, &ctx->config);
        }
    }
    if (init_trace) fprintf(stderr, "[INIT_TRACE] after CLI reapply\n");

    input_manager_apply_config(&ctx->input, &ctx->config);
    hyprlax_update_cursor_provider(ctx);

    /* Initialize logging system */
    if (init_trace) fprintf(stderr, "[INIT_TRACE] before log_init\n");
    log_init(ctx->config.debug, ctx->config.debug_log_path);
    if (init_trace) fprintf(stderr, "[INIT_TRACE] after log_init\n");
    /* Apply explicit log level (supports --trace and HYPRLAX_VERBOSE) */
    extern void log_set_level(log_level_t level);
    if (ctx->config.log_level >= 0) {
        log_set_level((log_level_t)ctx->config.log_level);
    }
    if (ctx->config.debug_log_path) {
        LOG_INFO("Debug logging to file: %s", ctx->config.debug_log_path);
    }

    /* Initialize modules in order */
    int ret;

    /* 0. Initialize multi-monitor support */
    LOG_INFO("[INIT] Step 0: Initializing multi-monitor support");
    ctx->monitors = monitor_list_create();
    if (!ctx->monitors) {
        LOG_ERROR("[INIT] Failed to create monitor list");
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    ctx->monitor_mode = MULTI_MON_ALL;  /* Default: use all monitors */
    LOG_DEBUG("[INIT] Multi-monitor mode: %s",
            ctx->monitor_mode == MULTI_MON_ALL ? "ALL" :
            ctx->monitor_mode == MULTI_MON_PRIMARY ? "PRIMARY" : "SPECIFIC");

    /* 1. Initialize IPC server first to check for existing instances */
    if (ctx->config.ipc_enabled) {
        LOG_INFO("[INIT] Step 1: Initializing IPC");
        ret = hyprlax_init_ipc(ctx);
        if (ret != HYPRLAX_SUCCESS) {
            LOG_ERROR("[INIT] IPC initialization failed with code %d", ret);
            return ret;  /* Exit if another instance is running */
        }
    } else if (ctx->config.debug) {
        LOG_INFO("[INIT] IPC disabled by configuration");
    }

    /* 2. Platform (windowing system) */
    LOG_INFO("[INIT] Step 2: Initializing platform");
    ret = hyprlax_init_platform(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("[INIT] Platform initialization failed with code %d", ret);
        return ret;
    }

    /* 3. Compositor (IPC and features) */
    LOG_INFO("[INIT] Step 3: Initializing compositor");
    ret = hyprlax_init_compositor(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("[INIT] Compositor initialization failed with code %d", ret);
        return ret;
    }

    /* Cursor follow note for compositors without global cursor IPC */
    if (ctx->config.debug && ctx->config.cursor_follow_global && ctx->compositor) {
        if (!(ctx->compositor->ops && ctx->compositor->ops->get_cursor_position)) {
            LOG_INFO("Cursor follow: no compositor provider; using platform pointer if available");
        }
    }

    /* 3b. Ensure cursor provider state matches configuration */
    hyprlax_update_cursor_provider(ctx);

    /* 4. Create window */
    LOG_INFO("[INIT] Step 4: Creating window");
    window_config_t window_config = {
        .width = HYPRLAX_DEFAULT_WINDOW_W,
        .height = HYPRLAX_DEFAULT_WINDOW_H,
        .x = 0,
        .y = 0,
        .fullscreen = HYPRLAX_DEFAULT_WINDOW_FULLSCREEN,
        .borderless = HYPRLAX_DEFAULT_WINDOW_BORDERLESS,
        .title = "hyprlax",
        .app_id = "hyprlax",
    };

    ret = PLATFORM_CREATE_WINDOW(ctx->platform, &window_config);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("[INIT] Window creation failed with code %d", ret);
        return ret;
    }

    /* 5. Renderer (OpenGL context) */
    LOG_INFO("[INIT] Step 5: Initializing renderer");
    ret = hyprlax_init_renderer(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("[INIT] Renderer initialization failed with code %d", ret);
        return ret;
    }

    /* 6. Create EGL surfaces for all monitors now that renderer exists */
    LOG_INFO("[INIT] Step 6: Creating EGL surfaces for monitors");
    /* Ensure monitors are realized if outputs are already known */
    if (ctx->platform && ctx->platform->ops && ctx->platform->ops->realize_monitors) {
        ctx->platform->ops->realize_monitors();
    }
    if (ctx->monitors) {
        monitor_instance_t *monitor = ctx->monitors->head;
        while (monitor) {
            if (monitor->wl_egl_window && !monitor->egl_surface) {
                monitor->egl_surface = gles2_create_monitor_surface(monitor->wl_egl_window);
                if (monitor->egl_surface) {
                    LOG_DEBUG("Created EGL surface for monitor %s", monitor->name);
                } else {
                    LOG_ERROR("Failed to create EGL surface for monitor %s", monitor->name);
                }
            }
            monitor = monitor->next;
        }
    }

    /* 7. Load textures for all layers now that GL is initialized */
    LOG_INFO("[INIT] Step 7: Loading layer textures");
    ret = hyprlax_load_layer_textures(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_WARN("[INIT] Warning: Some textures failed to load");
        /* Continue anyway - we can still run with missing textures */
    }

    /* Layer surface is already created in Step 4 (window creation) for Wayland */
    /* No need to create it again */

    /* 8. Setup epoll/timerfd event loop */
    hyprlax_setup_epoll(ctx);

    ctx->state = APP_STATE_RUNNING;
    ctx->running = true;

    LOG_INFO("hyprlax initialized successfully");
    LOG_DEBUG("  FPS target: %d", ctx->config.target_fps);
    LOG_DEBUG("  Shift amount: %.1f pixels", ctx->config.shift_pixels);
    LOG_DEBUG("  Animation duration: %.1f seconds", ctx->config.animation_duration);
    LOG_DEBUG("  Easing: %s", easing_to_string(ctx->config.default_easing));
    LOG_DEBUG("  VSync: %s", ctx->config.vsync ? "enabled" : "disabled");
    LOG_DEBUG("  Idle poll rate: %.1f Hz (%.0fms)", ctx->config.idle_poll_rate, 1000.0 / ctx->config.idle_poll_rate);

    return HYPRLAX_SUCCESS;
}

/* Load texture moved to core/render_core.c */

/* Add a layer */
int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path,
                     float shift_multiplier, float opacity, float blur) {
    if (!ctx || !image_path) return HYPRLAX_ERROR_INVALID_ARGS;

    parallax_layer_t *new_layer = layer_create(image_path, shift_multiplier, opacity);
    if (!new_layer) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Apply global scale factor from config (already has good default from layer_create) */
    new_layer->content_scale = ctx->config.scale_factor;
    new_layer->scale_is_custom = false;

    /* Load texture if OpenGL is initialized */
    if (ctx->renderer && ctx->renderer->initialized) {
        int img_width, img_height;
        GLuint texture = load_texture(image_path, &img_width, &img_height);
        if (texture != 0) {
            new_layer->texture_id = texture;
            new_layer->width = img_width;
            new_layer->height = img_height;
            new_layer->texture_width = img_width;
            new_layer->texture_height = img_height;
        }
    }
    new_layer->blur_amount = blur;

    /* Assign default z-index if not explicitly set elsewhere:
     * - First layer is assigned z=0
     * - Subsequent layers get max_z + 10
     * IPC 'add' with explicit z will override this immediately after. */
    int maxz = INT_MIN;
    for (parallax_layer_t *it = ctx->layers; it; it = it->next) {
        if (it->z_index > maxz) maxz = it->z_index;
    }
    if (maxz == INT_MIN) new_layer->z_index = 0; else new_layer->z_index = maxz + 10;

    ctx->layers = layer_list_add(ctx->layers, new_layer);
    ctx->layer_count = layer_list_count(ctx->layers);

    LOG_DEBUG("Added layer: %s (shift=%.1f, opacity=%.1f, blur=%.1f)",
                image_path, shift_multiplier, opacity, blur);

    return HYPRLAX_SUCCESS;
}

/* Remove a layer by ID */
void hyprlax_remove_layer(hyprlax_context_t *ctx, uint32_t layer_id) {
    if (!ctx) return;
    /* Find layer to allow GL cleanup */
    parallax_layer_t *layer = layer_list_find(ctx->layers, layer_id);
    if (layer && layer->texture_id != 0) {
        GLuint tid = (GLuint)layer->texture_id;
        glDeleteTextures(1, &tid);
        layer->texture_id = 0;
    }
    /* Remove from linked list and update count */
    ctx->layers = layer_list_remove(ctx->layers, layer_id);
    ctx->layer_count = layer_list_count(ctx->layers);
}

/* moved to core/render_core.c: hyprlax_load_layer_textures */

/* Handle per-monitor workspace change */
void hyprlax_handle_monitor_workspace_change(hyprlax_context_t *ctx,
                                            const char *monitor_name,
                                            int new_workspace) {
    if (!ctx || !ctx->monitors || !monitor_name) return;

    /* Find the monitor */
    monitor_instance_t *monitor = monitor_list_find_by_name(ctx->monitors, monitor_name);
    if (!monitor) {
        /* If monitor not found, fall back to primary or first monitor */
        monitor = ctx->monitors->primary;
        if (!monitor && ctx->monitors->head) {
            monitor = ctx->monitors->head;
        }
        if (!monitor) return;
    }

    /* Handle workspace change for this specific monitor */
    monitor_handle_workspace_change(ctx, monitor, new_workspace);

    if (ctx->config.debug) {
        LOG_DEBUG("Monitor %s: workspace changed to %d", monitor->name, new_workspace);
    }
}

/* Handle workspace change (legacy - applies to primary monitor) */
void hyprlax_handle_workspace_change(hyprlax_context_t *ctx, int new_workspace) {
    if (!ctx) return;

    int delta = new_workspace - ctx->current_workspace;

    if (ctx->config.debug) {
        LOG_DEBUG("Workspace change: %d -> %d (delta=%d)", ctx->current_workspace, new_workspace, delta);
    }

    ctx->current_workspace = new_workspace;

    /* If we have monitors, update the primary monitor */
    if (ctx->monitors && ctx->monitors->primary) {
        monitor_handle_workspace_change(ctx, ctx->monitors->primary, new_workspace);
    }

    /* Calculate shift in pixels from percentage (use first monitor's width as reference) */
    float shift_pixels = monitor_effective_shift_px(&ctx->config, ctx->monitors ? ctx->monitors->head : NULL);

    /* Calculate target offset (for legacy single-surface mode) */
    float target_x = ctx->workspace_offset_x + (delta * shift_pixels);
    float target_y = ctx->workspace_offset_y;

    LOG_TRACE("Target offset: %.1f, %.1f (shift=%.1f px from %.1f%%)",
           target_x, target_y, shift_pixels, ctx->config.shift_percent);

    /* Update all layers with animation */
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        float layer_target_x = target_x * layer->shift_multiplier;
        float layer_target_y = target_y * layer->shift_multiplier;

        layer_update_offset(layer, layer_target_x, layer_target_y,
                          ctx->config.animation_duration,
                          ctx->config.default_easing);

        layer = layer->next;
    }

    ctx->workspace_offset_x = target_x;
    ctx->workspace_offset_y = target_y;
}

/* Handle 2D workspace change (for Wayfire, Niri, etc.) */
void hyprlax_handle_workspace_change_2d(hyprlax_context_t *ctx,
                                       int from_x, int from_y,
                                       int to_x, int to_y) {
    if (!ctx) return;

    int delta_x = to_x - from_x;
    int delta_y = to_y - from_y;

    if (ctx->config.debug) {
        LOG_DEBUG("2D Workspace change: (%d,%d) -> (%d,%d) (delta=%d,%d)",
                  from_x, from_y, to_x, to_y, delta_x, delta_y);
    }

    /* Calculate shift in pixels from percentage (use first monitor's width as reference) */
    float shift_pixels = monitor_effective_shift_px(&ctx->config, ctx->monitors ? ctx->monitors->head : NULL);

    /* Calculate target offset for both axes */
    float target_x = ctx->workspace_offset_x + (delta_x * shift_pixels);
    float target_y = ctx->workspace_offset_y + (delta_y * shift_pixels);

    LOG_TRACE("Target offset: (%.1f, %.1f) shift=%.1f px from %.1f%%",
           target_x, target_y, shift_pixels, ctx->config.shift_percent);

    /* Update all layers with animation for both axes */
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        float layer_target_x = target_x * layer->shift_multiplier;
        float layer_target_y = target_y * layer->shift_multiplier;

        layer_update_offset(layer, layer_target_x, layer_target_y,
                          ctx->config.animation_duration,
                          ctx->config.default_easing);

        layer = layer->next;
    }

    ctx->workspace_offset_x = target_x;
    ctx->workspace_offset_y = target_y;
}

/* time helpers moved; scale computations handled by renderer */

/* Update layers */
void hyprlax_update_layers(hyprlax_context_t *ctx, double current_time) {
    if (!ctx) return;

    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        layer_tick(layer, current_time);
        layer = layer->next;
    }
}

/* hyprlax_render_frame moved to core/render_core.c */

/* has_active_animations removed (handled in core/event_loop.c) */

/* no-op (run loop lives in core/event_loop.c) */

/* Handle resize */
void hyprlax_handle_resize(hyprlax_context_t *ctx, int width, int height) {
    if (!ctx || !ctx->renderer) return;

    if (ctx->renderer->ops->resize) {
        ctx->renderer->ops->resize(width, height);
    }

    if (ctx->config.debug) {
        LOG_INFO("Window resized: %dx%d", width, height);
    }
}

/* Shutdown application */
void hyprlax_shutdown(hyprlax_context_t *ctx) {
    if (!ctx) return;

    ctx->state = APP_STATE_SHUTTING_DOWN;
    ctx->running = false;

    /* Close event loop FDs first */
    if (ctx->frame_timer_fd >= 0) { close(ctx->frame_timer_fd); ctx->frame_timer_fd = -1; }
    if (ctx->debounce_timer_fd >= 0) { close(ctx->debounce_timer_fd); ctx->debounce_timer_fd = -1; }
    if (ctx->epoll_fd >= 0) { close(ctx->epoll_fd); ctx->epoll_fd = -1; }

    /* Destroy layers */
    if (ctx->layers) {
        layer_list_destroy(ctx->layers);
        ctx->layers = NULL;
    }

    input_manager_destroy(&ctx->input);

    /* Shutdown modules in reverse order */

    /* IPC server */
    if (ctx->ipc_ctx) {
        ipc_cleanup((ipc_context_t*)ctx->ipc_ctx);
        ctx->ipc_ctx = NULL;
    }

    /* Renderer */
    if (ctx->renderer) {
        renderer_destroy(ctx->renderer);
        ctx->renderer = NULL;
    }

    /* Compositor */
    if (ctx->compositor) {
        compositor_destroy(ctx->compositor);
        ctx->compositor = NULL;
    }

    /* Platform */
    if (ctx->platform) {
        platform_destroy(ctx->platform);
        ctx->platform = NULL;
    }

    /* Monitors */
    if (ctx->monitors) {
        monitor_list_destroy(ctx->monitors);
        ctx->monitors = NULL;
    }

    if (ctx->config.debug) {
        LOG_INFO("hyprlax shut down");
    }
}

/* Runtime property control (IPC bridge) */
static bool parse_bool(const char *v) {
    if (!v) return false;
    return (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 ||
            strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0);
}

int hyprlax_runtime_set_property(hyprlax_context_t *ctx, const char *property, const char *value) {
    if (!ctx || !property || !value) return -1;

    /* Per-layer property: layer.<id>.* */
    if (strncmp(property, "layer.", 6) == 0) {
        const char *p = property + 6;
        char *endptr = NULL;
        long lid = strtol(p, &endptr, 10);
        if (lid <= 0 || !endptr || *endptr != '.') return -1;
        const char *leaf = endptr + 1;
        parallax_layer_t *layer = layer_list_find(ctx->layers, (uint32_t)lid);
        if (!layer) return -1;
        if (strcmp(leaf, "hidden") == 0) { layer->hidden = parse_bool_local(value); return 0; }
        if (strcmp(leaf, "path") == 0) {
            /* Update image path and reload texture if renderer is active */
            char *newpath = strdup(value);
            if (!newpath) return -1;
            /* Attempt to load texture first to avoid losing old path on failure */
            GLuint new_tex = 0; int w=0, h=0;
            if (ctx->renderer && ctx->renderer->initialized) {
                extern GLuint load_texture(const char *path, int *width, int *height); /* forward decl */
                new_tex = load_texture(newpath, &w, &h);
                if (new_tex == 0) { free(newpath); return -1; }
            }
            /* Replace path */
            if (layer->image_path) free(layer->image_path);
            layer->image_path = newpath;
            /* Swap texture */
            if (ctx->renderer && ctx->renderer->initialized) {
                if (layer->texture_id) {
                    GLuint tid = (GLuint)layer->texture_id;
                    glDeleteTextures(1, &tid);
                }
                layer->texture_id = new_tex;
                layer->width = w;
                layer->height = h;
                layer->texture_width = w;
                layer->texture_height = h;
            }
            return 0;
        }
        if (strcmp(leaf, "blur") == 0) { layer->blur_amount = atof(value); return 0; }
        if (strcmp(leaf, "fit") == 0) { int m = fit_from_string_local(value); if (m < 0) return -1; layer->fit_mode = m; return 0; }
        if (strcmp(leaf, "content_scale") == 0) { layer->content_scale = atof(value); return 0; }
        if (strcmp(leaf, "align.x") == 0) { layer->align_x = atof(value); if (layer->align_x<0) layer->align_x=0; if (layer->align_x>1) layer->align_x=1; return 0; }
        if (strcmp(leaf, "align.y") == 0) { layer->align_y = atof(value); if (layer->align_y<0) layer->align_y=0; if (layer->align_y>1) layer->align_y=1; return 0; }
        if (strcmp(leaf, "overflow") == 0) {
            int m = overflow_from_string_local(value);
            if (m == -2) return -1;
            layer->overflow_mode = m;
            return 0;
        }
        if (strcmp(leaf, "tile.x") == 0) { layer->tile_x = parse_bool_local(value) ? 1 : 0; return 0; }
        if (strcmp(leaf, "tile.y") == 0) { layer->tile_y = parse_bool_local(value) ? 1 : 0; return 0; }
        if (strcmp(leaf, "margin_px.x") == 0) { layer->margin_px_x = atof(value); return 0; }
        if (strcmp(leaf, "margin_px.y") == 0) { layer->margin_px_y = atof(value); return 0; }
        return -1;
    }

    /* Legacy parallax.mode removed; use parallax.input or sources weights instead. */
    if (strcmp(property, "parallax.input") == 0) {
        input_source_selection_t selection;
        input_source_selection_init(&selection);
        if (input_source_selection_add_spec(&selection, value) != HYPRLAX_SUCCESS ||
            !input_source_selection_modified(&selection)) {
            return -1;
        }
        input_source_selection_commit(&selection, &ctx->config);
        input_manager_apply_config(&ctx->input, &ctx->config);
        hyprlax_update_cursor_provider(ctx);
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
        return 0;
    }
    if (strcmp(property, "parallax.sources.cursor.weight") == 0) {
        ctx->config.parallax_cursor_weight = atof(value);
        if (ctx->config.parallax_cursor_weight < 0.0f) ctx->config.parallax_cursor_weight = 0.0f;
        if (ctx->config.parallax_cursor_weight > 1.0f) ctx->config.parallax_cursor_weight = 1.0f;
        input_manager_apply_config(&ctx->input, &ctx->config);
        hyprlax_update_cursor_provider(ctx);
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
        return 0;
    }
    if (strcmp(property, "parallax.sources.workspace.weight") == 0) {
        ctx->config.parallax_workspace_weight = atof(value);
        if (ctx->config.parallax_workspace_weight < 0.0f) ctx->config.parallax_workspace_weight = 0.0f;
        if (ctx->config.parallax_workspace_weight > 1.0f) ctx->config.parallax_workspace_weight = 1.0f;
        input_manager_apply_config(&ctx->input, &ctx->config);
        hyprlax_update_cursor_provider(ctx);
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
        return 0;
    }
    if (strcmp(property, "parallax.sources.window.weight") == 0) {
        ctx->config.parallax_window_weight = atof(value);
        if (ctx->config.parallax_window_weight < 0.0f) ctx->config.parallax_window_weight = 0.0f;
        if (ctx->config.parallax_window_weight > 1.0f) ctx->config.parallax_window_weight = 1.0f;
        input_manager_apply_config(&ctx->input, &ctx->config);
        if (ctx->frame_timer_fd >= 0) {
            arm_timerfd_ms(ctx->frame_timer_fd, 1, 0);
        }
        return 0;
    }
    if (strcmp(property, "parallax.invert.cursor.x") == 0) {
        ctx->config.invert_cursor_x = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.invert.cursor.y") == 0) {
        ctx->config.invert_cursor_y = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.invert.workspace.x") == 0) {
        ctx->config.invert_workspace_x = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.invert.workspace.y") == 0) {
        ctx->config.invert_workspace_y = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.invert.window.x") == 0) {
        ctx->config.invert_window_x = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.invert.window.y") == 0) {
        ctx->config.invert_window_y = parse_bool(value); return 0;
    }
    if (strcmp(property, "parallax.max_offset_px.x") == 0) {
        ctx->config.parallax_max_offset_x = atof(value); return 0;
    }
    if (strcmp(property, "parallax.max_offset_px.y") == 0) {
        ctx->config.parallax_max_offset_y = atof(value); return 0;
    }
    if (strcmp(property, "input.cursor.sensitivity_x") == 0) {
        ctx->config.cursor_sensitivity_x = atof(value); return 0;
    }
    if (strcmp(property, "input.cursor.sensitivity_y") == 0) {
        ctx->config.cursor_sensitivity_y = atof(value); return 0;
    }
    if (strcmp(property, "input.cursor.ema_alpha") == 0) {
        ctx->config.cursor_ema_alpha = atof(value); return 0;
    }
    if (strcmp(property, "input.cursor.deadzone_px") == 0) {
        ctx->config.cursor_deadzone_px = atof(value); return 0;
    }
    if (strcmp(property, "input.window.sensitivity_x") == 0) { ctx->config.window_sensitivity_x = atof(value); return 0; }
    if (strcmp(property, "input.window.sensitivity_y") == 0) { ctx->config.window_sensitivity_y = atof(value); return 0; }
    if (strcmp(property, "input.window.deadzone_px") == 0) { ctx->config.window_deadzone_px = atof(value); return 0; }
    if (strcmp(property, "input.window.ema_alpha") == 0) { ctx->config.window_ema_alpha = atof(value); return 0; }
    /* Render properties (global) */
    if (strcmp(property, "render.overflow") == 0) {
        int m = overflow_from_string_local(value);
        if (m == -2) return -1;
        ctx->config.render_overflow_mode = m;
        return 0;
    }
    if (strcmp(property, "render.tile.x") == 0) { ctx->config.render_tile_x = parse_bool_local(value) ? 1 : 0; return 0; }
    if (strcmp(property, "render.tile.y") == 0) { ctx->config.render_tile_y = parse_bool_local(value) ? 1 : 0; return 0; }
    if (strcmp(property, "render.margin_px.x") == 0) { ctx->config.render_margin_px_x = atof(value); return 0; }
    if (strcmp(property, "render.margin_px.y") == 0) { ctx->config.render_margin_px_y = atof(value); return 0; }
    return -1;
}

int hyprlax_runtime_get_property(hyprlax_context_t *ctx, const char *property, char *out, size_t out_size) {
    if (!ctx || !property || !out || out_size == 0) return -1;
    #define W(fmt, ...) do { snprintf(out, out_size, fmt, __VA_ARGS__); } while(0)
    /* Per-layer get: layer.<id>.* */
    if (strncmp(property, "layer.", 6) == 0) {
        const char *p = property + 6; char *end=NULL; long lid=strtol(p,&end,10);
        if (lid <= 0 || !end || *end != '.') return -1;
        const char *leaf = end+1;
        parallax_layer_t *layer = layer_list_find(ctx->layers, (uint32_t)lid);
        if (!layer) return -1;
        if (strcmp(leaf, "hidden") == 0) { W("%s", layer->hidden?"true":"false"); return 0; }
        if (strcmp(leaf, "blur") == 0) { W("%.2f", layer->blur_amount); return 0; }
        if (strcmp(leaf, "fit") == 0) { W("%s", fit_to_string_local(layer->fit_mode)); return 0; }
        if (strcmp(leaf, "content_scale") == 0) { W("%.3f", layer->content_scale); return 0; }
        if (strcmp(leaf, "align.x") == 0) { W("%.3f", layer->align_x); return 0; }
        if (strcmp(leaf, "align.y") == 0) { W("%.3f", layer->align_y); return 0; }
        if (strcmp(leaf, "overflow") == 0) {
            int eff = (layer->overflow_mode >= 0) ? layer->overflow_mode : ctx->config.render_overflow_mode;
            W("%s", overflow_to_string_local(eff)); return 0;
        }
        if (strcmp(leaf, "tile.x") == 0) {
            int over = (layer->overflow_mode >= 0) ? layer->overflow_mode : ctx->config.render_overflow_mode;
            int eff;
            if (layer->tile_x >= 0) eff = layer->tile_x;
            else if (over == 1 || over == 2) eff = 1;
            else if (over == 3) eff = 0;
            else eff = ctx->config.render_tile_x;
            W("%s", eff?"true":"false"); return 0;
        }
        if (strcmp(leaf, "tile.y") == 0) {
            int over = (layer->overflow_mode >= 0) ? layer->overflow_mode : ctx->config.render_overflow_mode;
            int eff;
            if (layer->tile_y >= 0) eff = layer->tile_y;
            else if (over == 1 || over == 3) eff = 1;
            else if (over == 2) eff = 0;
            else eff = ctx->config.render_tile_y;
            W("%s", eff?"true":"false"); return 0;
        }
        if (strcmp(leaf, "margin_px.x") == 0) { float eff = (layer->margin_px_x != 0.0f || layer->margin_px_y != 0.0f) ? layer->margin_px_x : ctx->config.render_margin_px_x; W("%.1f", eff); return 0; }
        if (strcmp(leaf, "margin_px.y") == 0) { float eff = (layer->margin_px_x != 0.0f || layer->margin_px_y != 0.0f) ? layer->margin_px_y : ctx->config.render_margin_px_y; W("%.1f", eff); return 0; }
        return -1;
    }
    /* Legacy parallax.mode removed */
    if (strcmp(property, "parallax.input") == 0) {
        char buffer[128] = "";
        size_t len = 0;
        struct { const char *name; float weight; } entries[] = {
            {"workspace", ctx->config.parallax_workspace_weight},
            {"cursor", ctx->config.parallax_cursor_weight},
            {"window", ctx->config.parallax_window_weight}
        };
        for (size_t i = 0; i < sizeof(entries)/sizeof(entries[0]); ++i) {
            if (entries[i].weight <= 0.0f) continue;
            len += snprintf(buffer + len, sizeof(buffer) - len,
                            len > 0 ? ",%s:%.3f" : "%s:%.3f",
                            entries[i].name, entries[i].weight);
            if (len >= sizeof(buffer)) break;
        }
        if (len == 0) {
            snprintf(buffer, sizeof(buffer), "none");
        }
        W("%s", buffer);
        return 0;
    }
    if (strcmp(property, "parallax.sources.cursor.weight") == 0) { W("%.3f", ctx->config.parallax_cursor_weight); return 0; }
    if (strcmp(property, "parallax.sources.workspace.weight") == 0) { W("%.3f", ctx->config.parallax_workspace_weight); return 0; }
    if (strcmp(property, "parallax.sources.window.weight") == 0) { W("%.3f", ctx->config.parallax_window_weight); return 0; }
    if (strcmp(property, "parallax.invert.cursor.x") == 0) { W("%s", ctx->config.invert_cursor_x?"true":"false"); return 0; }
    if (strcmp(property, "parallax.invert.cursor.y") == 0) { W("%s", ctx->config.invert_cursor_y?"true":"false"); return 0; }
    if (strcmp(property, "parallax.invert.workspace.x") == 0) { W("%s", ctx->config.invert_workspace_x?"true":"false"); return 0; }
    if (strcmp(property, "parallax.invert.workspace.y") == 0) { W("%s", ctx->config.invert_workspace_y?"true":"false"); return 0; }
    if (strcmp(property, "parallax.invert.window.x") == 0) { W("%s", ctx->config.invert_window_x?"true":"false"); return 0; }
    if (strcmp(property, "parallax.invert.window.y") == 0) { W("%s", ctx->config.invert_window_y?"true":"false"); return 0; }
    if (strcmp(property, "parallax.max_offset_px.x") == 0) { W("%.1f", ctx->config.parallax_max_offset_x); return 0; }
    if (strcmp(property, "parallax.max_offset_px.y") == 0) { W("%.1f", ctx->config.parallax_max_offset_y); return 0; }
    if (strcmp(property, "input.cursor.sensitivity_x") == 0) { W("%.3f", ctx->config.cursor_sensitivity_x); return 0; }
    if (strcmp(property, "input.cursor.sensitivity_y") == 0) { W("%.3f", ctx->config.cursor_sensitivity_y); return 0; }
    if (strcmp(property, "input.cursor.ema_alpha") == 0) { W("%.3f", ctx->config.cursor_ema_alpha); return 0; }
    if (strcmp(property, "input.cursor.deadzone_px") == 0) { W("%.1f", ctx->config.cursor_deadzone_px); return 0; }
    if (strcmp(property, "input.window.sensitivity_x") == 0) { W("%.3f", ctx->config.window_sensitivity_x); return 0; }
    if (strcmp(property, "input.window.sensitivity_y") == 0) { W("%.3f", ctx->config.window_sensitivity_y); return 0; }
    if (strcmp(property, "input.window.deadzone_px") == 0) { W("%.1f", ctx->config.window_deadzone_px); return 0; }
    if (strcmp(property, "input.window.ema_alpha") == 0) { W("%.3f", ctx->config.window_ema_alpha); return 0; }
    if (strcmp(property, "render.overflow") == 0) { W("%s", overflow_to_string_local(ctx->config.render_overflow_mode)); return 0; }
    if (strcmp(property, "render.tile.x") == 0) { W("%s", ctx->config.render_tile_x?"true":"false"); return 0; }
    if (strcmp(property, "render.tile.y") == 0) { W("%s", ctx->config.render_tile_y?"true":"false"); return 0; }
    if (strcmp(property, "render.margin_px.x") == 0) { W("%.1f", ctx->config.render_margin_px_x); return 0; }
    if (strcmp(property, "render.margin_px.y") == 0) { W("%.1f", ctx->config.render_margin_px_y); return 0; }
    #undef W
    return -1;
}
/* layer list stubs removed */
/* Helper to read either --opt=value or --opt value */
static inline const char* arg_get_val_local(const char *arg, const char *next) {
    if (!arg) return NULL;
    const char *eq = strchr(arg, '=');
    return eq ? (eq + 1) : next;
}
