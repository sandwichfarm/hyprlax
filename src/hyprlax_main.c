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
        fprintf(stderr, "Error: Cannot open config file: %s\n", filename);
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
                fprintf(stderr, "Config line %d: layer requires image path\n", line_num);
                continue;
            }
            
            /* Resolve path relative to config file if needed */
            char *resolved_image_path = resolve_config_relative_path(filename, image);
            if (!resolved_image_path) {
                fprintf(stderr, "Error: Failed to resolve image path at line %d: %s\n", line_num, image);
                continue;
            }
            
            float shift = shift_str ? atof(shift_str) : 1.0f;
            float opacity = opacity_str ? atof(opacity_str) : 1.0f;
            float blur = blur_str ? atof(blur_str) : 0.0f;
            
            if (ctx->config.debug) {
                fprintf(stderr, "Config parse layer: image=%s, shift=%.2f, opacity=%.2f, blur=%.2f\n",
                        resolved_image_path, shift, opacity, blur);
            }
            
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
                exit(0);  /* Exit successfully for help */
                
            case 'v':
                printf("hyprlax %s\n", HYPRLAX_VERSION);
                fprintf(stderr, "Buttery-smooth parallax wallpaper daemon with support for multiple compositors, platforms and renderers\n");
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
                    fprintf(stderr, "Failed to load config file: %s\n", optarg);
                    return -1;
                }
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
                
            case 1001:  /* --primary-only */
                ctx->monitor_mode = MULTI_MON_PRIMARY;
                break;
                
            case 1002:  /* --monitor */
                /* TODO: Add specific monitor to list */
                ctx->monitor_mode = MULTI_MON_SPECIFIC;
                fprintf(stderr, "Monitor selection: %s\n", optarg);
                break;
                
            case 1003:  /* --disable-monitor */
                /* TODO: Add monitor to exclusion list */
                fprintf(stderr, "Excluding monitor: %s\n", optarg);
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
                fprintf(stderr, "Error: Image file not found: %s\n", argv[i]);
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
    
    /* Share context with platform for monitor detection */
    extern void wayland_set_context(hyprlax_context_t *ctx);
    if (ctx->platform->type == PLATFORM_WAYLAND) {
        wayland_set_context(ctx);
    }
    
    if (ctx->config.debug) {
        fprintf(stderr, "Platform: %s\n", ctx->platform->ops->get_name());
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
        int ret = ctx->compositor->ops->connect_ipc(NULL);
        if (ret == HYPRLAX_SUCCESS && ctx->config.debug) {
            fprintf(stderr, "  IPC connected\n");
        }
    }
    
    if (ctx->config.debug) {
        fprintf(stderr, "Compositor: %s\n", ctx->compositor->ops->get_name());
        fprintf(stderr, "  Blur support: %s\n", 
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
        fprintf(stderr, "Renderer: %s\n", ctx->renderer->ops->get_name());
    }
    
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
        fprintf(stderr, "IPC server initialized\n");
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
    
    /* 0. Initialize multi-monitor support */
    fprintf(stderr, "[INIT] Step 0: Initializing multi-monitor support\n");
    ctx->monitors = monitor_list_create();
    if (!ctx->monitors) {
        fprintf(stderr, "[INIT] Failed to create monitor list\n");
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    ctx->monitor_mode = MULTI_MON_ALL;  /* Default: use all monitors */
    fprintf(stderr, "[INIT] Multi-monitor mode: %s\n", 
            ctx->monitor_mode == MULTI_MON_ALL ? "ALL" : 
            ctx->monitor_mode == MULTI_MON_PRIMARY ? "PRIMARY" : "SPECIFIC");
    
    /* 1. Initialize IPC server first to check for existing instances */
    fprintf(stderr, "[INIT] Step 1: Initializing IPC\n");
    ret = hyprlax_init_ipc(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "[INIT] IPC initialization failed with code %d\n", ret);
        return ret;  /* Exit if another instance is running */
    }
    
    /* 2. Platform (windowing system) */
    fprintf(stderr, "[INIT] Step 2: Initializing platform\n");
    ret = hyprlax_init_platform(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "[INIT] Platform initialization failed with code %d\n", ret);
        return ret;
    }
    
    /* 3. Compositor (IPC and features) */
    fprintf(stderr, "[INIT] Step 3: Initializing compositor\n");
    ret = hyprlax_init_compositor(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "[INIT] Compositor initialization failed with code %d\n", ret);
        return ret;
    }
    
    /* 4. Create window */
    fprintf(stderr, "[INIT] Step 4: Creating window\n");
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
        fprintf(stderr, "[INIT] Window creation failed with code %d\n", ret);
        return ret;
    }
    
    /* 5. Renderer (OpenGL context) */
    fprintf(stderr, "[INIT] Step 5: Initializing renderer\n");
    ret = hyprlax_init_renderer(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "[INIT] Renderer initialization failed with code %d\n", ret);
        return ret;
    }
    
    /* 6. Load textures for all layers now that GL is initialized */
    fprintf(stderr, "[INIT] Step 6: Loading layer textures\n");
    ret = hyprlax_load_layer_textures(ctx);
    if (ret != HYPRLAX_SUCCESS) {
        fprintf(stderr, "[INIT] Warning: Some textures failed to load\n");
        /* Continue anyway - we can still run with missing textures */
    }
    
    /* 7. Create layer surface if using Wayland */
    fprintf(stderr, "[INIT] Step 7: Creating layer surface\n");
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
        fprintf(stderr, "hyprlax initialized successfully\n");
        fprintf(stderr, "  FPS target: %d\n", ctx->config.target_fps);
        fprintf(stderr, "  Shift amount: %.1f pixels\n", ctx->config.shift_pixels);
        fprintf(stderr, "  Animation duration: %.1f seconds\n", ctx->config.animation_duration);
        fprintf(stderr, "  Easing: %s\n", easing_to_string(ctx->config.default_easing));
    }
    
    return HYPRLAX_SUCCESS;
}

/* Load texture from file */
static GLuint load_texture(const char *path, int *width, int *height) {
    int channels;
    unsigned char *data = stbi_load(path, width, height, &channels, 4);
    if (!data) {
        fprintf(stderr, "Failed to load image '%s': %s\n", path, stbi_failure_reason());
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
    
    if (ctx->config.debug) {
        fprintf(stderr, "Added layer: %s (shift=%.1f, opacity=%.1f, blur=%.1f)\n",
                image_path, shift_multiplier, opacity, blur);
    }
    
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
                    fprintf(stderr, "Loaded texture for layer: %s (%dx%d)\n",
                            layer->image_path, img_width, img_height);
                }
            } else {
                fprintf(stderr, "Failed to load texture for layer: %s\n", layer->image_path);
            }
        }
        layer = layer->next;
    }
    
    if (ctx->config.debug && loaded > 0) {
        fprintf(stderr, "Loaded %d layer textures\n", loaded);
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
    
    if (ctx->config.debug) {
        fprintf(stderr, "[DEBUG] Target offset: %.1f, %.1f (shift=%.1f)\n", 
               target_x, target_y, ctx->config.shift_pixels);
    }
    
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
    
    if (ctx->config.debug) {
        fprintf(stderr, "[DEBUG] Target offset: (%.1f, %.1f) shift=%.1f\n", 
               target_x, target_y, ctx->config.shift_pixels);
    }
    
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

/* Render frame */
void hyprlax_render_frame(hyprlax_context_t *ctx) {
    if (!ctx || !ctx->renderer) {
        if (ctx && ctx->config.debug) {
            fprintf(stderr, "[DEBUG] render_frame: No renderer available\n");
        }
        return;
    }
    
    static int render_count = 0;
    if (ctx->config.debug && render_count < 5) {
        fprintf(stderr, "[DEBUG] render_frame %d: Starting\n", render_count);
    }
    
    RENDERER_BEGIN_FRAME(ctx->renderer);
    RENDERER_CLEAR(ctx->renderer, 0.0f, 0.0f, 0.0f, 1.0f);
    
    /* Enable blending for multi-layer compositing */
    if (ctx->layer_count > 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    
    /* Get actual viewport dimensions */
    int viewport_width = 1920;
    int viewport_height = 1080;
    
    /* Get dimensions from platform if available */
    if (ctx->platform && ctx->platform->type == PLATFORM_WAYLAND) {
        extern void wayland_get_window_size(int *width, int *height);
        wayland_get_window_size(&viewport_width, &viewport_height);
    }
    
    /* Calculate optimal scale factor if not set */
    if (ctx->config.scale_factor <= 1.0f) {
        ctx->config.scale_factor = calculate_scale_factor(
            ctx->config.shift_pixels, 10, viewport_width);
        if (ctx->config.debug) {
            fprintf(stderr, "[DEBUG] Calculated scale factor: %.2f\n", ctx->config.scale_factor);
        }
    }
    
    float scale_factor = ctx->config.scale_factor;
    
    if (ctx->config.debug && render_count < 5) {
        fprintf(stderr, "[DEBUG] Rendering %d layers\n", ctx->layer_count);
    }
    
    /* Render each layer */
    parallax_layer_t *layer = ctx->layers;
    int layer_num = 0;
    while (layer) {
        if (layer->texture_id == 0) {
            if (ctx->config.debug && render_count < 5) {
                fprintf(stderr, "[DEBUG] Layer %d: No texture (id=0)\n", layer_num);
            }
            layer = layer->next;
            layer_num++;
            continue;
        }
        
        /* Calculate texture offset based on layer animation */
        float viewport_width_in_texture = 1.0f / scale_factor;
        float viewport_height_in_texture = 1.0f / scale_factor;
        float max_texture_offset_x = 1.0f - viewport_width_in_texture;
        float max_texture_offset_y = 1.0f - viewport_height_in_texture;
        float max_pixel_offset_x = (scale_factor - 1.0f) * viewport_width;
        float max_pixel_offset_y = (scale_factor - 1.0f) * viewport_height;
        float tex_offset_x = 0.0f;
        float tex_offset_y = 0.0f;
        
        if (max_pixel_offset_x > 0) {
            tex_offset_x = (layer->offset_x / max_pixel_offset_x) * max_texture_offset_x;
        }
        if (max_pixel_offset_y > 0) {
            tex_offset_y = (layer->offset_y / max_pixel_offset_y) * max_texture_offset_y;
        }
        
        /* Clamp to valid range */
        if (tex_offset_x > max_texture_offset_x) tex_offset_x = max_texture_offset_x;
        if (tex_offset_x < 0.0f) tex_offset_x = 0.0f;
        if (tex_offset_y > max_texture_offset_y) tex_offset_y = max_texture_offset_y;
        if (tex_offset_y < 0.0f) tex_offset_y = 0.0f;
        
        /* Render the layer texture */
        if (ctx->renderer->ops->draw_layer) {
            if (ctx->config.debug && render_count < 5) {
                fprintf(stderr, "[DEBUG] Layer %d: Drawing texture %u (%dx%d) at offset (%.3f, %.3f)\n", 
                       layer_num, layer->texture_id, layer->width, layer->height, 
                       tex_offset_x, tex_offset_y);
            }
            texture_t tex = {
                .id = layer->texture_id,
                .width = layer->width,
                .height = layer->height,
                .format = TEXTURE_FORMAT_RGBA
            };
            ctx->renderer->ops->draw_layer(&tex, tex_offset_x, tex_offset_y, 
                                          layer->opacity, layer->blur_amount);
        } else if (ctx->config.debug && render_count < 5) {
            fprintf(stderr, "[DEBUG] Layer %d: No draw_layer function!\n", layer_num);
        }
        
        layer = layer->next;
        layer_num++;
    }
    
    if (ctx->layer_count > 1) {
        glDisable(GL_BLEND);
    }
    
    RENDERER_END_FRAME(ctx->renderer);
    RENDERER_PRESENT(ctx->renderer);
    
    if (ctx->config.debug && render_count < 5) {
        fprintf(stderr, "[DEBUG] render_frame %d: Complete, flushing\n", render_count);
    }
    
    /* Commit Wayland surface after rendering */
    if (ctx->platform->type == PLATFORM_WAYLAND && ctx->platform->ops->flush_events) {
        ctx->platform->ops->flush_events();
    }
    
    render_count++;
}

/* Main run loop */
int hyprlax_run(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;
    
    if (ctx->config.debug) {
        fprintf(stderr, "[DEBUG] Starting main loop (target FPS: %d)\n", ctx->config.target_fps);
    }
    
    double last_time = get_time();
    double frame_time = 1.0 / ctx->config.target_fps;
    int frame_count = 0;
    double debug_timer = 0.0;
    
    while (ctx->running) {
        double current_time = get_time();
        ctx->delta_time = current_time - last_time;
        
        if (ctx->config.debug && frame_count < 5) {
            fprintf(stderr, "[DEBUG] Frame %d: delta=%.4f, frame_time=%.4f\n", 
                   frame_count, ctx->delta_time, frame_time);
        }
        
        /* Poll platform events */
        platform_event_t platform_event;
        if (ctx->config.debug && frame_count < 5) {
            fprintf(stderr, "[DEBUG] Polling platform events\n");
        }
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
        
        /* Poll IPC commands */
        if (ctx->ipc_ctx && ipc_process_commands((ipc_context_t*)ctx->ipc_ctx)) {
            /* IPC processed - layers may have changed */
            if (ctx->config.debug) {
                fprintf(stderr, "[DEBUG] IPC command processed\n");
            }
        }
        
        /* Poll compositor events */
        if (ctx->compositor && ctx->compositor->ops->poll_events) {
            compositor_event_t comp_event;
            int poll_result = ctx->compositor->ops->poll_events(&comp_event);
            if (poll_result == HYPRLAX_SUCCESS) {
                if (comp_event.type == COMPOSITOR_EVENT_WORKSPACE_CHANGE) {
                    /* For multi-monitor support, find the correct monitor */
                    monitor_instance_t *target_monitor = NULL;
                    
                    if (ctx->monitors && comp_event.data.workspace.monitor_name[0] != '\0') {
                        /* Use monitor name from event if available */
                        target_monitor = monitor_list_find_by_name(ctx->monitors, 
                                                                  comp_event.data.workspace.monitor_name);
                        if (ctx->config.debug && target_monitor) {
                            fprintf(stderr, "[DEBUG] Workspace event for monitor: %s\n", 
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
                            fprintf(stderr, "[DEBUG] Main loop: 2D Workspace changed from (%d,%d) to (%d,%d) on %s\n", 
                                   comp_event.data.workspace.from_x,
                                   comp_event.data.workspace.from_y,
                                   comp_event.data.workspace.to_x,
                                   comp_event.data.workspace.to_y,
                                   target_monitor ? target_monitor->name : "unknown");
                        }
                        
                        if (target_monitor) {
                            /* Update monitor's workspace context for 2D */
                            workspace_context_t new_context = {
                                .model = WS_MODEL_SET_BASED,
                                .data.wayfire_set = {
                                    .set_id = comp_event.data.workspace.to_y,
                                    .workspace_id = comp_event.data.workspace.to_x
                                }
                            };
                            monitor_handle_workspace_context_change(ctx, target_monitor, &new_context);
                        } else {
                            /* Fallback to global handler */
                            hyprlax_handle_workspace_change_2d(ctx,
                                                             comp_event.data.workspace.from_x,
                                                             comp_event.data.workspace.from_y,
                                                             comp_event.data.workspace.to_x,
                                                             comp_event.data.workspace.to_y);
                        }
                    } else {
                        /* Linear workspace change (Hyprland, Sway, etc.) */
                        if (ctx->config.debug) {
                            fprintf(stderr, "[DEBUG] Main loop: Workspace changed from %d to %d on %s\n", 
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
        
        /* Update animations */
        if (ctx->config.debug && frame_count < 5) {
            fprintf(stderr, "[DEBUG] Updating layer animations\n");
        }
        hyprlax_update_layers(ctx, current_time);
        
        /* Render frame if enough time has passed */
        if (ctx->delta_time >= frame_time) {
            if (ctx->config.debug && frame_count < 5) {
                fprintf(stderr, "[DEBUG] Rendering frame %d\n", frame_count);
            }
            hyprlax_render_frame(ctx);
            ctx->fps = 1.0 / ctx->delta_time;
            last_time = current_time;
            frame_count++;
            
            /* Print FPS every second in debug mode */
            if (ctx->config.debug) {
                debug_timer += ctx->delta_time;
                if (debug_timer >= 1.0) {
                    fprintf(stderr, "[DEBUG] FPS: %.1f, Layers: %d\n", ctx->fps, ctx->layer_count);
                    debug_timer = 0.0;
                }
            }
        } else {
            /* Sleep to maintain target FPS using nanosleep for better precision */
            double sleep_time = frame_time - ctx->delta_time;
            if (ctx->config.debug && frame_count < 5) {
                fprintf(stderr, "[DEBUG] Sleeping for %.4f seconds\n", sleep_time);
            }
            if (sleep_time > 0) {
                struct timespec ts;
                ts.tv_sec = (time_t)(sleep_time);  /* Integer seconds (usually 0) */
                ts.tv_nsec = (long)((sleep_time - (time_t)sleep_time) * 1e9);  /* Fractional part in nanoseconds */
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
        fprintf(stderr, "Window resized: %dx%d\n", width, height);
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
        fprintf(stderr, "hyprlax shut down\n");
    }
}