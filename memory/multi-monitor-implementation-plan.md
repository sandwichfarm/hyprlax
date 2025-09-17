# Multi-Monitor Support Implementation Plan

## Overview
This document provides a detailed, actionable implementation plan for adding multi-monitor support to hyprlax. The plan is organized into phases with clear dependencies, validation criteria, and risk mitigation strategies. 

**Key Changes**:
- TOML as primary configuration format (using tomlc99 library)
- Phase 0 adds zero-config multi-monitor support
- Simplified implementation by removing custom parser complexity

## Phase 0: Zero-Config Multi-Monitor Support (COMPLETED)
**Duration**: 1 week  
**Goal**: Default multi-monitor behavior with no configuration changes

### 0.1 Default Behavior Definition
**Default**: Automatically fill all connected monitors with parallax wallpaper
**Per-Monitor Independence**: Each monitor's parallax reacts to its own workspace changes

**Implementation**:
```c
// Multi-monitor modes (user can override default)
typedef enum {
    MULTI_MON_ALL,        // Use all monitors (DEFAULT)
    MULTI_MON_PRIMARY,    // Only primary monitor
    MULTI_MON_SPECIFIC,   // User-specified monitors
} multi_monitor_mode_t;

// Per-monitor workspace tracking
typedef struct monitor_instance {
    char name[64];
    int current_workspace;  // This monitor's active workspace
    float workspace_offset_x;  // This monitor's parallax offset
    float workspace_offset_y;
    // ... other fields
} monitor_instance_t;

// In hyprlax_init() - default behavior
ctx->multi_mode = MULTI_MON_ALL;  // Use all monitors by default
// Detect and setup all monitors automatically
```

### 0.2 Per-Monitor Workspace Tracking
**Files to Modify**:
- `src/platform/wayland.c` - Track which monitor events come from
- `src/compositor/*.c` - Parse monitor info from workspace events
- `src/hyprlax_main.c` - Update only affected monitor's parallax

**Implementation**:
```c
// Handle workspace change for specific monitor
void handle_workspace_change(hyprlax_context_t *ctx, 
                            const char *monitor_name,
                            int new_workspace) {
    monitor_instance_t *mon = find_monitor_by_name(ctx, monitor_name);
    if (mon) {
        int delta = new_workspace - mon->current_workspace;
        mon->current_workspace = new_workspace;
        
        // Animate only this monitor's parallax
        animate_monitor_parallax(ctx, mon, delta);
    }
}
```

**Tasks**:
- [ ] Detect all connected monitors at startup
- [ ] Create surface for each monitor automatically
- [ ] Track workspace per monitor
- [ ] Independent parallax animation per monitor
- [ ] Handle monitor hotplug events

### 0.3 CLI Options for Override
```bash
# Default: use all monitors (no flag needed!)
hyprlax --config simple.conf

# User overrides for different behavior
hyprlax --config simple.conf --primary-only       # Only primary monitor
hyprlax --config simple.conf --monitor DP-1       # Specific monitor(s)
hyprlax --config simple.conf --disable-monitor HDMI-2  # Exclude specific

# Still support CLI-only config on all monitors (default)
hyprlax --layer bg.png:0.1:1.0  # Shows on all monitors
```

### 0.4 Validation Milestone 0
**Success Criteria**:
- [ ] All monitors automatically used (no flag needed)
- [ ] Each monitor tracks its own workspace
- [ ] Parallax animations are independent per monitor
- [ ] Override options work correctly
- [ ] No breaking changes for existing users

---

## Phase 0.5: Compositor Workspace Abstraction (CRITICAL - NEW)
**Duration**: 1 week
**Goal**: Support different compositor workspace models

### 0.5.1 Workspace Model Types
```c
typedef enum {
    WS_MODEL_GLOBAL_NUMERIC,     // Hyprland, Sway (default)
    WS_MODEL_PER_OUTPUT_NUMERIC, // Niri, Hyprland+split-monitor
    WS_MODEL_TAG_BASED,          // River
    WS_MODEL_SET_BASED,          // Wayfire with wsets
} workspace_model_t;
```

### 0.5.2 Flexible Workspace Context
```c
typedef struct workspace_context {
    workspace_model_t model;
    union {
        int workspace_id;           // Numeric workspace
        uint32_t visible_tags;      // River tag bitmask
        struct {
            int set_id;
            int workspace_id;
        } wayfire_set;
    } data;
} workspace_context_t;
```

### 0.5.3 Update Monitor Structure
**Files to Modify**:
- `src/core/monitor.h` - Add flexible workspace tracking
- `src/core/monitor.c` - Implement model-agnostic offset calculation

### 0.5.4 Compositor Detection
**Files to Create**:
- `src/compositor/workspace_models.c` - Model detection and handling
- `src/compositor/workspace_models.h` - Interfaces

### 0.5.5 Handle Workspace Stealing/Movement
- Sway/Hyprland: Workspace can move to focused output
- Niri: Workspace can move between monitors
- Implement atomic multi-monitor updates

### 0.5.6 Validation Milestone 0.5
- [ ] River tag support working
- [ ] Niri vertical stacks working
- [ ] Workspace stealing handled correctly
- [ ] Wayfire wsets detected and handled
- [ ] No regression for numeric workspaces

---

## Phase 1: Foundation - Multi-Surface Architecture
**Duration**: 2-3 weeks  
**Goal**: Enable per-monitor surfaces with shared resources, maintaining current functionality

### 1.1 Refactor Monitor Data Structures
**Files to Modify**:
- `src/include/hyprlax.h`
- `src/include/platform.h`
- `src/hyprlax_main.c`

**Changes**:
```c
// In hyprlax.h - Replace single monitor tracking with list
typedef struct hyprlax_context {
    // Remove:
    // int current_monitor;
    
    // Add:
    monitor_list_t *monitors;        // Active monitors
    monitor_instance_t *primary;     // Primary monitor reference
    texture_cache_t *texture_cache;  // Shared texture cache
    egl_shared_t *egl_shared;       // Shared EGL context
    // ...
} hyprlax_context_t;
```

**New Files**:
- `src/core/monitor.c` - Monitor instance management
- `src/core/monitor.h` - Monitor structures and APIs
- `src/core/texture_cache.c` - Texture caching with refcount
- `src/core/texture_cache.h` - Cache interfaces

**Tasks**:
- [ ] Create monitor_instance_t structure with all per-monitor state
- [ ] Implement monitor list management (add, remove, find)
- [ ] Create texture cache with thread-safe refcounting
- [ ] Update hyprlax_context_t to use monitor list

### 1.2 Wayland Platform Multi-Surface Support
**Files to Modify**:
- `src/platform/wayland.c`
- `src/platform/wayland.h` (create if needed)

**Changes**:
```c
// Per-monitor surface creation
static int wayland_create_monitor_surface(platform_t *platform, 
                                         monitor_instance_t *monitor) {
    wayland_data_t *wl = (wayland_data_t*)platform->data;
    
    // Create surface for this monitor
    monitor->wl_surface = wl_compositor_create_surface(wl->compositor);
    
    // Create layer surface bound to specific output
    monitor->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wl->layer_shell,
        monitor->wl_surface,
        monitor->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        "hyprlax"
    );
    
    // Configure for fullscreen
    zwlr_layer_surface_v1_set_size(monitor->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(monitor->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    
    // Set exclusive zone
    zwlr_layer_surface_v1_set_exclusive_zone(monitor->layer_surface, -1);
    
    wl_surface_commit(monitor->wl_surface);
    return HYPRLAX_SUCCESS;
}
```

**Tasks**:
- [ ] Add wl_output listener for monitor hotplug events
- [ ] Implement per-monitor surface creation
- [ ] Handle output geometry and scale events
- [ ] Add output removal cleanup
- [ ] Update registry_global to track outputs

### 1.3 Shared EGL Context Implementation
**Files to Modify**:
- `src/renderer/gles2.c`
- `src/platform/wayland.c`

**Changes**:
```c
// In gles2.c - Shared context management
typedef struct {
    EGLDisplay display;
    EGLContext context;  // Single shared context
    EGLConfig config;
} egl_shared_t;

// Per-monitor EGL surface creation
static int create_monitor_egl_surface(egl_shared_t *shared,
                                     monitor_instance_t *monitor) {
    // Create EGL window surface for monitor
    monitor->egl_window = wl_egl_window_create(
        monitor->wl_surface,
        monitor->width * monitor->scale,
        monitor->height * monitor->scale
    );
    
    monitor->egl_surface = eglCreateWindowSurface(
        shared->display,
        shared->config,
        monitor->egl_window,
        NULL
    );
    
    return (monitor->egl_surface != EGL_NO_SURFACE) ? 0 : -1;
}
```

**Tasks**:
- [ ] Create egl_shared_t structure
- [ ] Modify renderer init to create single shared context
- [ ] Implement per-monitor EGL surface creation
- [ ] Add eglMakeCurrent management for surface switching
- [ ] Ensure proper cleanup on monitor removal

### 1.4 Per-Output Frame Scheduling
**Files to Modify**:
- `src/hyprlax_main.c`
- `src/platform/wayland.c`

**Implementation**:
```c
// Frame callback management
static const struct wl_callback_listener frame_listener = {
    .done = frame_callback_done
};

static void frame_callback_done(void *data, struct wl_callback *cb, uint32_t time) {
    monitor_instance_t *monitor = data;
    
    if (cb) wl_callback_destroy(cb);
    
    // Request next frame
    monitor->frame_callback = wl_surface_frame(monitor->wl_surface);
    wl_callback_add_listener(monitor->frame_callback, &frame_listener, monitor);
    
    // Mark ready for rendering
    monitor->frame_pending = false;
    monitor->last_frame_time = get_monotonic_time_ms();
}

// Modified main loop
int hyprlax_run(hyprlax_context_t *ctx) {
    while (ctx->running) {
        // Non-blocking event poll
        platform_poll_events(ctx->platform, 0);
        
        // Update global animation time
        double now = get_monotonic_time_ms();
        update_animation_time(ctx, now);
        
        // Render monitors that need frames
        monitor_instance_t *mon = ctx->monitors->head;
        while (mon) {
            if (!mon->frame_pending && should_render_monitor(mon, now)) {
                render_monitor_frame(ctx, mon);
                mon->frame_pending = true;
            }
            mon = mon->next;
        }
        
        // Minimal sleep to prevent busy-wait
        usleep(100);
    }
    return 0;
}
```

**Tasks**:
- [ ] Implement frame callback system per monitor
- [ ] Modify main loop for non-blocking operation
- [ ] Add per-monitor render scheduling
- [ ] Track frame timing per monitor
- [ ] Handle mixed refresh rates properly

### 1.5 Validation Milestone 1
**Success Criteria**:
- [ ] Multiple surfaces rendering (clone mode)
- [ ] No performance regression (maintain 144 FPS)
- [ ] Proper cleanup on exit
- [ ] No memory leaks (valgrind clean)

**Test Cases**:
```bash
# Single monitor (regression test)
./hyprlax --config examples/pixel-city/parallax.conf

# Multi-monitor clone
./hyprlax --config examples/pixel-city/parallax.conf --all-monitors

# Performance test
./hyprlax --debug --fps 144 --benchmark
```

---

## Phase 2: TOML Configuration System
**Duration**: 1 week  
**Goal**: Implement TOML-based configuration for all three modes

### 2.1 Integrate tomlc99 Library
**Files to Add**:
- `src/vendor/toml.h` - tomlc99 single header
- `src/vendor/toml.c` - tomlc99 implementation
- `src/core/config_toml.c` - TOML config loader
- `src/core/config_toml.h` - TOML config interfaces

**Makefile Changes**:
```makefile
# Add tomlc99 to build
SRCS += src/vendor/toml.c src/core/config_toml.c
```

**Tasks**:
- [ ] Vendor tomlc99 library
- [ ] Add to build system
- [ ] Create config_toml module

### 2.2 TOML Configuration Parser
**Implementation**:
```c
// src/core/config_toml.c
#include "vendor/toml.h"

typedef struct {
    config_t global_config;
    hashmap_t *monitor_overrides;  // Mode 2
    hashmap_t *monitor_configs;    // Mode 3
    hashmap_t *monitor_groups;
} toml_config_t;

int config_load_toml(const char *path, toml_config_t *config) {
    FILE *fp = fopen(path, "r");
    char errbuf[256];
    
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    
    if (!conf) {
        fprintf(stderr, "TOML error: %s\n", errbuf);
        return -1;
    }
    
    // Parse [global] section
    toml_table_t *global = toml_table_in(conf, "global");
    if (global) {
        parse_global_config(global, &config->global_config);
        
        // Parse [[global.layers]] array
        toml_array_t *layers = toml_array_in(global, "layers");
        if (layers) {
            parse_global_layers(layers, &config->global_config);
        }
    }
    
    // Parse [monitors.overrides] for Mode 2
    toml_table_t *monitors = toml_table_in(conf, "monitors");
    if (monitors) {
        toml_table_t *overrides = toml_table_in(monitors, "overrides");
        if (overrides) {
            parse_monitor_overrides(overrides, config->monitor_overrides);
        }
        
        // Parse [monitors.configs] for Mode 3
        toml_table_t *configs = toml_table_in(monitors, "configs");
        if (configs) {
            parse_monitor_configs(configs, config->monitor_configs);
        }
    }
    
    // Parse [groups] for advanced features
    toml_table_t *groups = toml_table_in(conf, "groups");
    if (groups) {
        parse_monitor_groups(groups, config->monitor_groups);
    }
    
    toml_free(conf);
    return 0;
}

// Helper to parse global config
static void parse_global_config(toml_table_t *global, config_t *config) {
    toml_datum_t d;
    
    d = toml_double_in(global, "duration");
    if (d.ok) config->animation_duration = d.u.d;
    
    d = toml_int_in(global, "shift");
    if (d.ok) config->shift_pixels = d.u.i;
    
    d = toml_string_in(global, "easing");
    if (d.ok) {
        config->default_easing = easing_from_string(d.u.s);
        free(d.u.s);
    }
    
    d = toml_int_in(global, "fps");
    if (d.ok) config->target_fps = d.u.i;
}
```

**Tasks**:
- [ ] Implement TOML parser wrapper
- [ ] Parse all three configuration modes
- [ ] Handle nested structures properly
- [ ] Support array parsing for layers

### 2.2 Configuration Resolution System
**Files to Create**:
- `src/core/config_resolver.c`
- `src/core/config_resolver.h`

**Implementation**:
```c
// Pattern matching with scoring
typedef struct {
    const char *pattern;
    const char *name;
    int score;  // Higher = better match
} pattern_match_t;

int calculate_match_score(const char *pattern, const char *name) {
    // Exact match
    if (strcmp(pattern, name) == 0) return 1000;
    
    // Pattern match
    if (wildcard_match(pattern, name)) {
        // Longer patterns score higher
        return 100 + strlen(pattern);
    }
    
    return -1;  // No match
}

// Main resolution function
config_t* resolve_monitor_config(multi_config_t *cfg, 
                                const char *monitor_name) {
    config_t *result = calloc(1, sizeof(config_t));
    
    // Find best matching section
    config_section_t *best = find_best_section(cfg->sections, monitor_name);
    
    if (best && best->clear_layers) {
        // Mode 3: Complete replacement
        config_from_section(result, best);
    } else {
        // Mode 1: Start with global
        *result = cfg->global_config;
        
        // Mode 2: Apply overrides
        if (best) {
            apply_section_overrides(result, &best->overrides);
            if (best->layers) {
                merge_layers(result, best->layers);
            }
        }
    }
    
    // CLI overrides always win
    if (cfg->cli_active) {
        apply_cli_overrides(result, &cfg->cli_overrides);
    }
    
    return result;
}
```

**Tasks**:
- [ ] Implement wildcard pattern matching
- [ ] Create scoring system for precedence
- [ ] Build configuration resolution pipeline
- [ ] Add override application logic
- [ ] Test with various patterns and precedence

### 2.3 Integration with Monitor System
**Files to Modify**:
- `src/hyprlax_main.c`
- `src/core/monitor.c`

**Changes**:
```c
// On monitor add
static void on_monitor_added(hyprlax_context_t *ctx, 
                            monitor_info_t *info) {
    // Create monitor instance
    monitor_instance_t *mon = monitor_create(info);
    
    // Resolve configuration for this monitor
    mon->config = resolve_monitor_config(&ctx->multi_config, info->name);
    
    // Create surfaces
    platform_create_monitor_surface(ctx->platform, mon);
    renderer_create_monitor_surface(ctx->renderer, mon);
    
    // Preload textures
    preload_monitor_textures(ctx, mon);
    
    // Add to active list
    monitor_list_add(ctx->monitors, mon);
}
```

**Tasks**:
- [ ] Hook configuration resolution into monitor creation
- [ ] Support runtime config reloading per monitor
- [ ] Handle configuration changes via IPC
- [ ] Validate override application

### 2.4 Validation Milestone 2
**Success Criteria**:
- [ ] Section parsing works correctly
- [ ] Pattern matching is deterministic
- [ ] Overrides apply properly
- [ ] Backwards compatibility maintained

**Test Configurations**:
```ini
# test-overrides.conf
fps = 60
layer bg.png 0.1 1.0

[monitor:DP-1]
shift = 100

[monitor:DP-*]
scale_mode = fit

[monitor:HDMI-1]
clear_layers = true
layer ultrawide.png 0.05 1.0
```

---

## Phase 3: Legacy .conf Compatibility  
**Duration**: 3-4 days  
**Goal**: Maintain backwards compatibility with existing configs

### 3.1 Legacy Config Converter
**Files to Create**:
- `src/core/config_legacy.c` - Legacy parser and converter
- `src/core/config_legacy.h`

**Implementation**:
```c
// Convert legacy .conf to internal format
int config_convert_legacy(const char *path, config_t *config) {
    FILE *f = fopen(path, "r");
    char line[512];
    
    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        
        // Parse simple key-value pairs
        char key[64], value[256];
        if (sscanf(line, "%s %s", key, value) == 2) {
            if (strcmp(key, "duration") == 0) {
                config->animation_duration = atof(value);
            } else if (strcmp(key, "shift") == 0) {
                config->shift_pixels = atoi(value);
            }
            // ... other properties
        }
        
        // Parse layer lines
        if (strncmp(line, "layer ", 6) == 0) {
            parse_legacy_layer(line + 6, config);
        }
    }
    
    fclose(f);
    return 0;
}

// Auto-detect format
config_format_t detect_config_format(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext) {
        if (strcmp(ext, ".toml") == 0) return FORMAT_TOML;
        if (strcmp(ext, ".conf") == 0) return FORMAT_LEGACY;
    }
    // Try to detect by content
    return probe_format_by_content(path);
}
```

**Tasks**:
- [ ] Implement legacy .conf parser
- [ ] Convert to internal config format
- [ ] Auto-detect config format
- [ ] Provide conversion tool for users

### 3.2 Migration Helper
**New Tool**:
```bash
# Conversion utility
hyprlax --convert old.conf > new.toml

# Validate TOML config
hyprlax --validate config.toml
```

**Tasks**:
- [ ] Create conversion utility
- [ ] Add validation command
- [ ] Document migration process

### 3.3 Validation Milestone 3
**Success Criteria**:
- [ ] External configs load properly
- [ ] Full per-monitor configs work
- [ ] TOML support works when enabled
- [ ] Performance unchanged

---

## Phase 4: Advanced Features
**Duration**: 1-2 weeks  
**Goal**: Groups and continuous mode

### 4.1 Monitor Groups
**Files to Create**:
- `src/core/monitor_group.c`
- `src/core/monitor_group.h`

**Implementation**:
```c
typedef struct monitor_group {
    char *name;
    char **monitor_names;
    int monitor_count;
    bool sync_animations;
    bool continuous_mode;
    config_overrides_t overrides;
    struct monitor_group *next;
} monitor_group_t;

// Group animation sync
void sync_group_animations(monitor_group_t *group, double time) {
    // Find group members
    for (int i = 0; i < group->monitor_count; i++) {
        monitor_instance_t *mon = find_monitor(group->monitor_names[i]);
        if (mon) {
            // Sync animation time
            mon->animation_time = time;
            mon->animation_phase = 0;  // Reset phase
        }
    }
}
```

**Tasks**:
- [ ] Implement group configuration parsing
- [ ] Add group membership tracking
- [ ] Implement animation synchronization
- [ ] Support group-level overrides

### 4.2 Continuous/Panoramic Mode
**Implementation**:
```c
// Coordinated panoramic rendering
typedef struct {
    monitor_group_t *group;
    int total_width;
    int total_height;
    float *monitor_offsets;  // X offset for each monitor
} panoramic_state_t;

void render_panoramic_monitor(hyprlax_context_t *ctx,
                             monitor_instance_t *mon,
                             panoramic_state_t *pano) {
    // Calculate this monitor's portion of panorama
    float x_offset = pano->monitor_offsets[mon->index];
    float width_ratio = (float)mon->width / pano->total_width;
    
    // Adjust texture coordinates
    float tex_x_start = x_offset / pano->total_width;
    float tex_x_end = tex_x_start + width_ratio;
    
    // Render with adjusted coordinates
    render_layers_panoramic(ctx, mon, tex_x_start, tex_x_end);
}
```

**Tasks**:
- [ ] Calculate total canvas size for groups
- [ ] Implement coordinate mapping
- [ ] Handle mixed scales and resolutions
- [ ] Test visual alignment

### 4.3 IPC Extensions
**Files to Modify**:
- `src/ipc.c`
- `src/ipc.h`

**New Commands**:
```c
// Per-monitor IPC commands
case IPC_CMD_LIST_MONITORS:
    list_active_monitors(response);
    break;
    
case IPC_CMD_SET_MONITOR:
    // hyprlax ctl set-monitor DP-1 shift 150
    set_monitor_property(args, response);
    break;
    
case IPC_CMD_RELOAD_MONITOR:
    // hyprlax ctl reload-monitor DP-1
    reload_monitor_config(args, response);
    break;
```

**Tasks**:
- [ ] Add monitor-specific IPC commands
- [ ] Implement runtime property changes
- [ ] Support monitor enable/disable
- [ ] Add group control commands

### 4.4 Final Validation
**Success Criteria**:
- [ ] All three modes work correctly
- [ ] Groups synchronize properly
- [ ] Continuous mode aligns visually
- [ ] 144 FPS maintained with 3 monitors
- [ ] Memory usage acceptable
- [ ] Zero memory leaks
- [ ] IPC commands work

---

## Testing Strategy

### Unit Test Suite
```c
// tests/test_monitor.c
void test_monitor_lifecycle();
void test_pattern_matching();
void test_config_resolution();
void test_texture_cache();

// tests/test_config_extended.c
void test_section_parsing();
void test_override_precedence();
void test_external_configs();
```

### Integration Tests
```bash
#!/bin/bash
# test-multi-monitor.sh

# Test basic multi-monitor
hyprlax --config test/multi.conf &
PID=$!
sleep 5
check_monitors 2
kill $PID

# Test hot-plug
hyprlax --config test/multi.conf &
PID=$!
simulate_monitor_remove "HDMI-1"
sleep 2
simulate_monitor_add "HDMI-1"
check_no_crash
kill $PID
```

### Performance Benchmarks
```c
// benchmark/multi_monitor_perf.c
void benchmark_render_time() {
    // Measure per-monitor render time
    // Target: <6.9ms at 144Hz
}

void benchmark_memory_usage() {
    // Check texture sharing
    // Monitor memory growth
}
```

---

## Risk Mitigation

### Risk 1: EGL Context Issues
**Mitigation**: 
- Test context sharing early
- Have fallback to per-surface contexts
- Profile eglMakeCurrent overhead

### Risk 2: Performance Regression
**Mitigation**:
- Continuous benchmarking
- Profile each phase
- Optimization pass after each phase

### Risk 3: Configuration Complexity
**Mitigation**:
- Extensive examples
- Clear documentation
- Config validation tool

### Risk 4: Wayland Compositor Differences
**Mitigation**:
- Test on Hyprland, Sway, River, Wayfire
- Handle capability differences
- Graceful degradation

---

## Timeline Summary

| Phase | Duration | Milestone |
|-------|----------|-----------|
| Phase 0: Zero-Config | 1 week | Multi-monitor with no config changes |
| Phase 1: Foundation | 2-3 weeks | Multi-surface rendering working |
| Phase 2: TOML Config | 1 week | All three modes with TOML |
| Phase 3: Legacy Support | 3-4 days | Backwards compatibility |
| Phase 4: Advanced | 1-2 weeks | Groups and continuous mode |
| **Total** | **5-7 weeks** | **Full multi-monitor support** |

---

## Definition of Done

- [ ] All three configuration modes work
- [ ] Performance targets met (144 FPS)
- [ ] No memory leaks
- [ ] Documentation complete
- [ ] Test coverage >80%
- [ ] Works on major Wayland compositors
- [ ] Backwards compatibility maintained
- [ ] Code review passed

---

## Next Steps

1. Review and approve implementation plan
2. Set up feature branch `feature/multi-monitor`
3. Begin Phase 1 implementation
4. Weekly progress reviews
5. Continuous integration testing

This implementation plan provides a clear path from the current single-monitor architecture to full multi-monitor support with three configuration modes, while maintaining performance and compatibility requirements.