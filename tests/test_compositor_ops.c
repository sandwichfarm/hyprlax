// Smoke test to ensure compositor/platform ops include get_event_fd hooks
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../src/include/compositor.h"
#include "../src/include/platform.h"

#ifdef ENABLE_HYPRLAND
extern const compositor_ops_t compositor_hyprland_ops;
#endif
#ifdef ENABLE_SWAY
extern const compositor_ops_t compositor_sway_ops;
#endif
#ifdef ENABLE_WAYFIRE
extern const compositor_ops_t compositor_wayfire_ops;
#endif
#ifdef ENABLE_NIRI
extern const compositor_ops_t compositor_niri_ops;
#endif
#ifdef ENABLE_RIVER
extern const compositor_ops_t compositor_river_ops;
#endif
#ifdef ENABLE_GENERIC_WAYLAND
extern const compositor_ops_t compositor_generic_wayland_ops;
#endif


START_TEST(test_compositor_get_event_fd_present)
{
#ifdef ENABLE_HYPRLAND
    ck_assert_ptr_nonnull(compositor_hyprland_ops.get_event_fd);
#endif
#ifdef ENABLE_SWAY
    ck_assert_ptr_nonnull(compositor_sway_ops.get_event_fd);
#endif
#ifdef ENABLE_WAYFIRE
    ck_assert_ptr_nonnull(compositor_wayfire_ops.get_event_fd);
#endif
#ifdef ENABLE_NIRI
    ck_assert_ptr_nonnull(compositor_niri_ops.get_event_fd);
#endif
#ifdef ENABLE_RIVER
    ck_assert_ptr_nonnull(compositor_river_ops.get_event_fd);
#endif
#ifdef ENABLE_GENERIC_WAYLAND
    ck_assert_ptr_nonnull(compositor_generic_wayland_ops.get_event_fd);
#endif
}
END_TEST


Suite *ops_suite(void)
{
    Suite *s = suite_create("CompositorOps");
    TCase *tc = tcase_create("FDHooks");
    tcase_add_test(tc, test_compositor_get_event_fd_present);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s = ops_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
