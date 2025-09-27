/*
 * animation.c - Animation state management
 *
 * Handles time-based animations with easing functions.
 * No allocations in the evaluate path for maximum performance.
 */

#include <string.h>
#include "../include/core.h"

/* Start a new animation */
void animation_start(animation_state_t *anim, float from, float to,
                    double duration, easing_type_t easing) {
    if (!anim) return;

    anim->from_value = from;
    anim->to_value = to;
    anim->duration = duration;
    anim->easing = easing;
    anim->start_time = -1.0;  // Will be set on first evaluate
    anim->active = true;
    anim->completed = false;
}

/* Stop an animation */
void animation_stop(animation_state_t *anim) {
    if (!anim) return;

    anim->active = false;
    anim->completed = true;
}

/* Evaluate animation at current time - no allocations */
float animation_evaluate(animation_state_t *anim, double current_time) {
    if (!anim || !anim->active) {
        return anim ? anim->to_value : 0.0f;
    }

    // Lazy initialization of start time
    if (anim->start_time < 0) {
        // Initialize start time on first evaluation
        anim->start_time = current_time;
    }

    // Calculate progress
    double elapsed = current_time - anim->start_time;
    if (elapsed <= 0.0) {
        return anim->from_value;
    }

    if (elapsed >= anim->duration) {
        anim->completed = true;
        anim->active = false;
        return anim->to_value;
    }

    // Calculate normalized time [0, 1]
    float t = (float)(elapsed / anim->duration);

    // Smooth completion: treat very close to 1.0 as complete
    if (t > 0.995f) {
        t = 1.0f;
    }

    // Apply easing
    float eased_t = apply_easing(t, anim->easing);

    // Interpolate value
    return anim->from_value + (anim->to_value - anim->from_value) * eased_t;
}

/* Check if animation is active */
bool animation_is_active(const animation_state_t *anim) {
    return anim && anim->active;
}

/* Check if animation is complete */
bool animation_is_complete(const animation_state_t *anim, double current_time) {
    if (!anim) return true;
    if (anim->completed) return true;
    if (!anim->active) return true;

    if (anim->start_time < 0) return false;

    double elapsed = current_time - anim->start_time;
    return elapsed >= anim->duration;
}