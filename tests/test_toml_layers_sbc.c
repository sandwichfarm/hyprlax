// Test TOML layer SBC parsing
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "include/hyprlax.h"
#include "include/config_toml.h"

// Minimal stubs
void ipc_cleanup(void *p) { (void)p; }
void renderer_destroy(renderer_t *p) { (void)p; }
void compositor_destroy(compositor_adapter_t *p) { (void)p; }
void platform_destroy(platform_t *p) { (void)p; }

START_TEST(test_toml_layer_sbc)
{
    const char *toml =
        "[global]\n"
        "fps = 144\n"
        "\n"
        "[[global.layers]]\n"
        "path = \"/tmp/a.png\"\n"
        "sbc = [0.2, 0.05, 1.1]\n"
        "\n"
        "[[global.layers]]\n"
        "path = \"/tmp/b.png\"\n"
        "saturation = 1.2\n"
        "brightness = 0.08\n"
        "contrast = 1.25\n";

    char path[] = "/tmp/hyprlax-test-layers-sbc-XXXXXX";
    int fd = mkstemp(path);
    ck_assert(fd >= 0);
    FILE *f = fdopen(fd, "w");
    ck_assert_ptr_nonnull(f);
    fwrite(toml, 1, strlen(toml), f);
    fclose(f);

    hyprlax_context_t *ctx = hyprlax_create();
    ck_assert_ptr_nonnull(ctx);
    int rc = config_apply_toml_to_context(ctx, path);
    ck_assert_int_eq(rc, 0);

    // Expect two layers added
    ck_assert_int_ge(ctx->layer_count, 2);
    parallax_layer_t *a = ctx->layers;
    parallax_layer_t *b = a ? a->next : NULL;
    ck_assert_ptr_nonnull(a);
    ck_assert_ptr_nonnull(b);

    // First layer: from composite array
    ck_assert(a->sbc_enabled == true);
    ck_assert_float_eq_tol(a->saturation, 0.2f, 0.0001);
    ck_assert_float_eq_tol(a->brightness, 0.05f, 0.0001);
    ck_assert_float_eq_tol(a->contrast, 1.1f, 0.0001);

    // Second layer: individual keys
    ck_assert(b->sbc_enabled == true);
    ck_assert_float_eq_tol(b->saturation, 1.2f, 0.0001);
    ck_assert_float_eq_tol(b->brightness, 0.08f, 0.0001);
    ck_assert_float_eq_tol(b->contrast, 1.25f, 0.0001);

    unlink(path);
}
END_TEST

Suite *toml_layers_sbc_suite(void) {
    Suite *s = suite_create("TOML_Layers_SBC");
    TCase *tc = tcase_create("Core");
    tcase_add_test(tc, test_toml_layer_sbc);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int failed;
    Suite *s = toml_layers_sbc_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_FORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
