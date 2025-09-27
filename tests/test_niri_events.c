// Niri event parsing tests. Validates that window cache + focus events
// produce workspace change events with correct 2D coordinates.

#include <check.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "include/compositor.h"

/* Test hooks provided by niri.c when compiled with -DUNIT_TEST */
void niri_test_setup_stream(int read_fd);
void niri_test_reset(void);
void niri_test_set_current_column(int column);

static int pipe_fds[2] = { -1, -1 };

static void setup(void) {
    if (pipe(pipe_fds) != 0) {
        ck_abort_msg("failed to create pipe for event injection");
    }
    niri_test_setup_stream(pipe_fds[0]);
}

static void teardown(void) {
    if (pipe_fds[0] >= 0) close(pipe_fds[0]);
    if (pipe_fds[1] >= 0) close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;
    niri_test_reset();
}

START_TEST(test_windows_changed_then_focus_emits_workspace_change)
{
    /* Write both events before polling to avoid any race/timing issues */
    const char *payload =
        "{\"WindowsChanged\":{\"windows\":[{\"id\":5,\"pos_in_scrolling_layout\":[2,1]}]}}\n"
        "{\"WindowFocusChanged\":{\"id\":5}}\n";
    ssize_t nw = write(pipe_fds[1], payload, strlen(payload));
    ck_assert_int_eq(nw, (ssize_t)strlen(payload));

    /* First poll consumes WindowsChanged and returns no data */
    compositor_event_t ev; extern const compositor_ops_t compositor_niri_ops;
    int rc = compositor_niri_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_ERROR_NO_DATA);

    /* Give the stream a moment to present readiness */
    usleep(1000);
    /* Second poll should yield a workspace change */
    rc = compositor_niri_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.type, COMPOSITOR_EVENT_WORKSPACE_CHANGE);
    ck_assert_int_eq(ev.data.workspace.to_x, 2);
    ck_assert_int_eq(ev.data.workspace.to_y, 1);
    /* from_* are unknown here and left as -1 */
    ck_assert_int_eq(ev.data.workspace.from_x, -1);
    ck_assert_int_eq(ev.data.workspace.from_y, -1);
}
END_TEST

/* WorkspaceActivated should use test-supplied column when present */
START_TEST(test_workspace_activated_uses_test_column)
{
    niri_test_set_current_column(2);
    const char *wa = "{\"WorkspaceActivated\":{\"id\":3}}\n";
    ssize_t nw = write(pipe_fds[1], wa, strlen(wa));
    ck_assert_int_eq(nw, (ssize_t)strlen(wa));

    compositor_event_t ev; extern const compositor_ops_t compositor_niri_ops;
    int rc = HYPRLAX_ERROR_NO_DATA;
    for (int i = 0; i < 10 && rc == HYPRLAX_ERROR_NO_DATA; i++) {
        rc = compositor_niri_ops.poll_events(&ev);
        if (rc == HYPRLAX_ERROR_NO_DATA) usleep(1000);
    }
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.type, COMPOSITOR_EVENT_WORKSPACE_CHANGE);
    ck_assert_int_eq(ev.data.workspace.to_x, 2);
    ck_assert_int_eq(ev.data.workspace.to_y, 3);
}
END_TEST

Suite *niri_suite(void)
{
    Suite *s = suite_create("NiriEvents");
    TCase *tc = tcase_create("Parsing");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_windows_changed_then_focus_emits_workspace_change);
    tcase_add_test(tc, test_workspace_activated_uses_test_column);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = niri_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_FORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
