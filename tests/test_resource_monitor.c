/*
 * test_resource_monitor.c - Unit tests for resource monitoring
 */

#include <check.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../src/include/resource_monitor.h"
#include "../src/include/log.h"

/* Test: Create and destroy resource monitor */
START_TEST(test_resource_monitor_create_destroy)
{
    resource_monitor_t *monitor = resource_monitor_create(10.0);
    ck_assert_ptr_nonnull(monitor);
    ck_assert(monitor->check_interval == 10.0);
    ck_assert(monitor->enabled == true);
    ck_assert_int_gt(monitor->fd_count_start, 0);
    ck_assert_uint_gt(monitor->memory_rss_start_kb, 0);

    resource_monitor_destroy(monitor);
}
END_TEST

/* Test: Verify FD counting works */
START_TEST(test_resource_monitor_fd_count)
{
    int initial_fds = resource_monitor_get_fd_count();
    ck_assert_int_gt(initial_fds, 0);

    /* Open a new file descriptor */
    int fd = open("/dev/null", O_RDONLY);
    ck_assert_int_ge(fd, 0);

    int new_fds = resource_monitor_get_fd_count();
    ck_assert_int_eq(new_fds, initial_fds + 1);

    close(fd);

    int final_fds = resource_monitor_get_fd_count();
    ck_assert_int_eq(final_fds, initial_fds);
}
END_TEST

/* Test: Verify memory measurements work */
START_TEST(test_resource_monitor_memory)
{
    size_t rss_kb = resource_monitor_get_memory_rss_kb();
    ck_assert_uint_gt(rss_kb, 0);
    ck_assert_uint_lt(rss_kb, 10000000);  /* Sanity check: less than 10GB */

    size_t vms_kb = resource_monitor_get_memory_vms_kb();
    ck_assert_uint_gt(vms_kb, 0);
    ck_assert_uint_lt(vms_kb, 10000000);  /* Sanity check: less than 10GB */
}
END_TEST

/* Test: Check periodic monitoring */
START_TEST(test_resource_monitor_periodic_check)
{
    resource_monitor_t *monitor = resource_monitor_create(1.0);
    ck_assert_ptr_nonnull(monitor);

    /* First check should be triggered (time > last_check_time + interval) */
    ck_assert(resource_monitor_should_check(monitor, 1.0) == true);
    ck_assert_uint_eq(monitor->check_count, 0);

    /* Perform the check */
    resource_monitor_check(monitor);
    ck_assert_uint_eq(monitor->check_count, 1);

    /* Should not trigger check immediately after (time = 1.5, last = 1.0, interval = 1.0) */
    ck_assert(resource_monitor_should_check(monitor, 1.5) == false);

    /* Should trigger after interval (time = 2.5, last = 1.0, interval = 1.0) */
    ck_assert(resource_monitor_should_check(monitor, 2.5) == true);

    resource_monitor_destroy(monitor);
}
END_TEST

/* Test: Verify growth tracking */
START_TEST(test_resource_monitor_growth_tracking)
{
    resource_monitor_t *monitor = resource_monitor_create(10.0);
    ck_assert_ptr_nonnull(monitor);

    int baseline_fds = monitor->fd_count_start;

    /* Open some file descriptors to simulate growth */
    int fd1 = open("/dev/null", O_RDONLY);
    int fd2 = open("/dev/null", O_RDONLY);
    int fd3 = open("/dev/null", O_RDONLY);

    /* Perform a check */
    resource_monitor_check(monitor);

    /* Verify growth was detected */
    ck_assert_int_eq(monitor->fd_count_current, baseline_fds + 3);
    ck_assert_int_eq(monitor->fd_count_max, baseline_fds + 3);

    /* Close some FDs */
    close(fd1);
    close(fd2);

    /* Check again */
    resource_monitor_check(monitor);
    ck_assert_int_eq(monitor->fd_count_current, baseline_fds + 1);
    ck_assert_int_eq(monitor->fd_count_max, baseline_fds + 3); /* Max should not decrease */

    close(fd3);
    resource_monitor_destroy(monitor);
}
END_TEST

/* Test: Environment variable configuration */
START_TEST(test_resource_monitor_env_config)
{
    /* Set environment variable to disable monitor */
    setenv("HYPRLAX_RESOURCE_MONITOR_DISABLE", "1", 1);

    resource_monitor_t *monitor = resource_monitor_create(10.0);
    ck_assert_ptr_nonnull(monitor);
    ck_assert(monitor->enabled == false);

    resource_monitor_destroy(monitor);
    unsetenv("HYPRLAX_RESOURCE_MONITOR_DISABLE");

    /* Test custom interval */
    setenv("HYPRLAX_RESOURCE_MONITOR_INTERVAL", "30.0", 1);

    monitor = resource_monitor_create(10.0);
    ck_assert_ptr_nonnull(monitor);
    ck_assert(monitor->check_interval == 30.0);

    resource_monitor_destroy(monitor);
    unsetenv("HYPRLAX_RESOURCE_MONITOR_INTERVAL");
}
END_TEST

/* Test: Disabled monitor doesn't perform checks */
START_TEST(test_resource_monitor_disabled)
{
    setenv("HYPRLAX_RESOURCE_MONITOR_DISABLE", "1", 1);

    resource_monitor_t *monitor = resource_monitor_create(10.0);
    ck_assert_ptr_nonnull(monitor);
    ck_assert(monitor->enabled == false);

    /* Should not trigger checks when disabled */
    ck_assert(resource_monitor_should_check(monitor, 0.0) == false);
    ck_assert(resource_monitor_should_check(monitor, 100.0) == false);

    /* Check should be no-op */
    resource_monitor_check(monitor);
    ck_assert_uint_eq(monitor->check_count, 0);

    resource_monitor_destroy(monitor);
    unsetenv("HYPRLAX_RESOURCE_MONITOR_DISABLE");
}
END_TEST

Suite *resource_monitor_suite(void)
{
    Suite *s = suite_create("ResourceMonitor");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_resource_monitor_create_destroy);
    tcase_add_test(tc_core, test_resource_monitor_fd_count);
    tcase_add_test(tc_core, test_resource_monitor_memory);
    tcase_add_test(tc_core, test_resource_monitor_periodic_check);
    tcase_add_test(tc_core, test_resource_monitor_growth_tracking);
    tcase_add_test(tc_core, test_resource_monitor_env_config);
    tcase_add_test(tc_core, test_resource_monitor_disabled);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    /* Initialize logging (suppress output during tests) */
    log_init(false, NULL);

    int number_failed;
    Suite *s = resource_monitor_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    log_cleanup();

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
