# Multi-Monitor Support Design Document v2
*Revised based on technical assessment feedback*

## Executive Summary

This document outlines the design and implementation strategy for multi-monitor support in hyprlax, addressing the technical concerns raised in the assessment. The design supports three configuration modes simultaneously while maintaining performance targets (144 FPS), minimizing dependencies, and ensuring deterministic behavior.

## Core Architecture Decisions

### 1. Single Process, Per-Output Surfaces
- One `wl_surface` and `zwlr_layer_surface_v1` per monitor
- Shared EGL context across all surfaces (single GL context)
- Per-output frame callbacks for proper scheduling
- Shared texture cache with reference counting

### 2. Configuration Without New Dependencies
- Extend existing `.conf` parser to support sections (INI-like)
- Optional TOML support via build flag (`ENABLE_TOML=1`)
- If TOML enabled, use vendored single-header parser (tomlc99)
- Clear precedence hierarchy with deterministic resolution

### 3. Per-Output Frame Scheduling
- Each monitor driven by its own Wayland frame callback
- Global animation clock with per-output sampling
- No blocking operations in render loop
- Target <6.9ms render time per surface at 144Hz

## Configuration System (TOML-Based)

### TOML as Primary Format

Using TOML as the primary configuration format provides:
- Well-established, widely understood syntax
- Excellent library support (tomlc99)
- Natural hierarchical structure for multi-monitor configs
- Less code to maintain (no custom parser)
- Better tooling and editor support

### Configuration Structure

```toml
# hyprlax.toml - TOML format supporting all modes

# Mode 1: Global configuration (default for all monitors)
[global]
duration = 4.0
shift = 200
easing = "expo"
fps = 60

# Global layers applied to all monitors
[[global.layers]]
image = "./backgrounds/layer1.png"
shift_multiplier = 0.1
opacity = 1.0
blur = 0.0

[[global.layers]]
image = "./backgrounds/layer2.png"
shift_multiplier = 0.3
opacity = 0.8
blur = 0.2

# Mode 2: Per-monitor overrides
[monitors.overrides."DP-1"]
shift = 150
scale_mode = "fit"

[monitors.overrides."DP-*"]  # Wildcard patterns
scale_mode = "cover"

# Mode 3: Full per-monitor config
[monitors.configs."HDMI-1"]
duration = 2.0
shift = 100
clear_layers = true  # Don't inherit global layers

[[monitors.configs."HDMI-1".layers]]
image = "./ultrawide/layer1.png"
shift_multiplier = 0.05
opacity = 1.0
blur = 0.0

# Groups for coordinated behavior
[groups.main]
monitors = ["DP-1", "DP-2"]
sync_animations = true
```

### Legacy .conf Support

For backwards compatibility, keep simple .conf support for basic single-monitor setups:
```bash
# Simple configs still work
hyprlax --config old-style.conf  # Auto-detected and converted
```

### Configuration Precedence (Deterministic)

1. **CLI Arguments** (highest)
2. **Per-monitor full config** (Mode 3)
3. **Per-monitor overrides** (Mode 2)
4. **Group settings**
5. **Global config** (Mode 1)
6. **Built-in defaults** (lowest)

**Wildcard Resolution Rules**:
- Exact name beats wildcard
- Longer pattern beats shorter (`DP-1*` > `DP-*`)
- Lexicographic order for ties (stable)

### Resolution Algorithm (Updated)

```c
typedef struct {
    config_t global_config;           // Mode 1 base
    config_section_t *sections;       // Mode 2 & 3 sections
    config_t cli_overrides;          // CLI args
    bool cli_active;
} multi_config_t;

config_t* resolve_monitor_config(multi_config_t *cfg, const char *name) {
    config_t *result = calloc(1, sizeof(config_t));
    
    // Find most specific section match
    config_section_t *section = find_best_match(cfg->sections, name);
    
    if (section && section->clear_layers) {
        // Mode 3: Full replacement
        load_section_config(result, section);
    } else {
        // Start with Mode 1 global
        *result = cfg->global_config;
        
        // Apply Mode 2 overrides if found
        if (section) {
            apply_section_overrides(result, section);
        }
    }
    
    // CLI always wins
    if (cfg->cli_active) {
        apply_cli_overrides(result, &cfg->cli_overrides);
    }
    
    return result;
}

// Deterministic pattern matching
config_section_t* find_best_match(config_section_t *sections, const char *name) {
    config_section_t *best = NULL;
    int best_score = -1;
    
    while (sections) {
        int score = match_score(sections->pattern, name);
        if (score > best_score) {
            best = sections;
            best_score = score;
        }
        sections = sections->next;
    }
    return best;
}

int match_score(const char *pattern, const char *name) {
    if (strcmp(pattern, name) == 0) return 1000;  // Exact match
    if (wildcard_match(pattern, name)) {
        return strlen(pattern);  // Longer patterns score higher
    }
    return -1;  // No match
}
```

## Rendering Architecture (Per-Output Scheduling)

### Shared EGL Context Strategy

```c
typedef struct {
    EGLDisplay egl_display;
    EGLContext egl_context;      // Single shared context
    EGLConfig egl_config;
} egl_shared_t;

typedef struct monitor_instance {
    // Monitor info
    char name[64];
    int width, height;
    int scale;
    int refresh_rate;
    
    // Wayland objects
    struct wl_output *output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    
    // EGL (shares context)
    EGLSurface egl_surface;
    
    // Frame scheduling
    struct wl_callback *frame_callback;
    bool frame_pending;
    double last_frame_time;
    double target_frame_time;  // Based on refresh rate
    
    // Workspace tracking (per-monitor independent)
    int current_workspace;
    float workspace_offset_x;  // This monitor's parallax offset
    float workspace_offset_y;
    
    // Config
    config_t *config;  // Resolved config for this monitor
    
    struct monitor_instance *next;
} monitor_instance_t;
```

### Per-Output Frame Callback System

```c
// Frame callback for each monitor
static void frame_callback_done(void *data, struct wl_callback *callback, uint32_t time) {
    monitor_instance_t *monitor = data;
    
    if (callback) {
        wl_callback_destroy(callback);
    }
    
    // Request next frame
    monitor->frame_callback = wl_surface_frame(monitor->surface);
    wl_callback_add_listener(monitor->frame_callback, &frame_listener, monitor);
    
    // Mark ready for rendering
    monitor->frame_pending = false;
    
    // Store timing
    monitor->last_frame_time = get_time_ms();
}

// Main render loop (non-blocking)
void hyprlax_render_loop(hyprlax_context_t *ctx) {
    while (ctx->running) {
        // Poll for events (non-blocking)
        platform_poll_events(ctx, 0);
        
        // Update global animation time
        double now = get_time_ms();
        ctx->animation_time = now;
        
        // Render monitors that need frames
        monitor_instance_t *mon = ctx->monitors;
        while (mon) {
            if (!mon->frame_pending && should_render(mon, now)) {
                render_monitor(ctx, mon);
                mon->frame_pending = true;
            }
            mon = mon->next;
        }
        
        // Small sleep to prevent busy-wait
        usleep(100);  // 0.1ms
    }
}

// Render single monitor
static void render_monitor(hyprlax_context_t *ctx, monitor_instance_t *mon) {
    // Make context current for this surface
    eglMakeCurrent(ctx->egl->display, mon->egl_surface, 
                   mon->egl_surface, ctx->egl->context);
    
    // Set viewport and projection
    glViewport(0, 0, mon->width * mon->scale, mon->height * mon->scale);
    
    // Update uniforms for this monitor
    update_monitor_uniforms(ctx, mon);
    
    // Render layers (shared textures)
    render_layers(ctx, mon->config);
    
    // Swap buffers
    eglSwapBuffers(ctx->egl->display, mon->egl_surface);
}
```

### Texture Cache with Reference Counting

```c
typedef struct texture_entry {
    char *path;
    GLuint texture_id;
    int width, height;
    int ref_count;
    struct texture_entry *next;
} texture_entry_t;

typedef struct {
    texture_entry_t *entries;
    pthread_mutex_t lock;
} texture_cache_t;

// Get or load texture (thread-safe)
GLuint texture_cache_get(texture_cache_t *cache, const char *path) {
    pthread_mutex_lock(&cache->lock);
    
    // Check if already loaded
    texture_entry_t *entry = find_entry(cache, path);
    if (entry) {
        entry->ref_count++;
        pthread_mutex_unlock(&cache->lock);
        return entry->texture_id;
    }
    
    // Load new texture
    GLuint tex_id = load_texture_from_file(path);
    if (tex_id) {
        entry = create_entry(path, tex_id);
        entry->ref_count = 1;
        add_entry(cache, entry);
    }
    
    pthread_mutex_unlock(&cache->lock);
    return tex_id;
}

// Release texture reference
void texture_cache_release(texture_cache_t *cache, const char *path) {
    pthread_mutex_lock(&cache->lock);
    
    texture_entry_t *entry = find_entry(cache, path);
    if (entry) {
        entry->ref_count--;
        if (entry->ref_count == 0) {
            glDeleteTextures(1, &entry->texture_id);
            remove_entry(cache, entry);
        }
    }
    
    pthread_mutex_unlock(&cache->lock);
}
```

## Monitor Hot-plug Support

### Lifecycle Management

```c
// Output added
static void output_added(hyprlax_context_t *ctx, struct wl_output *output) {
    // Create monitor instance
    monitor_instance_t *mon = calloc(1, sizeof(monitor_instance_t));
    
    // Get output info
    get_output_info(output, mon);
    
    // Resolve configuration
    mon->config = resolve_monitor_config(&ctx->multi_config, mon->name);
    
    // Create surface
    mon->surface = wl_compositor_create_surface(ctx->wl_compositor);
    
    // Create layer surface bound to this output
    mon->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ctx->layer_shell, mon->surface, output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "hyprlax");
    
    // Configure layer surface
    zwlr_layer_surface_v1_set_size(mon->layer_surface, 0, 0);  // Fullscreen
    zwlr_layer_surface_v1_set_anchor(mon->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(mon->layer_surface, -1);
    
    // Create EGL surface (shares context)
    mon->egl_surface = create_egl_surface(ctx->egl, mon->surface);
    
    // Setup frame callback
    mon->frame_callback = wl_surface_frame(mon->surface);
    wl_callback_add_listener(mon->frame_callback, &frame_listener, mon);
    
    // Add to list
    add_monitor(ctx, mon);
    
    // Load textures for this monitor's config
    preload_monitor_textures(ctx, mon);
}

// Output removed
static void output_removed(hyprlax_context_t *ctx, struct wl_output *output) {
    monitor_instance_t *mon = find_monitor_by_output(ctx, output);
    if (!mon) return;
    
    // Cancel pending frame
    if (mon->frame_callback) {
        wl_callback_destroy(mon->frame_callback);
    }
    
    // Destroy surfaces
    if (mon->layer_surface) {
        zwlr_layer_surface_v1_destroy(mon->layer_surface);
    }
    if (mon->egl_surface) {
        eglDestroySurface(ctx->egl->display, mon->egl_surface);
    }
    if (mon->surface) {
        wl_surface_destroy(mon->surface);
    }
    
    // Release texture references
    release_monitor_textures(ctx, mon);
    
    // Free config
    free(mon->config);
    
    // Remove from list and free
    remove_monitor(ctx, mon);
    free(mon);
}
```

## Workspace Event Handling (Per-Monitor Independence)

### Compositor Integration
Each compositor provides workspace change events with monitor information:

```c
// Handle workspace change from compositor
void handle_workspace_event(hyprlax_context_t *ctx, 
                           workspace_event_t *event) {
    // Find the monitor this workspace change affects
    monitor_instance_t *mon = find_monitor_by_name(ctx, event->monitor_name);
    if (!mon) return;
    
    // Calculate parallax delta for this monitor only
    int workspace_delta = event->new_workspace - mon->current_workspace;
    mon->current_workspace = event->new_workspace;
    
    // Animate this monitor's parallax independently
    if (workspace_delta != 0) {
        start_parallax_animation(ctx, mon, workspace_delta);
    }
}

// Independent animation per monitor
void start_parallax_animation(hyprlax_context_t *ctx,
                             monitor_instance_t *mon,
                             int workspace_delta) {
    // Calculate target offsets for this monitor
    float shift_amount = mon->config->shift_pixels * workspace_delta;
    
    // Set animation target for this monitor only
    mon->animation_target_x = mon->workspace_offset_x + shift_amount;
    mon->animation_start_time = get_time_ms();
    mon->animating = true;
    
    // Other monitors continue their own animations unaffected
}
```

## IPC Extensions for Multi-Monitor

### New IPC Commands

```c
// Extended IPC commands for per-monitor control
typedef enum {
    // Existing commands...
    IPC_CMD_LIST_MONITORS,
    IPC_CMD_SET_MONITOR,
    IPC_CMD_RELOAD_MONITOR,
    IPC_CMD_ENABLE_MONITOR,
    IPC_CMD_DISABLE_MONITOR,
} ipc_command_t;

// IPC handler extensions
void handle_monitor_command(ipc_context_t *ipc, const char *args) {
    char monitor[64], property[64], value[256];
    
    if (sscanf(args, "set %s %s %s", monitor, property, value) == 3) {
        // Update monitor config
        monitor_instance_t *mon = find_monitor(ctx, monitor);
        if (mon) {
            update_monitor_property(mon->config, property, value);
            // Trigger re-render on next frame
            mon->frame_pending = false;
        }
    }
}
```

## Phased Implementation (TOML-First)

### Phase 0: Zero-Config Multi-Monitor (NEW)
- Default behavior: automatically use all monitors (no flag needed)
- Per-monitor independent workspace tracking and parallax animation
- CLI options available for overrides (--primary-only, --monitor, etc.)
- No configuration changes required
- Automatic monitor detection and hotplug support

### Phase 1: Multi-Surface Foundation
- Implement per-output surfaces with shared EGL context
- Per-output frame callbacks and scheduling
- Texture cache with reference counting
- Validate 144 FPS target per monitor

### Phase 2: TOML Configuration
- Integrate tomlc99 library
- Support all three modes in TOML
- Pattern matching and precedence rules
- Well-established format, less code to maintain

### Phase 3: Legacy Compatibility
- Keep simple .conf parser for backwards compatibility
- Auto-detect format by extension
- Provide conversion tool
- Minimal maintenance burden

### Phase 4: Advanced Features
- Monitor groups and synchronized animations
- "Continuous" panoramic mode (coordinated rendering)
- Runtime IPC per-monitor control

## Performance Targets

### Rendering Budget
- Target: <6.9ms per surface at 144Hz
- Measurement points:
  - `eglMakeCurrent`: <0.1ms
  - Layer rendering: <5ms for 5 layers
  - `eglSwapBuffers`: <1ms
  - Frame callback overhead: <0.5ms

### Memory Targets
- Shared textures: Single copy in VRAM
- Per-monitor overhead: <10MB (surfaces, configs)
- Zero per-frame allocations

## Testing Strategy

### Unit Tests
```c
// Config resolution precedence
void test_config_precedence() {
    assert(cli_override_wins());
    assert(exact_match_beats_wildcard());
    assert(longer_pattern_beats_shorter());
    assert(lexicographic_tiebreak());
}

// Texture cache refcounting
void test_texture_cache() {
    assert(shared_texture_single_load());
    assert(refcount_on_monitor_add());
    assert(cleanup_on_zero_refcount());
}
```

### Integration Tests
- Multi-monitor lifecycle with add/remove
- Mixed refresh rates (60Hz, 144Hz, 240Hz)
- Fractional scaling (1.25x, 1.5x)
- Hot-plug during animation

### Performance Validation
- Profile with `gpuvis` and `apitrace`
- Measure frame timing with `WAYLAND_DEBUG=1`
- Validate no allocation with `heaptrack`

## Decision Points Resolved

1. **Configuration Format**: Extend .conf, optional TOML via flag
2. **Precedence Order**: CLI > Mode3 > Mode2 > Groups > Mode1 > Defaults
3. **Default Behavior**: Clone global config, enable on all monitors
4. **Continuous Mode**: Coordinated per-surface rendering (not stretched)
5. **Monitor Identification**: Name + EDID hash when available

## Conclusion

This revised design addresses all technical concerns while maintaining the flexibility of three configuration modes. By extending the existing .conf parser and making TOML optional, we avoid dependency creep. The per-output frame scheduling ensures smooth rendering across mixed refresh rates, while the shared EGL context and texture cache minimize resource usage. The phased implementation allows for incremental development with continuous validation against performance targets.