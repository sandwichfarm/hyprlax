// Test runtime set/get of parallax properties via helper API
#include <check.h>
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

    // Do not call hyprlax_destroy here to avoid pulling more deps
}
END_TEST

Suite *runtime_suite(void) {
    Suite *s = suite_create("RuntimeProps");
    TCase *tc = tcase_create("Core");
    tcase_add_test(tc, test_runtime_set_get);
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
