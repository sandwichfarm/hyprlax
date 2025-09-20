# Workspace Abstraction Implementation Plan

## Immediate Changes Needed (Phase 0.5)

### 1. Update Monitor Structure

```c
// src/core/monitor.h - REQUIRED CHANGES

typedef enum {
    WS_MODEL_GLOBAL_NUMERIC,     // Hyprland, Sway default
    WS_MODEL_PER_OUTPUT_NUMERIC, // Niri, Hyprland+split-monitor
    WS_MODEL_TAG_BASED,          // River
    WS_MODEL_SET_BASED,          // Wayfire with wsets
} workspace_model_t;

typedef struct monitor_instance {
    /* ... existing fields ... */
    
    /* REMOVE these oversimplified fields:
    int current_workspace;
    float workspace_offset_x;
    float workspace_offset_y;
    */
    
    /* ADD flexible workspace tracking */
    workspace_model_t ws_model;
    
    /* Context based on compositor */
    union {
        int workspace_id;              // Simple numeric
        uint32_t visible_tags;         // River tags
        struct {
            int set_id;
            int workspace_id;
        } wayfire_context;
    } current_context;
    
    union {
        int workspace_id;
        uint32_t visible_tags;
        struct {
            int set_id;
            int workspace_id;
        } wayfire_context;
    } previous_context;
    
    /* Calculated offsets (model-agnostic) */
    float parallax_offset_x;
    float parallax_offset_y;
    
    /* Compositor-specific quirks */
    struct {
        bool can_steal_workspace;      // Sway/Hyprland
        bool supports_workspace_move;  // Niri
        bool has_split_plugin;        // Hyprland
        bool has_wsets_plugin;        // Wayfire
    } capabilities;
} monitor_instance_t;
```

### 2. Update Workspace Change Handler

```c
// src/hyprlax_main.c - REPLACE current handler

void hyprlax_handle_monitor_workspace_change_v2(
    hyprlax_context_t *ctx,
    const char *monitor_name,
    workspace_change_event_t *event) {
    
    monitor_instance_t *monitor = monitor_list_find_by_name(
        ctx->monitors, monitor_name);
    if (!monitor) return;
    
    // Handle based on workspace model
    switch (monitor->ws_model) {
        case WS_MODEL_GLOBAL_NUMERIC:
            handle_numeric_workspace_change(ctx, monitor, event);
            break;
            
        case WS_MODEL_TAG_BASED:
            handle_river_tag_change(ctx, monitor, event);
            break;
            
        case WS_MODEL_SET_BASED:
            handle_wayfire_set_change(ctx, monitor, event);
            break;
            
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            handle_per_output_workspace_change(ctx, monitor, event);
            break;
    }
    
    // Handle multi-monitor effects (steal/move)
    if (event->affects_multiple_monitors) {
        handle_multi_monitor_update(ctx, event);
    }
}
```

### 3. Compositor Detection

```c
// src/compositor/compositor.c - ADD detection

workspace_model_t detect_workspace_model(compositor_type_t type) {
    switch (type) {
        case COMPOSITOR_HYPRLAND:
            // Check for split-monitor-workspaces plugin
            if (hyprland_has_split_plugin()) {
                return WS_MODEL_PER_OUTPUT_NUMERIC;
            }
            return WS_MODEL_GLOBAL_NUMERIC;
            
        case COMPOSITOR_SWAY:
            return WS_MODEL_GLOBAL_NUMERIC;
            
        case COMPOSITOR_RIVER:
            return WS_MODEL_TAG_BASED;
            
        case COMPOSITOR_NIRI:
            return WS_MODEL_PER_OUTPUT_NUMERIC;
            
        case COMPOSITOR_WAYFIRE:
            // Check for wsets plugin
            if (wayfire_has_wsets_plugin()) {
                return WS_MODEL_SET_BASED;
            }
            return WS_MODEL_PER_OUTPUT_NUMERIC;
            
        default:
            return WS_MODEL_GLOBAL_NUMERIC;
    }
}
```

## Critical Path Items

### Must Fix Before Phase 1:

1. **River Support**
   - Current `int workspace` can't represent tags
   - Need `uint32_t visible_tags` field
   - Implement tag-to-offset calculation

2. **Workspace Stealing** (Sway/Hyprland)
   - Need atomic update of 2 monitors
   - Current single-monitor update breaks consistency
   - Add `workspace_stolen_event` handling

3. **Niri Workspace Movement**
   - Workspace can move between monitors
   - Need to update both source and destination
   - Track workspace ID separately from position

4. **Wayfire wsets**
   - Workspace sets can swap between monitors
   - Need (set_id, workspace_id) tuple
   - Handle set swap as context change

## Minimal Working Implementation

For Phase 0.5, we can add a compatibility layer:

```c
// Compatibility shim for current code
typedef struct {
    workspace_model_t model;
    union {
        int numeric_id;
        uint32_t tag_mask;
    } value;
} workspace_id_t;

// Convert any workspace model to offset
float workspace_to_offset(const workspace_id_t *from,
                          const workspace_id_t *to,
                          float shift_pixels) {
    switch (from->model) {
        case WS_MODEL_GLOBAL_NUMERIC:
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            return (to->value.numeric_id - from->value.numeric_id) 
                   * shift_pixels;
            
        case WS_MODEL_TAG_BASED:
            // River: count bit position changes
            int from_tag = __builtin_ffs(from->value.tag_mask) - 1;
            int to_tag = __builtin_ffs(to->value.tag_mask) - 1;
            return (to_tag - from_tag) * shift_pixels;
            
        default:
            return 0.0f;
    }
}
```

## Testing Requirements

### Compositor-Specific Test Cases:

1. **Hyprland**
   - [ ] Basic workspace change
   - [ ] With split-monitor-workspaces plugin
   - [ ] Workspace binding to output

2. **Sway**
   - [ ] Workspace stealing to focused output
   - [ ] Workspace assignment to output
   - [ ] Multi-monitor workspace switch

3. **River**
   - [ ] Single tag visible
   - [ ] Multiple tags visible
   - [ ] Tag switching animation

4. **Niri**
   - [ ] Per-monitor workspace stacks
   - [ ] Workspace movement between monitors
   - [ ] Vertical workspace navigation

5. **Wayfire**
   - [ ] Basic grid workspace
   - [ ] With wsets plugin
   - [ ] Workspace set swapping

## Configuration Changes

Add to TOML config:

```toml
[compositor]
# Workspace model detection
workspace_model = "auto"  # auto, global, per-output, tags, sets

[compositor.hyprland]
detect_split_plugin = true
honor_workspace_bindings = true

[compositor.sway]
handle_workspace_steal = true
animate_on_steal = true

[compositor.river]
# When multiple tags visible
multi_tag_policy = "highest"  # highest, lowest, first, none
animate_on_tag_change = true

[compositor.niri]
track_workspace_movement = true
preserve_offset_on_move = false

[compositor.wayfire]
detect_wsets_plugin = true
treat_sets_as_contexts = true
```

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing setups | High | Keep compatibility shim for Phase 0 |
| Compositor API changes | Medium | Version detection, fallback modes |
| Complex state management | High | Clear separation of concerns |
| Performance regression | Medium | Cache context calculations |
| User confusion | High | Sensible defaults, clear docs |

## Implementation Order

1. **Week 1**: Add workspace_model_t and compatibility layer
2. **Week 2**: Update River and Niri adapters
3. **Week 3**: Handle workspace stealing/movement
4. **Week 4**: Add Wayfire wsets support
5. **Week 5**: Polish and testing

This preserves Phase 0 functionality while preparing for proper multi-compositor support.