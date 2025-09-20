# Multi-Monitor Support Design Document

## Executive Summary

This document outlines the design considerations and implementation strategy for adding multi-monitor support to hyprlax. The goal is to enable hyprlax to display parallax wallpapers across multiple monitors with flexible configuration options that support both simplicity and creative control.

## Current State Analysis

### Existing Architecture
- **Single Instance**: hyprlax runs as a single process with IPC-based single-instance protection
- **Monitor Awareness**: The codebase has basic monitor tracking (`current_monitor` field) but only uses it for workspace switching
- **Single Surface**: Currently creates one Wayland layer surface that spans the primary monitor
- **Configuration**: Simple line-based config format with global settings

### Key Components Affected
1. **Platform Layer** (`platform/wayland.c`): Currently creates single surface
2. **Compositor Integration**: Has monitor query capabilities but unused for multi-monitor
3. **Renderer**: Assumes single viewport/framebuffer
4. **Configuration System**: Simple parser, no hierarchical structure

## Design Goals

1. **Flexibility**: Support different use cases from simple mirroring to complex per-monitor setups
2. **Performance**: Minimize resource usage when possible (shared textures, single process)
3. **Backwards Compatibility**: Existing single-monitor configs should continue to work
4. **Creative Freedom**: Enable artistic control over multi-monitor compositions
5. **Simplicity**: Default behavior should "just work" for common cases

## Configuration Modes (All Supported Simultaneously)

The system will support all three configuration modes simultaneously, with a clear hierarchy of precedence and the ability to mix and match approaches:

### Mode 1: Single Config Across All Monitors (CLI & Simple Config)
**Use Case**: Simple setups, consistent aesthetic across displays, quick testing
```toml
# Default behavior - applies to all monitors
[global]
duration = 4
shift = 200
easing = "expo"
fps = 60

[[layers]]
image = "background.png"
shift = 0.1
opacity = 1.0
blur = 0.0
```

**Pros**:
- Simple and intuitive
- Works with CLI arguments
- Minimal configuration needed
- Shared texture memory

**Cons**:
- No per-monitor customization
- May look stretched/wrong on different aspect ratios

### Mode 2: Global Config with Per-Monitor Overrides
**Use Case**: Mostly consistent setup with tweaks for specific monitors
```toml
[global]
duration = 4
shift = 200
easing = "expo"

[[layers]]
image = "background.png"
shift = 0.1

# Override for specific monitor
[monitors.DP-1]
shift = 150  # Less shift on smaller monitor
scale_mode = "fit"  # Different scaling

[monitors.HDMI-2]
enabled = false  # Disable on this monitor
```

**Pros**:
- Good balance of simplicity and flexibility
- Shared base configuration
- Can handle different monitor sizes gracefully

**Cons**:
- More complex parser needed
- Monitor identification challenges

### Mode 3: Full Per-Monitor Configuration
**Use Case**: Creative setups, different themes per monitor, maximum control
```toml
[global]
fps = 60
# Shared settings only

[monitors.DP-1]
config = "nature-theme.toml"

[monitors.DP-2]
duration = 2
shift = 100
[[monitors.DP-2.layers]]
image = "city-scape-1.png"
shift = 0.2
[[monitors.DP-2.layers]]
image = "city-scape-2.png"
shift = 0.4
```

**Pros**:
- Maximum creative control
- Different themes per workspace/monitor
- Can optimize for each display's characteristics

**Cons**:
- Complex configuration
- Higher memory usage (duplicate textures)
- More complex implementation

## Unified Multi-Mode Configuration System

### Configuration Precedence and Resolution

All three modes work together with a clear precedence hierarchy:

1. **CLI Arguments** (Highest Priority)
   - Applies Mode 1 globally to all monitors
   - Useful for testing and temporary overrides
   - Example: `hyprlax --config base.conf --shift 300`

2. **Per-Monitor Config** (Mode 3)
   - Completely replaces global config for specified monitors
   - Example: `[monitors."DP-1"].config = "ultrawide.toml"`

3. **Monitor Overrides** (Mode 2)
   - Selective overrides of global settings
   - Example: `[monitors."HDMI-1"].shift = 150`

4. **Global Config** (Base)
   - Default for all monitors without specific config
   - Inherited by monitors with overrides

### Resolution Algorithm

```python
def get_monitor_config(monitor_name, ctx):
    # 1. Check for full per-monitor config (Mode 3)
    if monitor_name in ctx.monitor_configs:
        config = load_config(ctx.monitor_configs[monitor_name])
        # CLI args can still override even per-monitor configs
        apply_cli_overrides(config, ctx.cli_args)
        return config
    
    # 2. Start with global config
    config = copy(ctx.global_config)
    
    # 3. Apply monitor-specific overrides (Mode 2)
    if monitor_name in ctx.monitor_overrides:
        apply_overrides(config, ctx.monitor_overrides[monitor_name])
    
    # 4. Apply CLI overrides last (Mode 1)
    apply_cli_overrides(config, ctx.cli_args)
    
    return config
```

### Configuration File Structure Supporting All Modes

```toml
# hyprlax.toml - Unified configuration supporting all modes

version = "2.0"

# MODE 1 & BASE: Global defaults applied to all monitors
[global]
duration = 4.0
shift = 200
easing = "expo"
fps = 60
scale_mode = "cover"

# Global layers (Mode 1 default)
[[layers]]
image = "backgrounds/layer1.png"
shift_multiplier = 0.1
opacity = 1.0

# MODE 2: Per-monitor overrides
[monitors.overrides]
  # Pattern matching for multiple monitors
  ["DP-*"]
  scale_mode = "fit"  # All DP monitors use fit
  
  ["DP-1"]
  shift = 150  # DP-1 specifically uses less shift
  
  ["HDMI-1"]
  # Can override just specific properties
  duration = 2.0
  # Layers are inherited from global unless cleared
  clear_layers = false

# MODE 3: Complete per-monitor configurations
[monitors.configs]
  # This monitor uses a completely different config
  ["DP-2"]
  config = "configs/ultrawide.toml"  # External file
  
  # Or inline complete configuration
  ["HDMI-2"]
  inline = true
  duration = 6.0
  shift = 100
  [[monitors.configs."HDMI-2".layers]]
  image = "vertical/layer1.png"
  shift_multiplier = 0.05

# Advanced: Mixed mode example
[monitors.groups]
  # A group can have shared overrides
  [groups.office]
  monitors = ["DP-1", "DP-3"]
  # These monitors share overrides
  shift = 150
  duration = 3.0
  
  [groups.gaming]
  monitors = ["HDMI-1"]
  config = "configs/gaming.toml"  # Group uses Mode 3
```

### CLI Integration with Config Files

The CLI can work with any configuration mode:

```bash
# Mode 1: Pure CLI (applies to all monitors)
hyprlax --shift 200 --duration 4 --layer "bg.png:0.1:1.0"

# Mode 1 with config file base
hyprlax --config simple.conf --shift 300  # Overrides shift in config

# Mode 2/3 with CLI override
hyprlax --config complex.toml --fps 120  # Overrides FPS globally

# Disable specific monitor via CLI
hyprlax --config multi.toml --disable-monitor "HDMI-2"

# Test on single monitor only
hyprlax --config multi.toml --only-monitor "DP-1"
```

## Proposed Implementation Strategy

### Phase 1: Architecture Refactoring
1. **Multi-Surface Support**
   - Refactor platform layer to support multiple Wayland surfaces
   - One surface per monitor output
   - Track surface-to-monitor mapping

2. **Rendering Pipeline**
   - Support multiple render targets
   - Per-surface viewport management
   - Shared texture resources where possible

3. **Monitor Detection & Tracking**
   ```c
   typedef struct monitor_instance {
       char name[64];           // "DP-1", "HDMI-2"
       int id;                  // Compositor's monitor ID
       int x, y;               // Position in global space
       int width, height;      // Resolution
       float scale;            // HiDPI scale factor
       void *surface;          // Platform surface
       void *render_target;    // Renderer target
       config_t *config;       // Monitor-specific or shared config
       struct monitor_instance *next;
   } monitor_instance_t;
   ```

### Phase 2: Multi-Mode Configuration System

#### Dual-Format Support
**Supporting Both Formats**:
1. **Legacy .conf**: For simple Mode 1 configurations and backwards compatibility
2. **TOML**: For Modes 2 and 3 with hierarchical configuration needs

**Format Detection**:
```c
config_format_t detect_config_format(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext) {
        if (strcmp(ext, ".conf") == 0) return FORMAT_LEGACY;
        if (strcmp(ext, ".toml") == 0) return FORMAT_TOML;
    }
    // Try to parse first line to auto-detect
    return probe_config_format(path);
}
```

#### Configuration Schema
```toml
# hyprlax.toml - Proposed configuration format

version = "2.0"  # Config format version

# Global defaults
[defaults]
duration = 4.0
shift = 200
easing = "expo"
fps = 60
scale_mode = "cover"  # cover, fit, stretch, tile

# Global layers (applied to all monitors unless overridden)
[[layers]]
image = "backgrounds/layer1.png"
shift_multiplier = 0.1
opacity = 1.0
blur = 0.0

[[layers]]
image = "backgrounds/layer2.png"
shift_multiplier = 0.3
opacity = 0.8
blur = 0.2

# Monitor-specific configurations
[monitors]

  # Can use wildcards for matching
  [monitors."DP-*"]
  scale_mode = "fit"
  
  # Specific monitor override
  [monitors."DP-1"]
  shift = 150  # Override global shift
  
  # Can specify completely different layers
  [monitors."HDMI-1"]
  clear_global_layers = true  # Don't inherit global layers
  
    [[monitors."HDMI-1".layers]]
    image = "ultrawide/layer1.png"
    shift_multiplier = 0.05
    
    [[monitors."HDMI-1".layers]]
    image = "ultrawide/layer2.png"
    shift_multiplier = 0.15

# Advanced: Monitor groups for synchronized effects
[groups]
  [groups.main]
  monitors = ["DP-1", "DP-2"]
  sync_workspace_changes = true  # Move together
  continuous = true  # Treat as continuous surface
  
  [groups.side]
  monitors = ["HDMI-1"]
  independent = true  # Different workspace tracking
```

### Phase 3: Implementation Details

#### 1. Surface Management
```c
// Platform layer changes
typedef struct {
    monitor_instance_t *monitors;
    int monitor_count;
    bool per_monitor_surfaces;  // vs single stretched surface
} platform_state_t;

// Create surface for each monitor
int platform_create_monitor_surface(monitor_instance_t *monitor) {
    // Create Wayland surface
    // Position at monitor's coordinates
    // Set size to monitor dimensions
    // Create EGL window
    return HYPRLAX_SUCCESS;
}
```

#### 2. Render Loop Changes
```c
// Main render loop modification
void hyprlax_render_frame(hyprlax_context_t *ctx) {
    monitor_instance_t *monitor = ctx->monitors;
    
    while (monitor) {
        // Bind monitor's render target
        renderer_bind_target(ctx->renderer, monitor->render_target);
        
        // Set viewport
        glViewport(0, 0, monitor->width, monitor->height);
        
        // Get config (monitor-specific or global)
        config_t *cfg = monitor->config ? monitor->config : &ctx->config;
        
        // Render layers with monitor-specific parameters
        render_layers_for_monitor(ctx, monitor, cfg);
        
        // Swap buffers for this monitor
        platform_swap_buffers(monitor->surface);
        
        monitor = monitor->next;
    }
}
```

#### 3. Multi-Mode Configuration Loading
```c
// Configuration context supporting all three modes
typedef struct {
    config_t global_config;              // Mode 1 base config
    hashmap_t *monitor_overrides;       // Mode 2 overrides
    hashmap_t *monitor_configs;         // Mode 3 full configs
    hashmap_t *monitor_groups;          // Advanced grouping
    config_t cli_overrides;             // CLI arguments (highest priority)
    bool cli_override_active;
} multi_config_t;

// Main configuration loader
int config_load_multimode(const char *path, multi_config_t *cfg) {
    config_format_t format = detect_config_format(path);
    
    if (format == FORMAT_LEGACY) {
        // Legacy .conf - Mode 1 only
        return config_load_legacy(path, &cfg->global_config);
    }
    
    // TOML - Supports all modes
    toml_table_t *conf = toml_parse_file(path);
    
    // Load global config (Mode 1 base)
    toml_table_t *global = toml_table_in(conf, "global");
    if (global) {
        load_global_config(global, &cfg->global_config);
    }
    
    // Load monitor overrides (Mode 2)
    toml_table_t *overrides = toml_table_in(conf, "monitors.overrides");
    if (overrides) {
        load_monitor_overrides(overrides, cfg->monitor_overrides);
    }
    
    // Load full monitor configs (Mode 3)
    toml_table_t *configs = toml_table_in(conf, "monitors.configs");
    if (configs) {
        load_monitor_configs(configs, cfg->monitor_configs);
    }
    
    // Load monitor groups
    toml_table_t *groups = toml_table_in(conf, "monitors.groups");
    if (groups) {
        load_monitor_groups(groups, cfg->monitor_groups);
    }
    
    return HYPRLAX_SUCCESS;
}

// Resolution function for getting final config for a monitor
config_t* resolve_monitor_config(multi_config_t *cfg, const char *monitor_name) {
    config_t *result = calloc(1, sizeof(config_t));
    
    // Check Mode 3: Full per-monitor config
    config_t *full_config = hashmap_get(cfg->monitor_configs, monitor_name);
    if (full_config) {
        memcpy(result, full_config, sizeof(config_t));
    } else {
        // Start with Mode 1: Global config
        memcpy(result, &cfg->global_config, sizeof(config_t));
        
        // Apply Mode 2: Monitor overrides
        config_override_t *overrides = hashmap_get(cfg->monitor_overrides, monitor_name);
        if (overrides) {
            apply_overrides(result, overrides);
        }
        
        // Check for pattern-based overrides (e.g., "DP-*")
        apply_pattern_overrides(result, cfg->monitor_overrides, monitor_name);
    }
    
    // Always apply CLI overrides last (highest priority)
    if (cfg->cli_override_active) {
        apply_overrides(result, &cfg->cli_overrides);
    }
    
    return result;
}
```

## Migration Strategy

### Backwards Compatibility
1. **Auto-detect config format** by file extension (.conf vs .toml)
2. **Convert old format** on-the-fly:
   ```c
   if (is_legacy_config(path)) {
       config_t *legacy = parse_legacy_config(path);
       apply_to_all_monitors(legacy);
   }
   ```
3. **Provide conversion tool**: `hyprlax --convert-config old.conf > new.toml`

### Gradual Rollout
1. **Phase 1**: Single config across all monitors (Option 1)
   - Minimal changes, create surface per monitor
   - Same config applied to each
   
2. **Phase 2**: Add override support (Option 2)
   - Implement TOML parser
   - Support monitor-specific overrides
   
3. **Phase 3**: Full per-monitor configs (Option 3)
   - Complete flexibility
   - Advanced features (groups, continuous surfaces)

## Performance Considerations

### Memory Optimization
- **Shared Textures**: Load images once, use across monitors
- **Lazy Loading**: Only load textures for connected monitors
- **Smart Caching**: Track which monitors use which images

### Rendering Optimization
- **Dirty Tracking**: Only redraw monitors with changes
- **VSync Per Monitor**: Independent refresh rates
- **Culling**: Skip rendering for disabled/disconnected monitors

### Resource Management
```c
typedef struct {
    GLuint texture_id;
    int ref_count;      // Number of monitors using this
    char *path;         // For comparison
    int width, height;
} shared_texture_t;

// Texture sharing system
shared_texture_t *texture_cache_get(const char *path) {
    // Return existing texture if loaded
    // Increment ref_count
    // Load if not present
}
```

## Configuration Examples

### Example 1: Simple Mirroring
```toml
# All monitors show the same parallax effect
[defaults]
duration = 4
shift = 200

[[layers]]
image = "mountain.png"
shift_multiplier = 0.1
```

### Example 2: Ultrawide + Standard
```toml
[defaults]
duration = 4
shift = 200

# Standard monitor layers
[[layers]]
image = "standard/bg.png"
shift_multiplier = 0.1

# Ultrawide gets different aspect ratio images
[monitors."DP-2"]  # Assuming DP-2 is ultrawide
clear_global_layers = true

[[monitors."DP-2".layers]]
image = "ultrawide/bg.png"
shift_multiplier = 0.05  # Less shift due to wider display
```

### Example 3: Creative Multi-Monitor Setup
```toml
# Three monitors: continuous landscape
[groups.landscape]
monitors = ["DP-1", "DP-2", "DP-3"]
continuous = true  # Treat as one surface

[[layers]]
image = "panoramic-4k.png"  # Wide image spans all three
shift_multiplier = 0.1
```

## Open Questions & Decisions Needed

1. **Monitor Identification**:
   - Use compositor names (DP-1, HDMI-2)?
   - Use EDID/serial numbers?
   - Support both with fallback?

2. **Hot-plug Support**:
   - How to handle monitors being connected/disconnected?
   - Fallback configurations?
   - Automatic re-layout?

3. **Performance Modes**:
   - Single surface stretched (lower overhead)?
   - Per-monitor surfaces (better quality)?
   - User choice or automatic?

4. **Workspace Behavior**:
   - Independent workspace tracking per monitor?
   - Synchronized changes?
   - Configurable groups?

5. **Default Behavior**:
   - Clone/mirror across all monitors?
   - Stretch single image?
   - Disable on additional monitors?

## Recommended Path Forward

1. **Support All Three Modes** from the start
   - Mode 1: CLI and simple configs work everywhere
   - Mode 2: Override system for common adjustments
   - Mode 3: Full control for power users
   - Clear precedence rules prevent confusion

2. **Dual Format Support**
   - Keep .conf for simple Mode 1 configs
   - Use TOML for Modes 2 & 3
   - Auto-detect format by extension or content
   - Provide migration tools

3. **Implement in phases**:
   - Phase 1: Multi-surface rendering with Mode 1 (cloned config)
   - Phase 2: Add TOML parser and Mode 2 (overrides)
   - Phase 3: Full Mode 3 support (per-monitor configs)
   - Phase 4: Advanced features (groups, continuous surfaces)

4. **Configuration Precedence** (highest to lowest):
   - CLI arguments (always win)
   - Per-monitor config (Mode 3)
   - Monitor overrides (Mode 2)
   - Monitor group settings
   - Global config (Mode 1)
   - Built-in defaults

## Conclusion

Multi-monitor support requires significant architectural changes but can be implemented incrementally. By supporting all three configuration modes simultaneously, hyprlax can serve everyone from casual users who want a simple parallax wallpaper across all monitors (Mode 1) to creative professionals who need precise control over each display (Mode 3).

The unified configuration system with clear precedence rules ensures that:
- **Simple remains simple**: CLI args and basic configs just work across all monitors
- **Flexibility is available**: Override what you need without rewriting everything
- **Power users have control**: Complete per-monitor configs when needed
- **Everything works together**: Modes can be mixed and matched as needed

The dual-format support (.conf for simple, TOML for advanced) maintains backwards compatibility while enabling sophisticated multi-monitor setups. The phased implementation approach allows for stable incremental development while always maintaining a working state.

Key success factors:
1. Clear precedence hierarchy prevents confusion
2. Pattern matching enables efficient configuration
3. Monitor groups allow creative multi-display compositions
4. Shared texture memory optimizes performance
5. CLI overrides provide testing and temporary adjustments