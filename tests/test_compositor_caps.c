/* Simple cap/model sanity checks for adapters */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "include/compositor.h"
#include "compositor/workspace_models.h"

static void check_hyprland_caps(void) {
#ifdef ENABLE_HYPRLAND
    compositor_adapter_t *c = NULL;
    int rc = compositor_create_by_name(&c, "hyprland");
    assert(rc == 0 && c);
    /* Either statically set or normalized via ops */
    assert((c->caps & C_CAP_GLOBAL_CURSOR) != 0);
    workspace_model_t m = workspace_detect_model_for_adapter(c);
    assert(m == WS_MODEL_GLOBAL_NUMERIC || m == WS_MODEL_PER_OUTPUT_NUMERIC);
    compositor_destroy(c);
#else
    (void)check_hyprland_caps; /* no-op when not enabled */
#endif
}

static void check_river_model(void) {
#ifdef ENABLE_RIVER
    compositor_adapter_t *c = NULL;
    int rc = compositor_create_by_name(&c, "river");
    assert(rc == 0 && c);
    workspace_model_t m = workspace_detect_model_for_adapter(c);
    assert(m == WS_MODEL_TAG_BASED);
    compositor_destroy(c);
#endif
}

int main(void) {
    check_hyprland_caps();
    check_river_model();
    printf("ok\n");
    return 0;
}
