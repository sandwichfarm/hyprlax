// Hyprland event parsing tests to prevent regressions where
// monitor focus (focusedmon) incorrectly triggers workspace animations.

#include <check.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

#include "include/compositor.h"
#include "include/hyprlax_internal.h"

/* Test hooks provided by hyprland.c when compiled with -DUNIT_TEST */
void hyprland_test_setup_fd(int event_fd, const char *monitor_name, int initial_workspace);
void hyprland_test_set_reconnect_fd(int event_fd, int active_workspace);
void hyprland_test_reset(void);

static int pipe_fds[2] = { -1, -1 };

static void setup(void) {
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pipe_fds) != 0) {
        ck_abort_msg("failed to create socket pair for event injection");
    }
    /* Inject fake FD and initial state (workspace 1) */
    hyprland_test_setup_fd(pipe_fds[0], NULL, 1);
}

static void teardown(void) {
    if (pipe_fds[0] >= 0) close(pipe_fds[0]);
    if (pipe_fds[1] >= 0) close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;
    hyprland_test_reset();
}

START_TEST(test_focusedmon_does_not_emit_workspace_change)
{
    const char *line = "focusedmon>>DP-1,3\n";
    ssize_t nw = write(pipe_fds[1], line, strlen(line));
    ck_assert_int_eq(nw, (ssize_t)strlen(line));

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_ERROR_NO_DATA); /* No workspace change on focusedmon */
}
END_TEST

START_TEST(test_workspace_after_focusedmon_has_correct_from_and_monitor)
{
    /* Set monitor name via focusedmon; do not change current workspace */
    const char *lines = "focusedmon>>DP-3,3\nworkspace>>4\n";
    ssize_t nw = write(pipe_fds[1], lines, strlen(lines));
    ck_assert_int_eq(nw, (ssize_t)strlen(lines));

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.type, COMPOSITOR_EVENT_WORKSPACE_CHANGE);
    /* Initial workspace was 1; ensure from=1, to=4 */
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 4);
    /* Monitor name should be populated from focusedmon */
    ck_assert_str_eq(ev.data.workspace.monitor_name, "DP-3");
}
END_TEST

START_TEST(test_workspace_same_id_no_event)
{
    hyprland_test_reset();
    /* Recreate pipe and setup to workspace 3 */
    ck_assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pipe_fds) == 0);
    hyprland_test_setup_fd(pipe_fds[0], NULL, 3);
    const char *line = "workspace>>3\n";
    ck_assert_int_eq((ssize_t)strlen(line), write(pipe_fds[1], line, strlen(line)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_ERROR_NO_DATA);
}
END_TEST

START_TEST(test_multiple_focusedmon_no_event_last_wins)
{
    const char *lines = "focusedmon>>DP-1,2\nfocusedmon>>HDMI-A-1,7\n";
    ck_assert_int_eq((ssize_t)strlen(lines), write(pipe_fds[1], lines, strlen(lines)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_ERROR_NO_DATA);
    /* Now send a workspace event and ensure monitor is HDMI-A-1 */
    const char *ws = "workspace>>8\n";
    ck_assert_int_eq((ssize_t)strlen(ws), write(pipe_fds[1], ws, strlen(ws)));
    rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_str_eq(ev.data.workspace.monitor_name, "HDMI-A-1");
}
END_TEST

START_TEST(test_overlong_monitor_name_ignored)
{
    char longname[200];
    memset(longname, 'A', sizeof(longname));
    longname[sizeof(longname)-1] = '\0';
    char payload[256];
    snprintf(payload, sizeof(payload), "focusedmon>>%s,5\nworkspace>>6\n", longname);
    ck_assert_int_eq((ssize_t)strlen(payload), write(pipe_fds[1], payload, strlen(payload)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    /* Overlong name is ignored (not copied) by adapter */
    ck_assert(ev.data.workspace.monitor_name[0] == '\0');
}
END_TEST

START_TEST(test_workspace_with_no_monitor_name)
{
    const char *ws = "workspace>>9\n";
    ck_assert_int_eq((ssize_t)strlen(ws), write(pipe_fds[1], ws, strlen(ws)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert(ev.data.workspace.monitor_name[0] == '\0');
    ck_assert_int_eq(ev.data.workspace.from_x, 0);
    ck_assert_int_eq(ev.data.workspace.from_y, 0);
    ck_assert_int_eq(ev.data.workspace.to_x, 0);
    ck_assert_int_eq(ev.data.workspace.to_y, 0);
}
END_TEST

START_TEST(test_chained_workspace_events_update_from)
{
    const char *ws3 = "workspace>>3\n";
    ck_assert_int_eq((ssize_t)strlen(ws3), write(pipe_fds[1], ws3, strlen(ws3)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 3);
    /* second change to 5 */
    const char *ws5 = "workspace>>5\n";
    ck_assert_int_eq((ssize_t)strlen(ws5), write(pipe_fds[1], ws5, strlen(ws5)));
    rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 3);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 5);
}
END_TEST

START_TEST(test_monitor_name_copied_when_within_limit)
{
    const char *name = "DP-9-SMALL"; /* safely within 63 chars */
    char payload[128];
    snprintf(payload, sizeof(payload), "focusedmon>>%s,2\nworkspace>>7\n", name);
    ck_assert_int_eq((ssize_t)strlen(payload), write(pipe_fds[1], payload, strlen(payload)));
    compositor_event_t ev; extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);
    ck_assert_int_eq(rc, HYPRLAX_SUCCESS);
    ck_assert_str_eq(ev.data.workspace.monitor_name, name);
}
END_TEST

START_TEST(test_closed_event_stream_is_invalidated)
{
    close(pipe_fds[1]);
    pipe_fds[1] = -1;

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    int rc = compositor_hyprland_ops.poll_events(&ev);

    ck_assert_int_eq(rc, HYPRLAX_ERROR_NO_DATA);
    ck_assert_int_eq(compositor_hyprland_ops.get_event_fd(), -1);
    pipe_fds[0] = -1; /* The adapter owns and closes an invalidated event fd. */
}
END_TEST

START_TEST(test_final_workspace_event_is_read_before_hangup)
{
    const char *ws = "workspace>>6\n";
    ck_assert_int_eq(write(pipe_fds[1], ws, strlen(ws)), (ssize_t)strlen(ws));
    close(pipe_fds[1]);
    pipe_fds[1] = -1;

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 6);

    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_ERROR_NO_DATA);
    ck_assert_int_eq(compositor_hyprland_ops.get_event_fd(), -1);
    pipe_fds[0] = -1;
}
END_TEST

START_TEST(test_burst_workspace_events_are_delivered_in_order)
{
    const char *events = "workspace>>2\nworkspace>>3\n";
    ck_assert_int_eq(write(pipe_fds[1], events, strlen(events)),
                     (ssize_t)strlen(events));

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 2);

    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 2);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 3);
}
END_TEST

START_TEST(test_partial_workspace_event_waits_for_newline)
{
    const char *partial = "workspace>>";
    ck_assert_int_eq(write(pipe_fds[1], partial, strlen(partial)),
                     (ssize_t)strlen(partial));

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_ERROR_NO_DATA);
    ck_assert_int_ne(compositor_hyprland_ops.get_event_fd(), -1);

    ck_assert_int_eq(write(pipe_fds[1], "4\n", 2), 2);
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 4);
}
END_TEST

START_TEST(test_disconnect_reconnects_and_reconciles_workspace)
{
    int replacement[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK,
                                0, replacement), 0);

    close(pipe_fds[1]);
    pipe_fds[1] = -1;

    compositor_event_t ev;
    extern const compositor_ops_t compositor_hyprland_ops;
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_ERROR_NO_DATA);
    pipe_fds[0] = -1;

    hyprland_test_set_reconnect_fd(replacement[0], 3);
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 1);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 3);
    ck_assert_int_eq(compositor_hyprland_ops.get_event_fd(), replacement[0]);

    const char *next = "workspace>>4\n";
    ck_assert_int_eq(write(replacement[1], next, strlen(next)),
                     (ssize_t)strlen(next));
    ck_assert_int_eq(compositor_hyprland_ops.poll_events(&ev), HYPRLAX_SUCCESS);
    ck_assert_int_eq(ev.data.workspace.from_workspace, 3);
    ck_assert_int_eq(ev.data.workspace.to_workspace, 4);

    pipe_fds[0] = replacement[0];
    close(replacement[1]);
}
END_TEST

Suite *hyprland_suite(void)
{
    Suite *s = suite_create("HyprlandEvents");
    TCase *tc = tcase_create("Parsing");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_focusedmon_does_not_emit_workspace_change);
    tcase_add_test(tc, test_workspace_after_focusedmon_has_correct_from_and_monitor);
    tcase_add_test(tc, test_workspace_same_id_no_event);
    tcase_add_test(tc, test_multiple_focusedmon_no_event_last_wins);
    // TODO: This test triggers SIGILL in Valgrind due to unrecognized AVX/AVX512 instructions
    // Test passes normally but causes false positive "MEMORY ISSUES" in memcheck
    // Uncomment when Valgrind supports these CPU instructions
    // tcase_add_test(tc, test_overlong_monitor_name_ignored);
    tcase_add_test(tc, test_workspace_with_no_monitor_name);
    tcase_add_test(tc, test_chained_workspace_events_update_from);
    tcase_add_test(tc, test_monitor_name_copied_when_within_limit);
    tcase_add_test(tc, test_closed_event_stream_is_invalidated);
    tcase_add_test(tc, test_final_workspace_event_is_read_before_hangup);
    tcase_add_test(tc, test_burst_workspace_events_are_delivered_in_order);
    tcase_add_test(tc, test_partial_workspace_event_waits_for_newline);
    tcase_add_test(tc, test_disconnect_reconnects_and_reconciles_workspace);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = hyprland_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_FORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
