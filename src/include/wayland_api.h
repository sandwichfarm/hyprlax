/*
 * wayland_api.h - Minimal Wayland platform exports used by other modules
 */

#ifndef HYPRLAX_WAYLAND_API_H
#define HYPRLAX_WAYLAND_API_H

#include "hyprlax.h"

/* Provide application context to Wayland platform after creation */
void wayland_set_context(hyprlax_context_t *ctx);

/* Return latest known global cursor position in compositor coordinates.
 * Returns true if a recent position is known. */
bool wayland_get_cursor_global(double *x, double *y);

/* Query current Wayland window size (width/height in pixels) */
void wayland_get_window_size(int *width, int *height);

/* Commit a specific monitor surface (frame pacing + wl_surface_commit) */
void wayland_commit_monitor_surface(monitor_instance_t *monitor);

/* Force realization of monitors based on discovered outputs if none exist yet. */
void wayland_realize_monitors_now(void);

#endif /* HYPRLAX_WAYLAND_API_H */
