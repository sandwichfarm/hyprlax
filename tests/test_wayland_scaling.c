/* Exercise the production scale callbacks without a compositor or GPU. */
#include <check.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "../protocols/viewporter-client-protocol.h"

static int buffer_scale, buffer_width, buffer_height, buffer_scale_calls;
static uint32_t surface_version;
static int destination_width, destination_height, viewport_destroyed;
static int fake_surface, fake_window, fake_viewport, fake_viewporter;

static void record_buffer_scale(struct wl_surface *surface, int32_t scale) {
    ck_assert_ptr_eq(surface, &fake_surface);
    ck_assert_uint_ge(surface_version, WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION);
    buffer_scale = scale;
    buffer_scale_calls++;
}

static uint32_t record_surface_version(struct wl_surface *surface) {
    ck_assert_ptr_eq(surface, &fake_surface);
    return surface_version;
}

static void record_resize(struct wl_egl_window *window, int width, int height,
                          int dx, int dy) {
    ck_assert_ptr_eq(window, &fake_window);
    ck_assert_int_eq(dx, 0);
    ck_assert_int_eq(dy, 0);
    buffer_width = width;
    buffer_height = height;
}

static struct wp_viewport *record_viewport(struct wp_viewporter *viewporter,
                                           struct wl_surface *surface) {
    ck_assert_ptr_eq(viewporter, &fake_viewporter);
    ck_assert_ptr_eq(surface, &fake_surface);
    return (struct wp_viewport *)&fake_viewport;
}

static void record_destination(struct wp_viewport *viewport, int32_t width, int32_t height) {
    ck_assert_ptr_eq(viewport, &fake_viewport);
    destination_width = width;
    destination_height = height;
}

static void record_destroy(struct wp_viewport *viewport) {
    ck_assert_ptr_eq(viewport, &fake_viewport);
    ck_assert_int_eq(destination_width, -1);
    ck_assert_int_eq(destination_height, -1);
    viewport_destroyed++;
}

#define wl_surface_get_version record_surface_version
#define wl_surface_set_buffer_scale record_buffer_scale
#define wl_egl_window_resize record_resize
#define wp_viewporter_get_viewport record_viewport
#define wp_viewport_set_destination record_destination
#define wp_viewport_destroy record_destroy
#include "../src/platform/wayland.c"

/* The callback harness never creates a real renderer surface. */
void *gles2_create_monitor_surface(void *window) {
    (void)window;
    return NULL;
}

void gles2_destroy_monitor_surface(void *surface) {
    (void)surface;
}

static monitor_instance_t monitor;
static wayland_data_t wayland;
static monitor_list_t monitors;
static hyprlax_context_t context;
static output_info_t output;

static void setup(void) {
    buffer_scale = 1;
    buffer_scale_calls = 0;
    surface_version = 3;
    buffer_width = buffer_height = 0;
    destination_width = destination_height = viewport_destroyed = 0;
    monitor = (monitor_instance_t){
        .width = 2880, .height = 1800, .scale = 2,
        .wl_surface = (struct wl_surface *)&fake_surface,
        .wl_egl_window = &fake_window,
        .wl_output = (struct wl_output *)&output,
    };
    monitors = (monitor_list_t){ .head = &monitor, .count = 1 };
    context = (hyprlax_context_t){ .monitors = &monitors };
    wayland = (wayland_data_t){
        .viewporter = (struct wp_viewporter *)&fake_viewporter, .ctx = &context,
        .width = 1440, .height = 900,
    };
    output = (output_info_t){ .output = monitor.wl_output, .scale = 2 };
    g_wayland_data = &wayland;
}

START_TEST(test_integer_preferred_scale_maps_physical_buffer_to_logical_surface) {
    fractional_scale_preferred(&monitor, NULL, 240);
    ck_assert_int_eq(buffer_width, 2880);
    ck_assert_int_eq(buffer_height, 1800);
    ck_assert_int_eq(buffer_scale, 2);
    ck_assert_int_eq(buffer_width / buffer_scale, 1440);
    ck_assert_int_eq(buffer_height / buffer_scale, 900);
    ck_assert_ptr_null(monitor.wp_viewport);
}
END_TEST

START_TEST(test_fractional_scale_uses_viewport_with_unit_buffer_scale) {
    buffer_scale = 2;
    fractional_scale_preferred(&monitor, NULL, 180);
    ck_assert_int_eq(buffer_width, 2880);
    ck_assert_int_eq(buffer_height, 1800);
    ck_assert_int_eq(buffer_scale, 1);
    ck_assert_int_eq(destination_width, 1920);
    ck_assert_int_eq(destination_height, 1200);
}
END_TEST

START_TEST(test_fractional_to_integer_removes_stale_viewport) {
    fractional_scale_preferred(&monitor, NULL, 180);
    fractional_scale_preferred(&monitor, NULL, 240);
    ck_assert_int_eq(viewport_destroyed, 1);
    ck_assert_ptr_null(monitor.wp_viewport);
    ck_assert_int_eq(buffer_scale, 2);
    fractional_scale_preferred(&monitor, NULL, 120);
    ck_assert_int_eq(buffer_scale, 1);
    ck_assert_int_eq(buffer_width, 2880);
}
END_TEST

START_TEST(test_output_scale_updates_existing_surface_without_fractional_protocol) {
    output_handle_scale(&output, monitor.wl_output, 2);
    ck_assert_int_eq(buffer_scale, 2);
    output_handle_scale(&output, monitor.wl_output, 1);
    ck_assert_int_eq(monitor.scale, 1);
    ck_assert_int_eq(buffer_scale, 1);
}
END_TEST

START_TEST(test_preferred_scale_takes_precedence_over_output_scale) {
    fractional_scale_preferred(&monitor, NULL, 180);
    output_handle_scale(&output, monitor.wl_output, 2);
    ck_assert_int_eq(buffer_scale, 1);
    ck_assert_int_eq(destination_width, 1920);
    ck_assert_int_eq(destination_height, 1200);
}
END_TEST

START_TEST(test_legacy_surface_does_not_receive_unsupported_requests) {
    surface_version = 1;
    fractional_scale_preferred(&monitor, NULL, 120);
    ck_assert_int_eq(buffer_scale_calls, 0);
    ck_assert_int_eq(buffer_width, 2880);
    fractional_scale_preferred(&monitor, NULL, 180);
    ck_assert_int_eq(buffer_scale_calls, 0);
    ck_assert_int_eq(destination_width, 1920);
}
END_TEST

START_TEST(test_scale_is_applied_before_egl_window_exists) {
    monitor.wl_egl_window = NULL;
    wayland_apply_monitor_scale(&monitor);
    ck_assert_int_eq(buffer_scale, 2);
    ck_assert_int_eq(buffer_width, 0);
    ck_assert_ptr_null(monitor.wp_viewport);
    ck_assert(!wayland.has_pending_resize);
}
END_TEST

START_TEST(test_scale_changes_redraw_configured_idle_surface) {
    monitor.configured = true;
    fractional_scale_preferred(&monitor, NULL, 180);
    ck_assert(wayland.has_pending_resize);
    ck_assert_int_eq(wayland.pending_width, 1440);
    ck_assert_int_eq(wayland.pending_height, 900);
    wayland.has_pending_resize = false;
    fractional_scale_preferred(&monitor, NULL, 120);
    ck_assert(wayland.has_pending_resize);
    ck_assert_ptr_null(monitor.wp_viewport);
}
END_TEST

START_TEST(test_secondary_scale_does_not_change_global_renderer_aspect) {
    monitor.configured = true;
    monitor.width = 1920;
    monitor.height = 1080;
    fractional_scale_preferred(&monitor, NULL, 180);
    ck_assert(wayland.has_pending_resize);
    ck_assert_int_eq(wayland.pending_width, 1440);
    ck_assert_int_eq(wayland.pending_height, 900);
    ck_assert_int_eq(buffer_width, 1920);
    ck_assert_int_eq(buffer_height, 1080);
}
END_TEST

int main(void) {
    Suite *suite = suite_create("Wayland scaling");
    TCase *tc = tcase_create("Surface coordinates");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_integer_preferred_scale_maps_physical_buffer_to_logical_surface);
    tcase_add_test(tc, test_fractional_scale_uses_viewport_with_unit_buffer_scale);
    tcase_add_test(tc, test_fractional_to_integer_removes_stale_viewport);
    tcase_add_test(tc, test_output_scale_updates_existing_surface_without_fractional_protocol);
    tcase_add_test(tc, test_preferred_scale_takes_precedence_over_output_scale);
    tcase_add_test(tc, test_legacy_surface_does_not_receive_unsupported_requests);
    tcase_add_test(tc, test_scale_is_applied_before_egl_window_exists);
    tcase_add_test(tc, test_scale_changes_redraw_configured_idle_surface);
    tcase_add_test(tc, test_secondary_scale_does_not_change_global_renderer_aspect);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
