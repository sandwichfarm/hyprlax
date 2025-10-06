#define _GNU_SOURCE
/* Version is now defined at compile time via -DHYPRLAX_VERSION in Makefile */
#ifndef HYPRLAX_VERSION
#define HYPRLAX_VERSION "unknown"
#endif
#define INITIAL_MAX_LAYERS 8
#define MAX_CONFIG_LINE_SIZE 512  // Maximum line length in config files
#define BLUR_SHADER_MAX_SIZE 2048 // Maximum size for dynamically built shader
#define BLUR_KERNEL_SIZE 5.0f    // Size of the blur kernel
#define BLUR_WEIGHT_FALLOFF 0.15f // Weight falloff for blur samples
#define BLUR_MIN_THRESHOLD 0.001f // Minimum blur amount to apply effect

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <libgen.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "../protocols/xdg-shell-client-protocol.h"
#include "../protocols/wlr-layer-shell-client-protocol.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ipc.h"
#include "include/core.h"


// Layer structure for multi-layer parallax
struct layer {
    GLuint texture;
    int width, height;
    float shift_multiplier;  // How much this layer moves relative to base (0.0 = static, 1.0 = normal, 2.0 = double)
    float opacity;           // Layer opacity (0.0 - 1.0)
    char *image_path;

    // Per-layer animation state
    float current_offset;
    float target_offset;
    float start_offset;

    // Phase 3: Advanced per-layer settings
    easing_type_t easing;    // Per-layer easing function
    float animation_delay;   // Per-layer animation delay
    float animation_duration; // Per-layer animation duration
    double animation_start;  // When this layer's animation started
    int animating;          // Is this layer currently animating
    float blur_amount;      // Blur amount for depth (0.0 = no blur)
    // Per-layer tint (multiply color)
    float tint_color[3];    // RGB in 0..1
    float tint_strength;    // 0.0=no tint, 1.0=full
};

// Configuration
struct config {
    float shift_per_workspace;
    float animation_duration;
    float animation_delay;  // Delay before starting animation
    float scale_factor;
    easing_type_t easing;
    int target_fps;
    int vsync;
    int debug;
    int multi_layer_mode;  // Whether we're using multiple layers
    int max_workspaces;    // Maximum number of workspaces (detected from Hyprland)
    char *config_file_path;  // Path to the config file for resolving relative paths
} config = {
    .shift_per_workspace = 100.0f,  // Optimized shift to prevent smearing with 10 workspaces
    .animation_duration = 1.0f,  // Longer duration - user can "feel" it settling
    .animation_delay = 0.0f,
    .scale_factor = 1.2f,  // 20% larger to accommodate parallax without edge artifacts
    .easing = EASE_EXPO_OUT,  // Exponential ease out - fast then very gentle settling
    .target_fps = 144,
    .vsync = 1,
    .debug = 0,
    .multi_layer_mode = 0,
    .max_workspaces = 10,  // Default to 10, will be detected from Hyprland
    .config_file_path = NULL
};

// Global state
struct {
    // Wayland objects
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_output *output;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_callback *frame_callback;

    // EGL/OpenGL
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    GLuint texture;  // Single texture for backward compatibility
    GLuint shader_program;
    GLuint blur_shader_program;  // Blur shader for depth effects
    GLuint vbo, ebo;

    // Standard shader uniforms
    GLint u_opacity;  // Uniform location for opacity
    GLint u_texture;  // Uniform location for texture
    GLint u_tint;     // vec3 tint color
    GLint u_tint_strength; // float tint strength

    // Blur shader uniforms
    GLint blur_u_texture;  // Uniform location for texture in blur shader
    GLint blur_u_opacity;  // Uniform location for opacity in blur shader
    GLint blur_u_blur_amount;  // Uniform location for blur amount
    GLint blur_u_resolution;  // Uniform location for resolution
    GLint blur_u_tint;     // vec3 tint color (blur shader)
    GLint blur_u_tint_strength; // float tint strength (blur shader)

    // Window dimensions
    int width, height;
    int configured;  // Track if we've received initial configuration

    // Image data (for single layer mode)
    int img_width, img_height;

    // Multi-layer support
    struct layer *layers;
    int layer_count;
    int max_layers;

    // Animation state
    float current_offset;
    float target_offset;
    float start_offset;
    double animation_start;
    int animating;
    int current_workspace;
    int previous_workspace;  // Track previous workspace to detect actual changes

    // Performance tracking
    double last_frame_time;
    int frame_count;
    double fps_timer;

    // Hyprland IPC
    int ipc_fd;

    // hyprlax IPC for dynamic layer management
    ipc_context_t *ipc_ctx;

    // Running state
    int running;
} state = {0};

// Shader sources with better precision
const char *vertex_shader_src =
    "precision highp float;\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

const char *fragment_shader_src =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_tint_strength;\n"
    "void main() {\n"
    "    vec4 color = texture2D(u_texture, v_texcoord);\n"
    "    // Apply multiplicative tint\n"
    "    vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
    "    vec3 rgb = color.rgb * effective;\n"
    "    // Premultiply alpha for correct blending\n"
    "    float final_alpha = color.a * u_opacity;\n"
    "    gl_FragColor = vec4(rgb * final_alpha, final_alpha);\n"
    "}\n";

// Build blur fragment shader with constants
// Note: snprintf is used here for simple constant injection at runtime
// This is a common pattern in OpenGL applications for shader variants
char *build_blur_shader() {
    char *shader = malloc(BLUR_SHADER_MAX_SIZE);
    if (!shader) {
        fprintf(stderr, "Failed to allocate memory for blur shader\n");
        return NULL;
    }

    int written = snprintf(shader, BLUR_SHADER_MAX_SIZE,
        "precision highp float;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D u_texture;\n"
        "uniform float u_opacity;\n"
        "uniform float u_blur_amount;\n"
        "uniform vec2 u_resolution;\n"
        "uniform vec3 u_tint;\n"
        "uniform float u_tint_strength;\n"
        "void main() {\n"
        "    float blur = u_blur_amount;\n"
        "    if (blur < %.4f) {\n"  // BLUR_MIN_THRESHOLD
        "        vec4 color = texture2D(u_texture, v_texcoord);\n"
        "        vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
        "        vec3 rgb = color.rgb * effective;\n"
        "        gl_FragColor = vec4(rgb, color.a * u_opacity);\n"
        "        return;\n"
        "    }\n"
        "    \n"
        "    // DEBUG: Make blur effect super obvious by tinting red AND blurring\n"
        "    vec2 texelSize = 1.0 / u_resolution;\n"
        "    vec4 result = vec4(0.0);\n"
        "    float samples = 0.0;\n"
        "    \n"
        "    // Large blur radius for testing\n"
        "    for (float x = -10.0; x <= 10.0; x += 2.0) {\n"
        "        for (float y = -10.0; y <= 10.0; y += 2.0) {\n"
        "            vec2 offset = vec2(x, y) * texelSize * blur;\n"
        "            result += texture2D(u_texture, v_texcoord + offset);\n"
        "            samples += 1.0;\n"
        "        }\n"
        "    }\n"
        "    \n"
        "    result /= samples;\n"
        "    vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
        "    vec3 rgb = result.rgb * effective;\n"
        "    gl_FragColor = vec4(rgb, result.a * u_opacity);\n"
        "}\n",
        BLUR_MIN_THRESHOLD);

    // Check if formatting failed first
    if (written < 0) {
        fprintf(stderr, "Error: Blur shader formatting failed\n");
        free(shader);
        return NULL;
    }

    // Check if the shader was truncated
    if (written >= BLUR_SHADER_MAX_SIZE) {
        fprintf(stderr, "Error: Blur shader source too large (needed %d bytes, have %d)\n",
                written, BLUR_SHADER_MAX_SIZE);
        free(shader);
        return NULL;
    }

    return shader;
}

// Helper: Get time in seconds with high precision
double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Helper: Compile shader
GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compilation failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// Initialize OpenGL with optimizations
int init_gl() {
    // Create standard shader program
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);

    if (!vertex_shader || !fragment_shader) return -1;

    state.shader_program = glCreateProgram();
    glAttachShader(state.shader_program, vertex_shader);
    glAttachShader(state.shader_program, fragment_shader);
    glLinkProgram(state.shader_program);

    GLint status;
    glGetProgramiv(state.shader_program, GL_LINK_STATUS, &status);
    if (!status) {
        fprintf(stderr, "Shader linking failed\n");
        return -1;
    }

    // Create blur shader program with dynamic constants
    char *blur_shader_src = build_blur_shader();
    if (!blur_shader_src) {
        fprintf(stderr, "Failed to build blur shader\n");
        return -1;
    }

    if (config.debug) {
        fprintf(stderr, "Building blur shader with BLUR_KERNEL_SIZE=%.1f, BLUR_WEIGHT_FALLOFF=%.2f\n",
                BLUR_KERNEL_SIZE, BLUR_WEIGHT_FALLOFF);
    }

    GLuint blur_fragment = compile_shader(GL_FRAGMENT_SHADER, blur_shader_src);
    free(blur_shader_src);  // Free the dynamically built shader
    if (!blur_fragment) {
        fprintf(stderr, "Failed to compile blur fragment shader\n");
        return -1;
    }

    state.blur_shader_program = glCreateProgram();
    if (config.debug) {
        fprintf(stderr, "Created blur shader program: %d\n", state.blur_shader_program);
    }
    glAttachShader(state.blur_shader_program, vertex_shader);
    glAttachShader(state.blur_shader_program, blur_fragment);
    glLinkProgram(state.blur_shader_program);

    glGetProgramiv(state.blur_shader_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(state.blur_shader_program, sizeof(log), NULL, log);
        fprintf(stderr, "Blur shader linking failed: %s\n", log);
        return -1;
    }
    if (config.debug) {
        fprintf(stderr, "Blur shader linked successfully, program ID: %d\n", state.blur_shader_program);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glDeleteShader(blur_fragment);

    glUseProgram(state.shader_program);

    // Get uniform locations for standard shader with error checking
    state.u_texture = glGetUniformLocation(state.shader_program, "u_texture");
    if (state.u_texture == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_texture' in standard shader\n");
    }
    state.u_opacity = glGetUniformLocation(state.shader_program, "u_opacity");
    if (state.u_opacity == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_opacity' in standard shader\n");
    }
    state.u_tint = glGetUniformLocation(state.shader_program, "u_tint");
    if (state.u_tint == -1 && config.debug) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_tint' in standard shader\n");
    }
    state.u_tint_strength = glGetUniformLocation(state.shader_program, "u_tint_strength");
    if (state.u_tint_strength == -1 && config.debug) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_tint_strength' in standard shader\n");
    }

    // Get uniform locations for blur shader with error checking
    glUseProgram(state.blur_shader_program);
    state.blur_u_texture = glGetUniformLocation(state.blur_shader_program, "u_texture");
    if (state.blur_u_texture == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_texture' in blur shader\n");
    }
    state.blur_u_opacity = glGetUniformLocation(state.blur_shader_program, "u_opacity");
    if (state.blur_u_opacity == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_opacity' in blur shader\n");
    }
    state.blur_u_blur_amount = glGetUniformLocation(state.blur_shader_program, "u_blur_amount");
    if (state.blur_u_blur_amount == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_blur_amount' in blur shader\n");
    }
    state.blur_u_resolution = glGetUniformLocation(state.blur_shader_program, "u_resolution");
    if (state.blur_u_resolution == -1) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_resolution' in blur shader\n");
    }
    state.blur_u_tint = glGetUniformLocation(state.blur_shader_program, "u_tint");
    if (state.blur_u_tint == -1 && config.debug) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_tint' in blur shader\n");
    }
    state.blur_u_tint_strength = glGetUniformLocation(state.blur_shader_program, "u_tint_strength");
    if (state.blur_u_tint_strength == -1 && config.debug) {
        fprintf(stderr, "Warning: Failed to find uniform 'u_tint_strength' in blur shader\n");
    }

    if (config.debug) {
        fprintf(stderr, "Blur shader uniform locations: texture=%d, opacity=%d, blur_amount=%d, resolution=%d\n",
               state.blur_u_texture, state.blur_u_opacity, state.blur_u_blur_amount, state.blur_u_resolution);
    }

    // Switch back to standard shader
    glUseProgram(state.shader_program);

    // Create VBO and EBO for better performance
    glGenBuffers(1, &state.vbo);
    glGenBuffers(1, &state.ebo);

    // Set up static index buffer
    GLushort indices[] = {0, 1, 2, 0, 2, 3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Disable depth testing and blending for better performance
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    return 0;
}

// Load image as texture with mipmaps
int load_image(const char *path) {
    int channels;
    unsigned char *data = stbi_load(path, &state.img_width, &state.img_height, &channels, 4);
    if (!data) {
        fprintf(stderr, "Failed to load image '%s': %s\n", path, stbi_failure_reason());
        return -1;
    }

    glGenTextures(1, &state.texture);
    glBindTexture(GL_TEXTURE_2D, state.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.img_width, state.img_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Use trilinear filtering for smoother animation
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Calculate proper scale factor based on max workspaces and shift distance
    // Scale factor determines how much larger the image is than the viewport
    // We need: image_width = viewport_width * scale_factor
    // And: (scale_factor - 1) * viewport_width >= total_shift_needed
    int max_workspaces = 10;
    float total_shift_needed = (max_workspaces - 1) * config.shift_per_workspace;

    // Initialize state.width if not set (use default screen width)
    if (state.width == 0) {
        state.width = 1920;  // Default, will be updated when surface is configured
    }

    float min_scale_factor = 1.0f + (total_shift_needed / (float)state.width);

    // Use the larger of the configured scale or the minimum required
    if (config.scale_factor < min_scale_factor) {
        config.scale_factor = min_scale_factor;
        if (config.debug) {
            printf("Adjusted scale factor to %.2f to accommodate %d workspaces with %.0fpx shifts\n",
                   config.scale_factor, max_workspaces, config.shift_per_workspace);
        }
    }

    stbi_image_free(data);

    return 0;
}

// Load image as layer for multi-layer mode
int load_layer(struct layer *layer, const char *path, float shift_multiplier, float opacity, float blur_amount) {
    int channels;
    unsigned char *data = stbi_load(path, &layer->width, &layer->height, &channels, 4);
    if (!data) {
        fprintf(stderr, "Failed to load layer image '%s': %s\n", path, stbi_failure_reason());
        return -1;
    }

    glGenTextures(1, &layer->texture);
    glBindTexture(GL_TEXTURE_2D, layer->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, layer->width, layer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Use trilinear filtering for smoother animation
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    layer->shift_multiplier = shift_multiplier;
    layer->opacity = opacity;
    layer->image_path = strdup(path);
    if (!layer->image_path) {
        fprintf(stderr, "Error: Failed to allocate memory for image path\n");
        stbi_image_free(data);
        return -1;
    }
    layer->current_offset = 0.0f;
    layer->target_offset = 0.0f;
    layer->start_offset = 0.0f;

    // Initialize Phase 3 fields with defaults
    layer->easing = config.easing;  // Use global easing by default
    layer->animation_delay = 0.0f;  // Can be overridden per layer
    layer->animation_duration = config.animation_duration;  // Use global duration by default
    layer->animation_start = 0.0;
    layer->animating = 0;
    layer->blur_amount = blur_amount;
    layer->tint_color[0] = 1.0f;
    layer->tint_color[1] = 1.0f;
    layer->tint_color[2] = 1.0f;
    layer->tint_strength = 0.0f;

    stbi_image_free(data);

    if (config.debug) {
        printf("Loaded layer: %s (%.0fx%.0f) shift=%.2f opacity=%.2f\n",
               path, (float)layer->width, (float)layer->height, shift_multiplier, opacity);
    }

    return 0;
}

// Sync IPC layers with OpenGL textures
void sync_ipc_layers() {
    if (!state.ipc_ctx) return;

    // If there are no IPC layers but we have config layers, add them to IPC
    if (state.ipc_ctx->layer_count == 0 && state.layer_count > 0) {
        // Add existing layers to IPC context so they can be managed
        for (int i = 0; i < state.layer_count; i++) {
            if (state.layers[i].image_path) {
                ipc_add_layer(state.ipc_ctx, state.layers[i].image_path,
                             state.layers[i].shift_multiplier,
                             state.layers[i].opacity,
                             0.0f, 0.0f, i);
            }
        }
        return;  // Don't sync back, we just populated IPC from config
    }

    // First, mark all current layers for potential removal
    for (int i = 0; i < state.layer_count; i++) {
        if (state.layers[i].image_path) {
            state.layers[i].opacity = -1.0f; // Mark for removal
        }
    }

    // Process IPC layers
    for (int i = 0; i < state.ipc_ctx->layer_count; i++) {
        layer_t* ipc_layer = state.ipc_ctx->layers[i];
        if (!ipc_layer || !ipc_layer->visible) continue;

        // Find existing layer with same path
        struct layer* existing = NULL;
        for (int j = 0; j < state.layer_count; j++) {
            if (state.layers[j].image_path &&
                strcmp(state.layers[j].image_path, ipc_layer->image_path) == 0) {
                existing = &state.layers[j];
                break;
            }
        }

        if (existing) {
            // Update existing layer
            existing->opacity = ipc_layer->opacity;
            existing->shift_multiplier = ipc_layer->scale;
            // TODO: Apply x/y offsets when rendering
        } else {
            // Add new layer
            if (state.layer_count >= state.max_layers) {
                // Expand layer array
                int new_max = state.max_layers * 2;
                struct layer* new_layers = realloc(state.layers, sizeof(struct layer) * new_max);
                if (new_layers) {
                    state.layers = new_layers;
                    state.max_layers = new_max;
                    memset(&state.layers[state.layer_count], 0,
                           sizeof(struct layer) * (new_max - state.layer_count));
                }
            }

            if (state.layer_count < state.max_layers) {
                struct layer* new_layer = &state.layers[state.layer_count];
                if (load_layer(new_layer, ipc_layer->image_path,
                              ipc_layer->scale, ipc_layer->opacity, 0.0f) == 0) {
                    new_layer->image_path = strdup(ipc_layer->image_path);
                    state.layer_count++;
                }
            }
        }
    }

    // Remove layers that are no longer in IPC
    int write_idx = 0;
    for (int read_idx = 0; read_idx < state.layer_count; read_idx++) {
        if (state.layers[read_idx].opacity >= 0.0f) {
            // Keep this layer
            if (write_idx != read_idx) {
                state.layers[write_idx] = state.layers[read_idx];
            }
            write_idx++;
        } else {
            // Remove this layer
            if (state.layers[read_idx].texture) {
                glDeleteTextures(1, &state.layers[read_idx].texture);
            }
            if (state.layers[read_idx].image_path) {
                free(state.layers[read_idx].image_path);
            }
        }
    }
    state.layer_count = write_idx;

    // Sort layers by z-index (using shift_multiplier as z-index for now)
    // TODO: Add proper z-index field to struct layer
}

// Add a layer to the state
int add_layer(const char *path, float shift_multiplier, float opacity) {
    // Grow the layer array if needed
    if (state.layer_count >= state.max_layers) {
        // Check for integer overflow and reasonable limits
        if (state.max_layers > INT_MAX / 2 || state.max_layers > 1000) {
            fprintf(stderr, "Error: Maximum layer limit reached (%d layers)\n", state.max_layers);
            return -1;
        }
        int new_max = state.max_layers * 2;

        // Check multiplication overflow for size calculation
        if (new_max > SIZE_MAX / sizeof(struct layer)) {
            fprintf(stderr, "Error: Layer array size would exceed memory limits\n");
            return -1;
        }

        struct layer *new_layers = realloc(state.layers, new_max * sizeof(struct layer));
        if (!new_layers) {
            fprintf(stderr, "Failed to allocate memory for %d layers\n", new_max);
            return -1;
        }
        // Zero out the new memory
        memset(new_layers + state.max_layers, 0, (new_max - state.max_layers) * sizeof(struct layer));
        state.layers = new_layers;
        state.max_layers = new_max;
    }

    struct layer *layer = &state.layers[state.layer_count];
    if (load_layer(layer, path, shift_multiplier, opacity, 0.0f) == 0) {
        state.layer_count++;
        return 0;
    }

    return -1;
}

// Forward declaration
void render_frame();

// Frame callback for smooth animation
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    (void)data;
    (void)time;
    if (callback) wl_callback_destroy(callback);
    state.frame_callback = NULL;

    // Render the next frame
    render_frame();
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done
};

// Render frame with optimizations
void render_frame() {
    // Don't render if OpenGL isn't initialized yet
    if (state.shader_program == 0) {
        return;
    }
    double current_time = get_time();

    // Update animation for all layers
    if (config.multi_layer_mode) {
        // Per-layer animation with individual timing
        int any_animating = 0;
        for (int i = 0; i < state.layer_count; i++) {
            struct layer *layer = &state.layers[i];

            if (layer->animating) {
                double elapsed = current_time - layer->animation_start;

                // Check if we're still in delay period
                if (elapsed < layer->animation_delay) {
                    any_animating = 1;
                    continue;
                }

                // Adjust elapsed time for delay
                elapsed -= layer->animation_delay;

                if (elapsed >= layer->animation_duration) {
                    // This layer's animation is complete
                    layer->current_offset = layer->target_offset;
                    layer->animating = 0;
                } else {
                    float t = elapsed / layer->animation_duration;
                    // Smooth completion: treat very close to 1.0 as complete
                    if (t > 0.995f) {
                        t = 1.0f;
                    }
                    float eased = apply_easing(t, layer->easing);
                    layer->current_offset = layer->start_offset +
                        (layer->target_offset - layer->start_offset) * eased;
                    any_animating = 1;
                }
            }
        }
        state.animating = any_animating;
    } else {
        // Single layer mode (backward compatible)
        if (state.animating) {
            double elapsed = current_time - state.animation_start;

            if (elapsed >= config.animation_duration) {
                state.current_offset = state.target_offset;
                state.animating = 0;
            } else {
                float t = elapsed / config.animation_duration;
                // Smooth completion: treat very close to 1.0 as complete
                if (t > 0.995f) {
                    t = 1.0f;
                }
                float eased = apply_easing(t, config.easing);
                state.current_offset = state.start_offset +
                    (state.target_offset - state.start_offset) * eased;
            }
        }
    }

    // Clear
    glClear(GL_COLOR_BUFFER_BIT);

    // Enable blending for multi-layer mode
    if (config.multi_layer_mode && state.layer_count > 1) {
        glEnable(GL_BLEND);
        // Use blend function for premultiplied alpha
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Get attribute locations once
    GLint pos_attrib = glGetAttribLocation(state.shader_program, "position");
    GLint tex_attrib = glGetAttribLocation(state.shader_program, "texcoord");

    if (config.multi_layer_mode) {
        // Render each layer
        for (int i = 0; i < state.layer_count; i++) {
            struct layer *layer = &state.layers[i];

            // Calculate layer-specific texture offset
            float viewport_width_in_texture = 1.0f / config.scale_factor;
            float max_texture_offset = 1.0f - viewport_width_in_texture;
            float max_pixel_offset = (config.scale_factor - 1.0f) * state.width;
            float tex_offset = 0.0f;

            if (max_pixel_offset > 0) {
                tex_offset = (layer->current_offset / max_pixel_offset) * max_texture_offset;
            }

            // Clamp to valid range
            if (tex_offset > max_texture_offset) tex_offset = max_texture_offset;
            if (tex_offset < 0.0f) tex_offset = 0.0f;

            // Update vertex buffer for this layer
            float vertices[] = {
                -1.0f, -1.0f,  tex_offset, 1.0f,
                 1.0f, -1.0f,  tex_offset + viewport_width_in_texture, 1.0f,
                 1.0f,  1.0f,  tex_offset + viewport_width_in_texture, 0.0f,
                -1.0f,  1.0f,  tex_offset, 0.0f,
            };

            glBindBuffer(GL_ARRAY_BUFFER, state.vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

            // Set up vertex attributes
            glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(pos_attrib);
            glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(tex_attrib);

            // Select shader based on blur amount
            if (layer->blur_amount > BLUR_MIN_THRESHOLD && state.blur_shader_program != 0) {
                if (config.debug) {
                    static int blur_count = 0;
                    if (blur_count++ < 5) {  // Only print first 5 times to avoid spam
                        fprintf(stderr, "Using blur shader for layer %d with blur amount: %.2f\n",
                                i, layer->blur_amount);
                    }
                }
                glUseProgram(state.blur_shader_program);

                // Bind layer texture and set uniforms using pre-cached locations
                glBindTexture(GL_TEXTURE_2D, layer->texture);
                glUniform1i(state.blur_u_texture, 0);
                glUniform1f(state.blur_u_opacity, layer->opacity);
                glUniform1f(state.blur_u_blur_amount, layer->blur_amount);
                glUniform2f(state.blur_u_resolution, (float)state.width, (float)state.height);
                glUniform3f(state.blur_u_tint, layer->tint_color[0], layer->tint_color[1], layer->tint_color[2]);
                glUniform1f(state.blur_u_tint_strength, layer->tint_strength);

            } else {
                glUseProgram(state.shader_program);

                // Bind layer texture and set uniforms
                glBindTexture(GL_TEXTURE_2D, layer->texture);
                glUniform1i(state.u_texture, 0);
                glUniform1f(state.u_opacity, layer->opacity);
                glUniform3f(state.u_tint, layer->tint_color[0], layer->tint_color[1], layer->tint_color[2]);
                glUniform1f(state.u_tint_strength, layer->tint_strength);
            }

            // Draw layer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.ebo);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
        }
    } else {
        // Single layer mode (backward compatible)
        float viewport_width_in_texture = 1.0f / config.scale_factor;
        float max_texture_offset = 1.0f - viewport_width_in_texture;
        float max_pixel_offset = (config.scale_factor - 1.0f) * state.width;
        float tex_offset = 0.0f;

        if (max_pixel_offset > 0) {
            tex_offset = (state.current_offset / max_pixel_offset) * max_texture_offset;
        }

        if (tex_offset > max_texture_offset) tex_offset = max_texture_offset;
        if (tex_offset < 0.0f) tex_offset = 0.0f;

        float vertices[] = {
            -1.0f, -1.0f,  tex_offset, 1.0f,
             1.0f, -1.0f,  tex_offset + viewport_width_in_texture, 1.0f,
             1.0f,  1.0f,  tex_offset + viewport_width_in_texture, 0.0f,
            -1.0f,  1.0f,  tex_offset, 0.0f,
        };

        glBindBuffer(GL_ARRAY_BUFFER, state.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(tex_attrib);

        // Bind single texture
        glBindTexture(GL_TEXTURE_2D, state.texture);
        glUniform1i(state.u_texture, 0);
        glUniform1f(state.u_opacity, 1.0f);
        // No tint in single-image path by default
        glUniform3f(state.u_tint, 1.0f, 1.0f, 1.0f);
        glUniform1f(state.u_tint_strength, 0.0f);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.ebo);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    }

    // Swap buffers with vsync control
    if (config.vsync) {
        eglSwapInterval(state.egl_display, 1);
    } else {
        eglSwapInterval(state.egl_display, 0);
    }
    eglSwapBuffers(state.egl_display, state.egl_surface);

    // FPS tracking for debug
    if (config.debug) {
        state.frame_count++;
        if (current_time - state.fps_timer >= 1.0) {
            printf("FPS: %d\n", state.frame_count);
            state.frame_count = 0;
            state.fps_timer = current_time;
        }
    }

    // Request next frame if animating
    if (state.animating && !state.frame_callback) {
        state.frame_callback = wl_surface_frame(state.surface);
        wl_callback_add_listener(state.frame_callback, &frame_listener, NULL);
        wl_surface_commit(state.surface);
    }

    state.last_frame_time = current_time;
}

// Get the maximum workspace number from Hyprland
int detect_max_workspaces() {
    // Using fork/exec for better security than popen
    // This avoids shell injection vulnerabilities
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) {
        return 10; // Default fallback
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 10; // Default fallback
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Execute hyprctl directly without shell
        char *args[] = {"hyprctl", "workspaces", NULL};
        execvp("hyprctl", args);

        // If exec fails, exit child
        _exit(1);
    }

    // Parent process
    close(pipefd[1]); // Close write end

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return 10;
    }

    char buffer[256];
    int max_ws = 0;

    // Parse the output looking for workspace IDs
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for lines like "workspace ID X"
        char *id_str = strstr(buffer, "workspace ID ");
        if (id_str) {
            int ws_id = 0;
            if (sscanf(id_str, "workspace ID %d", &ws_id) == 1) {
                if (ws_id > max_ws) {
                    max_ws = ws_id;
                }
            }
        }
    }
    fclose(fp);
    waitpid(pid, NULL, 0);

    if (max_ws > 0) {
        // Always use at least 10 workspaces to avoid breaking parallax
        if (max_ws < 10) {
            max_ws = 10;
        }
        if (config.debug) {
            printf("Detected max workspace: %d\n", max_ws);
        }
        return max_ws;
    }

    // Try to check bindings as fallback (simpler parsing)
    if (pipe(pipefd) == -1) {
        return 10;
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 10;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        char *args[] = {"hyprctl", "binds", NULL};
        execvp("hyprctl", args);
        _exit(1);
    }

    // Parent process
    close(pipefd[1]);

    fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return 10;
    }

    max_ws = 0;

    // Look for workspace bindings
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for lines containing "workspace,"
        if (strstr(buffer, "workspace,")) {
            // Try to extract workspace number
            int ws_num = 0;
            char *ws_str = strstr(buffer, "workspace,");
                if (ws_str && sscanf(ws_str, "workspace, %d", &ws_num) == 1) {
                    if (ws_num > max_ws && ws_num <= 20) {  // Sanity check
                        max_ws = ws_num;
                    }
                }
            }
        }
        fclose(fp);
        waitpid(pid, NULL, 0);

        if (max_ws > 0) {
            if (config.debug) {
                printf("Detected max workspace from bindings: %d\n", max_ws);
            }
            return max_ws;
        }

    // Default to 10 workspaces if we can't detect
    if (config.debug) {
        printf("Could not detect max workspaces, defaulting to 10\n");
    }
    return 10;
}

// Connect to Hyprland IPC
int connect_hyprland_ipc() {
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) {
        fprintf(stderr, "Not running under Hyprland\n");
        return -1;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        runtime_dir = "/run/user/1000";
    }

    char socket_path[256];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock", runtime_dir, sig);

    state.ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (state.ipc_fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(state.ipc_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(state.ipc_fd);
        return -1;
    }

    // Make non-blocking
    fcntl(state.ipc_fd, F_SETFL, O_NONBLOCK);

    return 0;
}

// Process Hyprland IPC events
void process_ipc_events() {
    char buffer[1024];
    ssize_t n = read(state.ipc_fd, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';

        // Parse workspace events
        char *line = strtok(buffer, "\n");
        while (line) {
            if (strncmp(line, "workspace>>", 11) == 0) {
                int workspace = atoi(line + 11);

                // Only animate if this is a real workspace change
                if (workspace != state.current_workspace && workspace > 0) {
                    if (config.multi_layer_mode) {
                        // Multi-layer mode: update each layer's animation state
                        double now = get_time();

                        // Set new targets for each layer with individual timing
                        float base_target = (workspace - 1) * config.shift_per_workspace;
                        for (int i = 0; i < state.layer_count; i++) {
                            struct layer *layer = &state.layers[i];

                            // If currently animating, update current position
                            if (layer->animating) {
                                double elapsed = now - layer->animation_start - layer->animation_delay;
                                if (elapsed > 0) {
                                    float t = fminf(elapsed / layer->animation_duration, 1.0f);
                                    float eased = apply_easing(t, layer->easing);
                                    layer->current_offset = layer->start_offset +
                                        (layer->target_offset - layer->start_offset) * eased;
                                }
                            }

                            // Set new animation parameters
                            layer->start_offset = layer->current_offset;
                            layer->target_offset = base_target * layer->shift_multiplier;
                            layer->animation_start = now;
                            layer->animating = 1;
                        }
                    } else {
                        // Single layer mode (backward compatible)
                        if (state.animating) {
                            double elapsed = get_time() - state.animation_start;
                            float t = fminf(elapsed / config.animation_duration, 1.0f);
                            float eased = apply_easing(t, config.easing);
                            state.current_offset = state.start_offset + (state.target_offset - state.start_offset) * eased;
                        }

                        state.start_offset = state.current_offset;
                        state.target_offset = (workspace - 1) * config.shift_per_workspace;
                    }

                    state.animation_start = get_time() + config.animation_delay;
                    state.animating = 1;
                    state.previous_workspace = state.current_workspace;  // Track the previous workspace
                    state.current_workspace = workspace;

                    if (config.debug) {
                        printf("Workspace changed to %d (offset: %.2f -> %.2f)\n",
                               workspace, state.start_offset, state.target_offset);
                    }

                    // Request frame
                    if (!state.frame_callback) {
                        state.frame_callback = wl_surface_frame(state.surface);
                        wl_callback_add_listener(state.frame_callback, &frame_listener, NULL);
                        wl_surface_commit(state.surface);
                    }
                }
            }
            line = strtok(NULL, "\n");
        }
    }
}

// Layer surface configure
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
    state.width = width;
    state.height = height;
    state.configured = 1;  // Mark as configured

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (state.egl_window) {
        wl_egl_window_resize(state.egl_window, width, height, 0, 0);
    }

    glViewport(0, 0, width, height);
    render_frame();

    // Commit the surface to display the rendered frame
    wl_surface_commit(state.surface);
    wl_display_flush(state.display);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
    state.running = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

// Registry handlers
static void registry_global(void *data, struct wl_registry *registry, uint32_t id,
                           const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state.output = wl_registry_bind(registry, id, &wl_output_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state.layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] <image_path>\n", prog);
    printf("   or: %s [OPTIONS] --layer <image:shift:opacity> [...]\n", prog);
    printf("   or: %s [OPTIONS] --config <config_file>\n\n", prog);
    printf("Options:\n");
    printf("  -s, --shift <pixels>     Pixels to shift per workspace (default: 100)\n");
    printf("  -d, --duration <seconds> Animation duration (default: 1.0)\n");
    printf("  --delay <seconds>        Delay before animation starts (default: 0)\n");
    printf("  -e, --easing <type>      Easing function (default: expo)\n");
    printf("                           Options: linear, quad, cubic, quart, quint,\n");
    printf("                                   sine, expo, circ, back, elastic, snap\n");
    printf("  -f, --scale <factor>     Scale factor for panning room (default: 1.5)\n");
    printf("  -v, --vsync <0|1>        Enable vsync (default: 1)\n");
    printf("  --fps <rate>             Target FPS (default: 144)\n");
    printf("  --debug                  Enable debug output\n");
    printf("  --version                Show version information\n");
    printf("  -h, --help               Show this help\n");
    printf("\nMulti-layer Mode:\n");
    printf("  --layer <spec>           Add a layer with format: image:shift:opacity[:easing[:delay[:duration[:blur]]]]\n");
    printf("                           shift: 0.0=static, 1.0=normal, 2.0=double speed\n");
    printf("                           opacity: 0.0-1.0 (default 1.0)\n");
    printf("                           easing: per-layer easing function (optional)\n");
    printf("                           delay: per-layer animation delay in seconds (optional)\n");
    printf("                           duration: per-layer animation duration (optional)\n");
    printf("                           blur: blur amount for depth (0.0-10.0, default 0.0)\n");
    printf("  --config <file>          Load layers from config file\n");
    printf("  --tint <spec>            Apply tint to the last declared layer\n");
    printf("                           spec: #RRGGBB[:strength] or 'none'\n");
    printf("                           ex: --layer fg.png:1.0:0.8 --tint #ffaa33:0.5\n");
    printf("\nExamples:\n");
    printf("  # Single image (classic mode)\n");
    printf("  %s wallpaper.jpg\n", prog);
    printf("\n  # Multi-layer parallax\n");
    printf("  %s --layer sky.png:0.3:1.0 --layer mountains.png:0.6:1.0 --layer trees.png:1.0:1.0\n", prog);
    printf("\n  # Multi-layer with blur for depth\n");
    printf("  %s --layer background.png:0.3:1.0:expo:0:1.0:3.0 --layer midground.png:0.6:0.8:expo:0.1:1.0:1.0 --layer foreground.png:1.0:1.0\n", prog);
}

void print_version() {
    printf("hyprlax %s\n", HYPRLAX_VERSION);
    printf("Smooth parallax wallpaper animations for Hyprland\n");
}

// --- Tint parsing helpers ---
static int parse_hex_rgb(const char *s, float rgb[3]) {
    if (!s || s[0] != '#' || strlen(s) != 7) return -1;
    char r[3] = {s[1], s[2], 0};
    char g[3] = {s[3], s[4], 0};
    char b[3] = {s[5], s[6], 0};
    char *end = NULL;
    long rv = strtol(r, &end, 16); if (end && *end) return -1;
    long gv = strtol(g, &end, 16); if (end && *end) return -1;
    long bv = strtol(b, &end, 16); if (end && *end) return -1;
    rgb[0] = (float)rv / 255.0f;
    rgb[1] = (float)gv / 255.0f;
    rgb[2] = (float)bv / 255.0f;
    return 0;
}
static int parse_tint_spec(const char *spec, float rgb[3], float *strength) {
    if (!spec || !*spec) return -1;
    if (!strcmp(spec, "none")) {
        rgb[0] = rgb[1] = rgb[2] = 1.0f; if (strength) *strength = 0.0f; return 0;
    }
    char buf[128];
    if (strlen(spec) >= sizeof(buf)) return -1;
    /* Safe copy: avoid flagged unsafe copy; length validated above */
    snprintf(buf, sizeof(buf), "%s", spec);
    char *colon = strchr(buf, ':');
    if (colon) {
        *colon = '\0';
        const char *color = buf;
        const char *str = colon + 1;
        if (parse_hex_rgb(color, rgb) != 0) return -1;
        float s = (float)atof(str);
        if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
        if (strength) *strength = s;
        return 0;
    } else {
        if (parse_hex_rgb(buf, rgb) != 0) return -1;
        if (strength) *strength = 1.0f; // default when only color provided
        return 0;
    }
}

// Helper: Check if path is in a sensitive directory
static int is_sensitive_path(const char *resolved_path) {
    const char *sensitive_dirs[] = {
        "/etc", "/sys", "/proc", "/dev",
        "/boot", "/root", NULL
    };

    for (int i = 0; sensitive_dirs[i]; i++) {
        size_t dir_len = strlen(sensitive_dirs[i]);
        if (strncmp(resolved_path, sensitive_dirs[i], dir_len) == 0 &&
            (resolved_path[dir_len] == '/' || resolved_path[dir_len] == '\0')) {
            fprintf(stderr, "Error: Path validation failed - access to sensitive directory '%s' denied\n",
                    sensitive_dirs[i]);
            return 1;
        }
    }
    return 0;
}

// Helper: Resolve path for non-existent files
static char *resolve_nonexistent_path(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        fprintf(stderr, "Error: Path validation failed - memory allocation error\n");
        return NULL;
    }

    char *dir = dirname(path_copy);
    char *resolved_dir = realpath(dir, NULL);

    if (!resolved_dir) {
        fprintf(stderr, "Error: Path validation failed - parent directory '%s' does not exist\n", dir);
        free(path_copy);
        return NULL;
    }

    // Construct the resolved path
    char *base = basename((char *)path);
    size_t path_len = strlen(resolved_dir) + strlen(base) + 2;
    char *resolved_path = malloc(path_len);
    if (!resolved_path) {
        fprintf(stderr, "Error: Path validation failed - memory allocation error\n");
        free(resolved_dir);
        free(path_copy);
        return NULL;
    }

    snprintf(resolved_path, path_len, "%s/%s", resolved_dir, base);
    free(resolved_dir);
    free(path_copy);

    return resolved_path;
}

// Validate path to prevent directory traversal
int validate_path(const char *path) {
    if (!path) {
        fprintf(stderr, "Error: Path validation failed - NULL path provided\n");
        return 0;
    }

    // Try to resolve the path directly
    char *resolved_path = realpath(path, NULL);

    // If file doesn't exist, resolve parent directory
    if (!resolved_path) {
        resolved_path = resolve_nonexistent_path(path);
        if (!resolved_path) {
            return 0;
        }
    }

    // Check for sensitive directories
    int is_valid = !is_sensitive_path(resolved_path);

    free(resolved_path);
    return is_valid;
}

// Resolve path relative to config file directory
char* resolve_config_relative_path(const char *path) {
    if (!path) return NULL;

    // If path is already absolute, return a copy
    if (path[0] == '/') {
        return strdup(path);
    }

    // If no config file path stored, treat as relative to current directory
    if (!config.config_file_path) {
        return strdup(path);
    }

    // Get directory of config file
    char *config_dir = strdup(config.config_file_path);
    if (!config_dir) return NULL;

    char *last_slash = strrchr(config_dir, '/');
    if (last_slash) {
        *last_slash = '\0';  // Terminate at last slash to get directory
    } else {
        // Config file is in current directory
        free(config_dir);
        return strdup(path);
    }

    // Build full path
    size_t dir_len = strlen(config_dir);
    size_t path_len = strlen(path);
    char *full_path = malloc(dir_len + path_len + 2);
    if (!full_path) {
        free(config_dir);
        return NULL;
    }

    snprintf(full_path, dir_len + path_len + 2, "%s/%s", config_dir, path);
    free(config_dir);

    return full_path;
}

// Parse config file
int parse_config_file(const char *filename) {
    // Validate the config file path
    if (!validate_path(filename)) {
        fprintf(stderr, "Error: Invalid config file path: %s\n", filename);
        return -1;
    }

    // Store the config file path (resolve to absolute path)
    char *resolved_config_path = realpath(filename, NULL);
    if (resolved_config_path) {
        if (config.config_file_path) {
            free(config.config_file_path);
        }
        config.config_file_path = resolved_config_path;
    } else {
        // If realpath fails, store the original path
        if (config.config_file_path) {
            free(config.config_file_path);
        }
        config.config_file_path = strdup(filename);
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open config file: %s\n", filename);
        return -1;
    }

    char line[MAX_CONFIG_LINE_SIZE];
    int line_num = 0;

    while (fgets(line, MAX_CONFIG_LINE_SIZE, file)) {
        line_num++;

        // Check if line was truncated (no newline found)
        if (!strchr(line, '\n')) {
            // Only report error if not at EOF (EOF without newline is acceptable)
            if (!feof(file)) {
                fprintf(stderr, "Error: Line %d exceeds buffer size of %d characters\n",
                        line_num, MAX_CONFIG_LINE_SIZE - 1);
                // Skip rest of the line safely
                int c;
                while ((c = fgetc(file)) != EOF) {
                    if (c == '\n') break;
                }
                continue;
            }
        }

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        // Parse tokens
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

            // Resolve path relative to config file if needed
            char *resolved_image_path = resolve_config_relative_path(image);
            if (!resolved_image_path) {
                fprintf(stderr, "Error: Failed to resolve image path at line %d: %s\n", line_num, image);
                fclose(file);
                return -1;
            }

            // Validate resolved image path
            if (!validate_path(resolved_image_path)) {
                fprintf(stderr, "Error: Invalid image path at line %d: %s\n", line_num, resolved_image_path);
                free(resolved_image_path);
                fclose(file);
                return -1;
            }

            float shift = shift_str ? atof(shift_str) : 1.0f;
            float opacity = opacity_str ? atof(opacity_str) : 1.0f;
            float blur = blur_str ? atof(blur_str) : 0.0f;

            if (config.debug) {
                fprintf(stderr, "Config parse layer: image=%s, shift=%.2f, opacity=%.2f, blur=%.2f\n",
                        image, shift, opacity, blur);
            }

            // Initialize layer array if needed
            if (!state.layers) {
                state.max_layers = INITIAL_MAX_LAYERS;
                state.layers = calloc(state.max_layers, sizeof(struct layer));
                if (!state.layers) {
                    fprintf(stderr, "Error: Failed to allocate memory for layers at line %d\n", line_num);
                    fclose(file);
                    return -1;
                }
                state.layer_count = 0;
                config.multi_layer_mode = 1;
            }

            // Grow the layer array if needed
            if (state.layer_count >= state.max_layers) {
                // Check for integer overflow and reasonable limits
                if (state.max_layers > INT_MAX / 2 || state.max_layers > 1000) {
                    fprintf(stderr, "Error: Maximum layer limit reached (%d layers) at line %d\n", state.max_layers, line_num);
                    fclose(file);
                    return -1;
                }
                int new_max = state.max_layers * 2;

                // Check multiplication overflow for size calculation
                if (new_max > SIZE_MAX / sizeof(struct layer)) {
                    fprintf(stderr, "Error: Layer array size would exceed memory limits at line %d\n", line_num);
                    fclose(file);
                    return -1;
                }

                struct layer *new_layers = realloc(state.layers, new_max * sizeof(struct layer));
                if (!new_layers) {
                    fprintf(stderr, "Failed to allocate memory for %d layers at line %d\n", new_max, line_num);
                    fclose(file);
                    return -1;
                }
                memset(new_layers + state.max_layers, 0, (new_max - state.max_layers) * sizeof(struct layer));
                state.layers = new_layers;
                state.max_layers = new_max;
            }

            if (state.layer_count < state.max_layers) {
                struct layer *layer = &state.layers[state.layer_count];
                layer->image_path = resolved_image_path;  // Use the resolved path directly
                // Note: resolved_image_path is now owned by the layer, don't free it
                layer->shift_multiplier = shift;
                layer->opacity = opacity;
                layer->blur_amount = blur;
                if (config.debug) {
                    fprintf(stderr, "Stored layer %d: blur_amount=%.2f\n", state.layer_count, layer->blur_amount);
                }
                // Set defaults for Phase 3 features
                layer->easing = config.easing;
                layer->animation_delay = 0.0f;
                layer->animation_duration = config.animation_duration;
                state.layer_count++;
            }
        } else if (strcmp(cmd, "duration") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) config.animation_duration = atof(val);
        } else if (strcmp(cmd, "shift") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) config.shift_per_workspace = atof(val);
        } else if (strcmp(cmd, "easing") == 0) {
            char *val = strtok(NULL, " \t");
            if (val) {
                if (strcmp(val, "linear") == 0) config.easing = EASE_LINEAR;
                else if (strcmp(val, "quad") == 0) config.easing = EASE_QUAD_OUT;
                else if (strcmp(val, "cubic") == 0) config.easing = EASE_CUBIC_OUT;
                else if (strcmp(val, "quart") == 0) config.easing = EASE_QUART_OUT;
                else if (strcmp(val, "quint") == 0) config.easing = EASE_QUINT_OUT;
                else if (strcmp(val, "sine") == 0) config.easing = EASE_SINE_OUT;
                else if (strcmp(val, "expo") == 0) config.easing = EASE_EXPO_OUT;
                else if (strcmp(val, "circ") == 0) config.easing = EASE_CIRC_OUT;
                else if (strcmp(val, "back") == 0) config.easing = EASE_BACK_OUT;
                else if (strcmp(val, "elastic") == 0) config.easing = EASE_ELASTIC_OUT;
                else if (strcmp(val, "snap") == 0) config.easing = EASE_CUSTOM_SNAP;
            }
        }
    }

    fclose(file);
    return 0;
}

int main(int argc, char *argv[]) {
    // Parse arguments
    static struct option long_options[] = {
        {"shift", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'd'},
        {"delay", required_argument, 0, 0},
        {"easing", required_argument, 0, 'e'},
        {"scale", required_argument, 0, 'f'},
        {"vsync", required_argument, 0, 'v'},
        {"fps", required_argument, 0, 0},
        {"layer", required_argument, 0, 0},
        {"tint", required_argument, 0, 0},
        {"config", required_argument, 0, 0},
        {"debug", no_argument, 0, 0},
        {"version", no_argument, 0, 0},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "s:d:e:f:v:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 's':
                config.shift_per_workspace = atof(optarg);
                break;
            case 'd':
                config.animation_duration = atof(optarg);
                break;
            case 'e':
                if (strcmp(optarg, "linear") == 0) config.easing = EASE_LINEAR;
                else if (strcmp(optarg, "quad") == 0) config.easing = EASE_QUAD_OUT;
                else if (strcmp(optarg, "cubic") == 0) config.easing = EASE_CUBIC_OUT;
                else if (strcmp(optarg, "quart") == 0) config.easing = EASE_QUART_OUT;
                else if (strcmp(optarg, "quint") == 0) config.easing = EASE_QUINT_OUT;
                else if (strcmp(optarg, "sine") == 0) config.easing = EASE_SINE_OUT;
                else if (strcmp(optarg, "expo") == 0) config.easing = EASE_EXPO_OUT;
                else if (strcmp(optarg, "circ") == 0) config.easing = EASE_CIRC_OUT;
                else if (strcmp(optarg, "back") == 0) config.easing = EASE_BACK_OUT;
                else if (strcmp(optarg, "elastic") == 0) config.easing = EASE_ELASTIC_OUT;
                else if (strcmp(optarg, "snap") == 0) config.easing = EASE_CUSTOM_SNAP;
                break;
            case 'f':
                config.scale_factor = atof(optarg);
                break;
            case 'v':
                config.vsync = atoi(optarg);
                break;
            case 0:
                if (strcmp(long_options[option_index].name, "fps") == 0) {
                    config.target_fps = atoi(optarg);
                } else if (strcmp(long_options[option_index].name, "delay") == 0) {
                    config.animation_delay = atof(optarg);
                } else if (strcmp(long_options[option_index].name, "layer") == 0) {
                    // Parse extended layer specification: image:shift:opacity[:easing[:delay[:duration[:blur]]]]
                    char *spec = strdup(optarg);
                    if (!spec) {
                        fprintf(stderr, "Error: Failed to allocate memory for layer specification\n");
                        return 1;
                    }
                    char *image = strtok(spec, ":");
                    char *shift_str = strtok(NULL, ":");
                    char *opacity_str = strtok(NULL, ":");
                    char *easing_str = strtok(NULL, ":");
                    char *delay_str = strtok(NULL, ":");
                    char *duration_str = strtok(NULL, ":");
                    char *blur_str = strtok(NULL, ":");

                    if (!image) {
                        fprintf(stderr, "Error: Invalid layer specification: %s\n", optarg);
                        free(spec);
                        return 1;
                    }

                    float shift = shift_str ? atof(shift_str) : 1.0f;
                    float opacity = opacity_str ? atof(opacity_str) : 1.0f;

                    // Initialize layer array if needed
                    if (!state.layers) {
                        state.max_layers = INITIAL_MAX_LAYERS;
                        state.layers = calloc(state.max_layers, sizeof(struct layer));
                        if (!state.layers) {
                            fprintf(stderr, "Error: Failed to allocate memory for layers\n");
                            free(spec);
                            return 1;
                        }
                        state.layer_count = 0;
                        config.multi_layer_mode = 1;
                    }

                    // Grow the layer array if needed
                    if (state.layer_count >= state.max_layers) {
                        // Check for integer overflow and reasonable limits
                        if (state.max_layers > INT_MAX / 2 || state.max_layers > 1000) {
                            fprintf(stderr, "Error: Maximum layer limit reached (%d layers)\n", state.max_layers);
                            free(spec);
                            return 1;
                        }
                        int new_max = state.max_layers * 2;

                        // Check multiplication overflow for size calculation
                        if (new_max > SIZE_MAX / sizeof(struct layer)) {
                            fprintf(stderr, "Error: Layer array size would exceed memory limits\n");
                            free(spec);
                            return 1;
                        }

                        struct layer *new_layers = realloc(state.layers, new_max * sizeof(struct layer));
                        if (!new_layers) {
                            fprintf(stderr, "Error: Failed to allocate memory for %d layers\n", new_max);
                            free(spec);
                            return 1;
                        }
                        memset(new_layers + state.max_layers, 0, (new_max - state.max_layers) * sizeof(struct layer));
                        state.layers = new_layers;
                        state.max_layers = new_max;
                    }

                    // Store layer info for later loading
                    if (state.layer_count < state.max_layers) {
                        struct layer *layer = &state.layers[state.layer_count];
                        layer->image_path = strdup(image);
                        if (!layer->image_path) {
                            fprintf(stderr, "Error: Failed to allocate memory for layer image path\n");
                            free(spec);
                            return 1;
                        }
                        layer->shift_multiplier = shift;
                        layer->opacity = opacity;
                        layer->tint_color[0] = 1.0f;
                        layer->tint_color[1] = 1.0f;
                        layer->tint_color[2] = 1.0f;
                        layer->tint_strength = 0.0f;

                        // Phase 3: Parse optional per-layer settings
                        layer->easing = config.easing;  // Default to global
                        if (easing_str) {
                            if (strcmp(easing_str, "linear") == 0) layer->easing = EASE_LINEAR;
                            else if (strcmp(easing_str, "quad") == 0) layer->easing = EASE_QUAD_OUT;
                            else if (strcmp(easing_str, "cubic") == 0) layer->easing = EASE_CUBIC_OUT;
                            else if (strcmp(easing_str, "quart") == 0) layer->easing = EASE_QUART_OUT;
                            else if (strcmp(easing_str, "quint") == 0) layer->easing = EASE_QUINT_OUT;
                            else if (strcmp(easing_str, "sine") == 0) layer->easing = EASE_SINE_OUT;
                            else if (strcmp(easing_str, "expo") == 0) layer->easing = EASE_EXPO_OUT;
                            else if (strcmp(easing_str, "circ") == 0) layer->easing = EASE_CIRC_OUT;
                            else if (strcmp(easing_str, "back") == 0) layer->easing = EASE_BACK_OUT;
                            else if (strcmp(easing_str, "elastic") == 0) layer->easing = EASE_ELASTIC_OUT;
                            else if (strcmp(easing_str, "snap") == 0) layer->easing = EASE_CUSTOM_SNAP;
                        }

                        layer->animation_delay = delay_str ? atof(delay_str) : 0.0f;
                        layer->animation_duration = duration_str ? atof(duration_str) : config.animation_duration;
                        layer->blur_amount = blur_str ? atof(blur_str) : 0.0f;

                        state.layer_count++;
                    }

                    free(spec);
                } else if (strcmp(long_options[option_index].name, "config") == 0) {
                    if (parse_config_file(optarg) < 0) {
                        return 1;
                    }
                } else if (strcmp(long_options[option_index].name, "tint") == 0) {
                    if (!state.layers || state.layer_count <= 0) {
                        fprintf(stderr, "Error: --tint must follow a --layer specification\n");
                        return 1;
                    }
                    struct layer *last = &state.layers[state.layer_count - 1];
                    float rgb[3]; float s = 0.0f;
                    if (parse_tint_spec(optarg, rgb, &s) != 0) {
                        fprintf(stderr, "Error: invalid tint spec '%s' (expected #RRGGBB[:strength] or 'none')\n", optarg);
                        return 1;
                    }
                    last->tint_color[0] = rgb[0];
                    last->tint_color[1] = rgb[1];
                    last->tint_color[2] = rgb[2];
                    last->tint_strength = s;
                } else if (strcmp(long_options[option_index].name, "debug") == 0) {
                    config.debug = 1;
                } else if (strcmp(long_options[option_index].name, "version") == 0) {
                    print_version();
                    return 0;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    const char *image_path = NULL;

    // Check if we have layers or a single image
    if (config.multi_layer_mode) {
        if (state.layer_count == 0) {
            fprintf(stderr, "Error: At least one layer must be specified\n");
            print_usage(argv[0]);
            return 1;
        }
    } else {
        // Single image mode
        if (optind >= argc) {
            fprintf(stderr, "Error: Image path required\n");
            print_usage(argv[0]);
            return 1;
        }
        image_path = argv[optind];
    }

    // Print config if debug
    if (config.debug) {
        printf("Configuration:\n");
        if (config.multi_layer_mode) {
            printf("  Mode: Multi-layer (%d layers)\n", state.layer_count);
            for (int i = 0; i < state.layer_count; i++) {
                struct layer *ly = &state.layers[i];
                int r = (int)roundf(ly->tint_color[0] * 255.0f);
                int g = (int)roundf(ly->tint_color[1] * 255.0f);
                int b = (int)roundf(ly->tint_color[2] * 255.0f);
                printf("    Layer %d: %s (shift=%.2f, opacity=%.2f, blur=%.2f, tint=#%02x%02x%02x:%.2f)\n",
                       i, ly->image_path,
                       ly->shift_multiplier,
                       ly->opacity,
                       ly->blur_amount,
                       r, g, b, ly->tint_strength);
            }
        } else {
            printf("  Mode: Single image\n");
            printf("  Image: %s\n", image_path);
        }
        printf("  Shift: %.1f pixels/workspace\n", config.shift_per_workspace);
        printf("  Duration: %.2f seconds\n", config.animation_duration);
        printf("  Scale factor: %.2f\n", config.scale_factor);
        printf("  VSync: %s\n", config.vsync ? "enabled" : "disabled");
        printf("  Target FPS: %d\n", config.target_fps);
    }

    // Connect to Wayland
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to connect to Wayland\n");
        return 1;
    }

    // Get registry and bind interfaces
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, NULL);
    wl_display_roundtrip(state.display);

    if (!state.compositor || !state.layer_shell) {
        fprintf(stderr, "Missing required Wayland interfaces\n");
        return 1;
    }

    // Create surface
    state.surface = wl_compositor_create_surface(state.compositor);

    // Create layer surface
    state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state.layer_shell, state.surface, state.output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "hyprlax");

    zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, NULL);
    zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
    zwlr_layer_surface_v1_set_anchor(state.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

    wl_surface_commit(state.surface);
    wl_display_roundtrip(state.display);

    // Initialize EGL
    state.egl_display = eglGetDisplay((EGLNativeDisplayType)state.display);
    eglInitialize(state.egl_display, NULL, NULL);

    EGLint attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config_egl;
    EGLint num_configs;
    eglChooseConfig(state.egl_display, attributes, &config_egl, 1, &num_configs);

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    state.egl_context = eglCreateContext(state.egl_display, config_egl, EGL_NO_CONTEXT, context_attribs);

    // Create EGL window with initial dimensions (will be resized on configure)
    int initial_width = 1;
    int initial_height = 1;
    state.egl_window = wl_egl_window_create(state.surface, initial_width, initial_height);
    state.egl_surface = eglCreateWindowSurface(state.egl_display, config_egl, state.egl_window, NULL);

    eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface, state.egl_context);

    // Initialize OpenGL
    if (init_gl() < 0) return 1;

    // Don't set viewport yet - wait for configuration from compositor

    // Load images
    if (config.multi_layer_mode) {
        // Load all layers
        for (int i = 0; i < state.layer_count; i++) {
            struct layer *layer = &state.layers[i];
            if (load_layer(layer, layer->image_path, layer->shift_multiplier, layer->opacity, layer->blur_amount) < 0) {
                fprintf(stderr, "Failed to load layer %d '%s': %s\n", i, layer->image_path, stbi_failure_reason());
                return 1;
            }
        }
        if (config.debug) {
            printf("Loaded %d layers successfully\n", state.layer_count);
        }
    } else {
        // Load single image
        if (load_image(image_path) < 0) return 1;
    }

    // Detect maximum number of workspaces
    config.max_workspaces = detect_max_workspaces();
    if (config.debug) {
        printf("Maximum workspaces detected: %d\n", config.max_workspaces);
    }

    // Connect to Hyprland IPC
    if (connect_hyprland_ipc() < 0) {
        fprintf(stderr, "Warning: Failed to connect to Hyprland IPC\n");
    }

    // Initialize hyprlax IPC for dynamic layer management
    state.ipc_ctx = ipc_init();
    if (!state.ipc_ctx) {
        fprintf(stderr, "Warning: Failed to initialize IPC for layer management\n");
    } else if (config.multi_layer_mode && state.layer_count > 0) {
        // Add config-loaded layers to IPC context so they can be managed
        for (int i = 0; i < state.layer_count; i++) {
            if (state.layers[i].image_path) {
                ipc_add_layer(state.ipc_ctx, state.layers[i].image_path,
                             state.layers[i].shift_multiplier,
                             state.layers[i].opacity,
                             0.0f, 0.0f, i);
            }
        }
        if (config.debug) {
            printf("Added %d config layers to IPC context\n", state.layer_count);
        }
    }

    // Get initial workspace
    state.current_workspace = 1;
    state.previous_workspace = 1;
    state.current_offset = 0;
    state.target_offset = 0;

    // Initialize timers
    state.last_frame_time = get_time();
    state.fps_timer = state.last_frame_time;

    // Commit the surface to trigger configuration from compositor
    wl_surface_commit(state.surface);
    wl_display_flush(state.display);

    // Wait for initial configuration before rendering
    while (!state.configured && state.running) {
        wl_display_dispatch(state.display);
    }

    // Main loop
    state.running = 1;

    // Set up poll descriptors
    int nfds = 2;
    struct pollfd fds[3];
    fds[0].fd = wl_display_get_fd(state.display);
    fds[0].events = POLLIN;
    fds[1].fd = state.ipc_fd;
    fds[1].events = POLLIN;

    // Add our IPC socket if available
    if (state.ipc_ctx && state.ipc_ctx->socket_fd >= 0) {
        fds[2].fd = state.ipc_ctx->socket_fd;
        fds[2].events = POLLIN;
        nfds = 3;
    }

    while (state.running) {
        // Dispatch Wayland events
        wl_display_dispatch_pending(state.display);
        wl_display_flush(state.display);

        // Calculate timeout for target FPS
        int timeout = -1;
        if (state.animating) {
            double frame_time = 1.0 / config.target_fps;
            double elapsed = get_time() - state.last_frame_time;
            timeout = (int)((frame_time - elapsed) * 1000);
            if (timeout < 0) timeout = 0;
        }

        // Poll for events
        if (poll(fds, nfds, timeout) > 0) {
            if (fds[0].revents & POLLIN) {
                wl_display_dispatch(state.display);
            }
            if (fds[1].revents & POLLIN) {
                process_ipc_events();
            }
            // Handle our IPC for dynamic layer management
            if (nfds > 2 && (fds[2].revents & POLLIN)) {
                if (ipc_process_commands(state.ipc_ctx)) {
                    // Sync IPC layers with OpenGL textures
                    sync_ipc_layers();
                    // Trigger re-render
                    state.animating = 1;
                    state.animation_start = get_time();
                }
            }
        }

        // Render if animating and enough time has passed
        if (state.animating) {
            double current_time = get_time();
            double frame_time = 1.0 / config.target_fps;
            if (current_time - state.last_frame_time >= frame_time) {
                render_frame();
            }
        }
    }

    // Cleanup
    if (state.frame_callback) wl_callback_destroy(state.frame_callback);
    if (state.texture) glDeleteTextures(1, &state.texture);
    if (state.shader_program) glDeleteProgram(state.shader_program);
    if (state.blur_shader_program) glDeleteProgram(state.blur_shader_program);
    if (state.vbo) glDeleteBuffers(1, &state.vbo);
    if (state.ebo) glDeleteBuffers(1, &state.ebo);

    eglDestroySurface(state.egl_display, state.egl_surface);
    eglDestroyContext(state.egl_display, state.egl_context);
    eglTerminate(state.egl_display);

    wl_egl_window_destroy(state.egl_window);
    zwlr_layer_surface_v1_destroy(state.layer_surface);
    wl_surface_destroy(state.surface);

    if (state.ipc_fd >= 0) close(state.ipc_fd);

    // Clean up our IPC
    if (state.ipc_ctx) {
        ipc_cleanup(state.ipc_ctx);
    }

    wl_display_disconnect(state.display);

    // Clean up config file path
    if (config.config_file_path) {
        free(config.config_file_path);
        config.config_file_path = NULL;
    }

    return 0;
}
