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

#endif /* HYPRLAX_WAYLAND_API_H */
