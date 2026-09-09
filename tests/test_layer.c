/* Regression tests for layer visibility and GIF timing. */
#include <check.h>
#include <stdlib.h>

#include "include/core.h"

START_TEST(test_layer_visibility_controls_render_work)
{
    parallax_layer_t *layer = layer_create("wallpaper.png", 1.0f, 1.0f);
    ck_assert_ptr_nonnull(layer);

    ck_assert(layer_is_visible(layer));
    layer->opacity = 0.0f;
    ck_assert(!layer_is_visible(layer));
    layer->opacity = 1.0f;
    layer->hidden = true;
    ck_assert(!layer_is_visible(layer));

    layer_destroy(layer);
}
END_TEST

START_TEST(test_gif_timing_advances_when_not_rendered)
{
    parallax_layer_t *layer = layer_create("weather.gif", 1.0f, 0.0f);
    ck_assert_ptr_nonnull(layer);

    uint32_t textures[] = {11, 22, 33};
    int delays[] = {100, 100, 100};
    layer->is_gif = true;
    layer->frame_count = 3;
    layer->gif_textures = textures;
    layer->gif_delays = delays;
    layer->current_frame = 0;
    layer->texture_id = textures[0];
    layer->last_frame_time = 1.0;

    /* Transparent layer still follows wall-clock GIF timing. */
    layer_tick_gif(layer, 1.25);
    ck_assert_int_eq(layer->current_frame, 2);
    ck_assert_uint_eq(layer->texture_id, 33);
    ck_assert_double_eq_tol(layer->last_frame_time, 1.2, 0.0001);

    layer_tick_gif(layer, 1.35);
    ck_assert_int_eq(layer->current_frame, 0);
    ck_assert_uint_eq(layer->texture_id, 11);
    ck_assert_double_eq_tol(layer->last_frame_time, 1.3, 0.0001);

    layer->gif_textures = NULL;
    layer->gif_delays = NULL;
    layer_destroy(layer);
}
END_TEST

Suite *layer_suite(void)
{
    Suite *suite = suite_create("Layer");
    TCase *tc = tcase_create("Core");
    tcase_add_test(tc, test_layer_visibility_controls_render_work);
    tcase_add_test(tc, test_gif_timing_advances_when_not_rendered);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    Suite *suite = layer_suite();
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
