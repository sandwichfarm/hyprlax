/*
 * render_core.c - Render orchestration extracted from hyprlax_main
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <string.h>
#include "../include/hyprlax.h"
#include "../include/renderer.h"
#include "../core/monitor.h"
#include "../include/log.h"

static double rc_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

static inline int rc_is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

/* texture loader (definition moved from hyprlax_main.c) */
#include "../stb_image.h"
GLuint load_texture(const char *path, int *width, int *height) {
    int channels;
    unsigned char *data = stbi_load(path, width, height, &channels, 4);
    if (!data) {
        LOG_ERROR("Failed to load image '%s': %s", path, stbi_failure_reason());
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    if (rc_is_pow2(*width) && rc_is_pow2(*height)) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return texture;
}

static void hyprlax_render_monitor(hyprlax_context_t *ctx, monitor_instance_t *monitor, double now_time) {
    if (!ctx || !ctx->renderer || !monitor) {
        LOG_TRACE("Skipping render: ctx=%p, renderer=%p, monitor=%p", ctx, ctx ? ctx->renderer : NULL, monitor);
        return;
    }
    if (!monitor->egl_surface) {
        LOG_WARN("Monitor %s has no EGL surface", monitor->name);
        return;
    }
    if (gles2_make_current(monitor->egl_surface) != HYPRLAX_SUCCESS) {
        LOG_ERROR("Failed to make EGL surface current for monitor %s", monitor->name);
        return;
    }
    glViewport(0, 0, monitor->width * monitor->scale, monitor->height * monitor->scale);

    static int s_profile = -1;
    if (s_profile == -1) {
        const char *p = getenv("HYPRLAX_PROFILE");
        s_profile = (p && *p) ? 1 : 0;
    }
    double t_draw_start = 0.0, t_present_start = 0.0;
    if (s_profile) t_draw_start = rc_get_time();

    RENDERER_BEGIN_FRAME(ctx->renderer);
    /* Frame prep: either clear (default) or fade previous frame for trails */
    if (ctx->config.render_accumulate) {
        float a = ctx->config.render_trail_strength;
        if (a > 0.0f && ctx->renderer && ctx->renderer->ops && ctx->renderer->ops->fade_frame) {
            ctx->renderer->ops->fade_frame(0.0f, 0.0f, 0.0f, a);
        }
    } else {
        if (ctx->renderer && ctx->renderer->ops && ctx->renderer->ops->clear) {
            ctx->renderer->ops->clear(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    input_manager_tick(&ctx->input, monitor, now_time, NULL, NULL);

    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        if (layer->hidden || layer->texture_id == 0) { layer = layer->next; continue; }

        /* Workspace-driven offsets (pixels) */
        /* Use the current animated value only; offset_x/y are maintained by layer_tick
           and should not be summed with current to avoid double-application. */
        float workspace_x = layer->current_x;
        float workspace_y = layer->current_y;

        /* Apply optional workspace inversion (global xor layer) */
        bool workspace_invert_x = ctx->config.invert_workspace_x ^ layer->invert_workspace_x;
        bool workspace_invert_y = ctx->config.invert_workspace_y ^ layer->invert_workspace_y;
        if (workspace_invert_x) workspace_x = -workspace_x;
        if (workspace_invert_y) workspace_y = -workspace_y;

        /* Cursor-driven offsets (normalized -> pixels) */
        float cursor_weight = ctx->input.weights[INPUT_CURSOR];
        float window_weight = ctx->input.weights[INPUT_WINDOW];
        float cursor_x_px = 0.0f;
        float cursor_y_px = 0.0f;
        if (cursor_weight > 0.0f) {
            input_sample_t cursor_sample;
            bool have_cursor_sample = input_manager_last_source(&ctx->input, monitor, INPUT_CURSOR, &cursor_sample);
            if (!have_cursor_sample || !cursor_sample.valid) {
                cursor_sample.x = ctx->cursor_eased_x * ctx->config.parallax_max_offset_x;
                cursor_sample.y = ctx->cursor_eased_y * ctx->config.parallax_max_offset_y;
                cursor_sample.valid = true;
            }
            cursor_x_px = cursor_sample.x * layer->shift_multiplier_x;
            cursor_y_px = cursor_sample.y * layer->shift_multiplier_y;
            bool cursor_invert_x = ctx->config.invert_cursor_x ^ layer->invert_cursor_x;
            bool cursor_invert_y = ctx->config.invert_cursor_y ^ layer->invert_cursor_y;
            if (cursor_invert_x) cursor_x_px = -cursor_x_px;
            if (cursor_invert_y) cursor_y_px = -cursor_y_px;
        }

        float window_x_px = 0.0f;
        float window_y_px = 0.0f;
        if (window_weight > 0.0f) {
            input_sample_t window_sample;
            bool have_window_sample = input_manager_last_source(&ctx->input, monitor, INPUT_WINDOW, &window_sample);
            if (have_window_sample && window_sample.valid) {
                window_x_px = window_sample.x * layer->shift_multiplier_x;
                window_y_px = window_sample.y * layer->shift_multiplier_y;
                bool window_invert_x = ctx->config.invert_window_x ^ layer->invert_window_x;
                bool window_invert_y = ctx->config.invert_window_y ^ layer->invert_window_y;
                if (window_invert_x) window_x_px = -window_x_px;
                if (window_invert_y) window_y_px = -window_y_px;
            }
        }

        /* Blend according to selected mode */
        float workspace_weight = ctx->input.weights[INPUT_WORKSPACE];
        float offset_x = workspace_x * workspace_weight + cursor_x_px * cursor_weight + window_x_px * window_weight;
        float offset_y = workspace_y * workspace_weight + cursor_y_px * cursor_weight + window_y_px * window_weight;

        texture_t tex = {
            .id = (uint32_t)layer->texture_id,
            .width = layer->texture_width > 0 ? layer->texture_width : layer->width,
            .height = layer->texture_height > 0 ? layer->texture_height : layer->height,
            .format = TEXTURE_FORMAT_RGBA
        };

        int eff_over = (layer->overflow_mode >= 0) ? layer->overflow_mode : ctx->config.render_overflow_mode;
        int eff_tile_x = (layer->tile_x >= 0) ? layer->tile_x : ctx->config.render_tile_x;
        int eff_tile_y = (layer->tile_y >= 0) ? layer->tile_y : ctx->config.render_tile_y;

        if (ctx->renderer->ops->draw_layer_ex) {
            float eff_shift = monitor_effective_shift_px(&ctx->config, monitor);
            LOG_DEBUG("Rendering layer: fit_mode=%d, content_scale=%.2f, shift=%.1f",
                      layer->fit_mode, layer->content_scale, eff_shift);
            renderer_layer_params_t p = {
                .fit_mode = layer->fit_mode,
                .content_scale = layer->content_scale,
                .align_x = layer->align_x,
                .align_y = layer->align_y,
                .base_uv_x = layer->base_uv_x,
                .base_uv_y = layer->base_uv_y,
                .overflow_mode = eff_over,
                .margin_px_x = (layer->margin_px_x != 0.0f || layer->margin_px_y != 0.0f) ? layer->margin_px_x : ctx->config.render_margin_px_x,
                .margin_px_y = (layer->margin_px_y != 0.0f || layer->margin_px_x != 0.0f) ? layer->margin_px_y : ctx->config.render_margin_px_y,
                .tile_x = eff_tile_x,
                .tile_y = eff_tile_y,
                .auto_safe_norm_x = (ctx->config.parallax_max_offset_x > 0.0f && (eff_tile_x == 0) && (eff_over == 4))
                    ? (ctx->config.parallax_max_offset_x / (float)monitor->width) : 0.0f,
                .auto_safe_norm_y = (ctx->config.parallax_max_offset_y > 0.0f && (eff_tile_y == 0) && (eff_over == 4))
                    ? (ctx->config.parallax_max_offset_y / (float)monitor->height) : 0.0f,
                .tint_r = layer->tint_r,
                .tint_g = layer->tint_g,
                .tint_b = layer->tint_b,
                .tint_strength = layer->tint_strength,
            };
            ctx->renderer->ops->draw_layer_ex(
                &tex,
                offset_x / monitor->width,
                offset_y / monitor->height,
                layer->opacity,
                layer->blur_amount,
                &p
            );
        } else if (ctx->renderer->ops->draw_layer) {
            ctx->renderer->ops->draw_layer(
                &tex,
                offset_x / monitor->width,
                offset_y / monitor->height,
                layer->opacity,
                layer->blur_amount
            );
        }

        layer = layer->next;
    }

    RENDERER_END_FRAME(ctx->renderer);
    double t_draw_end = s_profile ? rc_get_time() : 0.0;
    if (s_profile) t_present_start = t_draw_end;
    RENDERER_PRESENT(ctx->renderer);
    double t_present_end = s_profile ? rc_get_time() : 0.0;
    if (s_profile && ctx->config.debug) {
        double draw_ms = (t_draw_end - t_draw_start) * 1000.0;
        double present_ms = (t_present_end - t_present_start) * 1000.0;
        LOG_DEBUG("[PROFILE] monitor=%s draw=%.2f ms present=%.2f ms", monitor->name, draw_ms, present_ms);
    }
    if (monitor->wl_surface && ctx->platform && ctx->platform->ops && ctx->platform->ops->commit_monitor_surface) {
        ctx->platform->ops->commit_monitor_surface(monitor);
    }
}

void hyprlax_render_frame(hyprlax_context_t *ctx) {
    if (!ctx || !ctx->renderer) {
        LOG_ERROR("render_frame: No renderer available");
        return;
    }
    if (!ctx->monitors || ctx->monitors->count == 0) {
        LOG_WARN("No monitors available for rendering");
        return;
    }
    double now_time = rc_get_time();
    if (ctx->config.cursor_anim_duration > 0.0) {
        if (animation_is_active(&ctx->cursor_anim_x))
            ctx->cursor_eased_x = animation_evaluate(&ctx->cursor_anim_x, now_time);
        else
            ctx->cursor_eased_x = ctx->cursor_norm_x;
        if (animation_is_active(&ctx->cursor_anim_y))
            ctx->cursor_eased_y = animation_evaluate(&ctx->cursor_anim_y, now_time);
        else
            ctx->cursor_eased_y = ctx->cursor_norm_y;
    } else {
        ctx->cursor_eased_x = ctx->cursor_norm_x;
        ctx->cursor_eased_y = ctx->cursor_norm_y;
    }
    monitor_instance_t *monitor = ctx->monitors->head;
    while (monitor) {
        hyprlax_render_monitor(ctx, monitor, now_time);
        monitor = monitor->next;
    }
}

int hyprlax_load_layer_textures(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    int loaded = 0;
    parallax_layer_t *layer = ctx->layers;
    while (layer) {
        if (layer->texture_id == 0 && layer->image_path) {
            int img_width, img_height;
            GLuint texture = load_texture(layer->image_path, &img_width, &img_height);
            if (texture != 0) {
                layer->texture_id = texture;
                layer->width = img_width;
                layer->height = img_height;
                layer->texture_width = img_width;
                layer->texture_height = img_height;
                loaded++;
                if (ctx->config.debug) {
                    LOG_DEBUG("Loaded texture for layer: %s (%dx%d)",
                              layer->image_path, img_width, img_height);
                }
            } else {
                LOG_ERROR("Failed to load texture for layer: %s", layer->image_path);
            }
        }
        layer = layer->next;
    }

    if (ctx->config.debug && loaded > 0) {
        LOG_INFO("Loaded %d layer textures", loaded);
    }

    return HYPRLAX_SUCCESS;
}

/* (render functions intentionally not duplicated here) */
