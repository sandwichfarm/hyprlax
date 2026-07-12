/*
 * test_event_loop.c - Comprehensive test suite for event_loop
 *
 * Tests the event loop system including:
 * - Timer creation, arming, and disarming
 * - Epoll fd management (add/remove)
 * - Frame timing and FPS control
 * - Debounce timer behavior
 * - Timer fd clearing
 */

#include <check.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <errno.h>

#include "include/hyprlax.h"

/* Forward declarations of functions under test */
int create_timerfd_monotonic(void);
void disarm_timerfd(int fd);
void arm_timerfd_ms(int fd, int initial_ms, int interval_ms);
int epoll_add_fd(int epfd, int fd, uint32_t events);
int epoll_del_fd(int epfd, int fd);
void hyprlax_clear_timerfd(int fd);

static int mock_compositor_event_fd = -1;

static int mock_get_compositor_event_fd(void) {
    return mock_compositor_event_fd;
}

/* Helper to check if a timerfd is armed */
static bool is_timerfd_armed(int fd) {
    if (fd < 0) return false;

    struct itimerspec its;
    if (timerfd_gettime(fd, &its) < 0) {
        return false;
    }

    /* Timer is armed if either value or interval is non-zero */
    return (its.it_value.tv_sec != 0 || its.it_value.tv_nsec != 0 ||
            its.it_interval.tv_sec != 0 || its.it_interval.tv_nsec != 0);
}

/* Helper to get timer interval in milliseconds */
static int get_timerfd_interval_ms(int fd) {
    if (fd < 0) return -1;

    struct itimerspec its;
    if (timerfd_gettime(fd, &its) < 0) {
        return -1;
    }

    return (int)(its.it_interval.tv_sec * 1000 + its.it_interval.tv_nsec / 1000000);
}

/* Helper to wait for timer expiration with timeout */
static bool wait_for_timer(int fd, int timeout_ms) {
    if (fd < 0) return false;

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    int result = poll(&pfd, 1, timeout_ms);
    return result > 0 && (pfd.revents & POLLIN);
}

/*****************************************************************************
 * Timer Creation Tests
 *****************************************************************************/

START_TEST(test_create_timerfd_monotonic)
{
    int fd = create_timerfd_monotonic();
    ck_assert_int_ge(fd, 0);

    /* Verify it's a valid timerfd */
    struct itimerspec its;
    int result = timerfd_gettime(fd, &its);
    ck_assert_int_eq(result, 0);

    /* Should be disarmed initially */
    ck_assert(!is_timerfd_armed(fd));

    close(fd);
}
END_TEST

START_TEST(test_create_multiple_timerfds)
{
    int fd1 = create_timerfd_monotonic();
    int fd2 = create_timerfd_monotonic();
    int fd3 = create_timerfd_monotonic();

    ck_assert_int_ge(fd1, 0);
    ck_assert_int_ge(fd2, 0);
    ck_assert_int_ge(fd3, 0);

    /* All should be different */
    ck_assert_int_ne(fd1, fd2);
    ck_assert_int_ne(fd2, fd3);
    ck_assert_int_ne(fd1, fd3);

    close(fd1);
    close(fd2);
    close(fd3);
}
END_TEST

/*****************************************************************************
 * Timer Arming/Disarming Tests
 *****************************************************************************/

START_TEST(test_arm_timerfd_ms)
{
    int fd = create_timerfd_monotonic();

    /* Arm with 100ms initial, 100ms interval */
    arm_timerfd_ms(fd, 100, 100);

    ck_assert(is_timerfd_armed(fd));

    int interval = get_timerfd_interval_ms(fd);
    ck_assert_int_ge(interval, 99);   /* Allow small rounding */
    ck_assert_int_le(interval, 101);

    close(fd);
}
END_TEST

START_TEST(test_arm_timerfd_oneshot)
{
    int fd = create_timerfd_monotonic();

    /* Arm with 50ms initial, 0 interval (one-shot) */
    arm_timerfd_ms(fd, 50, 0);

    ck_assert(is_timerfd_armed(fd));

    int interval = get_timerfd_interval_ms(fd);
    ck_assert_int_eq(interval, 0);

    close(fd);
}
END_TEST

START_TEST(test_arm_timerfd_various_intervals)
{
    int fd = create_timerfd_monotonic();

    int test_intervals[] = {1, 10, 16, 60, 100, 1000};

    for (size_t i = 0; i < sizeof(test_intervals) / sizeof(test_intervals[0]); i++) {
        arm_timerfd_ms(fd, test_intervals[i], test_intervals[i]);

        int actual = get_timerfd_interval_ms(fd);
        ck_assert_int_ge(actual, test_intervals[i] - 1);
        ck_assert_int_le(actual, test_intervals[i] + 1);
    }

    close(fd);
}
END_TEST

START_TEST(test_disarm_timerfd)
{
    int fd = create_timerfd_monotonic();

    /* Arm it first */
    arm_timerfd_ms(fd, 100, 100);
    ck_assert(is_timerfd_armed(fd));

    /* Disarm it */
    disarm_timerfd(fd);
    ck_assert(!is_timerfd_armed(fd));

    close(fd);
}
END_TEST

START_TEST(test_disarm_timerfd_invalid_fd)
{
    /* Should not crash with invalid fd */
    disarm_timerfd(-1);
    disarm_timerfd(9999);
}
END_TEST

START_TEST(test_arm_timerfd_invalid_fd)
{
    /* Should not crash with invalid fd */
    arm_timerfd_ms(-1, 100, 100);
    arm_timerfd_ms(9999, 100, 100);
}
END_TEST

START_TEST(test_rearm_timerfd)
{
    int fd = create_timerfd_monotonic();

    /* Arm with 100ms */
    arm_timerfd_ms(fd, 100, 100);
    int interval1 = get_timerfd_interval_ms(fd);
    ck_assert_int_ge(interval1, 99);
    ck_assert_int_le(interval1, 101);

    /* Rearm with different interval */
    arm_timerfd_ms(fd, 50, 50);
    int interval2 = get_timerfd_interval_ms(fd);
    ck_assert_int_ge(interval2, 49);
    ck_assert_int_le(interval2, 51);

    close(fd);
}
END_TEST

/*****************************************************************************
 * Timer Expiration Tests
 *****************************************************************************/

START_TEST(test_timerfd_expiration)
{
    int fd = create_timerfd_monotonic();

    /* Arm with very short timeout */
    arm_timerfd_ms(fd, 10, 0);

    /* Wait for expiration (with generous timeout) */
    bool expired = wait_for_timer(fd, 100);
    ck_assert(expired);

    /* Read the expiration count */
    uint64_t expirations = 0;
    ssize_t bytes = read(fd, &expirations, sizeof(expirations));
    ck_assert_int_eq(bytes, sizeof(expirations));
    ck_assert_int_ge(expirations, 1);

    close(fd);
}
END_TEST

START_TEST(test_timerfd_clear)
{
    int fd = create_timerfd_monotonic();

    /* Arm and wait for expiration */
    arm_timerfd_ms(fd, 10, 0);
    bool expired = wait_for_timer(fd, 100);
    ck_assert(expired);

    /* Clear the timer */
    hyprlax_clear_timerfd(fd);

    /* Timer should no longer be readable without blocking */
    uint64_t expirations = 0;
    ssize_t bytes = read(fd, &expirations, sizeof(expirations));
    ck_assert_int_eq(bytes, -1);
    ck_assert_int_eq(errno, EAGAIN);

    close(fd);
}
END_TEST

START_TEST(test_timerfd_clear_invalid_fd)
{
    /* Should not crash with invalid fd */
    hyprlax_clear_timerfd(-1);
    hyprlax_clear_timerfd(9999);
}
END_TEST

/*****************************************************************************
 * Epoll Management Tests
 *****************************************************************************/

START_TEST(test_epoll_add_fd)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ck_assert_int_ge(epfd, 0);

    int fd = create_timerfd_monotonic();
    ck_assert_int_ge(fd, 0);

    int result = epoll_add_fd(epfd, fd, EPOLLIN);
    ck_assert_int_eq(result, 0);

    /* Verify it's actually added by trying to add again (should fail) */
    int result2 = epoll_add_fd(epfd, fd, EPOLLIN);
    ck_assert_int_lt(result2, 0);
    ck_assert_int_eq(errno, EEXIST);

    close(fd);
    close(epfd);
}
END_TEST

START_TEST(test_epoll_add_multiple_fds)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int fd1 = create_timerfd_monotonic();
    int fd2 = create_timerfd_monotonic();
    int fd3 = create_timerfd_monotonic();

    ck_assert_int_eq(epoll_add_fd(epfd, fd1, EPOLLIN), 0);
    ck_assert_int_eq(epoll_add_fd(epfd, fd2, EPOLLIN), 0);
    ck_assert_int_eq(epoll_add_fd(epfd, fd3, EPOLLIN), 0);

    close(fd1);
    close(fd2);
    close(fd3);
    close(epfd);
}
END_TEST

START_TEST(test_epoll_add_fd_invalid_epfd)
{
    int fd = create_timerfd_monotonic();

    int result = epoll_add_fd(-1, fd, EPOLLIN);
    ck_assert_int_lt(result, 0);

    close(fd);
}
END_TEST

START_TEST(test_epoll_add_fd_invalid_fd)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);

    int result = epoll_add_fd(epfd, -1, EPOLLIN);
    ck_assert_int_lt(result, 0);

    close(epfd);
}
END_TEST

START_TEST(test_epoll_del_fd)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int fd = create_timerfd_monotonic();

    /* Add then delete */
    epoll_add_fd(epfd, fd, EPOLLIN);
    int result = epoll_del_fd(epfd, fd);
    ck_assert_int_eq(result, 0);

    /* Should be able to add again after deletion */
    int result2 = epoll_add_fd(epfd, fd, EPOLLIN);
    ck_assert_int_eq(result2, 0);

    close(fd);
    close(epfd);
}
END_TEST

START_TEST(test_epoll_del_fd_invalid)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);

    /* Deleting non-existent fd should fail */
    int result = epoll_del_fd(epfd, 9999);
    ck_assert_int_lt(result, 0);

    /* Invalid epfd */
    result = epoll_del_fd(-1, 0);
    ck_assert_int_lt(result, 0);

    close(epfd);
}
END_TEST

/*****************************************************************************
 * Epoll + Timer Integration Tests
 *****************************************************************************/

START_TEST(test_epoll_wait_for_timer)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int fd = create_timerfd_monotonic();

    epoll_add_fd(epfd, fd, EPOLLIN);
    arm_timerfd_ms(fd, 10, 0);

    /* Wait for timer expiration via epoll */
    struct epoll_event events[1];
    int n = epoll_wait(epfd, events, 1, 100);

    ck_assert_int_eq(n, 1);
    ck_assert_int_eq(events[0].data.fd, fd);
    ck_assert(events[0].events & EPOLLIN);

    close(fd);
    close(epfd);
}
END_TEST

START_TEST(test_epoll_multiple_timer_expiration)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int fd1 = create_timerfd_monotonic();
    int fd2 = create_timerfd_monotonic();

    epoll_add_fd(epfd, fd1, EPOLLIN);
    epoll_add_fd(epfd, fd2, EPOLLIN);

    /* Arm both timers with short timeouts */
    arm_timerfd_ms(fd1, 10, 0);
    arm_timerfd_ms(fd2, 20, 0);

    /* Wait for both to expire */
    struct epoll_event events[2];
    int total_events = 0;

    /* First event (fd1 should fire first) */
    int n = epoll_wait(epfd, events, 2, 50);
    ck_assert_int_ge(n, 1);
    total_events += n;

    /* Second event if not already received */
    if (n == 1) {
        n = epoll_wait(epfd, events + 1, 1, 50);
        ck_assert_int_ge(n, 0);
        total_events += n;
    }

    ck_assert_int_ge(total_events, 1);

    close(fd1);
    close(fd2);
    close(epfd);
}
END_TEST

START_TEST(test_refresh_compositor_event_fd_replaces_epoll_watch)
{
    int old_pair[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK,
                                0, old_pair), 0);

    const compositor_ops_t ops = {
        .get_event_fd = mock_get_compositor_event_fd,
    };
    compositor_adapter_t compositor = {
        .ops = &ops,
    };
    hyprlax_context_t ctx = {
        .compositor = &compositor,
        .epoll_fd = epoll_create1(EPOLL_CLOEXEC),
        .compositor_event_fd = old_pair[0],
    };
    ck_assert_int_ge(ctx.epoll_fd, 0);
    ck_assert_int_eq(epoll_add_fd(ctx.epoll_fd, old_pair[0], EPOLLIN), 0);

    close(old_pair[0]);
    mock_compositor_event_fd = -1;
    ck_assert(hyprlax_refresh_compositor_event_fd(&ctx));
    ck_assert_int_eq(ctx.compositor_event_fd, -1);

    int new_pair[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK,
                                0, new_pair), 0);
    mock_compositor_event_fd = new_pair[0];
    ck_assert(hyprlax_refresh_compositor_event_fd(&ctx));
    ck_assert_int_eq(ctx.compositor_event_fd, new_pair[0]);

    ck_assert_int_eq(write(new_pair[1], "n", 1), 1);

    struct epoll_event event;
    int n = epoll_wait(ctx.epoll_fd, &event, 1, 50);
    ck_assert_int_eq(n, 1);
    ck_assert_int_eq(event.data.fd, new_pair[0]);

    mock_compositor_event_fd = -1;
    close(old_pair[1]);
    close(new_pair[0]);
    close(new_pair[1]);
    close(ctx.epoll_fd);
}
END_TEST

/*****************************************************************************
 * FPS Calculation Tests
 *****************************************************************************/

START_TEST(test_fps_to_interval_conversion)
{
    /* Test FPS -> millisecond interval conversion */
    struct {
        int fps;
        int expected_ms_min;
        int expected_ms_max;
    } test_cases[] = {
        {60, 16, 17},    /* 1000/60 = 16.67ms */
        {30, 33, 34},    /* 1000/30 = 33.33ms */
        {144, 6, 7},     /* 1000/144 = 6.94ms */
        {120, 8, 9},     /* 1000/120 = 8.33ms */
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        int interval_ms = (int)(1000.0 / (double)test_cases[i].fps);
        ck_assert_int_ge(interval_ms, test_cases[i].expected_ms_min);
        ck_assert_int_le(interval_ms, test_cases[i].expected_ms_max);
    }
}
END_TEST

/*****************************************************************************
 * Edge Case Tests
 *****************************************************************************/

START_TEST(test_timer_sub_millisecond)
{
    int fd = create_timerfd_monotonic();

    /* Arm with 1ms interval (minimum reasonable) */
    arm_timerfd_ms(fd, 1, 1);

    ck_assert(is_timerfd_armed(fd));

    /* Should still work but may have platform-dependent precision */
    int interval = get_timerfd_interval_ms(fd);
    ck_assert_int_ge(interval, 0);
    ck_assert_int_le(interval, 2);

    close(fd);
}
END_TEST

START_TEST(test_timer_large_interval)
{
    int fd = create_timerfd_monotonic();

    /* Arm with 10 second interval */
    arm_timerfd_ms(fd, 10000, 10000);

    ck_assert(is_timerfd_armed(fd));

    int interval = get_timerfd_interval_ms(fd);
    ck_assert_int_ge(interval, 9999);
    ck_assert_int_le(interval, 10001);

    close(fd);
}
END_TEST

START_TEST(test_timer_zero_interval)
{
    int fd = create_timerfd_monotonic();

    /* Zero initial should result in no timeout */
    arm_timerfd_ms(fd, 0, 0);

    /* Timer should not be armed */
    ck_assert(!is_timerfd_armed(fd));

    close(fd);
}
END_TEST

/*****************************************************************************
 * Test Suite Definition
 *****************************************************************************/

Suite *event_loop_suite(void)
{
    Suite *s;
    TCase *tc_timer_create, *tc_timer_arm, *tc_timer_exp;
    TCase *tc_epoll, *tc_integration, *tc_edge;

    s = suite_create("EventLoop");

    /* Timer Creation Tests */
    tc_timer_create = tcase_create("TimerCreation");
    tcase_add_test(tc_timer_create, test_create_timerfd_monotonic);
    tcase_add_test(tc_timer_create, test_create_multiple_timerfds);
    suite_add_tcase(s, tc_timer_create);

    /* Timer Arming/Disarming Tests */
    tc_timer_arm = tcase_create("TimerArming");
    tcase_add_test(tc_timer_arm, test_arm_timerfd_ms);
    tcase_add_test(tc_timer_arm, test_arm_timerfd_oneshot);
    tcase_add_test(tc_timer_arm, test_arm_timerfd_various_intervals);
    tcase_add_test(tc_timer_arm, test_disarm_timerfd);
    tcase_add_test(tc_timer_arm, test_disarm_timerfd_invalid_fd);
    tcase_add_test(tc_timer_arm, test_arm_timerfd_invalid_fd);
    tcase_add_test(tc_timer_arm, test_rearm_timerfd);
    suite_add_tcase(s, tc_timer_arm);

    /* Timer Expiration Tests */
    tc_timer_exp = tcase_create("TimerExpiration");
    tcase_add_test(tc_timer_exp, test_timerfd_expiration);
    tcase_add_test(tc_timer_exp, test_timerfd_clear);
    tcase_add_test(tc_timer_exp, test_timerfd_clear_invalid_fd);
    suite_add_tcase(s, tc_timer_exp);

    /* Epoll Tests */
    tc_epoll = tcase_create("Epoll");
    tcase_add_test(tc_epoll, test_epoll_add_fd);
    tcase_add_test(tc_epoll, test_epoll_add_multiple_fds);
    tcase_add_test(tc_epoll, test_epoll_add_fd_invalid_epfd);
    tcase_add_test(tc_epoll, test_epoll_add_fd_invalid_fd);
    tcase_add_test(tc_epoll, test_epoll_del_fd);
    tcase_add_test(tc_epoll, test_epoll_del_fd_invalid);
    suite_add_tcase(s, tc_epoll);

    /* Integration Tests */
    tc_integration = tcase_create("Integration");
    tcase_add_test(tc_integration, test_epoll_wait_for_timer);
    tcase_add_test(tc_integration, test_epoll_multiple_timer_expiration);
    tcase_add_test(tc_integration, test_refresh_compositor_event_fd_replaces_epoll_watch);
    tcase_add_test(tc_integration, test_fps_to_interval_conversion);
    suite_add_tcase(s, tc_integration);

    /* Edge Case Tests */
    tc_edge = tcase_create("EdgeCases");
    tcase_add_test(tc_edge, test_timer_sub_millisecond);
    tcase_add_test(tc_edge, test_timer_large_interval);
    tcase_add_test(tc_edge, test_timer_zero_interval);
    suite_add_tcase(s, tc_edge);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = event_loop_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
