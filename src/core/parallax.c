/*
 * parallax.c - Parallax helpers
 */

#include <string.h>
#include "../include/core.h"

parallax_mode_t parallax_mode_from_string(const char *name) {
    if (!name) return PARALLAX_WORKSPACE;
    if (strcmp(name, "workspace") == 0 || strcmp(name, "workspace_only") == 0) {
        return PARALLAX_WORKSPACE;
    }
    if (strcmp(name, "cursor") == 0 || strcmp(name, "mouse") == 0) {
        return PARALLAX_CURSOR;
    }
    if (strcmp(name, "hybrid") == 0 || strcmp(name, "workspace_plus_cursor") == 0) {
        return PARALLAX_HYBRID;
    }
    return PARALLAX_WORKSPACE;
}

const char* parallax_mode_to_string(parallax_mode_t mode) {
    switch (mode) {
        case PARALLAX_WORKSPACE: return "workspace";
        case PARALLAX_CURSOR:    return "cursor";
        case PARALLAX_HYBRID:    return "hybrid";
        default:                 return "workspace";
    }
}

