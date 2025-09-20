# Compositor Workspace Model Analysis

## Critical Issues with Current Implementation

### 1. Workspace Model Incompatibility

Our current implementation assumes all compositors use numbered workspaces, but:

| Compositor | Workspace Model | Current Support | Issues |
|------------|----------------|-----------------|--------|
| Hyprland | Global IDs, focused output | ✓ Partial | Doesn't handle split-monitor-workspaces plugin |
| Sway | Global workspaces, can steal | ✓ Partial | Doesn't handle workspace stealing |
| Niri | Per-monitor stacks, vertical | ✗ Limited | Treats as global, doesn't handle moves |
| River | Tags (bitmask), not workspaces | ✗ Broken | Can't represent tag combinations |
| Wayfire | Grid per output, optional wsets | ✗ Limited | Doesn't handle workspace sets |

### 2. Event Model Mismatch

Current: Single workspace change event per monitor
Reality: Need to handle:
- Workspace stealing (update 2 monitors atomically)
- Workspace movement between monitors
- Tag visibility changes (multiple tags visible)
- Workspace set swapping

### 3. State Representation

Current structure is insufficient:
```c
// Current - too simplistic
struct monitor_instance {
    int current_workspace;  // Can't handle tags, sets, or stacks
    float workspace_offset_x;
    float workspace_offset_y;
};
```

## Proposed Solutions

### Solution 1: Flexible Context System

```c
// Workspace context that can represent any compositor model
typedef enum {
    WS_MODEL_GLOBAL,      // Hyprland, Sway (default)
    WS_MODEL_PER_OUTPUT,  // Niri, Hyprland with split plugin
    WS_MODEL_TAGS,        // River
    WS_MODEL_SETS,        // Wayfire with wsets
} workspace_model_t;

typedef struct workspace_context {
    workspace_model_t model;
    union {
        int workspace_id;           // Global workspace
        struct {
            int stack_index;        // Position in stack
            int workspace_id;       // Workspace ID
        } per_output;
        struct {
            uint32_t visible_tags;  // Bitmask of visible tags
            uint32_t focused_tag;   // Primary tag for animation
        } tags;
        struct {
            int set_id;
            int workspace_id;
        } sets;
    } data;
} workspace_context_t;

// Updated monitor structure
typedef struct monitor_instance {
    // ... existing fields ...
    
    // Flexible workspace tracking
    workspace_context_t context;
    workspace_context_t previous_context;  // For detecting changes
    
    // Animation state
    float context_offset_x;  // Based on context, not just workspace
    float context_offset_y;
    
    // Compositor-specific state
    void *compositor_data;  // Plugin detection, etc.
} monitor_instance_t;
```

### Solution 2: Compositor-Specific Adapters

```c
// Abstract workspace change event
typedef struct {
    monitor_instance_t *monitor;
    workspace_context_t old_context;
    workspace_context_t new_context;
    
    // Multi-monitor updates (for steal/move)
    monitor_instance_t *secondary_monitor;  // If affected
    workspace_context_t secondary_old_context;
    workspace_context_t secondary_new_context;
    
    bool is_steal;   // Workspace moved to this monitor
    bool is_move;    // Workspace physically moved
} workspace_change_event_t;

// Compositor adapter interface
typedef struct compositor_adapter_ops {
    // Context management
    workspace_model_t (*get_workspace_model)(void);
    int (*compare_contexts)(const workspace_context_t *a, 
                          const workspace_context_t *b);
    float (*calculate_offset)(const workspace_context_t *from,
                            const workspace_context_t *to,
                            float shift_pixels);
    
    // Event handling
    void (*handle_workspace_event)(hyprlax_context_t *ctx,
                                  workspace_change_event_t *event);
    
    // Compositor-specific features
    bool (*detect_plugins)(void);  // e.g., split-monitor-workspaces
    void (*apply_quirks)(monitor_instance_t *monitor);
} compositor_adapter_ops_t;
```

### Solution 3: Policy Configuration

```c
// Per-compositor policy settings
typedef struct {
    // Workspace binding
    bool bind_workspace_to_output;  // Sway/Hyprland
    bool honor_external_bindings;   // Don't override user config
    
    // Plugin/extension support
    bool enable_split_monitor;      // Hyprland
    bool treat_wsets_as_contexts;   // Wayfire
    
    // River tag handling
    enum {
        TAG_POLICY_HIGHEST,    // Use highest visible tag
        TAG_POLICY_LOWEST,     // Use lowest visible tag
        TAG_POLICY_FIRST_SET,  // Use first set tag
        TAG_POLICY_NO_PARALLAX // Disable when multiple visible
    } river_tag_policy;
    
    // Animation triggers
    bool animate_on_focus_change;
    bool animate_on_workspace_change;
    bool animate_on_tag_delta;
    
    // Hot-plug behavior
    bool sticky_wallpaper_per_workspace;  // vs per output
} compositor_policy_t;
```

## Implementation Phases

### Phase 1: Core Abstraction (Week 1)
1. Implement flexible `workspace_context_t`
2. Update monitor structure
3. Create base compositor adapter interface
4. Maintain backward compatibility

### Phase 2: Compositor Adapters (Week 2-3)
1. Hyprland adapter (with plugin detection)
2. Sway adapter (with steal handling)
3. River adapter (tag-based model)
4. Niri adapter (vertical stacks)
5. Wayfire adapter (wsets support)

### Phase 3: Policy System (Week 4)
1. Configuration for per-compositor policies
2. TOML configuration support
3. Runtime policy switching
4. Default sensible policies per compositor

### Phase 4: Edge Cases (Week 5)
1. Multi-tag visibility (River)
2. Workspace stealing/movement
3. Mirrored outputs
4. Different DPI/scales per output
5. Hot-plug with workspace memory

## Backward Compatibility Strategy

1. **Default behavior**: Current implementation remains for simple cases
2. **Progressive enhancement**: Detect compositor capabilities at runtime
3. **Fallback chain**: Advanced → Simple → Legacy
4. **Config migration**: Auto-convert old configs to new format

## Testing Matrix

| Test Case | Hyprland | Sway | River | Niri | Wayfire |
|-----------|----------|------|-------|------|---------|
| Basic workspace change | ✓ | ✓ | Adapt | Adapt | Adapt |
| Multi-monitor independent | Plugin | ✗ | ✓ | ✓ | ✓ |
| Workspace steal/move | ✓ | ✓ | N/A | ✓ | Sets |
| Hot-plug restore | ✓ | ✓ | ✓ | ✓ | ✓ |
| Multi-tag/set | N/A | N/A | ✓ | N/A | ✓ |

## Risk Mitigation

1. **Compositor API changes**: Version detection and compatibility modes
2. **Performance impact**: Lazy evaluation of context changes
3. **Complexity explosion**: Modular adapter system with clear interfaces
4. **User confusion**: Sensible defaults with advanced options hidden
5. **Testing burden**: Automated test harness per compositor