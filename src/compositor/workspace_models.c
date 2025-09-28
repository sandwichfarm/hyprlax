/*
 * workspace_models.c - Compositor workspace model implementation
 */

#include "workspace_models.h"
#include "../include/compositor.h"
#include "../include/hyprlax.h"
#include "../core/monitor.h"
#include "../include/log.h"
#include <stdio.h>
#include <stdlib.h>
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

workspace_model_t workspace_detect_model_for_adapter(const compositor_adapter_t *adapter) {
    if (!adapter) return WS_MODEL_GLOBAL_NUMERIC;
    /* Prefer explicit capability bits if set */
    uint64_t c = adapter->caps;
    if (c & C_CAP_WS_TAG_BASED) return WS_MODEL_TAG_BASED;
    if (c & C_CAP_WS_SET_BASED) return WS_MODEL_SET_BASED;
    if (c & C_CAP_WS_PER_OUTPUT_NUMERIC) return WS_MODEL_PER_OUTPUT_NUMERIC;
    if (c & C_CAP_WS_GLOBAL_NUMERIC) return WS_MODEL_GLOBAL_NUMERIC;
    /* No caps signaled: default to global numeric */
    return WS_MODEL_GLOBAL_NUMERIC;
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

    LOG_DEBUG("Workspace steal: %s -> %s",
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

    LOG_DEBUG("Workspace move: %s -> %s",
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

/* Calculate 2D parallax offset for 2D workspace models */
workspace_offset_t workspace_calculate_offset_2d(const workspace_context_t *from,
                                                const workspace_context_t *to,
                                                float shift_pixels,
                                                const workspace_policy_t *policy) {
    workspace_offset_t offset = {0.0f, 0.0f};

    if (!from || !to || from->model != to->model) {
        LOG_DEBUG("workspace_calculate_offset_2d: Invalid params or model mismatch");
        return offset;
    }

    LOG_DEBUG("workspace_calculate_offset_2d:");
    LOG_DEBUG("  Model: %s", workspace_model_to_string(from->model));
    LOG_DEBUG("  Shift pixels: %.1f", shift_pixels);

    switch (from->model) {
        case WS_MODEL_SET_BASED:
            /* Wayfire: 2D grid within sets */
            if (from->data.wayfire_set.set_id == to->data.wayfire_set.set_id) {
                /* Within same set, calculate 2D offset */
                /* Assuming 3x3 grid by default */
                int from_x = from->data.wayfire_set.workspace_id % 3;
                int from_y = from->data.wayfire_set.workspace_id / 3;
                int to_x = to->data.wayfire_set.workspace_id % 3;
                int to_y = to->data.wayfire_set.workspace_id / 3;

                offset.x = (to_x - from_x) * shift_pixels;
                offset.y = (to_y - from_y) * shift_pixels;

                LOG_DEBUG("  Wayfire set %d: (%d,%d) -> (%d,%d)",
                          from->data.wayfire_set.set_id, from_x, from_y, to_x, to_y);
            }
            break;

        case WS_MODEL_PER_OUTPUT_NUMERIC:
            /* Niri: 2D scrollable workspaces */
            /* Decode 2D position from workspace_id */
            /* Using encoding: workspace_id = y * MAX_COLUMNS + x */
            /* MAX_COLUMNS must be larger than any possible column count */
            int columns = 1000; /* Use large value to prevent overlap */
            int from_x = from->data.workspace_id % columns;
            int from_y = from->data.workspace_id / columns;
            int to_x = to->data.workspace_id % columns;
            int to_y = to->data.workspace_id / columns;

            offset.x = (to_x - from_x) * shift_pixels;
            /* Y shift is proportional to maintain aspect ratio */
            /* This is temporary until TOML config supports separate X/Y values */
            offset.y = (to_y - from_y) * shift_pixels;

            LOG_DEBUG("  Niri workspace ID %d->%d decoded as:",
                      from->data.workspace_id, to->data.workspace_id);
            LOG_DEBUG("    Position: (%d,%d) -> (%d,%d)", from_x, from_y, to_x, to_y);
            LOG_DEBUG("    Delta: X=%d, Y=%d", to_x - from_x, to_y - from_y);
            break;

        default:
            /* For 1D models, just use X axis */
            offset.x = workspace_calculate_offset(from, to, shift_pixels, policy);
            offset.y = 0.0f;
            LOG_DEBUG("  1D model, using X-only offset");
            break;
    }

    LOG_DEBUG("  Calculated offset: X=%.1f, Y=%.1f", offset.x, offset.y);

    return offset;
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
