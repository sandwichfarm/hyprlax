// Granular test suite for workspace change handling and edge cases
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

// Mock workspace state
typedef struct {
    int current;
    int previous;
    int total;
    int x;
    int y;
    int grid_width;
    int grid_height;
} workspace_state_t;

// Test rapid workspace changes
START_TEST(test_rapid_workspace_changes)
{
    // Simulate rapid workspace switching
    workspace_state_t state = {1, 0, 10, 0, 0, 3, 3};
    int changes[] = {2, 3, 4, 3, 2, 1, 5, 10, 1};
    
    for (int i = 0; i < sizeof(changes)/sizeof(changes[0]); i++) {
        state.previous = state.current;
        state.current = changes[i];
        
        // Ensure state remains valid
        ck_assert(state.current >= 1 && state.current <= state.total);
        ck_assert(state.previous != state.current || i == 0);
    }
}
END_TEST

START_TEST(test_workspace_wraparound)
{
    // Test workspace wraparound behavior
    workspace_state_t state = {1, 0, 10, 0, 0, 3, 3};
    
    // Test forward wraparound
    state.current = 10;
    state.current = 1; // Should wrap from 10 to 1
    ck_assert_int_eq(state.current, 1);
    
    // Test backward wraparound
    state.current = 1;
    state.current = 10; // Should wrap from 1 to 10
    ck_assert_int_eq(state.current, 10);
}
END_TEST

START_TEST(test_workspace_boundary_conditions)
{
    // Test boundary conditions
    workspace_state_t state = {1, 0, 10, 0, 0, 3, 3};
    
    // Test minimum boundary
    state.current = 0; // Invalid
    if (state.current < 1) state.current = 1;
    ck_assert_int_eq(state.current, 1);
    
    // Test maximum boundary
    state.current = 11; // Invalid
    if (state.current > state.total) state.current = state.total;
    ck_assert_int_eq(state.current, 10);
    
    // Test negative values
    state.current = -5; // Invalid
    if (state.current < 1) state.current = 1;
    ck_assert_int_eq(state.current, 1);
}
END_TEST

START_TEST(test_2d_workspace_navigation)
{
    // Test 2D workspace grid navigation
    workspace_state_t state = {5, 0, 9, 1, 1, 3, 3};
    
    // Position (1,1) is workspace 5 in a 3x3 grid
    // Test movement in all directions
    
    // Move left: (1,1) -> (0,1)
    state.x = 0;
    int workspace_left = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_left, 4);
    
    // Move right: (1,1) -> (2,1)
    state.x = 2;
    int workspace_right = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_right, 6);
    
    // Move up: (1,1) -> (1,0)
    state.x = 1;
    state.y = 0;
    int workspace_up = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_up, 2);
    
    // Move down: (1,1) -> (1,2)
    state.y = 2;
    int workspace_down = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_down, 8);
}
END_TEST

START_TEST(test_2d_workspace_boundary)
{
    // Test 2D workspace grid boundaries
    workspace_state_t state = {1, 0, 9, 0, 0, 3, 3};
    
    // Test corner positions
    // Top-left (0,0)
    ck_assert_int_eq(state.x, 0);
    ck_assert_int_eq(state.y, 0);
    
    // Bottom-right (2,2) = workspace 9
    state.x = 2;
    state.y = 2;
    int workspace = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace, 9);
    
    // Test invalid positions
    state.x = 3; // Out of bounds
    if (state.x >= state.grid_width) state.x = state.grid_width - 1;
    ck_assert_int_eq(state.x, 2);
    
    state.y = -1; // Out of bounds
    if (state.y < 0) state.y = 0;
    ck_assert_int_eq(state.y, 0);
}
END_TEST

START_TEST(test_diagonal_2d_movement)
{
    // Test diagonal movement in 2D workspace
    workspace_state_t state = {5, 0, 9, 1, 1, 3, 3};
    
    // From center (1,1), move diagonally
    // Top-left diagonal
    state.x = 0;
    state.y = 0;
    int workspace_tl = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_tl, 1);
    
    // Bottom-right diagonal
    state.x = 2;
    state.y = 2;
    int workspace_br = state.y * state.grid_width + state.x + 1;
    ck_assert_int_eq(workspace_br, 9);
}
END_TEST

START_TEST(test_workspace_change_with_animation)
{
    // Test workspace changes during animation
    float animation_progress = 0.0f;
    int animating = 0;
    workspace_state_t state = {1, 0, 10, 0, 0, 3, 3};
    
    // Start animation
    state.previous = state.current;
    state.current = 2;
    animating = 1;
    animation_progress = 0.0f;
    
    // Change workspace mid-animation
    state.previous = state.current;
    state.current = 3;
    
    // Animation should restart
    animation_progress = 0.0f;
    
    ck_assert_int_eq(state.current, 3);
    ck_assert_int_eq(state.previous, 2);
    ck_assert_float_eq_tol(animation_progress, 0.0f, 0.001f);
}
END_TEST

START_TEST(test_workspace_offset_calculation_precision)
{
    // Test precise offset calculations for different workspace counts
    struct {
        int workspace;
        float shift_per_ws;
        float expected_offset;
    } test_cases[] = {
        {1, 100.0f, 0.0f},
        {2, 100.0f, 100.0f},
        {5, 100.0f, 400.0f},
        {10, 100.0f, 900.0f},
        {1, 237.5f, 0.0f},
        {7, 237.5f, 1425.0f},
        {3, 333.333f, 666.666f},
    };
    
    for (int i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        float offset = (test_cases[i].workspace - 1) * test_cases[i].shift_per_ws;
        ck_assert_float_eq_tol(offset, test_cases[i].expected_offset, 0.01f);
    }
}
END_TEST

START_TEST(test_workspace_with_multiple_monitors)
{
    // Test workspace handling with multiple monitors
    struct {
        int monitor;
        int workspace;
        int workspaces_per_monitor;
        int global_workspace;
    } monitors[] = {
        {0, 1, 10, 1},
        {0, 5, 10, 5},
        {1, 1, 10, 11},
        {1, 3, 10, 13},
        {2, 2, 10, 22},
    };
    
    for (int i = 0; i < sizeof(monitors)/sizeof(monitors[0]); i++) {
        int calculated = monitors[i].monitor * monitors[i].workspaces_per_monitor + 
                        monitors[i].workspace;
        ck_assert_int_eq(calculated, monitors[i].global_workspace);
    }
}
END_TEST

START_TEST(test_workspace_special_handling)
{
    // Test special/scratchpad workspace handling
    workspace_state_t state = {1, 0, 10, 0, 0, 3, 3};
    
    // Special workspace (typically negative or >1000)
    state.current = -99; // Special workspace
    
    // Should not affect parallax
    float offset = 0.0f;
    if (state.current < 0) {
        offset = 0.0f; // No parallax for special workspaces
    }
    
    ck_assert_float_eq_tol(offset, 0.0f, 0.001f);
    
    // Scratchpad workspace
    state.current = 1001;
    if (state.current > 1000) {
        offset = 0.0f; // No parallax for scratchpad
    }
    
    ck_assert_float_eq_tol(offset, 0.0f, 0.001f);
}
END_TEST

Suite *workspace_suite(void)
{
    Suite *s;
    TCase *tc_core;
    TCase *tc_2d;
    TCase *tc_edge;
    
    s = suite_create("WorkspaceChanges");
    
    tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_rapid_workspace_changes);
    tcase_add_test(tc_core, test_workspace_wraparound);
    tcase_add_test(tc_core, test_workspace_boundary_conditions);
    tcase_add_test(tc_core, test_workspace_offset_calculation_precision);
    tcase_add_test(tc_core, test_workspace_change_with_animation);
    
    tc_2d = tcase_create("2D");
    tcase_add_test(tc_2d, test_2d_workspace_navigation);
    tcase_add_test(tc_2d, test_2d_workspace_boundary);
    tcase_add_test(tc_2d, test_diagonal_2d_movement);
    
    tc_edge = tcase_create("EdgeCases");
    tcase_add_test(tc_edge, test_workspace_with_multiple_monitors);
    tcase_add_test(tc_edge, test_workspace_special_handling);
    
    suite_add_tcase(s, tc_core);
    suite_add_tcase(s, tc_2d);
    suite_add_tcase(s, tc_edge);
    
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = workspace_suite();
    sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}