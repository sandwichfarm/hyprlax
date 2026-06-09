/*
 * test_monitor_lifecycle.c - Regression tests for monitor removal cleanup
 */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>

#include "../src/core/monitor.h"
#include "../src/include/log.h"

void gles2_destroy_monitor_surface(EGLSurface surface) {
    (void)surface;
}

static struct wl_output *fake_output(uintptr_t id) {
    return (struct wl_output *)id;
}

START_TEST(test_remove_by_output_destroys_secondary_monitor)
{
    monitor_list_t *list = monitor_list_create();
    ck_assert_ptr_nonnull(list);

    monitor_instance_t *primary = monitor_instance_create("eDP-1");
    monitor_instance_t *secondary = monitor_instance_create("DP-1");
    ck_assert_ptr_nonnull(primary);
    ck_assert_ptr_nonnull(secondary);

    primary->wl_output = fake_output(0x1000);
    secondary->wl_output = fake_output(0x2000);

    monitor_list_add(list, primary);
    monitor_list_add(list, secondary);
    ck_assert_int_eq(list->count, 2);
    ck_assert_ptr_eq(list->primary, primary);

    ck_assert(monitor_list_remove_by_output(list, secondary->wl_output));

    ck_assert_int_eq(list->count, 1);
    ck_assert_ptr_eq(list->head, primary);
    ck_assert_ptr_eq(list->primary, primary);
    ck_assert_ptr_null(primary->next);
    ck_assert_ptr_null(monitor_list_find_by_output(list, fake_output(0x2000)));

    monitor_list_destroy(list);
}
END_TEST

START_TEST(test_remove_by_output_promotes_primary)
{
    monitor_list_t *list = monitor_list_create();
    ck_assert_ptr_nonnull(list);

    monitor_instance_t *primary = monitor_instance_create("eDP-1");
    monitor_instance_t *secondary = monitor_instance_create("DP-1");
    ck_assert_ptr_nonnull(primary);
    ck_assert_ptr_nonnull(secondary);

    primary->wl_output = fake_output(0x3000);
    secondary->wl_output = fake_output(0x4000);

    monitor_list_add(list, primary);
    monitor_list_add(list, secondary);

    ck_assert(monitor_list_remove_by_output(list, primary->wl_output));

    ck_assert_int_eq(list->count, 1);
    ck_assert_ptr_eq(list->head, secondary);
    ck_assert_ptr_eq(list->primary, secondary);
    ck_assert(secondary->is_primary);

    monitor_list_destroy(list);
}
END_TEST

START_TEST(test_remove_by_output_missing_is_noop)
{
    monitor_list_t *list = monitor_list_create();
    ck_assert_ptr_nonnull(list);

    monitor_instance_t *monitor = monitor_instance_create("eDP-1");
    ck_assert_ptr_nonnull(monitor);
    monitor->wl_output = fake_output(0x5000);
    monitor_list_add(list, monitor);

    ck_assert(!monitor_list_remove_by_output(list, fake_output(0x6000)));

    ck_assert_int_eq(list->count, 1);
    ck_assert_ptr_eq(list->head, monitor);
    ck_assert_ptr_eq(list->primary, monitor);

    monitor_list_destroy(list);
}
END_TEST

Suite *monitor_lifecycle_suite(void)
{
    Suite *s = suite_create("MonitorLifecycle");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_remove_by_output_destroys_secondary_monitor);
    tcase_add_test(tc_core, test_remove_by_output_promotes_primary);
    tcase_add_test(tc_core, test_remove_by_output_missing_is_noop);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    log_init(false, NULL);

    Suite *s = monitor_lifecycle_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    log_cleanup();
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
