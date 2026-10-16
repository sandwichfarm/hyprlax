// Test runtime set/get of parallax properties via helper API
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/hyprlax.h"

// Stubs to satisfy hyprlax_main.o links (unused in this test)
void ipc_cleanup(void *p) { (void)p; }
void renderer_destroy(renderer_t *p) { (void)p; }
void compositor_destroy(compositor_adapter_t *p) { (void)p; }
void platform_destroy(platform_t *p) { (void)p; }

extern int hyprlax_runtime_set_property(hyprlax_context_t *ctx, const char *property, const char *value);
extern int hyprlax_runtime_get_property(hyprlax_context_t *ctx, const char *property, char *out, size_t out_size);

/* Recorded by the reload stub in stubs_gfx.c */
extern const parallax_layer_t *stub_reload_last_layer;
extern char stub_reload_last_path[256];
extern int stub_reload_calls;
extern int stub_reload_result;

START_TEST(test_runtime_set_get)
{
    hyprlax_context_t *ctx = hyprlax_create();
    ck_assert_ptr_nonnull(ctx);

    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, "parallax.input", "cursor:1.0"), 0);
    /* Check that cursor weight is set to 1.0 and others to 0 */
    ck_assert_float_eq_tol(ctx->config.parallax_cursor_weight, 1.0f, 0.0001);
    ck_assert_float_eq_tol(ctx->config.parallax_workspace_weight, 0.0f, 0.0001);

    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, "parallax.sources.cursor.weight", "0.42"), 0);
    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, "parallax.sources.workspace.weight", "0.58"), 0);
    ck_assert_float_eq_tol(ctx->config.parallax_cursor_weight, 0.42f, 0.0001);
    ck_assert_float_eq_tol(ctx->config.parallax_workspace_weight, 0.58f, 0.0001);

    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, "parallax.invert.cursor.x", "true"), 0);
    ck_assert(ctx->config.invert_cursor_x == true);
    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, "parallax.invert.cursor.y", "false"), 0);
    ck_assert(ctx->config.invert_cursor_y == false);

    char buf[128];
    ck_assert_int_eq(hyprlax_runtime_get_property(ctx, "parallax.input", buf, sizeof(buf)), 0);
    /* After setting weights, should return "workspace:0.58,cursor:0.42" or similar */
    ck_assert(strstr(buf, "cursor:0.42") != NULL);
    ck_assert(strstr(buf, "workspace:0.58") != NULL);
    ck_assert_int_eq(hyprlax_runtime_get_property(ctx, "parallax.sources.cursor.weight", buf, sizeof(buf)), 0);
    ck_assert_str_eq(buf, "0.420");

    hyprlax_destroy(ctx);
}
END_TEST

/* layer.<id>.path must go through the layer loader once the renderer is up so
 * animated GIF layers rebuild all frame textures instead of swapping one. */
START_TEST(test_runtime_layer_path_uses_layer_loader)
{
    hyprlax_context_t *ctx = hyprlax_create();
    ck_assert_ptr_nonnull(ctx);
    ck_assert_int_eq(hyprlax_add_layer(ctx, "/tmp/first.png", 1.0f, 1.0f, 0.0f), HYPRLAX_SUCCESS);
    parallax_layer_t *layer = ctx->layers;
    ck_assert_ptr_nonnull(layer);
    char prop[64];
    snprintf(prop, sizeof(prop), "layer.%u.path", layer->id);

    /* Renderer not initialized: only the path is recorded, nothing loaded. */
    stub_reload_calls = 0;
    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, prop, "/tmp/second.gif"), 0);
    ck_assert_str_eq(layer->image_path, "/tmp/second.gif");
    ck_assert_int_eq(stub_reload_calls, 0);

    /* Renderer initialized: the request is handed to the layer loader. */
    renderer_t *fake_renderer = calloc(1, sizeof(renderer_t));
    ck_assert_ptr_nonnull(fake_renderer);
    fake_renderer->initialized = true;
    ctx->renderer = fake_renderer;

    stub_reload_result = 0;
    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, prop, "/tmp/third.gif"), 0);
    ck_assert_int_eq(stub_reload_calls, 1);
    ck_assert_ptr_eq(stub_reload_last_layer, layer);
    ck_assert_str_eq(stub_reload_last_path, "/tmp/third.gif");

    /* A failed reload is reported and the loader is still the one deciding. */
    stub_reload_result = -1;
    ck_assert_int_eq(hyprlax_runtime_set_property(ctx, prop, "/tmp/missing.gif"), -1);
    ck_assert_int_eq(stub_reload_calls, 2);

    ctx->renderer = NULL;
    free(fake_renderer);
    hyprlax_destroy(ctx);
}
END_TEST

Suite *runtime_suite(void) {
    Suite *s = suite_create("RuntimeProps");
    TCase *tc = tcase_create("Core");
    tcase_add_test(tc, test_runtime_set_get);
    tcase_add_test(tc, test_runtime_layer_path_uses_layer_loader);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int failed;
    Suite *s = runtime_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_FORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
