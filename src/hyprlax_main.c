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
#include "include/hyprlax.h"
#include "include/hyprlax_internal.h"

/* Create application context */
hyprlax_context_t* hyprlax_create(void) {
    hyprlax_context_t *ctx = calloc(1, sizeof(hyprlax_context_t));
    if (!ctx) {
        fprintf(stderr, "Failed to allocate application context\n");
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
        {"renderer", required_argument, 0, 'r'},
        {"platform", required_argument, 0, 'p'},
        {"compositor", required_argument, 0, 'C'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "hvf:s:d:e:c:Dr:p:C:", 
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
                printf("  -r, --renderer <backend>  Renderer backend (gles2, auto)\n");
                printf("  -p, --platform <backend>  Platform backend (wayland, x11, auto)\n");
                printf("  -C, --compositor <backend> Compositor (hyprland, sway, generic, auto)\n");
                printf("\nEasing types:\n");
                printf("  linear, quad, cubic, quart, quint, sine, expo, circ,\n");
                printf("  back, elastic, bounce, snap\n");
                return -1;
                
            case 'v':
                printf("hyprlax %s\n", HYPRLAX_VERSION);
                printf("Modular parallax wallpaper for multiple compositors\n");
                return -1;
                
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
                config_load_file(&ctx->config, ctx->config.config_path);
                break;
                
            case 'D':
                ctx->config.debug = true;
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
    } else if (strcmp(ctx->backends.platform_backend, "x11") == 0) {
        platform_type = PLATFORM_X11;
    }
    
    /* Create platform instance */
    int ret = platform_create(&ctx->platform, platform_type);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to create platform adapter\n");
        return ret;
    }
    
    /* Initialize platform */
    ret = PLATFORM_INIT(ctx->platform);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to initialize platform\n");
        platform_destroy(ctx->platform);
        ctx->platform = NULL;
        return ret;
    }
    
    /* Connect to display */
    ret = PLATFORM_CONNECT(ctx->platform, NULL);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to connect to display\n");
        platform_destroy(ctx->platform);
        ctx->platform = NULL;
        return ret;
    }
    
    if (ctx->config.debug) {
        printf("Platform: %s\n", ctx->platform->ops->get_name());
    }
    
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
        fprintf(stderr, "Failed to create compositor adapter\n");
        return ret;
    }
    
    /* Initialize compositor */
    ret = COMPOSITOR_INIT(ctx->compositor, ctx->platform);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to initialize compositor\n");
        compositor_destroy(ctx->compositor);
        ctx->compositor = NULL;
        return ret;
    }
    
    /* Connect IPC if available */
    if (ctx->compositor->ops->connect_ipc) {
        ctx->compositor->ops->connect_ipc(NULL);
    }
    
    if (ctx->config.debug) {
        printf("Compositor: %s\n", ctx->compositor->ops->get_name());
        printf("  Blur support: %s\n", 
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
        fprintf(stderr, "Failed to create renderer\n");
        return ret;
    }
    
    /* Initialize renderer with native handles */
    renderer_config_t render_config = {
        .width = 1920,   /* Default, will be updated */
        .height = 1080,
        .vsync = true,
        .target_fps = ctx->config.target_fps,
        .capabilities = 0,
    };
    
    void *native_display = PLATFORM_GET_NATIVE_DISPLAY(ctx->platform);
    void *native_window = PLATFORM_GET_NATIVE_WINDOW(ctx->platform);
    
    ret = RENDERER_INIT(ctx->renderer, native_display, native_window, &render_config);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to initialize renderer\n");
        renderer_destroy(ctx->renderer);
        ctx->renderer = NULL;
        return ret;
    }
    
    if (ctx->config.debug) {
        printf("Renderer: %s\n", ctx->renderer->ops->get_name());
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
    
    /* Initialize modules in order */
    int ret;
    
    /* 1. Platform (windowing system) */
    ret = hyprlax_init_platform(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Platform initialization failed\n");
        return ret;
    }
    
    /* 2. Compositor (IPC and features) */
    ret = hyprlax_init_compositor(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Compositor initialization failed\n");
        return ret;
    }
    
    /* 3. Create window */
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
        fprintf(stderr, "Window creation failed\n");
        return ret;
    }
    
    /* 4. Renderer (OpenGL context) */
    ret = hyprlax_init_renderer(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Renderer initialization failed\n");
        return ret;
    }
    
    /* 5. Create layer surface if using Wayland */
    if (ctx->platform->type == PLATFORM_WAYLAND && ctx->compositor->ops->create_layer_surface) {
        layer_surface_config_t layer_config = {
            .layer = LAYER_BACKGROUND,
            .anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT,
            .exclusive_zone = -1,
            .margin_top = 0,
            .margin_bottom = 0,
            .margin_left = 0,
            .margin_right = 0,
            .keyboard_interactive = false,
            .accept_input = false,
        };
        
        ctx->compositor->ops->create_layer_surface(NULL, &layer_config);
    }
    
    ctx->state = APP_STATE_RUNNING;
    ctx->running = true;
    
    if (ctx->config.debug) {
        printf("hyprlax initialized successfully\n");
        printf("  FPS target: %d\n", ctx->config.target_fps);
        printf("  Shift amount: %.1f pixels\n", ctx->config.shift_pixels);
        printf("  Animation duration: %.1f seconds\n", ctx->config.animation_duration);
        printf("  Easing: %s\n", easing_to_string(ctx->config.default_easing));
    }
    
    return HYPRLAX_SUCCESS;
}

/* Add a layer */
int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path,
                     float shift_multiplier, float opacity, float blur) {
    if (!ctx || !image_path) return HYPRLAX_ERROR_INVALID_ARGS;
    
    parallax_layer_t *new_layer = layer_create(image_path, shift_multiplier, opacity);
    if (!new_layer) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    new_layer->blur_amount = blur;
    ctx->layers = layer_list_add(ctx->layers, new_layer);
    ctx->layer_count = layer_list_count(ctx->layers);
    
    if (ctx->config.debug) {
        printf("Added layer: %s (shift=%.1f, opacity=%.1f, blur=%.1f)\n",
               image_path, shift_multiplier, opacity, blur);
    }
    
    return HYPRLAX_SUCCESS;
}

/* Handle workspace change */
void hyprlax_handle_workspace_change(hyprlax_context_t *ctx, int new_workspace) {
    if (!ctx) return;
    
    int delta = new_workspace - ctx->current_workspace;
    ctx->current_workspace = new_workspace;
    
    /* Calculate target offset */
    float target_x = ctx->workspace_offset_x + (delta * ctx->config.shift_pixels);
    float target_y = ctx->workspace_offset_y;
    
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

/* Get current time in seconds */
static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
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

/* Render frame */
void hyprlax_render_frame(hyprlax_context_t *ctx) {
    if (!ctx || !ctx->renderer) return;
    
    RENDERER_BEGIN_FRAME(ctx->renderer);
    RENDERER_CLEAR(ctx->renderer, 0.0f, 0.0f, 0.0f, 1.0f);
    
    /* Render each layer */
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        /* In real implementation, would render texture with offset */
        /* For now, this is a placeholder */
        layer = layer->next;
    }
    
    RENDERER_END_FRAME(ctx->renderer);
    RENDERER_PRESENT(ctx->renderer);
}

/* Main run loop */
int hyprlax_run(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;
    
    double last_time = get_time();
    double frame_time = 1.0 / ctx->config.target_fps;
    
    while (ctx->running) {
        double current_time = get_time();
        ctx->delta_time = current_time - last_time;
        
        /* Poll platform events */
        platform_event_t platform_event;
        if (PLATFORM_POLL_EVENTS(ctx->platform, &platform_event) == HYPRLAX_SUCCESS) {
            switch (platform_event.type) {
                case PLATFORM_EVENT_CLOSE:
                    ctx->running = false;
                    break;
                case PLATFORM_EVENT_RESIZE:
                    hyprlax_handle_resize(ctx, 
                                        platform_event.data.resize.width,
                                        platform_event.data.resize.height);
                    break;
                default:
                    break;
            }
        }
        
        /* Poll compositor events */
        if (ctx->compositor && ctx->compositor->ops->poll_events) {
            compositor_event_t comp_event;
            if (ctx->compositor->ops->poll_events(&comp_event) == HYPRLAX_SUCCESS) {
                if (comp_event.type == COMPOSITOR_EVENT_WORKSPACE_CHANGE) {
                    hyprlax_handle_workspace_change(ctx, 
                                                  comp_event.data.workspace.to_workspace);
                }
            }
        }
        
        /* Update animations */
        hyprlax_update_layers(ctx, current_time);
        
        /* Render frame if enough time has passed */
        if (ctx->delta_time >= frame_time) {
            hyprlax_render_frame(ctx);
            ctx->fps = 1.0 / ctx->delta_time;
            last_time = current_time;
        } else {
            /* Sleep to maintain target FPS */
            usleep((frame_time - ctx->delta_time) * 1000000);
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
        printf("Window resized: %dx%d\n", width, height);
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
        printf("hyprlax shut down\n");
    }
}