/*
 * workspace_models.h - Compositor workspace model abstraction
 *
 * Provides abstraction for different compositor workspace models:
 * - Numeric workspaces (Hyprland, Sway)
 * - Tag-based (River)
 * - Workspace sets (Wayfire)
 * - Per-output stacks (Niri)
 */

#ifndef WORKSPACE_MODELS_H
#define WORKSPACE_MODELS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef struct monitor_instance monitor_instance_t;
typedef struct hyprlax_context hyprlax_context_t;
typedef struct compositor_adapter compositor_adapter_t;

/* Workspace model types */
typedef enum {
    WS_MODEL_GLOBAL_NUMERIC,     /* Hyprland, Sway (default) */
    WS_MODEL_PER_OUTPUT_NUMERIC, /* Niri, Hyprland with split-monitor plugin */
    WS_MODEL_TAG_BASED,          /* River */
    WS_MODEL_SET_BASED,          /* Wayfire with wsets */
} workspace_model_t;

/* Flexible workspace context */
typedef struct workspace_context {
    workspace_model_t model;
    union {
        /* Numeric workspace (most common) */
        int workspace_id;

        /* River tags (bitmask) */
        struct {
            uint32_t visible_tags;  /* Which tags are visible */
            uint32_t focused_tag;   /* Primary tag for animation */
        } tags;

        /* Wayfire workspace sets */
        struct {
            int set_id;
            int workspace_id;
        } wayfire_set;

        /* Niri per-output stack */
        struct {
            int stack_index;        /* Position in vertical stack */
            int workspace_id;       /* Workspace identifier */
        } stack;
    } data;
} workspace_context_t;

/* Workspace change event (can affect multiple monitors) */
typedef struct workspace_change_event {
    /* Primary monitor affected */
    monitor_instance_t *monitor;
    workspace_context_t old_context;
    workspace_context_t new_context;

    /* Secondary monitor (for steal/move operations) */
    monitor_instance_t *secondary_monitor;
    workspace_context_t secondary_old_context;
    workspace_context_t secondary_new_context;

    /* Event flags */
    bool affects_multiple_monitors;
    bool is_workspace_steal;    /* Workspace moved to this monitor */
    bool is_workspace_move;     /* Physical workspace movement */
    bool is_set_swap;          /* Wayfire set swap */
} workspace_change_event_t;

/* Compositor capabilities */
typedef struct compositor_capabilities {
    bool can_steal_workspace;      /* Sway/Hyprland */
    bool supports_workspace_move;  /* Niri */
    bool has_split_plugin;        /* Hyprland split-monitor-workspaces */
    bool has_wsets_plugin;        /* Wayfire wsets */
    bool supports_tags;           /* River */
    bool supports_vertical_stack; /* Niri */
} compositor_capabilities_t;

/* Policy for handling special cases */
typedef struct workspace_policy {
    /* River: when multiple tags visible */
    enum {
        TAG_POLICY_HIGHEST,    /* Use highest bit tag */
        TAG_POLICY_LOWEST,     /* Use lowest bit tag */
        TAG_POLICY_FIRST_SET,  /* Use first set tag */
        TAG_POLICY_NO_PARALLAX /* Disable parallax */
    } multi_tag_policy;

    /* Workspace stealing/moving */
    bool animate_on_steal;
    bool animate_on_move;
    bool preserve_offset_on_move;

    /* Plugin detection */
    bool auto_detect_plugins;
    bool enable_split_monitor;    /* Hyprland */
    bool enable_wsets;            /* Wayfire */
} workspace_policy_t;

/* Workspace model detection */
workspace_model_t workspace_detect_model(int compositor_type);
bool workspace_detect_capabilities(int compositor_type,
                                  compositor_capabilities_t *caps);

/* Capability-based detection (core-friendly: no type leakage) */
workspace_model_t workspace_detect_model_for_adapter(const compositor_adapter_t *adapter);

/* Context comparison and conversion */
bool workspace_context_equal(const workspace_context_t *a,
                            const workspace_context_t *b);
int workspace_context_compare(const workspace_context_t *a,
                             const workspace_context_t *b);

/* Offset structure for 2D workspace models */
typedef struct {
    float x;
    float y;
} workspace_offset_t;

/* Calculate parallax offset based on context change */
float workspace_calculate_offset(const workspace_context_t *from,
                                const workspace_context_t *to,
                                float shift_pixels,
                                const workspace_policy_t *policy);

/* Calculate 2D parallax offset for 2D workspace models */
workspace_offset_t workspace_calculate_offset_2d(const workspace_context_t *from,
                                                const workspace_context_t *to,
                                                float shift_pixels,
                                                const workspace_policy_t *policy);

/* Handle workspace change based on model */
void workspace_handle_change(hyprlax_context_t *ctx,
                            workspace_change_event_t *event);

/* Handle special multi-monitor cases */
void workspace_handle_steal(hyprlax_context_t *ctx,
                           monitor_instance_t *from_monitor,
                           monitor_instance_t *to_monitor,
                           const workspace_context_t *workspace);

void workspace_handle_move(hyprlax_context_t *ctx,
                          monitor_instance_t *from_monitor,
                          monitor_instance_t *to_monitor,
                          const workspace_context_t *workspace);

/* Utility functions */
const char* workspace_model_to_string(workspace_model_t model);
void workspace_context_to_string(const workspace_context_t *context,
                                char *buffer, size_t size);

/* River tag helpers */
int workspace_tag_to_index(uint32_t tag_mask);
uint32_t workspace_index_to_tag(int index);
int workspace_count_tags(uint32_t tag_mask);

#endif /* WORKSPACE_MODELS_H */
