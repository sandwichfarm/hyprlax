/*
 * workspace_models.c - Compositor workspace model implementation
 */

#include "workspace_models.h"
#include "../include/compositor.h"
#include "../include/hyprlax.h"
#include "../core/monitor.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Detect workspace model based on compositor */
workspace_model_t workspace_detect_model(int compositor_type) {
    switch (compositor_type) {
        case COMPOSITOR_HYPRLAND:
            /* Check for split-monitor-workspaces plugin */
            /* TODO: Implement actual plugin detection via hyprctl */
            return WS_MODEL_GLOBAL_NUMERIC;
            
        case COMPOSITOR_SWAY:
            return WS_MODEL_GLOBAL_NUMERIC;
            
        case COMPOSITOR_RIVER:
            return WS_MODEL_TAG_BASED;
            
        case COMPOSITOR_NIRI:
            return WS_MODEL_PER_OUTPUT_NUMERIC;
            
        case COMPOSITOR_WAYFIRE:
            /* Check for wsets plugin */
            /* TODO: Implement actual plugin detection */
            return WS_MODEL_PER_OUTPUT_NUMERIC;
            
        default:
            return WS_MODEL_GLOBAL_NUMERIC;
    }
}

/* Detect compositor capabilities */
bool workspace_detect_capabilities(int compositor_type,
                                  compositor_capabilities_t *caps) {
    if (!caps) return false;
    
    memset(caps, 0, sizeof(*caps));
    
    switch (compositor_type) {
        case COMPOSITOR_HYPRLAND:
            caps->can_steal_workspace = true;
            /* TODO: Detect split-monitor-workspaces plugin */
            break;
            
        case COMPOSITOR_SWAY:
            caps->can_steal_workspace = true;
            break;
            
        case COMPOSITOR_RIVER:
            caps->supports_tags = true;
            break;
            
        case COMPOSITOR_NIRI:
            caps->supports_workspace_move = true;
            caps->supports_vertical_stack = true;
            break;
            
        case COMPOSITOR_WAYFIRE:
            /* TODO: Detect wsets plugin */
            break;
    }
    
    return true;
}

/* Compare workspace contexts */
bool workspace_context_equal(const workspace_context_t *a,
                            const workspace_context_t *b) {
    if (!a || !b || a->model != b->model) return false;
    
    switch (a->model) {
        case WS_MODEL_GLOBAL_NUMERIC:
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            return a->data.workspace_id == b->data.workspace_id;
            
        case WS_MODEL_TAG_BASED:
            return a->data.tags.visible_tags == b->data.tags.visible_tags;
            
        case WS_MODEL_SET_BASED:
            return a->data.wayfire_set.set_id == b->data.wayfire_set.set_id &&
                   a->data.wayfire_set.workspace_id == b->data.wayfire_set.workspace_id;
            
        default:
            return false;
    }
}

/* Compare workspace contexts (for ordering) */
int workspace_context_compare(const workspace_context_t *a,
                             const workspace_context_t *b) {
    if (!a || !b) return 0;
    if (a->model != b->model) return (int)a->model - (int)b->model;
    
    switch (a->model) {
        case WS_MODEL_GLOBAL_NUMERIC:
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            return a->data.workspace_id - b->data.workspace_id;
            
        case WS_MODEL_TAG_BASED:
            return workspace_tag_to_index(a->data.tags.focused_tag) -
                   workspace_tag_to_index(b->data.tags.focused_tag);
            
        case WS_MODEL_SET_BASED:
            if (a->data.wayfire_set.set_id != b->data.wayfire_set.set_id)
                return a->data.wayfire_set.set_id - b->data.wayfire_set.set_id;
            return a->data.wayfire_set.workspace_id - b->data.wayfire_set.workspace_id;
            
        default:
            return 0;
    }
}

/* Calculate parallax offset based on context change */
float workspace_calculate_offset(const workspace_context_t *from,
                                const workspace_context_t *to,
                                float shift_pixels,
                                const workspace_policy_t *policy) {
    if (!from || !to || from->model != to->model) return 0.0f;
    
    int delta = 0;
    
    switch (from->model) {
        case WS_MODEL_GLOBAL_NUMERIC:
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            delta = to->data.workspace_id - from->data.workspace_id;
            break;
            
        case WS_MODEL_TAG_BASED: {
            /* River: calculate based on tag position */
            if (!policy) {
                /* Default: use focused tag */
                int from_idx = workspace_tag_to_index(from->data.tags.focused_tag);
                int to_idx = workspace_tag_to_index(to->data.tags.focused_tag);
                delta = to_idx - from_idx;
            } else {
                /* Apply policy for multiple visible tags */
                uint32_t from_tag = from->data.tags.focused_tag;
                uint32_t to_tag = to->data.tags.focused_tag;
                
                switch (policy->multi_tag_policy) {
                    case TAG_POLICY_HIGHEST:
                        from_tag = from->data.tags.visible_tags;
                        to_tag = to->data.tags.visible_tags;
                        /* Find highest bit */
                        while (from_tag & (from_tag - 1)) from_tag &= from_tag - 1;
                        while (to_tag & (to_tag - 1)) to_tag &= to_tag - 1;
                        break;
                    case TAG_POLICY_LOWEST:
                        /* Find lowest bit */
                        from_tag = from->data.tags.visible_tags & -from->data.tags.visible_tags;
                        to_tag = to->data.tags.visible_tags & -to->data.tags.visible_tags;
                        break;
                    case TAG_POLICY_NO_PARALLAX:
                        if (workspace_count_tags(from->data.tags.visible_tags) > 1 ||
                            workspace_count_tags(to->data.tags.visible_tags) > 1) {
                            return 0.0f;
                        }
                        break;
                }
                
                int from_idx = workspace_tag_to_index(from_tag);
                int to_idx = workspace_tag_to_index(to_tag);
                delta = to_idx - from_idx;
            }
            break;
        }
            
        case WS_MODEL_SET_BASED:
            /* Wayfire: only animate within same set */
            if (from->data.wayfire_set.set_id == to->data.wayfire_set.set_id) {
                delta = to->data.wayfire_set.workspace_id - 
                       from->data.wayfire_set.workspace_id;
            }
            break;
    }
    
    return delta * shift_pixels;
}

/* Handle workspace change based on model */
void workspace_handle_change(hyprlax_context_t *ctx,
                            workspace_change_event_t *event) {
    if (!ctx || !event || !event->monitor) return;
    
    /* Calculate offset for primary monitor */
    float offset = workspace_calculate_offset(&event->old_context,
                                             &event->new_context,
                                             ctx->config.shift_pixels,
                                             NULL); /* TODO: Get policy from config */
    
    /* Update primary monitor */
    monitor_instance_t *monitor = event->monitor;
    monitor->parallax_offset_x += offset;
    
    /* Start animation */
    monitor_start_parallax_animation(ctx, monitor, 
                                    workspace_context_compare(&event->old_context,
                                                            &event->new_context));
    
    /* Handle secondary monitor if affected */
    if (event->affects_multiple_monitors && event->secondary_monitor) {
        float secondary_offset = workspace_calculate_offset(
            &event->secondary_old_context,
            &event->secondary_new_context,
            ctx->config.shift_pixels,
            NULL);
        
        monitor_instance_t *secondary = event->secondary_monitor;
        secondary->parallax_offset_x += secondary_offset;
        
        monitor_start_parallax_animation(ctx, secondary,
                                        workspace_context_compare(
                                            &event->secondary_old_context,
                                            &event->secondary_new_context));
    }
}

/* Handle workspace stealing (Sway/Hyprland) */
void workspace_handle_steal(hyprlax_context_t *ctx,
                           monitor_instance_t *from_monitor,
                           monitor_instance_t *to_monitor,
                           const workspace_context_t *workspace) {
    if (!ctx || !from_monitor || !to_monitor || !workspace) return;
    
    fprintf(stderr, "Workspace steal: %s -> %s\n",
            from_monitor->name, to_monitor->name);
    
    /* Create event for atomic update */
    workspace_change_event_t event = {
        .monitor = to_monitor,
        .old_context = to_monitor->current_context,
        .new_context = *workspace,
        .secondary_monitor = from_monitor,
        .secondary_old_context = from_monitor->current_context,
        .affects_multiple_monitors = true,
        .is_workspace_steal = true
    };
    
    /* Clear workspace from source monitor */
    memset(&event.secondary_new_context, 0, sizeof(workspace_context_t));
    event.secondary_new_context.model = workspace->model;
    
    workspace_handle_change(ctx, &event);
    
    /* Update contexts */
    to_monitor->current_context = *workspace;
    from_monitor->current_context = event.secondary_new_context;
}

/* Handle workspace movement (Niri) */
void workspace_handle_move(hyprlax_context_t *ctx,
                          monitor_instance_t *from_monitor,
                          monitor_instance_t *to_monitor,
                          const workspace_context_t *workspace) {
    if (!ctx || !from_monitor || !to_monitor || !workspace) return;
    
    fprintf(stderr, "Workspace move: %s -> %s\n",
            from_monitor->name, to_monitor->name);
    
    /* Similar to steal but preserves workspace identity */
    workspace_handle_steal(ctx, from_monitor, to_monitor, workspace);
}

/* Convert model to string */
const char* workspace_model_to_string(workspace_model_t model) {
    switch (model) {
        case WS_MODEL_GLOBAL_NUMERIC: return "global_numeric";
        case WS_MODEL_PER_OUTPUT_NUMERIC: return "per_output_numeric";
        case WS_MODEL_TAG_BASED: return "tag_based";
        case WS_MODEL_SET_BASED: return "set_based";
        default: return "unknown";
    }
}

/* Convert context to string for debugging */
void workspace_context_to_string(const workspace_context_t *context,
                                char *buffer, size_t size) {
    if (!context || !buffer || size == 0) return;
    
    switch (context->model) {
        case WS_MODEL_GLOBAL_NUMERIC:
        case WS_MODEL_PER_OUTPUT_NUMERIC:
            snprintf(buffer, size, "workspace:%d", context->data.workspace_id);
            break;
            
        case WS_MODEL_TAG_BASED:
            snprintf(buffer, size, "tags:0x%x(focus:%d)",
                    context->data.tags.visible_tags,
                    workspace_tag_to_index(context->data.tags.focused_tag));
            break;
            
        case WS_MODEL_SET_BASED:
            snprintf(buffer, size, "set:%d,ws:%d",
                    context->data.wayfire_set.set_id,
                    context->data.wayfire_set.workspace_id);
            break;
            
        default:
            strncpy(buffer, "unknown", size - 1);
            buffer[size - 1] = '\0';
            break;
    }
}

/* River tag helpers */
int workspace_tag_to_index(uint32_t tag_mask) {
    if (tag_mask == 0) return -1;
    /* Find position of lowest set bit (1-indexed) */
    return __builtin_ffs(tag_mask) - 1;
}

uint32_t workspace_index_to_tag(int index) {
    if (index < 0 || index >= 32) return 0;
    return 1u << index;
}

int workspace_count_tags(uint32_t tag_mask) {
    return __builtin_popcount(tag_mask);
}