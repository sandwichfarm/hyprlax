# Hyprlax Modularization - High-Level Design Document

## Executive Summary

This document outlines the architectural changes required to transform hyprlax from a Hyprland-specific wallpaper engine into a modular, compositor-agnostic parallax wallpaper system. The goal is to maintain backwards compatibility while enabling support for multiple compositors (Hyprland, Niri, Sway, KDE, GNOME) and display servers (Wayland, X11).

## Current Architecture

### System Overview
Hyprlax is currently a monolithic C application that creates animated parallax wallpapers specifically for the Hyprland compositor on Wayland. It consists of:

- **Single executable** (`hyprlax`) - 75KB main source file
- **Control utility** (`hyprlax-ctl`) - IPC client for runtime control
- **Direct dependencies** on Hyprland IPC and wlroots layer-shell protocol

### Component Analysis

```
┌─────────────────────────────────────────────────────────┐
│                     hyprlax (current)                    │
├─────────────────────────────────────────────────────────┤
│  Configuration │ Hyprland IPC │ Animation │ Rendering   │
│     Parser     │   Handler    │   Engine  │  Pipeline   │
├────────────────┴──────────────┴───────────┴─────────────┤
│              Wayland Layer Shell Protocol                │
├──────────────────────────────────────────────────────────┤
│                  EGL/OpenGL ES 2.0                       │
└──────────────────────────────────────────────────────────┘
```

## Proposed Architecture

### Modular Design

```
┌─────────────────────────────────────────────────────────┐
│                    hyprlax-core                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │
│  │Animation │ │ Rendering│ │  Layer   │ │  Config  │  │
│  │  Engine  │ │ Pipeline │ │ Manager  │ │  Parser  │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │
└─────────────────────────┬───────────────────────────────┘
                          │ Abstract Interfaces
┌─────────────────────────┴───────────────────────────────┐
│                  Abstraction Layer                       │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐   │
│  │ Compositor   │ │   Display    │ │   Graphics   │   │
│  │  Interface   │ │   Interface  │ │   Interface  │   │
│  └──────────────┘ └──────────────┘ └──────────────┘   │
└─────────────────────────┬───────────────────────────────┘
                          │ Implementations
        ┌─────────────────┼─────────────────┐
        │                 │                 │
┌───────▼────────┐ ┌──────▼──────┐ ┌───────▼────────┐
│    Hyprland    │ │    Niri     │ │     Sway       │
│    Adapter     │ │   Adapter   │ │    Adapter     │
├────────────────┤ ├─────────────┤ ├────────────────┤
│ Wayland + IPC  │ │Wayland + IPC│ │ Wayland + IPC  │
└────────────────┘ └─────────────┘ └────────────────┘
```

### Key Design Principles

1. **Plugin Architecture**: Compositor support via dynamically loaded adapters
2. **Interface Segregation**: Clear boundaries between core and compositor-specific code
3. **Dependency Inversion**: Core depends on abstractions, not implementations
4. **Backwards Compatibility**: Existing command-line interface preserved
5. **Runtime Detection**: Automatic compositor detection with manual override

## Module Specifications

### 1. Core Module (`libhyprlax-core`)

**Responsibilities:**
- Animation engine and easing functions
- Layer management and compositing logic
- OpenGL shader compilation and management
- Configuration parsing and validation
- Image loading and texture management

**Public API:**
```c
// Core initialization
hyprlax_core* hyprlax_core_init(const hyprlax_config* config);
void hyprlax_core_destroy(hyprlax_core* core);

// Layer management
int hyprlax_add_layer(hyprlax_core* core, const layer_spec* spec);
void hyprlax_remove_layer(hyprlax_core* core, int layer_id);
void hyprlax_update_layer(hyprlax_core* core, int layer_id, const layer_update* update);

// Animation control
void hyprlax_set_workspace(hyprlax_core* core, int workspace);
void hyprlax_tick(hyprlax_core* core, double delta_time);

// Rendering
void hyprlax_render(hyprlax_core* core, render_context* ctx);
```

### 2. Compositor Interface

**Abstract Interface:**
```c
typedef struct compositor_adapter {
    const char* name;
    const char* version;
    
    // Lifecycle
    int (*init)(struct compositor_adapter* self, void* display);
    void (*destroy)(struct compositor_adapter* self);
    
    // Workspace management
    int (*get_current_workspace)(struct compositor_adapter* self);
    int (*get_workspace_count)(struct compositor_adapter* self);
    void (*set_workspace_callback)(struct compositor_adapter* self, 
                                   workspace_change_cb callback, void* userdata);
    
    // Event handling
    int (*process_events)(struct compositor_adapter* self, int timeout_ms);
    
    // Capabilities
    uint32_t (*get_capabilities)(struct compositor_adapter* self);
} compositor_adapter;
```

**Compositor Capabilities:**
```c
enum compositor_capability {
    CAP_WORKSPACE_EVENTS    = 1 << 0,  // Real-time workspace change events
    CAP_WORKSPACE_QUERY     = 1 << 1,  // Query current workspace
    CAP_WORKSPACE_COUNT     = 1 << 2,  // Query total workspace count
    CAP_MONITOR_EVENTS      = 1 << 3,  // Monitor connect/disconnect events
    CAP_WINDOW_RULES        = 1 << 4,  // Window placement rules
    CAP_VIRTUAL_DESKTOPS   = 1 << 5,  // Virtual desktop support
};
```

### 3. Display Interface

**Abstract Interface:**
```c
typedef struct display_adapter {
    const char* name;
    display_type type;  // WAYLAND, X11, etc.
    
    // Connection management
    void* (*connect)(const char* display_name);
    void (*disconnect)(void* display);
    
    // Surface/Window creation
    void* (*create_surface)(void* display, const surface_config* config);
    void (*destroy_surface)(void* surface);
    
    // Graphics context
    void* (*create_graphics_context)(void* surface, const graphics_config* config);
    
    // Event loop integration
    int (*get_fd)(void* display);
    int (*dispatch)(void* display);
    int (*flush)(void* display);
} display_adapter;
```

### 4. Compositor Adapters

#### Hyprland Adapter
- **IPC Socket**: `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock`
- **Events**: Parse `workspace>>N` format
- **Commands**: Use `hyprctl` for queries
- **Detection**: Check `HYPRLAND_INSTANCE_SIGNATURE` environment variable

#### Niri Adapter
- **IPC Socket**: Niri's IPC socket location
- **Events**: Subscribe to workspace change events
- **Commands**: Use niri-msg or equivalent
- **Detection**: Check for Niri-specific environment or socket

#### Sway Adapter
- **IPC Socket**: `$SWAYSOCK` or `$I3SOCK`
- **Events**: Subscribe via i3/Sway IPC protocol
- **Commands**: Use `swaymsg` for queries
- **Detection**: Check `SWAYSOCK` environment variable

#### X11 Adapter (for Hypr, i3, etc.)
- **Events**: Monitor `_NET_CURRENT_DESKTOP` property changes
- **Queries**: Read EWMH properties
- **Window**: Set as desktop window type
- **Detection**: Check `DISPLAY` environment variable

## Data Flow

### Initialization Sequence
```
1. Detect environment (compositor, display server)
2. Load appropriate adapter modules
3. Initialize display connection
4. Create surface/window
5. Initialize graphics context
6. Initialize core engine
7. Load configuration and layers
8. Start event loop
```

### Event Processing
```
Compositor Event → Adapter → Core Engine → Animation Update → Render
     ↑                                                           ↓
     └──────────────── Frame Callback ←─────────────────────────┘
```

## Configuration

### Adapter Selection
```toml
[compositor]
# Auto-detect by default
adapter = "auto"  # Options: auto, hyprland, niri, sway, x11

# Manual override
[compositor.hyprland]
socket_path = "/custom/path/to/socket"

[compositor.x11]
display = ":1"
```

### Layer Configuration (unchanged)
```toml
[[layers]]
image = "background.jpg"
shift_multiplier = 0.5
opacity = 1.0
blur = 2.0
```

## Compatibility Matrix

| Feature | Hyprland | Niri | Sway | KDE | GNOME | X11 |
|---------|----------|------|------|-----|-------|-----|
| Workspace Events | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Layer Shell | ✓ | ✓ | ✓ | ✗ | ✗ | N/A |
| Background Mode | ✓ | ✓ | ✓ | ✓* | ✓* | ✓ |
| Multi-Monitor | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Hot-Reload | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Blur Effects | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

\* May require desktop-specific integration

## Error Handling

### Graceful Degradation
1. **Missing Adapter**: Fall back to static image if compositor unsupported
2. **IPC Failure**: Continue with manual workspace switching
3. **Protocol Unavailable**: Try alternative protocols (XDG shell vs layer shell)
4. **Graphics Failure**: Fall back to software rendering if available

### User Feedback
- Clear error messages indicating which component failed
- Suggestions for resolution (install adapter, check permissions, etc.)
- Debug mode with verbose logging for troubleshooting

## Performance Considerations

### Memory Management
- **Lazy Loading**: Load adapters only when needed
- **Shared Libraries**: Core functionality in shared library
- **Resource Pooling**: Reuse textures and buffers where possible

### CPU/GPU Optimization
- **Event Batching**: Process multiple events per frame
- **Render Skipping**: Skip frames when no animation active
- **Adaptive Quality**: Reduce effects under load

## Security Considerations

### IPC Security
- **Socket Permissions**: Restrict IPC socket to user only
- **Input Validation**: Validate all IPC commands and parameters
- **Path Sanitization**: Sanitize image paths to prevent directory traversal

### Adapter Isolation
- **Sandboxing**: Run adapters with minimal privileges
- **Resource Limits**: Limit memory and CPU usage per adapter
- **Audit Logging**: Log all adapter actions in debug mode

## Testing Strategy

### Unit Tests
- Core engine functions
- Individual adapter implementations
- Abstract interface compliance

### Integration Tests
- Adapter loading and initialization
- Cross-adapter compatibility
- Configuration parsing and validation

### System Tests
- Full pipeline with each supported compositor
- Multi-monitor scenarios
- Performance benchmarks

## Migration Path

### Phase 1: Internal Refactoring
- Extract core functionality into separate module
- Create abstract interfaces
- Implement Hyprland adapter using new interfaces

### Phase 2: Additional Adapters
- Implement Niri adapter
- Implement Sway adapter
- Implement X11 adapter

### Phase 3: Advanced Features
- Plugin system for external adapters
- Runtime adapter switching
- Advanced configuration UI

## API Stability

### Versioning Scheme
- **Core API**: Semantic versioning (MAJOR.MINOR.PATCH)
- **Adapter API**: Separate version per adapter
- **Configuration**: Forward-compatible with version detection

### Deprecation Policy
- Minimum 2 release cycles before removal
- Clear migration guides for breaking changes
- Compatibility shims where feasible

## Documentation Requirements

### Developer Documentation
- Adapter development guide
- API reference with examples
- Architecture diagrams

### User Documentation
- Installation per compositor
- Configuration guide
- Troubleshooting guide

## Conclusion

This modular architecture will transform hyprlax from a Hyprland-specific tool into a universal parallax wallpaper engine while maintaining its performance and feature set. The abstraction layers provide flexibility for future compositor support without requiring core engine modifications.