// Test TOML parsing for parallax globals
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "include/config_toml.h"
#include "include/core.h"
#include <unistd.h>

static const char *toml_text =
    "[global]\n"
    "fps = 120\n"
    "duration = 1.2\n"
    "shift = 240\n"
    "debug = true\n"
    "\n"
    "[global.parallax]\n"
    "mode = \"hybrid\"\n"
    "\n"
    "[global.parallax.sources]\n"
    "workspace.weight = 0.6\n"
    "cursor.weight = 0.4\n"
    "\n"
    "[global.parallax.invert.workspace]\n"
    "x = true\n"
    "y = false\n"
    "\n"
    "[global.parallax.invert.cursor]\n"
    "x = false\n"
    "y = true\n"
    "\n"
    "[global.parallax.max_offset_px]\n"
    "x = 300\n"
    "y = 180\n"
    "\n"
    "[global.input.cursor]\n"
    "sensitivity_x = 1.2\n"
    "sensitivity_y = 0.8\n"
    "deadzone_px = 5\n"
    "ema_alpha = 0.2\n";

START_TEST(test_parse_parallax_globals)
{
    // write TOML to temp file
    char path[] = "/tmp/hyprlax-test-toml-XXXXXX";
    int fd = mkstemp(path);
    ck_assert_msg(fd >= 0, "Failed to create temp file");
    FILE *f = fdopen(fd, "w");
    ck_assert_ptr_nonnull(f);
    fwrite(toml_text, 1, strlen(toml_text), f);
    fclose(f);

    config_t cfg;
    config_set_defaults(&cfg);
    int rc = config_load_toml(&cfg, path);
    ck_assert_int_eq(rc, 0);

    ck_assert_int_eq(cfg.target_fps, 120);
    ck_assert_float_eq_tol(cfg.animation_duration, 1.2, 0.0001);
    ck_assert_float_eq_tol(cfg.shift_pixels, 240.0f, 0.0001);
    ck_assert(cfg.debug == true);

    /* Check that weights are set correctly - mode is now determined by weights */
    ck_assert_float_eq_tol(cfg.parallax_workspace_weight, 0.6f, 0.0001);
    ck_assert_float_eq_tol(cfg.parallax_cursor_weight, 0.4f, 0.0001);

    ck_assert(cfg.invert_workspace_x == true);
    ck_assert(cfg.invert_workspace_y == false);
    ck_assert(cfg.invert_cursor_x == false);
    ck_assert(cfg.invert_cursor_y == true);

    ck_assert_float_eq_tol(cfg.parallax_max_offset_x, 300.0f, 0.0001);
    ck_assert_float_eq_tol(cfg.parallax_max_offset_y, 180.0f, 0.0001);

    ck_assert_float_eq_tol(cfg.cursor_sensitivity_x, 1.2f, 0.0001);
    ck_assert_float_eq_tol(cfg.cursor_sensitivity_y, 0.8f, 0.0001);
    ck_assert_float_eq_tol(cfg.cursor_deadzone_px, 5.0f, 0.0001);
    ck_assert_float_eq_tol(cfg.cursor_ema_alpha, 0.2f, 0.0001);

    unlink(path);
}
END_TEST

Suite *toml_suite(void) {
    Suite *s = suite_create("TOML_Parallax");
    TCase *tc = tcase_create("Core");
    tcase_add_test(tc, test_parse_parallax_globals);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int failed;
    Suite *s = toml_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_FORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
