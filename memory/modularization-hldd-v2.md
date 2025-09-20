# Hyprlax Modularization - High-Level Design Document v2

## Executive Summary

This document outlines a pragmatic approach to transform hyprlax from a Hyprland-specific wallpaper engine into a modular, multi-compositor parallax wallpaper system while maintaining its single-binary architecture, minimal dependencies, and high performance. The design preserves backwards compatibility and the project's Unix philosophy while enabling support for multiple compositors through internal modularization and static linking.

## Design Principles

1. **Single-Binary Distribution**: All functionality in one executable, statically linked
2. **Minimal Dependencies**: No new dependencies without clear user value
3. **Performance First**: No allocations or I/O in render hot paths
4. **Progressive Enhancement**: Start with Hyprland, add compositors incrementally
5. **Clean Interfaces**: Design for future pluginization without implementing it now
6. **EGL Everywhere**: Use EGL/GLES2 consistently across all platforms

## Current Architecture

```
hyprlax (monolithic, 75KB)
├── Configuration parsing
├── Hyprland IPC handling  
├── Animation engine
├── OpenGL ES 2.0 rendering
├── Wayland layer-shell protocol
└── EGL context management
```

## Proposed Architecture v2

### Internal Module Structure

```
hyprlax (single binary)
├── src/
│   ├── main.c                 # Entry point, event loop
│   ├── core/                  # Platform-agnostic logic
│   │   ├── animation.c        # Animation engine, easing
│   │   ├── layer.c           # Layer management
│   │   ├── config.c          # Configuration parsing
│   │   └── image.c           # Image loading (stb_image)
│   ├── gfx/                   # Graphics (consumes GL context)
│   │   ├── renderer.c        # OpenGL ES 2.0 rendering
│   │   ├── shader.c          # Shader compilation/management
│   │   └── texture.c         # Texture loading/management
│   ├── platform/              # Display server abstraction
│   │   ├── platform.h        # Platform interface
│   │   ├── platform_wayland.c # Wayland/EGL implementation
│   │   └── platform_x11.c    # X11/EGL implementation
│   ├── compositor/            # Compositor-specific code
│   │   ├── compositor.h      # Compositor interface
│   │   ├── compositor_hyprland.c
│   │   ├── compositor_sway.c
│   │   └── compositor_x11_ewmh.c
│   └── include/               # Internal headers
│       └── hyprlax_internal.h
└── Makefile                   # GNU Make build system
```

### Runtime Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Main Event Loop                       │
├─────────────────────────────────────────────────────────┤
│                  ┌──────────────┐                        │
│                  │ Core Engine  │                        │
│  ┌──────────┐   │ ┌──────────┐ │   ┌──────────┐       │
│  │Compositor│───│ │Animation │ │───│ Graphics │       │
│  │ Adapter  │   │ │  Layer   │ │   │ Renderer │       │
│  └──────────┘   │ │  Config  │ │   └──────────┘       │
│        ↑        │ └──────────┘ │          ↑            │
│        │        └──────────────┘          │            │
├────────┼──────────────────────────────────┼────────────┤
│        ↓                                  ↓            │
│  ┌──────────┐                      ┌──────────┐       │
│  │ Platform │                      │   EGL    │       │
│  │ Adapter  │                      │ Context  │       │
│  └──────────┘                      └──────────┘       │
├─────────────────────────────────────────────────────────┤
│         Wayland/X11              OpenGL ES 2.0          │
└─────────────────────────────────────────────────────────┘

Adapters are selected at runtime but linked statically
```

## Module Specifications

### 1. Core Module (`src/core/`)

**Purpose**: Platform-agnostic parallax engine logic

**Interface** (`src/include/core.h`):
```c
// Animation engine
typedef struct {
    double current_time;
    double start_time;
    double duration;
    easing_type_t easing;
    float from_value;
    float to_value;
} animation_state_t;

float animation_evaluate(const animation_state_t* anim, double time);
void animation_start(animation_state_t* anim, float from, float to, double duration);

// Layer management  
typedef struct {
    uint32_t id;
    char* image_path;
    float shift_multiplier;
    float opacity;
    float blur_amount;
    animation_state_t x_animation;
    animation_state_t y_animation;
    int z_index;
} layer_t;

layer_t* layer_create(const char* image_path, float shift, float opacity);
void layer_destroy(layer_t* layer);
void layer_update_offset(layer_t* layer, float x, float y, double duration);

// Configuration
typedef struct {
    int target_fps;
    float shift_pixels;
    double animation_duration;
    easing_type_t default_easing;
    bool debug;
    char* config_path;
} config_t;

int config_parse(config_t* cfg, int argc, char** argv);
int config_load_file(config_t* cfg, const char* path);
```

**Design Notes**:
- Pure C, no external dependencies except libc/libm
- All functions are reentrant, no global state
- No allocations in animation evaluation paths
- Clear ownership semantics (who allocates, who frees)

### 2. Graphics Module (`src/gfx/`)

**Purpose**: OpenGL ES 2.0 rendering, consumes provided GL context

**Interface** (`src/include/gfx.h`):
```c
// Renderer state
typedef struct {
    GLuint standard_shader;
    GLuint blur_shader;
    GLuint current_shader;
    struct {
        GLint u_texture;
        GLint u_opacity;
        GLint u_blur_amount;
        GLint u_transform;
    } uniforms;
} renderer_t;

// Lifecycle
int renderer_init(renderer_t* r);
void renderer_destroy(renderer_t* r);

// Rendering
void renderer_begin_frame(renderer_t* r, int width, int height);
void renderer_draw_layer(renderer_t* r, const layer_t* layer, GLuint texture);
void renderer_end_frame(renderer_t* r);

// Texture management
GLuint texture_load(const char* path);
void texture_destroy(GLuint texture);
```

**Design Notes**:
- Assumes valid GL context exists
- No GL context creation/management
- Shader source embedded in binary
- Zero allocations in render path

### 3. Platform Interface (`src/platform/platform.h`)

**Purpose**: Abstract display server and graphics context creation

**Interface**:
```c
typedef struct platform_adapter platform_adapter_t;

typedef struct {
    int width;
    int height;
    int x;
    int y;
    bool exclusive;  // Exclusive zone for wallpaper
} surface_config_t;

typedef struct {
    void* native_display;  // wl_display* or Display*
    void* native_surface;  // wl_surface* or Window
    void* gl_context;      // EGLContext
    void* gl_surface;      // EGLSurface
} platform_context_t;

// Platform adapter vtable
struct platform_adapter {
    const char* name;
    
    // Lifecycle
    int (*init)(platform_adapter_t* self, const char* display_name);
    void (*destroy)(platform_adapter_t* self);
    
    // Surface management
    int (*create_surface)(platform_adapter_t* self, 
                         const surface_config_t* config,
                         platform_context_t* ctx);
    void (*destroy_surface)(platform_adapter_t* self, platform_context_t* ctx);
    
    // Graphics context (EGL)
    int (*create_gl_context)(platform_adapter_t* self, platform_context_t* ctx);
    void (*make_current)(platform_adapter_t* self, platform_context_t* ctx);
    void (*swap_buffers)(platform_adapter_t* self, platform_context_t* ctx);
    
    // Event handling
    int (*get_fd)(platform_adapter_t* self);
    int (*dispatch_events)(platform_adapter_t* self, int timeout_ms);
    
    // Frame callbacks
    void (*request_frame)(platform_adapter_t* self, 
                         void (*callback)(void* data), void* data);
    
    // Private data
    void* priv;
};

// Platform detection and selection
platform_adapter_t* platform_detect(void);
platform_adapter_t* platform_create(const char* name);

// Statically linked implementations
extern platform_adapter_t platform_wayland;
extern platform_adapter_t platform_x11;
```

**Implementation Notes**:
- `platform_wayland.c`: EGL on Wayland, layer-shell with xdg-shell fallback
- `platform_x11.c`: EGL on X11, root window management, EWMH atoms
- Both use EGL/GLES2, no GLX

### 4. Compositor Interface (`src/compositor/compositor.h`)

**Purpose**: Abstract compositor-specific IPC and workspace management

**Interface**:
```c
typedef struct compositor_adapter compositor_adapter_t;

typedef enum {
    CAP_WORKSPACE_EVENTS    = 1 << 0,
    CAP_WORKSPACE_QUERY     = 1 << 1,
    CAP_MONITOR_INFO        = 1 << 2,
    CAP_WORKSPACE_COUNT     = 1 << 3,
} compositor_capability_t;

typedef struct {
    int workspace;
    int previous_workspace;
    char* monitor_name;
} workspace_event_t;

// Compositor adapter vtable
struct compositor_adapter {
    const char* name;
    uint32_t capabilities;
    
    // Lifecycle
    int (*init)(compositor_adapter_t* self);
    void (*destroy)(compositor_adapter_t* self);
    
    // Detection
    bool (*detect)(void);  // Static method for auto-detection
    
    // Workspace management
    int (*get_current_workspace)(compositor_adapter_t* self);
    int (*get_workspace_count)(compositor_adapter_t* self);
    
    // Event handling
    int (*connect_events)(compositor_adapter_t* self);
    int (*get_event_fd)(compositor_adapter_t* self);
    int (*process_events)(compositor_adapter_t* self, 
                         void (*callback)(const workspace_event_t* event, void* data),
                         void* data);
    
    // Private data
    void* priv;
};

// Compositor detection and selection
compositor_adapter_t* compositor_detect(void);
compositor_adapter_t* compositor_create(const char* name);

// Statically linked implementations  
extern compositor_adapter_t compositor_hyprland;
extern compositor_adapter_t compositor_sway;
extern compositor_adapter_t compositor_x11_ewmh;
```

**Implementation Notes**:
- `compositor_hyprland.c`: Hyprland IPC socket, `workspace>>` events
- `compositor_sway.c`: i3/Sway IPC protocol
- `compositor_x11_ewmh.c`: `_NET_CURRENT_DESKTOP` property monitoring

## Data Flow

### Initialization Sequence
```
1. Parse command-line arguments
2. Detect or select compositor adapter
3. Detect or select platform adapter  
4. Initialize platform (display connection)
5. Create surface and GL context
6. Initialize renderer
7. Initialize compositor IPC
8. Load configuration and layers
9. Enter main event loop
```

### Main Event Loop
```c
// Pseudo-code for main loop
while (running) {
    // Poll both compositor and platform events
    poll([compositor_fd, platform_fd], timeout);
    
    // Process compositor events (workspace changes)
    if (compositor_has_events) {
        compositor_process_events(on_workspace_change);
    }
    
    // Process platform events (frame callbacks, resize)
    if (platform_has_events) {
        platform_dispatch_events();
    }
    
    // Update animations
    animation_tick(delta_time);
    
    // Render if needed
    if (animation_active || frame_requested) {
        renderer_begin_frame();
        for (each layer) {
            renderer_draw_layer(layer);
        }
        renderer_end_frame();
        platform_swap_buffers();
    }
}
```

## Build System

### Makefile Structure
```makefile
# Configuration
CC = gcc
CFLAGS = -Wall -Wextra -O3 -flto -Isrc/include
LDFLAGS = -flto

# Feature flags
ENABLE_X11 ?= yes
ENABLE_SWAY ?= yes
ENABLE_DEBUG ?= no

# Source organization
CORE_SRCS = src/core/*.c
GFX_SRCS = src/gfx/*.c
PLATFORM_SRCS = src/platform/platform_wayland.c
COMPOSITOR_SRCS = src/compositor/compositor_hyprland.c

ifeq ($(ENABLE_X11),yes)
    PLATFORM_SRCS += src/platform/platform_x11.c
    COMPOSITOR_SRCS += src/compositor/compositor_x11_ewmh.c
    CFLAGS += -DENABLE_X11
endif

ifeq ($(ENABLE_SWAY),yes)
    COMPOSITOR_SRCS += src/compositor/compositor_sway.c
    CFLAGS += -DENABLE_SWAY
endif

# Single binary target
hyprlax: $(ALL_SRCS)
    $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Debug build with symbols, no optimization
debug: CFLAGS = -Wall -Wextra -g -O0 -DDEBUG -Isrc/include
debug: hyprlax
```

## Future Plugin Considerations

While not implemented initially, the interface design allows for future plugin support:

### Plugin ABI (Future)
```c
// Version 1.0 ABI - frozen on release
typedef struct {
    uint32_t abi_version;  // HYPRLAX_ABI_VERSION
    uint32_t adapter_version;
    
    // Required functions
    compositor_adapter_t* (*create)(void);
    void (*destroy)(compositor_adapter_t*);
    
    // Metadata
    const char* name;
    const char* description;
    const char* author;
} hyprlax_plugin_t;

// Plugin entry point
const hyprlax_plugin_t* hyprlax_plugin_init(uint32_t abi_version);
```

### Plugin Discovery (Future)
```
# System plugins (root-owned, trusted)
/usr/lib/hyprlax/adapters/*.so

# User plugins (opt-in only, requires --enable-user-plugins)
~/.local/lib/hyprlax/adapters/*.so  # Disabled by default
```

### Security Considerations (Future)
- Plugins loaded from system directories only by default
- Optional signature verification
- Capability-based sandboxing
- No dlopen in hot paths

## Configuration

### Adapter Selection
```bash
# Auto-detect compositor and platform
hyprlax image.jpg

# Manual compositor selection
hyprlax --compositor=sway image.jpg

# Manual platform selection (rare)
hyprlax --platform=x11 image.jpg

# Force X11 EWMH for testing
hyprlax --compositor=x11-ewmh --platform=x11 image.jpg
```

### Configuration File
```toml
# ~/.config/hyprlax/config.toml
[general]
fps = 144
shift = 150
duration = 1.0
easing = "cubic"

[adapters]
# Usually auto-detected
compositor = "auto"
platform = "auto"

# Manual overrides
[adapters.hyprland]
socket_path = "/custom/path"

[[layers]]
image = "background.jpg"
shift_multiplier = 1.0
opacity = 1.0
blur = 0.0
```

## Performance Requirements

### Hot Path Rules
No allocations, no I/O, no string operations in:
- `animation_evaluate()`
- `renderer_draw_layer()`
- `platform_swap_buffers()`
- Main event loop

### Benchmarks
```bash
# Micro-benchmark for hot paths
make benchmark

# Expected results:
# animation_evaluate: <100ns per call
# renderer_draw_layer: <1ms per layer
# Full frame: <16ms (60 FPS) with 5 layers
```

## Testing Strategy

### Core Tests
Pure C unit tests for core logic:
```c
// tests/test_animation.c
void test_easing_functions(void) {
    assert(ease_linear(0.5f) == 0.5f);
    assert(ease_quad_out(0.0f) == 0.0f);
    assert(ease_quad_out(1.0f) == 1.0f);
}

// tests/test_config.c  
void test_config_parsing(void) {
    config_t cfg = {0};
    char* argv[] = {"hyprlax", "--fps", "60"};
    assert(config_parse(&cfg, 3, argv) == 0);
    assert(cfg.target_fps == 60);
}
```

### Integration Tests
```bash
# Test with mock compositor
tests/integration/test_hyprland_mock.sh

# Test with headless EGL
tests/integration/test_egl_surfaceless.sh
```

### Manual Testing Matrix
| Compositor | Wayland | X11 | Status |
|------------|---------|-----|--------|
| Hyprland   | ✓       | N/A | Primary |
| Sway       | ✓       | N/A | Supported |
| i3         | N/A     | ✓   | Supported |
| Openbox    | N/A     | ✓   | Basic |
| GNOME      | ✓       | ✓   | Future |
| KDE        | ✓       | ✓   | Future |

## Error Handling

### Initialization Failures
```c
// Graceful fallback chain
if (!compositor_detect()) {
    fprintf(stderr, "No supported compositor detected\n");
    fprintf(stderr, "Supported: hyprland, sway, X11/EWMH\n");
    // Continue without workspace events
}

if (!platform_create()) {
    fprintf(stderr, "Failed to connect to display server\n");
    return EXIT_FAILURE;  // Fatal
}
```

### Runtime Errors
```c
// Workspace IPC disconnection
if (compositor_disconnected()) {
    // Attempt reconnection
    if (!compositor_reconnect()) {
        // Continue without workspace changes
        workspace_events_enabled = false;
    }
}
```

## Migration Path

### Phase 1: Internal Restructuring (Weeks 1-2)
- Move code into module structure
- Create internal interfaces
- Maintain exact same behavior

### Phase 2: Wayland/X11 Platform Split (Weeks 3-4)
- Implement platform abstraction
- Add X11/EGL platform support
- Test on both platforms

### Phase 3: Compositor Adapters (Weeks 5-8)
- Implement Sway adapter
- Implement X11/EWMH adapter
- Validate with real compositors

### Phase 4: Polish and Release (Weeks 9-10)
- Performance optimization
- Documentation
- Distribution packages

## Conclusion

This pragmatic design maintains hyprlax's simplicity and performance while enabling multi-compositor support through internal modularization. The single-binary architecture with statically linked adapters provides the benefits of clean code organization without the complexity of dynamic loading. The design anticipates future plugin support without implementing it prematurely, focusing instead on delivering working multi-compositor support with minimal risk and maximum compatibility.