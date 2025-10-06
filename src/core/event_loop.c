/*
 * event_loop.c - Timerfd/epoll helpers and setup
 */

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include "../include/hyprlax.h"
#include <stdlib.h>
#include "../include/platform.h"
#include "../include/compositor.h"
#include "../include/log.h"
#include "../ipc.h"
#include "../include/defaults.h"

int create_timerfd_monotonic(void) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    return fd;
}

void disarm_timerfd(int fd) {
    if (fd < 0) return;
    struct itimerspec its = {0};
    timerfd_settime(fd, 0, &its, NULL);
}

void arm_timerfd_ms(int fd, int initial_ms, int interval_ms) {
    if (fd < 0) return;
    struct itimerspec its;
    its.it_value.tv_sec = initial_ms / 1000;
    its.it_value.tv_nsec = (initial_ms % 1000) * 1000000L;
    its.it_interval.tv_sec = interval_ms / 1000;
    its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000L;
    timerfd_settime(fd, 0, &its, NULL);
}

int epoll_add_fd(int epfd, int fd, uint32_t events) {
    if (epfd < 0 || fd < 0) return -1;
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

int epoll_del_fd(int epfd, int fd) {
    if (epfd < 0 || fd < 0) return -1;
    /* According to epoll_ctl docs, the event pointer is ignored for DEL */
    return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
}

void hyprlax_setup_epoll(hyprlax_context_t *ctx) {
    if (!ctx) return;
    ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epoll_fd < 0) return;

    ctx->platform_event_fd = (ctx->platform && ctx->platform->ops->get_event_fd)
        ? ctx->platform->ops->get_event_fd() : -1;
    ctx->compositor_event_fd = (ctx->compositor && ctx->compositor->ops->get_event_fd)
        ? ctx->compositor->ops->get_event_fd() : -1;
    ctx->ipc_event_fd = (ctx->ipc_ctx) ? ((ipc_context_t*)ctx->ipc_ctx)->socket_fd : -1;

    ctx->frame_timer_fd = create_timerfd_monotonic();
    ctx->debounce_timer_fd = create_timerfd_monotonic();
    ctx->frame_timer_armed = false;
    ctx->debounce_pending = false;
    disarm_timerfd(ctx->frame_timer_fd);
    disarm_timerfd(ctx->debounce_timer_fd);

    epoll_add_fd(ctx->epoll_fd, ctx->platform_event_fd, EPOLLIN);
    epoll_add_fd(ctx->epoll_fd, ctx->compositor_event_fd, EPOLLIN);
    if (ctx->cursor_event_fd >= 0) {
        epoll_add_fd(ctx->epoll_fd, ctx->cursor_event_fd, EPOLLIN);
    }
    epoll_add_fd(ctx->epoll_fd, ctx->ipc_event_fd, EPOLLIN);
    epoll_add_fd(ctx->epoll_fd, ctx->frame_timer_fd, EPOLLIN);
    epoll_add_fd(ctx->epoll_fd, ctx->debounce_timer_fd, EPOLLIN);
}

void hyprlax_arm_frame_timer(hyprlax_context_t *ctx, int fps) {
    if (!ctx) return;
    if (fps <= 0) fps = HYPRLAX_DEFAULT_FPS;
    int interval_ms = (int)(1000.0 / (double)fps);
    if (ctx->frame_timer_fd >= 0) {
        arm_timerfd_ms(ctx->frame_timer_fd, interval_ms, interval_ms);
        ctx->frame_timer_armed = true;
    }
}

void hyprlax_disarm_frame_timer(hyprlax_context_t *ctx) {
    if (!ctx) return;
    if (ctx->frame_timer_fd >= 0) {
        disarm_timerfd(ctx->frame_timer_fd);
    }
    ctx->frame_timer_armed = false;
}

void hyprlax_arm_debounce(hyprlax_context_t *ctx, int debounce_ms) {
    if (!ctx) return;
    if (ctx->debounce_timer_fd >= 0) {
        arm_timerfd_ms(ctx->debounce_timer_fd, debounce_ms, 0);
        ctx->debounce_pending = true;
    }
}

void hyprlax_clear_timerfd(int fd) {
    if (fd < 0) return;
    uint64_t expirations;
    (void)read(fd, &expirations, sizeof(expirations));
}

/* Internal time helper */
static double ev_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

/* Main run loop */
int hyprlax_run(hyprlax_context_t *ctx) {
    if (!ctx) return HYPRLAX_ERROR_INVALID_ARGS;

    if (ctx->config.debug) {
        LOG_DEBUG("Starting main loop (target FPS: %d)", ctx->config.target_fps);
    }

    static int s_render_diag = -1;
    if (s_render_diag == -1) {
        const char *p = getenv("HYPRLAX_RENDER_DIAG");
        s_render_diag = (p && *p) ? 1 : 0;
    }

    double last_render_time = ev_get_time();
    double last_frame_time = last_render_time;
    double frame_time = 1.0 / (double)(ctx->config.target_fps > 0 ? ctx->config.target_fps : HYPRLAX_DEFAULT_FPS);
    int prev_target_fps = ctx->config.target_fps;
    int frame_count = 0;
    double debug_timer = 0.0;
    bool needs_render = true;

    while (ctx->running) {
        int current_fps = ctx->config.target_fps;
        if (current_fps <= 0) current_fps = HYPRLAX_DEFAULT_FPS;
        if (current_fps != prev_target_fps) {
            const char *fc_env = getenv("HYPRLAX_FRAME_CALLBACK");
            bool use_frame_callback = (fc_env && *fc_env);
            if (!use_frame_callback) {
                hyprlax_arm_frame_timer(ctx, current_fps);
            }
            prev_target_fps = current_fps;
        }
        frame_time = 1.0 / (double)current_fps;

        /* diagnostics placeholders retained for future verbose tracing */
        double current_time = ev_get_time();
        ctx->delta_time = current_time - last_frame_time;
        last_frame_time = current_time;

        platform_event_t platform_event;
        if (PLATFORM_POLL_EVENTS(ctx->platform, &platform_event) == HYPRLAX_SUCCESS) {
            switch (platform_event.type) {
                case PLATFORM_EVENT_CLOSE: ctx->running = false; break;
                case PLATFORM_EVENT_RESIZE:
                    hyprlax_handle_resize(ctx, platform_event.data.resize.width, platform_event.data.resize.height);
                    needs_render = true; break;
                default: break;
            }
        }

        if (ctx->ipc_ctx && ipc_process_commands((ipc_context_t*)ctx->ipc_ctx)) {
            needs_render = true;
        }

        if (ctx->compositor && ctx->compositor->ops->poll_events) {
            compositor_event_t comp_event;
            if (ctx->compositor->ops->poll_events(&comp_event) == HYPRLAX_SUCCESS) {
                if (comp_event.type == COMPOSITOR_EVENT_WORKSPACE_CHANGE) {
                    extern void process_workspace_event(hyprlax_context_t *ctx, const compositor_event_t *comp_event);
                    process_workspace_event(ctx, &comp_event);
                    needs_render = true;
                }
            }
        }

        bool animations_active = false;
        {
            parallax_layer_t *layer = ctx->layers;
            while (layer) {
                if (animation_is_active(&layer->x_animation) || animation_is_active(&layer->y_animation)) { animations_active = true; break; }
                layer = layer->next;
            }
            if (!animations_active && ctx->monitors) {
            monitor_instance_t *m = ctx->monitors->head;
            while (m) { if (m->animating) { animations_active = true; break; } m = m->next; }
            }
        }

        const char *use_fc = getenv("HYPRLAX_FRAME_CALLBACK");
        if (animations_active) {
            if (use_fc && *use_fc && ctx->monitors) {
                bool can_render = false;
                monitor_instance_t *m = ctx->monitors->head;
                while (m) { if (!m->frame_pending) { can_render = true; break; } m = m->next; }
                needs_render = needs_render || can_render;
            } else {
                needs_render = true;
            }
        }

        if (needs_render) {
            double time_since_render = current_time - last_render_time;
            if (time_since_render < frame_time) {
                const char *fc_env = getenv("HYPRLAX_FRAME_CALLBACK");
                bool use_frame_callback = (fc_env && *fc_env);
                if (!use_frame_callback) {
                    int sleep_ms = (int)((frame_time - time_since_render) * 1000.0);
                    if (sleep_ms > 0) {
                        struct timespec ts; ts.tv_sec = sleep_ms / 1000; ts.tv_nsec = (sleep_ms % 1000) * 1000000L;
                        nanosleep(&ts, NULL);
                    }
                }
            }
            /* Ensure input providers (e.g., cursor) update during continuous render
               windows (animations), even when we aren't blocking on epoll. */
            hyprlax_cursor_tick(ctx);
            /* Advance animations before rendering */
            hyprlax_update_layers(ctx, current_time);
            if (ctx->monitors) {
                monitor_instance_t *m = ctx->monitors->head;
                while (m) { monitor_update_animation(m, current_time); m = m->next; }
            }
            hyprlax_render_frame(ctx);
            ctx->fps = 1.0 / (time_since_render > 0 ? time_since_render : frame_time);
            last_render_time = current_time;
            frame_count++;
            needs_render = ctx->deferred_render_needed;
            ctx->deferred_render_needed = false;
            if (ctx->config.debug) {
                debug_timer += time_since_render;
                if (debug_timer >= 1.0) {
                    LOG_DEBUG("FPS: %.1f, Layers: %d, Animations: %s", ctx->fps, ctx->layer_count, animations_active ? "active" : "idle");
                    debug_timer = 0.0;
                }
            }
        } else {
            const char *fc_env = getenv("HYPRLAX_FRAME_CALLBACK");
            bool use_frame_callback = (fc_env && *fc_env);
            if (!use_frame_callback) {
                if (animations_active) {
                    if (!ctx->frame_timer_armed) hyprlax_arm_frame_timer(ctx, ctx->config.target_fps);
                } else {
                    if (ctx->frame_timer_armed) hyprlax_disarm_frame_timer(ctx);
                    if (needs_render) {
                        double target_wake_time = last_render_time + frame_time;
                        double remain = target_wake_time - current_time;
                        int remain_ms = (int)(remain * 1000.0);
                        if (remain_ms < 1) remain_ms = 1;
                        arm_timerfd_ms(ctx->frame_timer_fd, remain_ms, 0);
                    }
                }
            }

            if (ctx->epoll_fd >= 0) {
                struct epoll_event evlist[6];
                int n = epoll_wait(ctx->epoll_fd, evlist, 6, -1);
                if (n < 0) {
                    if (errno == EINTR) { if (!ctx->running) break; continue; }
                    continue;
                }
                if (n > 0) {
                    for (int i = 0; i < n; i++) {
                        int fd = evlist[i].data.fd;
                        if (fd == ctx->debounce_timer_fd) {
                            hyprlax_clear_timerfd(fd);
                            if (ctx->debounce_pending) {
                                ctx->debounce_pending = false;
                                extern void process_workspace_event(hyprlax_context_t *ctx, const compositor_event_t *comp_event);
                                process_workspace_event(ctx, &ctx->pending_event);
                                needs_render = true;
                            }
                        } else if (fd == ctx->frame_timer_fd) {
                            hyprlax_clear_timerfd(fd);
                            needs_render = true;
                        } else if (fd == ctx->cursor_event_fd) {
                            if (hyprlax_cursor_tick(ctx)) needs_render = true;
                        } else {
                            /* Other FDs handled in subsequent loop iteration */
                        }
                    }
                }
                continue;
            } else {
                double sleep_time = 1.0 / ctx->config.idle_poll_rate;
                struct timespec ts;
                ts.tv_sec = (time_t)sleep_time;
                ts.tv_nsec = (long)((sleep_time - (time_t)sleep_time) * 1e9);
                if (ts.tv_nsec > 999999999) ts.tv_nsec = 999999999;
                if (ts.tv_nsec < 0) ts.tv_nsec = 0;
                nanosleep(&ts, NULL);
            }
        }
    }

    return HYPRLAX_SUCCESS;
}
