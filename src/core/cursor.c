/*
 * cursor.c - Cursor input processing (sampling, smoothing, easing)
 */

#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "../include/hyprlax.h"
#include "../include/core.h"
#include "../include/log.h"
#include "../core/monitor.h"

static void cursor_apply_sample(hyprlax_context_t *ctx, float norm_x, float norm_y) {
    if (!ctx) return;
    float sx = norm_x * ctx->config.cursor_sensitivity_x;
    float sy = norm_y * ctx->config.cursor_sensitivity_y;

    float a = ctx->config.cursor_ema_alpha;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    ctx->cursor_ema_x = ctx->cursor_ema_x + a * (sx - ctx->cursor_ema_x);
    ctx->cursor_ema_y = ctx->cursor_ema_y + a * (sy - ctx->cursor_ema_y);

    if (ctx->cursor_ema_x < -1.0f) ctx->cursor_ema_x = -1.0f;
    if (ctx->cursor_ema_x >  1.0f) ctx->cursor_ema_x =  1.0f;
    if (ctx->cursor_ema_y < -1.0f) ctx->cursor_ema_y = -1.0f;
    if (ctx->cursor_ema_y >  1.0f) ctx->cursor_ema_y =  1.0f;

    ctx->cursor_norm_x = ctx->cursor_ema_x;
    ctx->cursor_norm_y = ctx->cursor_ema_y;
}

bool hyprlax_cursor_tick(hyprlax_context_t *ctx) {
    if (!ctx) return false;
    if (ctx->cursor_event_fd >= 0) {
        uint64_t expirations; (void)read(ctx->cursor_event_fd, &expirations, sizeof(expirations));
    }

    double x = 0.0, y = 0.0;
    bool got_pos = false;

    if (ctx->config.cursor_follow_global &&
        ctx->compositor && ctx->compositor->ops && ctx->compositor->ops->get_cursor_position) {
        double cx = 0.0, cy = 0.0;
        if (ctx->compositor->ops->get_cursor_position(&cx, &cy) == HYPRLAX_SUCCESS) {
            x = cx; y = cy; got_pos = true;
            LOG_TRACE("Compositor cursor: x=%.1f, y=%.1f", x, y);
        }
    }
    if (!got_pos && ctx->platform && ctx->platform->ops && ctx->platform->ops->get_cursor_global) {
        double px = 0.0, py = 0.0;
        if (ctx->platform->ops->get_cursor_global(&px, &py)) {
            x = px; y = py; got_pos = true;
            LOG_TRACE("Platform pointer: x=%.1f, y=%.1f", x, y);
        }
    }
    if (!got_pos) return false;

    int mon_x = 0, mon_y = 0, mon_w = 1920, mon_h = 1080;
    if (ctx->monitors && ctx->monitors->head) {
        monitor_instance_t *m = ctx->monitors->head;
        monitor_instance_t *found = NULL;
        while (m) {
            if (x >= m->global_x && x < (m->global_x + m->width) &&
                y >= m->global_y && y < (m->global_y + m->height)) {
                found = m; break;
            }
            m = m->next;
        }
        if (!found) found = ctx->monitors->primary ? ctx->monitors->primary : ctx->monitors->head;
        if (found) {
            mon_x = found->global_x; mon_y = found->global_y; mon_w = found->width; mon_h = found->height;
        }
    }

    double cx = mon_x + mon_w * 0.5;
    double cy = mon_y + mon_h * 0.5;
    double dx = x - cx;
    double dy = y - cy;

    double dz = ctx->config.cursor_deadzone_px;
    if (fabs(dx) < dz) dx = 0.0;
    if (fabs(dy) < dz) dy = 0.0;

    float nx = 0.0f, ny = 0.0f;
    if (mon_w > 0) nx = (float)(dx / (mon_w * 0.5));
    if (mon_h > 0) ny = (float)(dy / (mon_h * 0.5));
    if (nx < -1.0f) nx = -1.0f;
    if (nx > 1.0f) nx = 1.0f;
    if (ny < -1.0f) ny = -1.0f;
    if (ny > 1.0f) ny = 1.0f;

    float prev_x = ctx->cursor_norm_x;
    float prev_y = ctx->cursor_norm_y;

    cursor_apply_sample(ctx, nx, ny);

    if (ctx->config.cursor_anim_duration > 0.0) {
        if (!ctx->cursor_ease_initialized) {
            ctx->cursor_eased_x = ctx->cursor_norm_x;
            ctx->cursor_eased_y = ctx->cursor_norm_y;
            ctx->cursor_ease_initialized = true;
        }
        const float thr = 0.0003f;
        if (animation_is_active(&ctx->cursor_anim_x)) {
            if (fabsf(ctx->cursor_anim_x.to_value - ctx->cursor_norm_x) > thr) {
                ctx->cursor_anim_x.to_value = ctx->cursor_norm_x;
                ctx->cursor_anim_x.duration = ctx->config.cursor_anim_duration;
                ctx->cursor_anim_x.easing = ctx->config.cursor_easing;
            }
        } else {
            float ex = ctx->cursor_eased_x;
            if (fabsf(ctx->cursor_norm_x - ex) > thr) {
                animation_start(&ctx->cursor_anim_x, ex, ctx->cursor_norm_x,
                                ctx->config.cursor_anim_duration, ctx->config.cursor_easing);
            }
        }
        if (animation_is_active(&ctx->cursor_anim_y)) {
            if (fabsf(ctx->cursor_anim_y.to_value - ctx->cursor_norm_y) > thr) {
                ctx->cursor_anim_y.to_value = ctx->cursor_norm_y;
                ctx->cursor_anim_y.duration = ctx->config.cursor_anim_duration;
                ctx->cursor_anim_y.easing = ctx->config.cursor_easing;
            }
        } else {
            float ey = ctx->cursor_eased_y;
            if (fabsf(ctx->cursor_norm_y - ey) > thr) {
                animation_start(&ctx->cursor_anim_y, ey, ctx->cursor_norm_y,
                                ctx->config.cursor_anim_duration, ctx->config.cursor_easing);
            }
        }
    }

    float dxn = fabsf(ctx->cursor_norm_x - prev_x);
    float dyn = fabsf(ctx->cursor_norm_y - prev_y);
    if (ctx->config.debug) return true;
    return (dxn > 0.0015f || dyn > 0.0015f);
}
