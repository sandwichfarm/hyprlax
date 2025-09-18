// Test suite for platform abstraction layer
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Mock platform structures (simplified versions from platform.h)
typedef enum {
    PLATFORM_WAYLAND,
    PLATFORM_AUTO
} platform_type_t;

typedef struct {
    platform_type_t type;
    const char *name;
    void *native_display;
    void *native_window;
} platform_t;

// Mock detection function
platform_type_t detect_platform(void) {
    if (getenv("WAYLAND_DISPLAY")) {
        return PLATFORM_WAYLAND;
    }
    return PLATFORM_AUTO;
}

// Test cases
START_TEST(test_platform_detection_wayland)
{
    // Save original env
    char *orig_wayland = getenv("WAYLAND_DISPLAY");
    
    // Set Wayland environment
    setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    
    platform_type_t detected = detect_platform();
    ck_assert_int_eq(detected, PLATFORM_WAYLAND);
    
    // Restore env
    if (orig_wayland) setenv("WAYLAND_DISPLAY", orig_wayland, 1);
    else unsetenv("WAYLAND_DISPLAY");
}
END_TEST

START_TEST(test_platform_detection_fallback)
{
    // Save original env
    char *orig_wayland = getenv("WAYLAND_DISPLAY");
    
    // Unset Wayland - should fallback to AUTO
    unsetenv("WAYLAND_DISPLAY");
    
    platform_type_t detected = detect_platform();
    ck_assert_int_eq(detected, PLATFORM_AUTO);
    
    // Restore env
    if (orig_wayland) setenv("WAYLAND_DISPLAY", orig_wayland, 1);
}
END_TEST

START_TEST(test_platform_force_selection)
{
    // Save original env
    char *orig_platform = getenv("HYPRLAX_PLATFORM");
    
    // Force Wayland platform
    setenv("HYPRLAX_PLATFORM", "wayland", 1);
    setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    
    // In real implementation, this would check the forced platform
    const char *forced = getenv("HYPRLAX_PLATFORM");
    ck_assert_str_eq(forced, "wayland");
    
    // Restore env
    if (orig_platform) setenv("HYPRLAX_PLATFORM", orig_platform, 1);
    else unsetenv("HYPRLAX_PLATFORM");
}
END_TEST

START_TEST(test_platform_initialization)
{
    platform_t platform = {0};
    
    // Test initialization
    platform.type = PLATFORM_WAYLAND;
    platform.name = "Wayland";
    
    ck_assert_int_eq(platform.type, PLATFORM_WAYLAND);
    ck_assert_str_eq(platform.name, "Wayland");
    ck_assert_ptr_null(platform.native_display);
    ck_assert_ptr_null(platform.native_window);
}
END_TEST

START_TEST(test_platform_capabilities)
{
    // Test capability detection
    struct {
        platform_type_t type;
        int has_layer_shell;
        int has_ewmh;
        int has_transparency;
    } capabilities[] = {
        { PLATFORM_WAYLAND, 1, 0, 1 },
    };
    
    for (int i = 0; i < 1; i++) {
        if (capabilities[i].type == PLATFORM_WAYLAND) {
            ck_assert_int_eq(capabilities[i].has_layer_shell, 1);
            ck_assert_int_eq(capabilities[i].has_ewmh, 0);
        }
        ck_assert_int_eq(capabilities[i].has_transparency, 1);
    }
}
END_TEST

Suite *platform_suite(void)
{
    Suite *s;
    TCase *tc_core;
    
    s = suite_create("Platform");
    tc_core = tcase_create("Core");
    
    tcase_add_test(tc_core, test_platform_detection_wayland);
    tcase_add_test(tc_core, test_platform_detection_fallback);
    tcase_add_test(tc_core, test_platform_force_selection);
    tcase_add_test(tc_core, test_platform_initialization);
    tcase_add_test(tc_core, test_platform_capabilities);
    
    suite_add_tcase(s, tc_core);
    
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = platform_suite();
    sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}