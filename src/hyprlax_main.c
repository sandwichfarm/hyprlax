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
#include "include/hyprlax.h"
#include "include/hyprlax_internal.h"
#include "include/log.h"
#include "include/renderer.h"
#include "ipc.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GLES2/gl2.h>

#define MAX_CONFIG_LINE_SIZE 512

/* Forward declarations */
static int hyprlax_load_layer_textures(hyprlax_context_t *ctx);

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

    /* Set default configuration */
    config_set_defaults(&ctx->config);

    /* Set default backends */
    ctx->backends.renderer_backend = "auto";
    ctx->backends.platform_backend = "auto";
    ctx->backends.compositor_backend = "auto";

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

/* Resolve path relative to config file directory */
static char* resolve_config_relative_path(const char *config_path, const char *relative_path) {
    if (!relative_path) return NULL;

    /* If path is absolute, return a copy */
    if (relative_path[0] == '/') {
        return strdup(relative_path);
    }

    /* Get directory of config file */
    char *config_copy = strdup(config_path);
    if (!config_copy) return NULL;

    char *config_dir = dirname(config_copy);

    /* Construct full path */
    char full_path[PATH_MAX];
    snprintf( full_path, sizeof(full_path), "%s/%s", config_dir, relative_path);

    free(config_copy);

    /* Resolve to absolute path */
    char *resolved = realpath(full_path, NULL);
    if (!resolved) {
        /* If realpath fails, just return the constructed path */
        return strdup(full_path);
    }

    return resolved;
}

/* Parse config file */
static int parse_config_file(hyprlax_context_t *ctx, const char *filename) {
    if (!ctx || !filename) return -1;

    FILE *file = fopen(filename, "r");
    if (!file) {
        LOG_ERROR("Cannot open config file: %s", filename);
        return -1;
    }

    char line[MAX_CONFIG_LINE_SIZE];
    int line_num = 0;

    while (fgets(line, MAX_CONFIG_LINE_SIZE, file)) {
        line_num++;

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        /* Remove newline */
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        /* Parse tokens */
        char *cmd = strtok(line, " \t");
        if (!cmd) continue;

        if (strcmp(cmd, "layer") == 0) {
            char *image = strtok(NULL, " \t");
            char *shift_str = strtok(NULL, " \t");
            char *opacity_str = strtok(NULL, " \t");
            char *blur_str = strtok(NULL, " \t");

            if (!image) {
                LOG_ERROR("Config line %d: layer requires image path", line_num);
                continue;
            }

            /* Resolve path relative to config file if needed */
            char *resolved_image_path = resolve_config_relative_path(filename, image);
            if (!resolved_image_path) {
                LOG_ERROR("Failed to resolve image path at line %d: %s", line_num, image);
                continue;
            }

            float shift = shift_str ? atof(shift_str) : 1.0f;
            float opacity = opacity_str ? atof(opacity_str) : 1.0f;
            float blur = blur_str ? atof(blur_str) : 0.0f;

            LOG_DEBUG("Config parse layer: image=%s, shift=%.2f, opacity=%.2f, blur=%.2f",
                    resolved_image_path, shift, opacity, blur);

            /* Add the layer */
            hyprlax_add_layer(ctx, resolved_image_path, shift, opacity, blur);
            free(resolved_image_path);

        } else if (strcmp(cmd, "duration") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) ctx->config.animation_duration = atof(val);
        } else if (strcmp(cmd, "shift") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) ctx->config.shift_pixels = atof(val);
        } else if (strcmp(cmd, "scale") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) ctx->config.scale_factor = atof(val);
        } else if (strcmp(cmd, "fps") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) ctx->config.target_fps = atoi(val);
        } else if (strcmp(cmd, "easing") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) {
                ctx->config.default_easing = easing_from_string(val);
            }
        } else if (strcmp(cmd, "vsync") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) {
                ctx->config.vsync = (atoi(val) != 0);
            }
        } else if (strcmp(cmd, "idle_poll_rate") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) {
                ctx->config.idle_poll_rate = atof(val);
                if (ctx->config.idle_poll_rate < 0.1f || ctx->config.idle_poll_rate > 10.0f) {
                    ctx->config.idle_poll_rate = 2.0f;
                }
            }
        }
    }

    fclose(file);
    return 0;
}

/* Parse command-line arguments */
static int parse_arguments(hyprlax_context_t *ctx, int argc, char **argv) {
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
        {"renderer", required_argument, 0, 'r'},
        {"platform", required_argument, 0, 'p'},
        {"compositor", required_argument, 0, 'C'},
        {"vsync", no_argument, 0, 'V'},
        {"idle-poll-rate", required_argument, 0, 1003},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hvf:s:d:e:c:DL::r:p:C:V",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [OPTIONS] [--layer <image:shift:opacity:blur>...]\n", argv[0]);
                printf("\nOptions:\n");
                printf("  -h, --help                Show this help message\n");
                printf("  -v, --version             Show version information\n");
                printf("  -f, --fps <rate>          Target FPS (default: 60)\n");
                printf("  -s, --shift <pixels>      Shift amount per workspace (default: 150)\n");
                printf("  -d, --duration <seconds>  Animation duration (default: 1.0)\n");
                printf("  -e, --easing <type>       Easing function (default: cubic)\n");
                printf("  -c, --config <file>       Load configuration from file\n");
                printf("  -D, --debug               Enable debug output\n");
                printf("  -L, --debug-log[=FILE]    Write debug output to file (default: /tmp/hyprlax-PID.log)\n");
                printf("  -r, --renderer <backend>  Renderer backend (gles2, auto)\n");
                printf("  -p, --platform <backend>  Platform backend (wayland, auto)\n");
                printf("  -C, --compositor <backend> Compositor (hyprland, sway, generic, auto)\n");
                printf("  -V, --vsync               Enable VSync (default: off)\n");
                printf("  --idle-poll-rate <hz>     Polling rate when idle (default: 2.0 Hz)\n");
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

            case 's':
                ctx->config.shift_pixels = atof(optarg);
                break;

            case 'd':
                ctx->config.animation_duration = atof(optarg);
                break;

            case 'e':
                ctx->config.default_easing = easing_from_string(optarg);
                break;

            case 'c':
                ctx->config.config_path = strdup(optarg);
                if (parse_config_file(ctx, ctx->config.config_path) < 0) {
                    LOG_ERROR("Failed to load config file: %s", optarg);
                    return -1;
                }
                break;

            case 'D':
                ctx->config.debug = true;
                break;

            case 'L':
                ctx->config.debug = true;  /* Debug log implies debug mode */
                if (optarg) {
                    ctx->config.debug_log_path = strdup(optarg);
                } else {
                    /* Default log file with timestamp */
                    char log_file[256];
                    snprintf(log_file, sizeof(log_file), "/tmp/hyprlax-%d.log", getpid());
                    ctx->config.debug_log_path = strdup(log_file);
                }
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
                if (ctx->config.idle_poll_rate < 0.1f || ctx->config.idle_poll_rate > 10.0f) {
                    LOG_WARN("Invalid idle poll rate: %.1f, using default 2.0 Hz", ctx->config.idle_poll_rate);
                    ctx->config.idle_poll_rate = 2.0f;
                }
                break;

            case 1004:  /* --disable-monitor */
                /* TODO: Add monitor to exclusion list */
                LOG_DEBUG("Excluding monitor: %s", optarg);
                break;

            default:
                return -1;
        }
    }

    /* Parse layer arguments */
    for (int i = optind; i < argc; i++) {
        if (strcmp(argv[i], "--layer") == 0 && i + 1 < argc) {
            /* Parse layer specification: image:shift:opacity:blur */
            char *layer_spec = argv[++i];
            char *image = strtok(layer_spec, ":");
            char *shift_str = strtok(NULL, ":");
            char *opacity_str = strtok(NULL, ":");
            char *blur_str = strtok(NULL, ":");

            if (image) {
                float shift = shift_str ? atof(shift_str) : 1.0f;
                float opacity = opacity_str ? atof(opacity_str) : 1.0f;
                float blur = blur_str ? atof(blur_str) : 0.0f;

                hyprlax_add_layer(ctx, image, shift, opacity, blur);
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

    return 0;
}

/* Initialize platform module */
int hyprlax_init_platform(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Determine platform type */
    platform_type_t platform_type = PLATFORM_AUTO;
    if (strcmp(ctx->backends.platform_backend, "wayland") == 0) {
        platform_type = PLATFORM_WAYLAND;
    }

    /* Create platform instance */
    int ret = platform_create(&ctx->platform, platform_type);
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
    extern void wayland_set_context(hyprlax_context_t *ctx);
    if (ctx->platform->type == PLATFORM_WAYLAND) {
        wayland_set_context(ctx);
    }

    LOG_DEBUG("Platform: %s", ctx->platform->ops->get_name());

    return HYPRLAX_SUCCESS;
}

/* Initialize compositor module */
int hyprlax_init_compositor(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Determine compositor type */
    compositor_type_t compositor_type = COMPOSITOR_AUTO;
    if (strcmp(ctx->backends.compositor_backend, "hyprland") == 0) {
        compositor_type = COMPOSITOR_HYPRLAND;
    } else if (strcmp(ctx->backends.compositor_backend, "sway") == 0) {
        compositor_type = COMPOSITOR_SWAY;
    } else if (strcmp(ctx->backends.compositor_backend, "generic") == 0) {
        compositor_type = COMPOSITOR_GENERIC_WAYLAND;
    }

    /* Create compositor adapter */
    int ret = compositor_create(&ctx->compositor, compositor_type);
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

    if (ctx->platform->type == PLATFORM_WAYLAND) {
        extern void wayland_get_window_size(int *width, int *height);
        wayland_get_window_size(&actual_width, &actual_height);
        LOG_DEBUG("[INIT] Got window size from Wayland: %dx%d", actual_width, actual_height);
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

    /* Parse arguments */
    if (parse_arguments(ctx, argc, argv) < 0) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    /* Initialize logging system */
    log_init(ctx->config.debug, ctx->config.debug_log_path);
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
    LOG_INFO("[INIT] Step 1: Initializing IPC");
    ret = hyprlax_init_ipc(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        LOG_ERROR("[INIT] IPC initialization failed with code %d", ret);
        return ret;  /* Exit if another instance is running */
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

    /* 4. Create window */
    LOG_INFO("[INIT] Step 4: Creating window");
    window_config_t window_config = {
        .width = 1920,
        .height = 1080,
        .x = 0,
        .y = 0,
        .fullscreen = true,
        .borderless = true,
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

/* Load texture from file */
static GLuint load_texture(const char *path, int *width, int *height) {
    int channels;
    unsigned char *data = stbi_load(path, width, height, &channels, 4);
    if (!data) {
        LOG_ERROR("Failed to load image '%s': %s", path, stbi_failure_reason());
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    /* Use trilinear filtering for smoother animation */
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return texture;
}

/* Add a layer */
int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path,
                     float shift_multiplier, float opacity, float blur) {
    if (!ctx || !image_path) return HYPRLAX_ERROR_INVALID_ARGS;

    parallax_layer_t *new_layer = layer_create(image_path, shift_multiplier, opacity);
    if (!new_layer) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

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

    ctx->layers = layer_list_add(ctx->layers, new_layer);
    ctx->layer_count = layer_list_count(ctx->layers);

    LOG_DEBUG("Added layer: %s (shift=%.1f, opacity=%.1f, blur=%.1f)",
                image_path, shift_multiplier, opacity, blur);

    return HYPRLAX_SUCCESS;
}

/* Load textures for all layers (called after GL init) */
static int hyprlax_load_layer_textures(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    int loaded = 0;
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        if (layer->texture_id == 0 && layer->image_path) {
            int img_width, img_height;
            GLuint texture = load_texture(layer->image_path, &img_width, &img_height);
            if (texture != 0) {
                layer->texture_id = texture;
                layer->width = img_width;
                layer->height = img_height;
                layer->texture_width = img_width;
                layer->texture_height = img_height;
                loaded++;
                if (ctx->config.debug) {
                    LOG_DEBUG("Loaded texture for layer: %s (%dx%d)",
                            layer->image_path, img_width, img_height);
                }
            } else {
                LOG_ERROR("Failed to load texture for layer: %s", layer->image_path);
            }
        }
        layer = layer->next;
    }

    if (ctx->config.debug && loaded > 0) {
        LOG_INFO("Loaded %d layer textures", loaded);
    }

    return HYPRLAX_SUCCESS;
}

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
        fprintf(stderr, "[DEBUG] Monitor %s: workspace changed to %d\n",
                monitor->name, new_workspace);
    }
}

/* Handle workspace change (legacy - applies to primary monitor) */
void hyprlax_handle_workspace_change(hyprlax_context_t *ctx, int new_workspace) {
    if (!ctx) return;

    int delta = new_workspace - ctx->current_workspace;

    if (ctx->config.debug) {
        fprintf(stderr, "[DEBUG] Workspace change: %d -> %d (delta=%d)\n",
                ctx->current_workspace, new_workspace, delta);
    }

    ctx->current_workspace = new_workspace;

    /* If we have monitors, update the primary monitor */
    if (ctx->monitors && ctx->monitors->primary) {
        monitor_handle_workspace_change(ctx, ctx->monitors->primary, new_workspace);
    }

    /* Calculate target offset (for legacy single-surface mode) */
    float target_x = ctx->workspace_offset_x + (delta * ctx->config.shift_pixels);
    float target_y = ctx->workspace_offset_y;

    LOG_TRACE("Target offset: %.1f, %.1f (shift=%.1f)",
           target_x, target_y, ctx->config.shift_pixels);

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
        fprintf(stderr, "[DEBUG] 2D Workspace change: (%d,%d) -> (%d,%d) (delta=%d,%d)\n",
               from_x, from_y, to_x, to_y, delta_x, delta_y);
    }

    /* Calculate target offset for both axes */
    float target_x = ctx->workspace_offset_x + (delta_x * ctx->config.shift_pixels);
    float target_y = ctx->workspace_offset_y + (delta_y * ctx->config.shift_pixels);

    LOG_TRACE("Target offset: (%.1f, %.1f) shift=%.1f",
           target_x, target_y, ctx->config.shift_pixels);

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

/* Get current time in seconds */
static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

/* Calculate optimal scale factor for parallax effect */
static float calculate_scale_factor(float shift_pixels, int max_workspaces, int viewport_width) {
    /* Scale factor determines how much larger the image is than the viewport
     * We need: image_width = viewport_width * scale_factor
     * And: (scale_factor - 1) * viewport_width >= total_shift_needed */
    float total_shift_needed = (max_workspaces - 1) * shift_pixels;
    float min_scale_factor = 1.0f + (total_shift_needed / (float)viewport_width);

    /* Return at least 1.5x for a nice parallax effect */
    return fmaxf(min_scale_factor, 1.5f);
}

/* Update layers */
void hyprlax_update_layers(hyprlax_context_t *ctx, double current_time) {
    if (!ctx) return;

    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        layer_tick(layer, current_time);
        layer = layer->next;
    }
}

/* Render frame to a specific monitor */
static void hyprlax_render_monitor(hyprlax_context_t *ctx, monitor_instance_t *monitor) {
    if (!ctx || !ctx->renderer || !monitor) {
        LOG_TRACE("Skipping render: ctx=%p, renderer=%p, monitor=%p", ctx, ctx ? ctx->renderer : NULL, monitor);
        return;
    }

    /* Skip if monitor doesn't have an EGL surface */
    if (!monitor->egl_surface) {
        LOG_WARN("Monitor %s has no EGL surface", monitor->name);
        return;
    }

    /* Monitor rendering debug - commented out for performance
    LOG_TRACE("Rendering to monitor %s (%dx%d, surface=%p)",
              monitor->name, monitor->width, monitor->height, monitor->egl_surface); */

    /* Switch to this monitor's EGL surface */
    if (gles2_make_current(monitor->egl_surface) != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to make EGL surface current for monitor %s", monitor->name);
        return;
    }

    /* Set viewport for this monitor */
    glViewport(0, 0, monitor->width * monitor->scale, monitor->height * monitor->scale);
    /* Viewport debug - commented out for performance
    LOG_TRACE("Set viewport: %dx%d (scale=%d)",
              monitor->width * monitor->scale, monitor->height * monitor->scale, monitor->scale); */

    /* Optional profiling */
    static int s_profile = -1;
    if (s_profile == -1) {
        const char *p = getenv("HYPRLAX_PROFILE");
        s_profile = (p && *p) ? 1 : 0;
    }
    double t_draw_start = 0.0;
    double t_present_start = 0.0;

    /* Clear and prepare for rendering */
    RENDERER_BEGIN_FRAME(ctx->renderer);
    RENDERER_CLEAR(ctx->renderer, 0.0f, 0.0f, 0.0f, 1.0f);

    /* Enable/disable blending only when needed to avoid redundant state changes */
    static bool s_blend_enabled = false;
    bool need_blend = (ctx->layer_count > 1);
    if (need_blend != s_blend_enabled) {
        if (need_blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        s_blend_enabled = need_blend;
    }

    /* Calculate scale factor for this monitor */
    float scale_factor = ctx->config.scale_factor;
    if (scale_factor <= 1.0f) {
        scale_factor = calculate_scale_factor(
            ctx->config.shift_pixels, 10, monitor->width);
    }

    /* Render each layer with this monitor's offset */
    if (s_profile) t_draw_start = get_time();
    parallax_layer_t *layer = ctx->layers;
    int layer_num = 0;
    /* Layer count debug - commented out for performance
    LOG_TRACE("Rendering %d layers for monitor %s", ctx->layer_count, monitor->name); */
    while (layer) {
        if (layer->texture_id == 0) {
            layer = layer->next;
            continue;
        }

        /* Use the layer's own animated offset */
        float offset_x = layer->offset_x;
        float offset_y = layer->offset_y;

        /* Create texture struct for drawing */
        texture_t tex = {
            .id = layer->texture_id,
            .width = layer->width,
            .height = layer->height
        };

        /* Bind texture */
        if (ctx->renderer->ops->bind_texture) {
            ctx->renderer->ops->bind_texture(&tex, 0);
        }

        if (ctx->renderer->ops->draw_layer) {
            /* Draw the layer with offset and effects */
            /* Layer drawing debug - commented out for performance
            LOG_TRACE("Drawing layer %d (tex_id=%u, offset=%.3f,%.3f, opacity=%.2f)",
                     layer_num++, layer->texture_id,
                     offset_x / monitor->width, offset_y / monitor->height, layer->opacity); */
            layer_num++;
            ctx->renderer->ops->draw_layer(
                &tex,
                offset_x / monitor->width,  /* Normalized X offset */
                offset_y / monitor->height, /* Normalized Y offset */
                layer->opacity,
                layer->blur_amount
            );
        }

        layer = layer->next;
    }

    /* Present this monitor's frame */
    /* Present debug - commented out for performance
    LOG_TRACE("Ending frame and presenting for monitor %s", monitor->name); */
    RENDERER_END_FRAME(ctx->renderer);
    double t_draw_end = s_profile ? get_time() : 0.0;
    if (s_profile) t_present_start = t_draw_end;
    RENDERER_PRESENT(ctx->renderer);
    double t_present_end = s_profile ? get_time() : 0.0;

    if (s_profile && ctx->config.debug) {
        double draw_ms = (t_draw_end - t_draw_start) * 1000.0;
        double present_ms = (t_present_end - t_present_start) * 1000.0;
        LOG_DEBUG("[PROFILE] monitor=%s draw=%.2f ms present=%.2f ms", monitor->name, draw_ms, present_ms);
    }

    /* Commit the Wayland surface to make the frame visible */
    if (monitor->wl_surface) {
        /* Need platform to commit the surface */
        extern void wayland_commit_monitor_surface(monitor_instance_t *monitor);
        wayland_commit_monitor_surface(monitor);
        /* Commit debug - commented out for performance
        LOG_TRACE("Committed surface for monitor %s", monitor->name); */
    }

    /* Frame presented debug - commented out for performance
    LOG_TRACE("Frame presented for monitor %s", monitor->name); */
}

/* Render frame to all monitors */
void hyprlax_render_frame(hyprlax_context_t *ctx) {
    if (!ctx || !ctx->renderer) {
        LOG_ERROR("render_frame: No renderer available");
        return;
    }
    
    /* Track if this frame is actually different from last frame (unused placeholder removed) */

    /* Always use monitor list - single monitor is just count=1 */
    if (!ctx->monitors || ctx->monitors->count == 0) {
        LOG_WARN("No monitors available for rendering");
        return;
    }

    /* Monitor count debug - commented out for performance
    LOG_TRACE("Rendering frame to %d monitor(s)", ctx->monitors->count); */

    /* Render to each monitor */
    monitor_instance_t *monitor = ctx->monitors->head;
    while (monitor) {
        /* Monitor call debug - commented out for performance
        LOG_TRACE("Calling hyprlax_render_monitor for %s", monitor->name); */
        hyprlax_render_monitor(ctx, monitor);
        monitor = monitor->next;
    }

    /* After presenting a frame, diagnostics no longer snapshot prev offsets
       to avoid touching core layer structs. */
}

/* Check if any layer has active animations */
static bool has_active_animations(hyprlax_context_t *ctx) {
    if (!ctx) return false;
    
    /* Check layer animations */
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        if (animation_is_active(&layer->x_animation) || 
            animation_is_active(&layer->y_animation)) {
            return true;
        }
        layer = layer->next;
    }
    
    /* Check monitor animations */
    if (ctx->monitors) {
        monitor_instance_t *monitor = ctx->monitors->head;
        while (monitor) {
            if (monitor->animating) {
                return true;
            }
            monitor = monitor->next;
        }
    }
    
    return false;
}

/* Main run loop */
int hyprlax_run(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    if (ctx->config.debug) {
        LOG_DEBUG("Starting main loop (target FPS: %d)", ctx->config.target_fps);
    }

    /* Optional render diagnostics (why are we rendering while idle) */
    static int s_render_diag = -1;
    if (s_render_diag == -1) {
        const char *p = getenv("HYPRLAX_RENDER_DIAG");
        s_render_diag = (p && *p) ? 1 : 0;
    }

    double last_render_time = get_time();
    double last_frame_time = last_render_time;
    double frame_time = 1.0 / ctx->config.target_fps;
    int frame_count = 0;
    double debug_timer = 0.0;
    bool needs_render = true;  /* Render first frame */
    
    if (ctx->config.debug) {
        LOG_DEBUG("Frame time calculated: %.6f seconds (target FPS: %d)", 
                  frame_time, ctx->config.target_fps);
    }

    while (ctx->running) {
        /* Reset per-iteration diagnostic flags */
        bool diag_resize = false;
        bool diag_ipc = false;
        bool diag_comp = false;
        bool diag_final = false;

        double current_time = get_time();
        ctx->delta_time = current_time - last_frame_time;
        last_frame_time = current_time;

        /* Frame timing debug - disabled for performance
        if (ctx->config.debug && frame_count < 5) {
            LOG_TRACE("Frame %d: delta=%.4f, frame_time=%.4f",
                   frame_count, ctx->delta_time, frame_time);
        } */

        /* Poll platform events */
        platform_event_t platform_event;
        /* Platform events debug - disabled for performance
        if (ctx->config.debug && frame_count < 5) {
            LOG_TRACE("Polling platform events");
        } */
        if (PLATFORM_POLL_EVENTS(ctx->platform, &platform_event) == HYPRLAX_SUCCESS) {
            switch (platform_event.type) {
                case PLATFORM_EVENT_CLOSE:
                    ctx->running = false;
                    break;
                case PLATFORM_EVENT_RESIZE:
                    hyprlax_handle_resize(ctx,
                                        platform_event.data.resize.width,
                                        platform_event.data.resize.height);
                    needs_render = true;  /* Window resize requires re-render */
                    diag_resize = true;
                    break;
                default:
                    break;
            }
        }

        /* Poll IPC commands */
        if (ctx->ipc_ctx && ipc_process_commands((ipc_context_t*)ctx->ipc_ctx)) {
            /* IPC processed - layers may have changed */
            needs_render = true;  /* IPC commands may have changed layers */
            diag_ipc = true;
            if (ctx->config.debug) {
                LOG_TRACE("IPC command processed");
            }
        }

        /* Poll compositor events */
        if (ctx->compositor && ctx->compositor->ops->poll_events) {
            compositor_event_t comp_event;
            int poll_result = ctx->compositor->ops->poll_events(&comp_event);
            if (poll_result == HYPRLAX_SUCCESS) {
                if (comp_event.type == COMPOSITOR_EVENT_WORKSPACE_CHANGE) {
                    needs_render = true;  /* Workspace change requires re-render */
                    diag_comp = true;
                    /* For multi-monitor support, find the correct monitor */
                    monitor_instance_t *target_monitor = NULL;

                    if (ctx->monitors && comp_event.data.workspace.monitor_name[0] != '\0') {
                        /* Use monitor name from event if available */
                        target_monitor = monitor_list_find_by_name(ctx->monitors,
                                                                  comp_event.data.workspace.monitor_name);
                        if (ctx->config.debug && target_monitor) {
                            LOG_TRACE("Workspace event for monitor: %s",
                                   comp_event.data.workspace.monitor_name);
                        }
                    }

                    /* Fall back to primary/first monitor if not found */
                    if (!target_monitor && ctx->monitors) {
                        target_monitor = monitor_list_get_primary(ctx->monitors);
                        if (!target_monitor) {
                            target_monitor = ctx->monitors->head;
                        }
                    }

                    /* Check if this is a 2D workspace change */
                    if (comp_event.data.workspace.from_x != 0 ||
                        comp_event.data.workspace.from_y != 0 ||
                        comp_event.data.workspace.to_x != 0 ||
                        comp_event.data.workspace.to_y != 0) {
                        /* 2D workspace change */
                        if (ctx->config.debug) {
                            LOG_DEBUG("Main loop: 2D Workspace event detected");
                            LOG_DEBUG("  From: workspace %d at (%d,%d)",
                                   comp_event.data.workspace.from_workspace,
                                   comp_event.data.workspace.from_x,
                                   comp_event.data.workspace.from_y);
                            LOG_DEBUG("  To: workspace %d at (%d,%d)",
                                   comp_event.data.workspace.to_workspace,
                                   comp_event.data.workspace.to_x,
                                   comp_event.data.workspace.to_y);
                            LOG_DEBUG("  Monitor: %s", target_monitor ? target_monitor->name : "unknown");
                        }

                        if (target_monitor) {
                            /* Update monitor's workspace context for 2D compositors */
                            /* Detect the actual model based on compositor type */
                            workspace_model_t model = workspace_detect_model(
                                ctx->compositor ? ctx->compositor->type : COMPOSITOR_AUTO);

                            if (ctx->config.debug) {
                                LOG_DEBUG("  Detected model: %s", workspace_model_to_string(model));
                            }

                            workspace_context_t new_context;
                            new_context.model = model;

                            if (model == WS_MODEL_PER_OUTPUT_NUMERIC) {
                                /* Niri: encode 2D position as linear workspace ID */
                                /* workspace_id = y * MAX_COLUMNS + x */
                                /* MAX_COLUMNS=1000 to prevent overlap between dimensions */
                                new_context.data.workspace_id = comp_event.data.workspace.to_y * 1000 +
                                                               comp_event.data.workspace.to_x;
                                if (ctx->config.debug) {
                                    LOG_DEBUG("  Niri: Encoded (%d,%d) as workspace ID %d",
                                           comp_event.data.workspace.to_x,
                                           comp_event.data.workspace.to_y,
                                           new_context.data.workspace_id);
                                }
                            } else if (model == WS_MODEL_SET_BASED) {
                                /* Wayfire: use set and workspace within set */
                                new_context.data.wayfire_set.set_id = comp_event.data.workspace.to_y;
                                new_context.data.wayfire_set.workspace_id = comp_event.data.workspace.to_x;
                                if (ctx->config.debug) {
                                    LOG_DEBUG("  Wayfire: Set %d, workspace %d",
                                           new_context.data.wayfire_set.set_id,
                                           new_context.data.wayfire_set.workspace_id);
                                }
                            } else {
                                /* Fallback for unknown 2D models */
                                new_context.model = WS_MODEL_SET_BASED;
                                new_context.data.wayfire_set.set_id = comp_event.data.workspace.to_y;
                                new_context.data.wayfire_set.workspace_id = comp_event.data.workspace.to_x;
                                if (ctx->config.debug) {
                                    LOG_DEBUG("  Fallback: Using SET_BASED model");
                                }
                            }

                            if (ctx->config.debug) {
                                LOG_DEBUG("  Calling monitor_handle_workspace_context_change");
                            }
                            monitor_handle_workspace_context_change(ctx, target_monitor, &new_context);
                        } else {
                            /* Fallback to deprecated global handler if no monitor found */
                            hyprlax_handle_workspace_change_2d(ctx,
                                                             comp_event.data.workspace.from_x,
                                                             comp_event.data.workspace.from_y,
                                                             comp_event.data.workspace.to_x,
                                                             comp_event.data.workspace.to_y);
                        }
                    } else {
                        /* Linear workspace change (Hyprland, Sway, etc.) */
                        if (ctx->config.debug) {
                            LOG_DEBUG("Workspace changed from %d to %d on %s",
                                   comp_event.data.workspace.from_workspace,
                                   comp_event.data.workspace.to_workspace,
                                   target_monitor ? target_monitor->name : "unknown");
                        }

                        if (target_monitor) {
                            /* Update monitor's workspace context */
                            workspace_context_t new_context = {
                                .model = WS_MODEL_GLOBAL_NUMERIC,
                                .data.workspace_id = comp_event.data.workspace.to_workspace
                            };
                            monitor_handle_workspace_context_change(ctx, target_monitor, &new_context);
                        } else {
                            /* Fallback to global handler */
                            hyprlax_handle_workspace_change(ctx,
                                                          comp_event.data.workspace.to_workspace);
                        }
                    }
                }
            }
        }

        /* Check if animations are active */
        bool animations_active = has_active_animations(ctx);
        
        /* While animating, always need to render; with frame callbacks, wait for compositor pacing */
        const char *use_fc = getenv("HYPRLAX_FRAME_CALLBACK");
        if (animations_active) {
            if (use_fc && *use_fc && ctx->monitors) {
                /* Render when at least one monitor's frame callback has fired */
                bool can_render = false;
                monitor_instance_t *m = ctx->monitors->head;
                while (m) {
                    if (!m->frame_pending) { can_render = true; break; }
                    m = m->next;
                }
                needs_render = needs_render || can_render;
            } else {
                needs_render = true;
            }
        }
        
        /* Only update animations if they're active */
        if (animations_active) {
            /* Update layer animations */
            hyprlax_update_layers(ctx, current_time);

            /* Update monitor animations for parallax */
            if (ctx->monitors) {
                monitor_instance_t *monitor = ctx->monitors->head;
                while (monitor) {
                    monitor_update_animation(monitor, current_time);
                    monitor = monitor->next;
                }
            }
            
            /* Re-check if animations are still active after update */
            bool still_animating = has_active_animations(ctx);
            
            /* Ensure one final frame when animations complete */
            if (!still_animating && animations_active) {
                needs_render = true;  /* Final frame */
                diag_final = true;
            }
            
            /* Update the animations_active flag for next iteration */
            animations_active = still_animating;
        }

        /* Calculate time since last render */
        double time_since_render = current_time - last_render_time;
        
        /* Render frame if:
         * 1. We need to render (animation active, workspace changed, etc.)
         * 2. Enough time has passed since last frame (respecting target FPS)
         */
        if (needs_render && ((use_fc && *use_fc) ? true : (time_since_render >= frame_time))) {
            if (ctx->config.debug) {
                static double last_log_time = 0;
                if (current_time - last_log_time > 1.0) {
                    LOG_DEBUG("RENDERING: animations=%d, time_since_render=%.3f", 
                              animations_active, time_since_render);
                    last_log_time = current_time;
                }
            }
            if (s_render_diag && !animations_active) {
                /* Log first layer offsets to aid diagnosis without prev snapshot */
                float first_x = 0.0f, first_y = 0.0f;
                if (ctx->layers) {
                    first_x = ctx->layers->offset_x;
                    first_y = ctx->layers->offset_y;
                }
                LOG_DEBUG("[RENDER_DIAG] idle render: resize=%d ipc=%d comp=%d final=%d tsr=%.3f ft=%.3f layers=%d first_offset=%.4f,%.4f",
                          diag_resize, diag_ipc, diag_comp, diag_final,
                          time_since_render, frame_time, ctx->layer_count,
                          first_x, first_y);
            }
            hyprlax_render_frame(ctx);
            ctx->fps = 1.0 / time_since_render;
            last_render_time = current_time;
            frame_count++;
            
            /* Clear needs_render flag - it will be set again if needed */
            needs_render = false;

            /* Print FPS every second in debug mode */
            if (ctx->config.debug) {
                debug_timer += time_since_render;
                if (debug_timer >= 1.0) {
                    LOG_DEBUG("FPS: %.1f, Layers: %d, Animations: %s", 
                             ctx->fps, ctx->layer_count,
                             animations_active ? "active" : "idle");
                    debug_timer = 0.0;
                }
            }
        } else {
            /* Calculate sleep time */
            double sleep_time;
            
            /* If no animations are active, sleep longer to reduce CPU/GPU usage */
            if (!animations_active && !needs_render) {
                /* Use configured idle poll rate (default 2 Hz = 500ms) */
                sleep_time = 1.0 / ctx->config.idle_poll_rate;
                if (ctx->config.debug) {
                    static int idle_count = 0;
                    if (idle_count++ % 10 == 0) {  /* Log every 5 seconds */
                        LOG_DEBUG("IDLE: animations=%d, needs_render=%d, sleeping %.3fs", 
                                  animations_active, needs_render, sleep_time);
                    }
                }
            } else {
                /* Calculate normal frame timing */
                if (use_fc && *use_fc) {
                    /* With frame callbacks, avoid long sleeps and let Wayland pacing drive us */
                    sleep_time = 0.001; /* minimal */
                } else {
                    double target_wake_time = last_render_time + frame_time;
                    sleep_time = target_wake_time - current_time;
                    /* Don't sleep if we need to render soon */
                    if (sleep_time < 0 || sleep_time < 0.001) sleep_time = 0.001; /* Min 1ms */
                }
            }
            
            if (sleep_time > 0) {
                struct timespec ts;
                ts.tv_sec = (time_t)(sleep_time);
                ts.tv_nsec = (long)((sleep_time - (time_t)sleep_time) * 1e9);
                if (ts.tv_nsec > 999999999) {
                    ts.tv_nsec = 999999999;
                } else if (ts.tv_nsec < 0) {
                    ts.tv_nsec = 0;
                }
                nanosleep(&ts, NULL);
            }
        }
    }

    return HYPRLAX_SUCCESS;
}

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

    /* Destroy layers */
    if (ctx->layers) {
        layer_list_destroy(ctx->layers);
        ctx->layers = NULL;
    }

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

    if (ctx->config.debug) {
        LOG_INFO("hyprlax shut down");
    }
}
