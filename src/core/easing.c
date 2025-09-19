/*
 * easing.c - Easing functions for smooth animation
 *
 * Pure mathematical functions with no side effects or allocations.
 * All functions map input t ∈ [0,1] to output ∈ [0,1].
 */

#include <math.h>
#include <string.h>
#include "../include/core.h"

/* Linear interpolation - constant speed */
float ease_linear(float t) {
    return t;
}

/* Quadratic easing out - decelerating to zero velocity */
float ease_quad_out(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

/* Cubic easing out - decelerating to zero velocity */
float ease_cubic_out(float t) {
    return 1.0f - powf(1.0f - t, 3.0f);
}

/* Quartic easing out - decelerating to zero velocity */
float ease_quart_out(float t) {
    return 1.0f - powf(1.0f - t, 4.0f);
}

/* Quintic easing out - decelerating to zero velocity */
float ease_quint_out(float t) {
    return 1.0f - powf(1.0f - t, 5.0f);
}

/* Sinusoidal easing out - decelerating to zero velocity */
float ease_sine_out(float t) {
    return sinf((t * M_PI) / 2.0f);
}

/* Exponential easing out - decelerating to zero velocity */
float ease_expo_out(float t) {
    return t == 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

/* Circular easing out - decelerating to zero velocity */
float ease_circ_out(float t) {
    return sqrtf(1.0f - powf(t - 1.0f, 2.0f));
}

/* Back easing out - overshooting cubic easing */
float ease_back_out(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

/* Elastic easing out - exponentially decaying sine wave */
float ease_elastic_out(float t) {
    const float c4 = (2.0f * M_PI) / 3.0f;

    if (t == 0) return 0;
    if (t == 1) return 1;

    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
}

/* Bounce easing out - exponentially decaying parabolic bounce */
float ease_bounce_out(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;

    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

/* Custom snap easing - fast start with sharp deceleration */
float ease_custom_snap(float t) {
    if (t < 0.4f) {
        // Accelerate quickly for first 40% of time
        return 1.0f - powf(1.0f - (t * 2.5f), 6.0f);
    } else {
        // Then ease out more gently
        return 1.0f - powf(1.0f - t, 8.0f);
    }
}

/* Apply easing function by type */
float apply_easing(float t, easing_type_t type) {
    // Clamp input to valid range
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    switch (type) {
        case EASE_LINEAR:       return ease_linear(t);
        case EASE_QUAD_OUT:     return ease_quad_out(t);
        case EASE_CUBIC_OUT:    return ease_cubic_out(t);
        case EASE_QUART_OUT:    return ease_quart_out(t);
        case EASE_QUINT_OUT:    return ease_quint_out(t);
        case EASE_SINE_OUT:     return ease_sine_out(t);
        case EASE_EXPO_OUT:     return ease_expo_out(t);
        case EASE_CIRC_OUT:     return ease_circ_out(t);
        case EASE_BACK_OUT:     return ease_back_out(t);
        case EASE_ELASTIC_OUT:  return ease_elastic_out(t);
        case EASE_BOUNCE_OUT:   return ease_bounce_out(t);
        case EASE_CUSTOM_SNAP:  return ease_custom_snap(t);
        default:                return ease_linear(t);
    }
}

/* String to easing type conversion */
easing_type_t easing_from_string(const char *name) {
    if (!name) return EASE_LINEAR;

    if (strcmp(name, "linear") == 0)       return EASE_LINEAR;
    if (strcmp(name, "quad") == 0)         return EASE_QUAD_OUT;
    if (strcmp(name, "cubic") == 0)        return EASE_CUBIC_OUT;
    if (strcmp(name, "quart") == 0)        return EASE_QUART_OUT;
    if (strcmp(name, "quint") == 0)        return EASE_QUINT_OUT;
    if (strcmp(name, "sine") == 0)         return EASE_SINE_OUT;
    if (strcmp(name, "expo") == 0)         return EASE_EXPO_OUT;
    if (strcmp(name, "circ") == 0)         return EASE_CIRC_OUT;
    if (strcmp(name, "back") == 0)         return EASE_BACK_OUT;
    if (strcmp(name, "elastic") == 0)      return EASE_ELASTIC_OUT;
    if (strcmp(name, "bounce") == 0)       return EASE_BOUNCE_OUT;
    if (strcmp(name, "snap") == 0)         return EASE_CUSTOM_SNAP;

    return EASE_LINEAR;
}

/* Easing type to string conversion */
const char* easing_to_string(easing_type_t type) {
    static const char *names[] = {
        "linear", "quad", "cubic", "quart", "quint",
        "sine", "expo", "circ", "back", "elastic",
        "bounce", "snap"
    };

    if (type >= 0 && type < EASE_MAX) {
        return names[type];
    }
    return "linear";
}