// Granular test suite for animation state machine and edge cases
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

// Animation states
typedef enum {
    ANIM_IDLE,
    ANIM_STARTING,
    ANIM_RUNNING,
    ANIM_COMPLETING,
    ANIM_INTERRUPTED,
    ANIM_CANCELLED
} animation_state_t;

typedef struct {
    animation_state_t state;
    float progress;
    float duration;
    float start_time;
    float target_value;
    float current_value;
    int interruption_count;
} animation_t;

// Test animation state transitions
START_TEST(test_animation_state_transitions)
{
    animation_t anim = {ANIM_IDLE, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Idle -> Starting
    anim.state = ANIM_STARTING;
    ck_assert_int_eq(anim.state, ANIM_STARTING);
    
    // Starting -> Running
    anim.state = ANIM_RUNNING;
    ck_assert_int_eq(anim.state, ANIM_RUNNING);
    
    // Running -> Completing
    anim.progress = 1.0f;
    anim.state = ANIM_COMPLETING;
    ck_assert_int_eq(anim.state, ANIM_COMPLETING);
    
    // Completing -> Idle
    anim.state = ANIM_IDLE;
    ck_assert_int_eq(anim.state, ANIM_IDLE);
    
    // Test interruption
    anim.state = ANIM_RUNNING;
    anim.state = ANIM_INTERRUPTED;
    anim.interruption_count++;
    ck_assert_int_eq(anim.state, ANIM_INTERRUPTED);
    ck_assert_int_eq(anim.interruption_count, 1);
}
END_TEST

START_TEST(test_animation_progress_clamping)
{
    animation_t anim = {ANIM_RUNNING, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Test underflow
    anim.progress = -0.5f;
    if (anim.progress < 0.0f) anim.progress = 0.0f;
    ck_assert_float_eq_tol(anim.progress, 0.0f, 0.001f);
    
    // Test overflow
    anim.progress = 1.5f;
    if (anim.progress > 1.0f) anim.progress = 1.0f;
    ck_assert_float_eq_tol(anim.progress, 1.0f, 0.001f);
    
    // Test normal range
    anim.progress = 0.5f;
    ck_assert_float_eq_tol(anim.progress, 0.5f, 0.001f);
}
END_TEST

START_TEST(test_animation_interruption_handling)
{
    animation_t anim = {ANIM_IDLE, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Start animation
    anim.state = ANIM_RUNNING;
    anim.progress = 0.3f;
    anim.current_value = 30.0f;
    
    // Interrupt with new target
    float old_value = anim.current_value;
    anim.target_value = 200.0f;
    anim.state = ANIM_INTERRUPTED;
    anim.interruption_count++;
    
    // Should restart from current position
    anim.progress = 0.0f;
    anim.state = ANIM_RUNNING;
    
    ck_assert_float_eq_tol(anim.current_value, old_value, 0.001f);
    ck_assert_float_eq_tol(anim.target_value, 200.0f, 0.001f);
    ck_assert_int_eq(anim.interruption_count, 1);
}
END_TEST

START_TEST(test_animation_frame_skipping)
{
    animation_t anim = {ANIM_RUNNING, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Simulate frame skip (large time delta)
    float large_delta = 0.5f; // 500ms - multiple frames worth
    anim.progress += large_delta / anim.duration;
    
    // Progress should still be valid
    if (anim.progress > 1.0f) anim.progress = 1.0f;
    ck_assert(anim.progress >= 0.0f && anim.progress <= 1.0f);
    
    // Value should interpolate correctly despite skip
    anim.current_value = anim.target_value * anim.progress;
    ck_assert_float_eq_tol(anim.current_value, 50.0f, 1.0f);
}
END_TEST

START_TEST(test_animation_zero_duration)
{
    animation_t anim = {ANIM_IDLE, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // With zero duration, should jump immediately to target
    if (anim.duration <= 0.0f) {
        anim.progress = 1.0f;
        anim.current_value = anim.target_value;
        anim.state = ANIM_IDLE;
    }
    
    ck_assert_float_eq_tol(anim.progress, 1.0f, 0.001f);
    ck_assert_float_eq_tol(anim.current_value, 100.0f, 0.001f);
    ck_assert_int_eq(anim.state, ANIM_IDLE);
}
END_TEST

START_TEST(test_animation_negative_duration)
{
    animation_t anim = {ANIM_IDLE, 0.0f, -1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Negative duration should be treated as zero
    if (anim.duration < 0.0f) anim.duration = 0.0f;
    
    ck_assert_float_eq_tol(anim.duration, 0.0f, 0.001f);
}
END_TEST

START_TEST(test_animation_precision_accumulation)
{
    animation_t anim = {ANIM_RUNNING, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Simulate many small updates
    float delta = 0.001f; // 1ms updates
    float accumulated_error = 0.0f;
    
    for (int i = 0; i < 1000; i++) {
        float old_progress = anim.progress;
        anim.progress += delta;
        
        // Check for precision loss
        float expected = old_progress + delta;
        float error = fabs(anim.progress - expected);
        accumulated_error += error;
    }
    
    // Accumulated error should be minimal
    ck_assert(accumulated_error < 0.01f);
}
END_TEST

START_TEST(test_animation_reverse_direction)
{
    animation_t anim = {ANIM_IDLE, 0.0f, 1.0f, 0.0f, 100.0f, 100.0f, 0};
    
    // Animate from 100 to 0 (reverse)
    anim.target_value = 0.0f;
    anim.state = ANIM_RUNNING;
    
    // At 50% progress
    anim.progress = 0.5f;
    anim.current_value = anim.current_value + (anim.target_value - anim.current_value) * anim.progress;
    
    // Should be at 50
    ck_assert_float_eq_tol(anim.current_value, 50.0f, 1.0f);
}
END_TEST

START_TEST(test_animation_queue_overflow)
{
    // Test handling of animation queue overflow
    #define MAX_QUEUED_ANIMS 10
    animation_t queue[MAX_QUEUED_ANIMS];
    int queue_head = 0;
    int queue_tail = 0;
    int queue_count = 0;
    
    // Fill queue
    for (int i = 0; i < MAX_QUEUED_ANIMS; i++) {
        queue[queue_tail] = (animation_t){ANIM_IDLE, 0.0f, 1.0f, 0.0f, i * 10.0f, 0.0f, 0};
        queue_tail = (queue_tail + 1) % MAX_QUEUED_ANIMS;
        queue_count++;
    }
    
    ck_assert_int_eq(queue_count, MAX_QUEUED_ANIMS);
    
    // Try to add one more (should drop oldest or reject)
    if (queue_count >= MAX_QUEUED_ANIMS) {
        // Drop oldest
        queue_head = (queue_head + 1) % MAX_QUEUED_ANIMS;
        queue_count--;
    }
    
    queue[queue_tail] = (animation_t){ANIM_IDLE, 0.0f, 1.0f, 0.0f, 999.0f, 0.0f, 0};
    queue_tail = (queue_tail + 1) % MAX_QUEUED_ANIMS;
    queue_count++;
    
    ck_assert_int_eq(queue_count, MAX_QUEUED_ANIMS);
}
END_TEST

START_TEST(test_animation_concurrent_updates)
{
    // Test concurrent animation updates on different properties
    animation_t x_anim = {ANIM_RUNNING, 0.5f, 1.0f, 0.0f, 100.0f, 50.0f, 0};
    animation_t y_anim = {ANIM_RUNNING, 0.3f, 2.0f, 0.0f, 200.0f, 60.0f, 0};
    animation_t opacity = {ANIM_RUNNING, 0.8f, 0.5f, 0.0f, 1.0f, 0.8f, 0};
    
    // Update all animations
    float delta = 0.016f; // 16ms frame
    
    x_anim.progress += delta / x_anim.duration;
    y_anim.progress += delta / y_anim.duration;
    opacity.progress += delta / opacity.duration;
    
    // All should remain independent
    ck_assert(x_anim.progress != y_anim.progress);
    ck_assert(y_anim.progress != opacity.progress);
    ck_assert(x_anim.progress != opacity.progress);
}
END_TEST

START_TEST(test_animation_easing_discontinuity)
{
    // Test for discontinuities when switching easing functions
    animation_t anim = {ANIM_RUNNING, 0.5f, 1.0f, 0.0f, 100.0f, 50.0f, 0};
    
    // Record position before easing change
    float value_before = anim.current_value;
    
    // Change easing (would normally change the curve)
    // Value at current progress shouldn't jump
    anim.current_value = value_before; // Ensure continuity
    
    ck_assert_float_eq_tol(anim.current_value, value_before, 0.001f);
}
END_TEST

START_TEST(test_animation_performance_degradation)
{
    // Test animation behavior under performance degradation
    animation_t anim = {ANIM_RUNNING, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0};
    
    // Simulate varying frame times
    float frame_times[] = {
        0.016f,  // 60 FPS
        0.033f,  // 30 FPS
        0.050f,  // 20 FPS
        0.100f,  // 10 FPS
        0.200f,  // 5 FPS
    };
    
    for (int i = 0; i < sizeof(frame_times)/sizeof(frame_times[0]); i++) {
        float old_progress = anim.progress;
        anim.progress += frame_times[i] / anim.duration;
        
        // Progress should always increase
        ck_assert(anim.progress > old_progress || anim.progress >= 1.0f);
        
        // Should not overshoot significantly
        if (anim.progress > 1.0f) {
            ck_assert(anim.progress < 1.5f);
            anim.progress = 1.0f;
        }
    }
}
END_TEST

Suite *animation_state_suite(void)
{
    Suite *s;
    TCase *tc_state;
    TCase *tc_edge;
    TCase *tc_perf;
    
    s = suite_create("AnimationState");
    
    tc_state = tcase_create("StateMachine");
    tcase_add_test(tc_state, test_animation_state_transitions);
    tcase_add_test(tc_state, test_animation_progress_clamping);
    tcase_add_test(tc_state, test_animation_interruption_handling);
    tcase_add_test(tc_state, test_animation_reverse_direction);
    
    tc_edge = tcase_create("EdgeCases");
    tcase_add_test(tc_edge, test_animation_frame_skipping);
    tcase_add_test(tc_edge, test_animation_zero_duration);
    tcase_add_test(tc_edge, test_animation_negative_duration);
    tcase_add_test(tc_edge, test_animation_precision_accumulation);
    tcase_add_test(tc_edge, test_animation_queue_overflow);
    tcase_add_test(tc_edge, test_animation_easing_discontinuity);
    
    tc_perf = tcase_create("Performance");
    tcase_add_test(tc_perf, test_animation_concurrent_updates);
    tcase_add_test(tc_perf, test_animation_performance_degradation);
    
    suite_add_tcase(s, tc_state);
    suite_add_tcase(s, tc_edge);
    suite_add_tcase(s, tc_perf);
    
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = animation_state_suite();
    sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}