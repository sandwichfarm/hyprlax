/*
 * animation.c - Animation state management
 *
 * Handles time-based animations with easing functions.
 * No allocations in the evaluate path for maximum performance.
 * Uses 64-bit millisecond timestamps to prevent overflow.
 */

#include <string.h>
#include "../include/core.h"
#include "../include/defaults.h"

/* Start a new animation */
void animation_start(animation_state_t *anim, float from, float to,
                    int duration_ms, easing_type_t easing) {
    if (!anim) return;

    anim->from_value = from;
    anim->to_value = to;
    anim->duration_ms = duration_ms;
    anim->easing = easing;
    anim->start_time = -1;  /* Will be set on first evaluate */
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
float animation_evaluate(animation_state_t *anim, timestamp_ms_t current_time) {
    if (!anim || !anim->active) {
        return anim ? anim->to_value : 0.0f;
    }

    /* Lazy initialization of start time */
    if (anim->start_time < 0) {
        anim->start_time = current_time;
    }

    /* Calculate elapsed time in milliseconds */
    int64_t elapsed_ms = time_elapsed_ms(anim->start_time, current_time);

    if (elapsed_ms <= 0) {
        return anim->from_value;
    }

    if (elapsed_ms >= anim->duration_ms) {
        anim->completed = true;
        anim->active = false;
        return anim->to_value;
    }

    /* Calculate normalized time [0, 1] */
    float t = (float)elapsed_ms / (float)anim->duration_ms;

    /* Smooth completion: treat very close to 1.0 as complete */
    if (t > HYPRLAX_ANIM_COMPLETE_EPS) {
        t = 1.0f;
    }

    /* Apply easing */
    float eased_t = apply_easing(t, anim->easing);

    /* Interpolate value */
    return anim->from_value + (anim->to_value - anim->from_value) * eased_t;
}

/* Check if animation is active */
bool animation_is_active(const animation_state_t *anim) {
    return anim && anim->active;
}

/* Check if animation is complete */
bool animation_is_complete(const animation_state_t *anim, timestamp_ms_t current_time) {
    if (!anim) return true;
    if (anim->completed) return true;
    if (!anim->active) return true;

    if (anim->start_time < 0) return false;

    int64_t elapsed_ms = time_elapsed_ms(anim->start_time, current_time);
    return elapsed_ms >= anim->duration_ms;
}
