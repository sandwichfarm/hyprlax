/*
 * layer.c - Layer management
 *
 * Handles creation, destruction, and manipulation of parallax layers.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/core.h"
#include "../include/log.h"
#include "../include/defaults.h"

static uint32_t next_layer_id = 1;

/* Create a new layer */
parallax_layer_t* layer_create(const char *image_path, float shift_multiplier, float opacity) {
    if (!image_path) return NULL;

    parallax_layer_t *layer = calloc(1, sizeof(parallax_layer_t));
    if (!layer) return NULL;

    layer->id = next_layer_id++;
    layer->image_path = strdup(image_path);
    if (!layer->image_path) {
        free(layer);
        return NULL;
    }

    layer->shift_multiplier = shift_multiplier;
    layer->shift_multiplier_x = shift_multiplier;
    layer->shift_multiplier_y = shift_multiplier;
    layer->opacity = opacity;
    layer->blur_amount = 0.0f;
    layer->z_index = 0;

    layer->invert_workspace_x = false;
    layer->invert_workspace_y = false;
    layer->invert_cursor_x = false;
    layer->invert_cursor_y = false;
    layer->invert_window_x = false;
    layer->invert_window_y = false;
    layer->hidden = false;

    layer->current_x = 0.0f;
    layer->current_y = 0.0f;

    layer->texture_id = 0;
    layer->texture_width = 0;
    layer->texture_height = 0;

    /* Content scaling defaults - these work for the common case */
    layer->fit_mode = LAYER_FIT_COVER;  /* Cover mode to ensure scale is applied and prevent smearing */
    layer->content_scale = HYPRLAX_DEFAULT_LAYER_SCALE;  /* Use config default that prevents smearing */
    layer->scale_is_custom = false;     /* Will inherit global unless overridden */
    layer->align_x = 0.5f;
    layer->align_y = 0.5f;
    layer->base_uv_x = 0.0f;
    layer->base_uv_y = 0.0f;

    /* Overflow/margins inherit by default */
    layer->overflow_mode = -1;
    layer->margin_px_x = 0.0f;
    layer->margin_px_y = 0.0f;
    layer->tile_x = -1;
    layer->tile_y = -1;

    /* Tint defaults: no tint */
    layer->tint_r = 1.0f;
    layer->tint_g = 1.0f;
    layer->tint_b = 1.0f;
    layer->tint_strength = 0.0f;

    layer->next = NULL;

    return layer;
}

/* Destroy a layer and free resources */
void layer_destroy(parallax_layer_t *layer) {
    if (!layer) return;

    if (layer->image_path) {
        free(layer->image_path);
    }

    // Note: OpenGL texture cleanup should be done by the renderer

    free(layer);
}

/* Update layer target offset with animation */
void layer_update_offset(parallax_layer_t *layer, float target_x, float target_y,
                        int duration_ms, easing_type_t easing) {
    if (!layer) return;

    /* Start animation from current position to target */
    animation_start(&layer->x_animation, layer->current_x, target_x, duration_ms, easing);
    animation_start(&layer->y_animation, layer->current_y, target_y, duration_ms, easing);
}

/* Update layer animations */
void layer_tick(parallax_layer_t *layer, timestamp_ms_t current_time) {
    if (!layer) return;

    /* Update current position from animations */
    if (animation_is_active(&layer->x_animation)) {
        layer->current_x = animation_evaluate(&layer->x_animation, current_time);
        layer->offset_x = layer->current_x;  /* Update offset for rendering */
    }

    if (animation_is_active(&layer->y_animation)) {
        layer->current_y = animation_evaluate(&layer->y_animation, current_time);
        layer->offset_y = layer->current_y;  /* Update offset for rendering */
    }
}

bool layer_is_visible(const parallax_layer_t *layer) {
    return layer && !layer->hidden && layer->opacity > 0.0f;
}

/* Advance animated GIF timing independently from rendering. This keeps hidden
 * or transparent layers on their real timeline without forcing GPU work. */
void layer_tick_gif(parallax_layer_t *layer, double current_time) {
    if (!layer || !layer->is_gif || layer->frame_count <= 1 ||
        !layer->gif_delays || !layer->gif_textures) {
        return;
    }

    if (!isfinite(current_time) || !isfinite(layer->last_frame_time) ||
        current_time < layer->last_frame_time) {
        layer->last_frame_time = current_time;
        return;
    }

    int current_delay_ms = layer->gif_delays[layer->current_frame];
    if (current_delay_ms < 10) current_delay_ms = 10;
    if (current_time - layer->last_frame_time < current_delay_ms / 1000.0) {
        return;
    }

    /* Skip complete loops mathematically after a long idle period. This keeps
     * wall-clock phase without replaying every elapsed frame. */
    double cycle = 0.0;
    for (int i = 0; i < layer->frame_count; i++) {
        int delay_ms = layer->gif_delays[i];
        if (delay_ms < 10) delay_ms = 10;
        cycle += delay_ms / 1000.0;
    }
    if (cycle > 0.0) {
        double elapsed = current_time - layer->last_frame_time;
        if (elapsed >= cycle) {
            double loops = floor(elapsed / cycle);
            layer->last_frame_time += loops * cycle;
        }
    }

    for (int advanced = 0; advanced < layer->frame_count; advanced++) {
        int delay_ms = layer->gif_delays[layer->current_frame];
        if (delay_ms < 10) delay_ms = 10;
        double delay = delay_ms / 1000.0;
        if (current_time - layer->last_frame_time < delay) break;

        layer->current_frame = (layer->current_frame + 1) % layer->frame_count;
        layer->texture_id = layer->gif_textures[layer->current_frame];
        layer->last_frame_time += delay;
    }
}

/* Add a layer to the list */
parallax_layer_t* layer_list_add(parallax_layer_t *head, parallax_layer_t *new_layer) {
    if (!new_layer) return head;

    if (!head) {
        return new_layer;
    }

    // Add to end of list
    parallax_layer_t *current = head;
    while (current->next) {
        current = current->next;
    }
    current->next = new_layer;
    new_layer->next = NULL;

    return head;
}

/* Remove a layer from the list */
parallax_layer_t* layer_list_remove(parallax_layer_t *head, uint32_t layer_id) {
    if (!head) return NULL;

    // Special case: removing head
    if (head->id == layer_id) {
        parallax_layer_t *new_head = head->next;
        layer_destroy(head);
        return new_head;
    }

    // Find and remove from list
    parallax_layer_t *current = head;
    while (current->next) {
        if (current->next->id == layer_id) {
            parallax_layer_t *to_remove = current->next;
            current->next = to_remove->next;
            layer_destroy(to_remove);
            break;
        }
        current = current->next;
    }

    return head;
}

/* Find a layer by ID */
parallax_layer_t* layer_list_find(parallax_layer_t *head, uint32_t layer_id) {
    parallax_layer_t *current = head;
    while (current) {
        if (current->id == layer_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/* Destroy all layers in the list */
void layer_list_destroy(parallax_layer_t *head) {
    while (head) {
        parallax_layer_t *next = head->next;
        layer_destroy(head);
        head = next;
    }
}

/* Count layers in the list */
int layer_list_count(parallax_layer_t *head) {
    int count = 0;
    parallax_layer_t *current = head;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}

/* Sort the linked list by z_index ascending (stable) */
parallax_layer_t* layer_list_sort_by_z(parallax_layer_t *head) {
    if (!head || !head->next) return head;
    parallax_layer_t *sorted = NULL;
    parallax_layer_t *node = head;
    while (node) {
        parallax_layer_t *next = node->next;
        /* insert node into sorted at proper position */
        if (!sorted || node->z_index < sorted->z_index) {
            node->next = sorted;
            sorted = node;
        } else {
            parallax_layer_t *cur = sorted;
            while (cur->next && cur->next->z_index <= node->z_index) {
                cur = cur->next;
            }
            node->next = cur->next;
            cur->next = node;
        }
        node = next;
    }
    return sorted;
}
