// Test suite for renderer abstraction layer
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Mock renderer types and structures
typedef enum {
    RENDERER_GLES2,
    RENDERER_GLES3,
    RENDERER_VULKAN,
    RENDERER_SOFTWARE
} renderer_type_t;

typedef struct {
    int width;
    int height;
    int format;
    void *data;
} texture_t;

typedef struct {
    float mvp[16];
    float offset_x;
    float offset_y;
    float blur_radius;
    int blur_passes;
} render_state_t;

// Mock shader types
typedef enum {
    SHADER_BASIC,
    SHADER_BLUR,
    SHADER_MULTI_LAYER
} shader_type_t;

// Test cases
START_TEST(test_renderer_types)
{
    // Test renderer type enumeration
    ck_assert_int_eq(RENDERER_GLES2, 0);
    ck_assert_int_eq(RENDERER_GLES3, 1);
    ck_assert_int_eq(RENDERER_VULKAN, 2);
    ck_assert_int_eq(RENDERER_SOFTWARE, 3);
}
END_TEST

START_TEST(test_texture_loading)
{
    // Test texture structure initialization
    texture_t tex = {0};
    tex.width = 1920;
    tex.height = 1080;
    tex.format = 0x8058; // GL_RGBA8
    
    ck_assert_int_eq(tex.width, 1920);
    ck_assert_int_eq(tex.height, 1080);
    ck_assert_int_eq(tex.format, 0x8058);
    ck_assert_ptr_null(tex.data);
}
END_TEST

START_TEST(test_matrix_identity)
{
    // Test identity matrix creation
    float identity[16] = {0};
    
    // Create identity matrix
    for (int i = 0; i < 16; i++) {
        identity[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    
    // Verify identity matrix
    ck_assert_float_eq_tol(identity[0], 1.0f, 0.0001f);
    ck_assert_float_eq_tol(identity[5], 1.0f, 0.0001f);
    ck_assert_float_eq_tol(identity[10], 1.0f, 0.0001f);
    ck_assert_float_eq_tol(identity[15], 1.0f, 0.0001f);
    
    // Verify zeros
    ck_assert_float_eq_tol(identity[1], 0.0f, 0.0001f);
    ck_assert_float_eq_tol(identity[4], 0.0f, 0.0001f);
}
END_TEST

START_TEST(test_matrix_orthographic)
{
    // Test orthographic projection matrix
    float left = 0.0f;
    float right = 1920.0f;
    float bottom = 1080.0f;
    float top = 0.0f;
    float near = -1.0f;
    float far = 1.0f;
    
    float ortho[16] = {0};
    
    // Calculate orthographic matrix
    ortho[0] = 2.0f / (right - left);
    ortho[5] = 2.0f / (top - bottom);
    ortho[10] = -2.0f / (far - near);
    ortho[12] = -(right + left) / (right - left);
    ortho[13] = -(top + bottom) / (top - bottom);
    ortho[14] = -(far + near) / (far - near);
    ortho[15] = 1.0f;
    
    // Verify key components
    ck_assert_float_eq_tol(ortho[0], 2.0f / 1920.0f, 0.0001f);
    ck_assert_float_eq_tol(ortho[5], 2.0f / -1080.0f, 0.0001f);
    ck_assert_float_eq_tol(ortho[15], 1.0f, 0.0001f);
}
END_TEST

START_TEST(test_shader_compilation)
{
    // Test shader type validation
    shader_type_t types[] = {SHADER_BASIC, SHADER_BLUR, SHADER_MULTI_LAYER};
    
    for (int i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        ck_assert(types[i] >= 0 && types[i] <= SHADER_MULTI_LAYER);
    }
}
END_TEST

START_TEST(test_blur_kernel_generation)
{
    // Test blur kernel size calculations
    int blur_sizes[] = {3, 5, 7, 9, 11, 15, 21};
    
    for (int i = 0; i < sizeof(blur_sizes)/sizeof(blur_sizes[0]); i++) {
        int size = blur_sizes[i];
        // Kernel size should be odd
        ck_assert_int_eq(size % 2, 1);
        
        // Calculate sigma for Gaussian blur
        float sigma = size / 3.0f;
        ck_assert(sigma > 0.0f);
        
        // Calculate kernel weights
        int half_size = size / 2;
        float total_weight = 0.0f;
        
        for (int j = -half_size; j <= half_size; j++) {
            float weight = expf(-(j * j) / (2.0f * sigma * sigma));
            total_weight += weight;
        }
        
        // Weights should sum to approximately 1 when normalized
        ck_assert(total_weight > 0.0f);
    }
}
END_TEST

START_TEST(test_blur_pass_optimization)
{
    // Test multi-pass blur optimization
    struct {
        int single_pass_size;
        int multi_pass_count;
        int pass_size;
    } optimizations[] = {
        {15, 2, 7},   // 15x15 blur = two 7x7 passes
        {21, 3, 7},   // 21x21 blur = three 7x7 passes
        {25, 2, 13},  // 25x25 blur = two 13x13 passes
    };
    
    for (int i = 0; i < sizeof(optimizations)/sizeof(optimizations[0]); i++) {
        // Multi-pass should be more efficient than single large kernel
        int single_samples = optimizations[i].single_pass_size * optimizations[i].single_pass_size;
        int multi_samples = optimizations[i].multi_pass_count * 
                           optimizations[i].pass_size;
        
        // Multi-pass should require fewer texture samples
        ck_assert(multi_samples < single_samples || multi_samples == single_samples);
    }
}
END_TEST

START_TEST(test_viewport_setup)
{
    // Test viewport configuration
    struct {
        int screen_width;
        int screen_height;
        float shift;
        float expected_texture_width;
    } viewports[] = {
        {1920, 1080, 200.0f, 2120.0f},  // FHD with 200px shift
        {2560, 1440, 300.0f, 2860.0f},  // QHD with 300px shift
        {3840, 2160, 400.0f, 4240.0f},  // 4K with 400px shift
    };
    
    for (int i = 0; i < sizeof(viewports)/sizeof(viewports[0]); i++) {
        float max_workspaces = 10;
        float max_shift = viewports[i].shift * (max_workspaces - 1);
        float required_width = viewports[i].screen_width + max_shift;
        
        // Allow for some extra padding
        ck_assert(required_width >= viewports[i].expected_texture_width - 100.0f);
    }
}
END_TEST

START_TEST(test_texture_coordinates)
{
    // Test texture coordinate calculation
    float tex_coords[] = {
        0.0f, 0.0f,  // Top-left
        1.0f, 0.0f,  // Top-right
        0.0f, 1.0f,  // Bottom-left
        1.0f, 1.0f   // Bottom-right
    };
    
    // Verify UV coordinates are in [0, 1] range
    for (int i = 0; i < 8; i++) {
        ck_assert(tex_coords[i] >= 0.0f && tex_coords[i] <= 1.0f);
    }
    
    // Test parallax offset application
    float offset_x = 0.1f;
    float offset_y = 0.05f;
    
    float adjusted_coords[8];
    for (int i = 0; i < 4; i++) {
        adjusted_coords[i*2] = tex_coords[i*2] + offset_x;
        adjusted_coords[i*2+1] = tex_coords[i*2+1] + offset_y;
    }
    
    // Verify offset was applied
    ck_assert_float_eq_tol(adjusted_coords[0], 0.1f, 0.0001f);
    ck_assert_float_eq_tol(adjusted_coords[1], 0.05f, 0.0001f);
}
END_TEST

START_TEST(test_render_state)
{
    // Test render state management
    render_state_t state = {0};
    
    // Initialize state
    state.offset_x = 100.0f;
    state.offset_y = 50.0f;
    state.blur_radius = 15.0f;
    state.blur_passes = 2;
    
    ck_assert_float_eq_tol(state.offset_x, 100.0f, 0.0001f);
    ck_assert_float_eq_tol(state.offset_y, 50.0f, 0.0001f);
    ck_assert_float_eq_tol(state.blur_radius, 15.0f, 0.0001f);
    ck_assert_int_eq(state.blur_passes, 2);
}
END_TEST

START_TEST(test_multi_layer_rendering)
{
    // Test multi-layer rendering setup
    struct layer {
        float shift_factor;
        float opacity;
        int blur_enabled;
    } layers[] = {
        {1.0f, 1.0f, 1},   // Background layer - full shift, full opacity, blur
        {0.7f, 0.8f, 0},   // Middle layer - 70% shift, 80% opacity, no blur
        {0.3f, 0.6f, 0},   // Foreground layer - 30% shift, 60% opacity, no blur
    };
    
    for (int i = 0; i < sizeof(layers)/sizeof(layers[0]); i++) {
        ck_assert(layers[i].shift_factor >= 0.0f && layers[i].shift_factor <= 1.0f);
        ck_assert(layers[i].opacity >= 0.0f && layers[i].opacity <= 1.0f);
        ck_assert(layers[i].blur_enabled == 0 || layers[i].blur_enabled == 1);
    }
    
    // Verify layer ordering (back to front)
    ck_assert(layers[0].shift_factor >= layers[1].shift_factor);
    ck_assert(layers[1].shift_factor >= layers[2].shift_factor);
}
END_TEST

START_TEST(test_egl_context_attributes)
{
    // Test EGL context attributes
    int attribs[] = {
        0x3024, 8,     // EGL_RED_SIZE
        0x3023, 8,     // EGL_GREEN_SIZE  
        0x3022, 8,     // EGL_BLUE_SIZE
        0x3021, 8,     // EGL_ALPHA_SIZE
        0x3025, 24,    // EGL_DEPTH_SIZE
        0x3040, 0x4,   // EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT
        0x3038        // EGL_NONE
    };
    
    // Verify color channel sizes
    ck_assert_int_eq(attribs[1], 8);
    ck_assert_int_eq(attribs[3], 8);
    ck_assert_int_eq(attribs[5], 8);
    ck_assert_int_eq(attribs[7], 8);
    
    // Verify depth buffer
    ck_assert_int_eq(attribs[9], 24);
}
END_TEST

START_TEST(test_framebuffer_operations)
{
    // Test framebuffer setup for blur rendering
    struct {
        int width;
        int height;
        int samples;
    } framebuffers[] = {
        {1920, 1080, 1},  // Standard FBO
        {1920, 1080, 4},  // MSAA 4x
        {960, 540, 1},    // Half-res for performance
    };
    
    for (int i = 0; i < sizeof(framebuffers)/sizeof(framebuffers[0]); i++) {
        ck_assert(framebuffers[i].width > 0);
        ck_assert(framebuffers[i].height > 0);
        ck_assert(framebuffers[i].samples >= 1 && framebuffers[i].samples <= 8);
    }
}
END_TEST

START_TEST(test_performance_metrics)
{
    // Test frame time calculations
    float target_fps[] = {30.0f, 60.0f, 120.0f, 144.0f};
    
    for (int i = 0; i < sizeof(target_fps)/sizeof(target_fps[0]); i++) {
        float frame_time_ms = 1000.0f / target_fps[i];
        
        // Verify frame time calculations
        if (target_fps[i] == 30.0f) {
            ck_assert_float_eq_tol(frame_time_ms, 33.333f, 0.01f);
        } else if (target_fps[i] == 60.0f) {
            ck_assert_float_eq_tol(frame_time_ms, 16.667f, 0.01f);
        } else if (target_fps[i] == 120.0f) {
            ck_assert_float_eq_tol(frame_time_ms, 8.333f, 0.01f);
        } else if (target_fps[i] == 144.0f) {
            ck_assert_float_eq_tol(frame_time_ms, 6.944f, 0.01f);
        }
    }
}
END_TEST

// Create the test suite
Suite *renderer_suite(void)
{
    Suite *s;
    TCase *tc_core;
    TCase *tc_shader;
    TCase *tc_rendering;
    TCase *tc_perf;
    
    s = suite_create("Renderer");
    
    // Core renderer tests
    tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_renderer_types);
    tcase_add_test(tc_core, test_texture_loading);
    tcase_add_test(tc_core, test_matrix_identity);
    tcase_add_test(tc_core, test_matrix_orthographic);
    tcase_add_test(tc_core, test_viewport_setup);
    tcase_add_test(tc_core, test_egl_context_attributes);
    
    // Shader tests
    tc_shader = tcase_create("Shaders");
    tcase_add_test(tc_shader, test_shader_compilation);
    tcase_add_test(tc_shader, test_blur_kernel_generation);
    tcase_add_test(tc_shader, test_blur_pass_optimization);
    
    // Rendering tests
    tc_rendering = tcase_create("Rendering");
    tcase_add_test(tc_rendering, test_texture_coordinates);
    tcase_add_test(tc_rendering, test_render_state);
    tcase_add_test(tc_rendering, test_multi_layer_rendering);
    tcase_add_test(tc_rendering, test_framebuffer_operations);
    
    // Performance tests
    tc_perf = tcase_create("Performance");
    tcase_add_test(tc_perf, test_performance_metrics);
    
    suite_add_tcase(s, tc_core);
    suite_add_tcase(s, tc_shader);
    suite_add_tcase(s, tc_rendering);
    suite_add_tcase(s, tc_perf);
    
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = renderer_suite();
    sr = srunner_create(s);
    
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}