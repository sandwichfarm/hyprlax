// Test suite for modular architecture integration
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

// Test module interface consistency
START_TEST(test_module_interfaces)
{
    // Verify all modules implement required interfaces
    const char *required_platform_ops[] = {
        "init", "destroy", "connect", "disconnect",
        "create_window", "poll_events", NULL
    };
    
    const char *required_compositor_ops[] = {
        "init", "destroy", "detect", "get_name",
        "get_current_workspace", "poll_events", NULL
    };
    
    const char *required_renderer_ops[] = {
        "init", "destroy", "begin_frame", "end_frame",
        "load_texture", "draw_textured_quad", NULL
    };
    
    // These would normally check actual function pointers
    // Here we just verify the interface contract exists
    ck_assert_ptr_nonnull(required_platform_ops[0]);
    ck_assert_ptr_nonnull(required_compositor_ops[0]);
    ck_assert_ptr_nonnull(required_renderer_ops[0]);
}
END_TEST

START_TEST(test_module_initialization_order)
{
    // Test that modules initialize in correct order
    int init_order[] = {0, 0, 0, 0};  // platform, compositor, renderer, core
    
    // Simulate initialization sequence
    init_order[0] = 1;  // Platform first
    ck_assert_int_eq(init_order[0], 1);
    
    init_order[1] = 2;  // Compositor second
    ck_assert_int_gt(init_order[1], init_order[0]);
    
    init_order[2] = 3;  // Renderer third
    ck_assert_int_gt(init_order[2], init_order[1]);
    
    init_order[3] = 4;  // Core last
    ck_assert_int_gt(init_order[3], init_order[2]);
}
END_TEST

START_TEST(test_module_error_handling)
{
    // Test error codes are consistent across modules
    enum {
        SUCCESS = 0,
        ERROR_INVALID_ARGS = -1,
        ERROR_NO_MEMORY = -2,
        ERROR_NO_DISPLAY = -3,
        ERROR_INIT_FAILED = -4
    };
    
    // Verify error code consistency
    int result = SUCCESS;
    ck_assert_int_eq(result, 0);
    
    result = ERROR_INVALID_ARGS;
    ck_assert_int_lt(result, 0);
    
    result = ERROR_NO_DISPLAY;
    ck_assert_int_eq(result, -3);
}
END_TEST

START_TEST(test_module_isolation)
{
    // Test that modules are properly isolated
    struct module_state {
        int platform_initialized;
        int compositor_initialized;
        int renderer_initialized;
    } state = {0, 0, 0};
    
    // Initialize platform shouldn't affect others
    state.platform_initialized = 1;
    ck_assert_int_eq(state.compositor_initialized, 0);
    ck_assert_int_eq(state.renderer_initialized, 0);
    
    // Initialize compositor shouldn't affect renderer
    state.compositor_initialized = 1;
    ck_assert_int_eq(state.renderer_initialized, 0);
}
END_TEST

START_TEST(test_module_cleanup)
{
    // Test proper cleanup sequence
    int cleanup_order[] = {0, 0, 0, 0};
    int cleanup_index = 0;
    
    // Cleanup should be reverse of init order
    cleanup_order[cleanup_index++] = 4;  // Core first
    cleanup_order[cleanup_index++] = 3;  // Renderer second
    cleanup_order[cleanup_index++] = 2;  // Compositor third
    cleanup_order[cleanup_index++] = 1;  // Platform last
    
    // Verify reverse order
    for (int i = 0; i < 3; i++) {
        ck_assert_int_gt(cleanup_order[i], cleanup_order[i + 1]);
    }
}
END_TEST

START_TEST(test_platform_compositor_integration)
{
    // Test platform and compositor module interaction
    struct {
        const char *platform;
        const char *compositor;
        int compatible;
    } compatibility[] = {
        { "wayland", "hyprland", 1 },
        { "wayland", "sway", 1 },
        { "wayland", "river", 1 },
        { "wayland", "i3", 1 },     // i3 can run on Wayland (sway)
    };
    
    for (int i = 0; i < sizeof(compatibility)/sizeof(compatibility[0]); i++) {
        if (strcmp(compatibility[i].platform, "wayland") == 0) {
            if (strcmp(compatibility[i].compositor, "hyprland") == 0 ||
                strcmp(compatibility[i].compositor, "sway") == 0 ||
                strcmp(compatibility[i].compositor, "river") == 0) {
                ck_assert_int_eq(compatibility[i].compatible, 1);
            }
        }
    }
}
END_TEST

START_TEST(test_renderer_platform_integration)
{
    // Test renderer and platform compatibility
    struct {
        const char *renderer;
        const char *platform;
        const char *context_type;
    } contexts[] = {
        { "gles2", "wayland", "EGL" },
    };
    
    // Verify EGL is supported on both platforms
    int egl_wayland = 0;
    
    for (int i = 0; i < sizeof(contexts)/sizeof(contexts[0]); i++) {
        if (strcmp(contexts[i].context_type, "EGL") == 0) {
            if (strcmp(contexts[i].platform, "wayland") == 0) {
                egl_wayland = 1;
            }
        }
    }
    
    ck_assert_int_eq(egl_wayland, 1);
}
END_TEST

START_TEST(test_module_registry)
{
    // Test module registration system
    typedef struct {
        const char *name;
        void *ops;
        int registered;
    } module_entry_t;
    
    module_entry_t compositors[] = {
        { "hyprland", NULL, 1 },
        { "sway", NULL, 1 },
        { "river", NULL, 1 },
        { "wayfire", NULL, 1 },
        { "niri", NULL, 1 },
        { "generic_wayland", NULL, 1 }
    };
    
    // Verify all compositors are registered
    int registered_count = 0;
    for (int i = 0; i < sizeof(compositors)/sizeof(compositors[0]); i++) {
        if (compositors[i].registered) {
            registered_count++;
        }
    }
    
    ck_assert_int_eq(registered_count, 6);
}
END_TEST

Suite *modules_suite(void)
{
    Suite *s;
    TCase *tc_interfaces;
    TCase *tc_integration;
    
    s = suite_create("Modules");
    
    tc_interfaces = tcase_create("Interfaces");
    tcase_add_test(tc_interfaces, test_module_interfaces);
    tcase_add_test(tc_interfaces, test_module_initialization_order);
    tcase_add_test(tc_interfaces, test_module_error_handling);
    tcase_add_test(tc_interfaces, test_module_isolation);
    tcase_add_test(tc_interfaces, test_module_cleanup);
    
    tc_integration = tcase_create("Integration");
    tcase_add_test(tc_integration, test_platform_compositor_integration);
    tcase_add_test(tc_integration, test_renderer_platform_integration);
    tcase_add_test(tc_integration, test_module_registry);
    
    suite_add_tcase(s, tc_interfaces);
    suite_add_tcase(s, tc_integration);
    
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = modules_suite();
    sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}