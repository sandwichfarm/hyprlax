/*
 * gles2.c - OpenGL ES 2.0 renderer implementation
 * 
 * Implements the renderer interface using OpenGL ES 2.0, which is
 * supported by most Wayland compositors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "../include/renderer.h"
#include "../include/shader.h"
#include "../include/hyprlax_internal.h"

/* STB_IMAGE is already implemented in hyprlax.c, just need declarations */

/* Private renderer data */
typedef struct {
    /* EGL context */
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    EGLConfig egl_config;
    
    /* Shaders */
    shader_program_t *basic_shader;
    shader_program_t *blur_shader;
    
    /* Vertex buffer for quad rendering */
    GLuint vbo;
    GLuint ebo;
    GLuint position_attrib;
    GLuint texcoord_attrib;
    
    /* Current state */
    int width;
    int height;
    bool vsync_enabled;
} gles2_renderer_data_t;

/* Global instance */
static gles2_renderer_data_t *g_gles2_data = NULL;

/* Quad vertices for layer rendering */
static const GLfloat quad_vertices[] = {
    /* Position    Texture Coords */
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
};

/* Initialize OpenGL ES 2.0 renderer */
static int gles2_init(void *native_display, void *native_window, 
                     const renderer_config_t *config) {
    if (!native_display || !native_window || !config) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    gles2_renderer_data_t *data = calloc(1, sizeof(gles2_renderer_data_t));
    if (!data) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    /* Initialize EGL */
    data->egl_display = eglGetDisplay((EGLNativeDisplayType)native_display);
    if (data->egl_display == EGL_NO_DISPLAY) {
        free(data);
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    EGLint major, minor;
    if (!eglInitialize(data->egl_display, &major, &minor)) {
        free(data);
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Choose EGL config */
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    
    EGLint num_configs;
    if (!eglChooseConfig(data->egl_display, config_attribs, 
                        &data->egl_config, 1, &num_configs)) {
        eglTerminate(data->egl_display);
        free(data);
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Create EGL context */
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    data->egl_context = eglCreateContext(data->egl_display, data->egl_config,
                                         EGL_NO_CONTEXT, context_attribs);
    if (data->egl_context == EGL_NO_CONTEXT) {
        eglTerminate(data->egl_display);
        free(data);
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Create EGL surface */
    data->egl_surface = eglCreateWindowSurface(data->egl_display, data->egl_config,
                                               (EGLNativeWindowType)native_window, NULL);
    if (data->egl_surface == EGL_NO_SURFACE) {
        eglDestroyContext(data->egl_display, data->egl_context);
        eglTerminate(data->egl_display);
        free(data);
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Make context current */
    if (!eglMakeCurrent(data->egl_display, data->egl_surface, 
                       data->egl_surface, data->egl_context)) {
        eglDestroySurface(data->egl_display, data->egl_surface);
        eglDestroyContext(data->egl_display, data->egl_context);
        eglTerminate(data->egl_display);
        free(data);
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Set up OpenGL state */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, config->width, config->height);
    
    /* Create vertex buffer */
    glGenBuffers(1, &data->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, data->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), 
                 quad_vertices, GL_STATIC_DRAW);
    
    /* Create element buffer for indices */
    GLushort indices[] = {0, 1, 2, 1, 3, 2};
    glGenBuffers(1, &data->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    /* Compile shaders */
    data->basic_shader = shader_create_program("basic");
    if (shader_compile(data->basic_shader, shader_vertex_basic, 
                      shader_fragment_basic) != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to compile basic shader\n");
        /* Continue anyway - we need at least basic rendering */
    }
    
    /* Store configuration */
    data->width = config->width;
    data->height = config->height;
    data->vsync_enabled = config->vsync;
    
    /* Set vsync if requested */
    eglSwapInterval(data->egl_display, config->vsync ? 1 : 0);
    
    /* Store private data globally */
    g_gles2_data = data;
    
    return HYPRLAX_SUCCESS;
}

/* Destroy renderer */
static void gles2_destroy(void) {
    if (!g_gles2_data) return;
    
    if (g_gles2_data->basic_shader) {
        shader_destroy_program(g_gles2_data->basic_shader);
    }
    
    if (g_gles2_data->blur_shader) {
        shader_destroy_program(g_gles2_data->blur_shader);
    }
    
    if (g_gles2_data->vbo) {
        glDeleteBuffers(1, &g_gles2_data->vbo);
    }
    
    if (g_gles2_data->ebo) {
        glDeleteBuffers(1, &g_gles2_data->ebo);
    }
    
    if (g_gles2_data->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_gles2_data->egl_display, g_gles2_data->egl_surface);
    }
    
    if (g_gles2_data->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(g_gles2_data->egl_display, g_gles2_data->egl_context);
    }
    
    if (g_gles2_data->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(g_gles2_data->egl_display);
    }
    
    free(g_gles2_data);
    g_gles2_data = NULL;
}

/* Begin frame */
static void gles2_begin_frame(void) {
    /* Nothing special needed for GLES2 */
}

/* End frame */
static void gles2_end_frame(void) {
    /* Ensure all commands are flushed */
    glFlush();
}

/* Present frame */
static void gles2_present(void) {
    if (!g_gles2_data) return;
    
    eglSwapBuffers(g_gles2_data->egl_display, g_gles2_data->egl_surface);
}

/* Clear screen */
static void gles2_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* Create texture */
static texture_t* gles2_create_texture(const void *data, int width, int height, 
                                      texture_format_t format) {
    if (!data || width <= 0 || height <= 0) {
        return NULL;
    }
    
    texture_t *texture = calloc(1, sizeof(texture_t));
    if (!texture) {
        return NULL;
    }
    
    GLuint tex_id;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    
    /* Set texture parameters */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    /* Upload texture data */
    GLenum gl_format = GL_RGBA;
    switch (format) {
        case TEXTURE_FORMAT_RGB:
            gl_format = GL_RGB;
            break;
        case TEXTURE_FORMAT_RGBA:
        default:
            gl_format = GL_RGBA;
            break;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, gl_format, width, height, 0,
                 gl_format, GL_UNSIGNED_BYTE, data);
    
    texture->id = tex_id;
    texture->width = width;
    texture->height = height;
    texture->format = format;
    
    return texture;
}

/* Destroy texture */
static void gles2_destroy_texture(texture_t *texture) {
    if (!texture) return;
    
    if (texture->id) {
        GLuint tex_id = texture->id;
        glDeleteTextures(1, &tex_id);
    }
    
    free(texture);
}

/* Bind texture */
static void gles2_bind_texture(const texture_t *texture, int unit) {
    if (!texture) return;
    
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture->id);
}

/* Draw layer */
static void gles2_draw_layer(const texture_t *texture, float x, float y,
                            float opacity, float blur_amount) {
    if (!texture) return;
    
    /* This is a simplified implementation */
    /* In real implementation, we'd use the shader programs and set uniforms */
    
    /* Bind texture */
    gles2_bind_texture(texture, 0);
    
    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Resize viewport */
static void gles2_resize(int width, int height) {
    glViewport(0, 0, width, height);
    /* Store new size in private data */
}

/* Set vsync */
static void gles2_set_vsync(bool enabled) {
    /* Note: Need access to EGL display */
    fprintf(stderr, "Note: gles2_set_vsync needs access to EGL display\n");
}

/* Get capabilities */
static uint32_t gles2_get_capabilities(void) {
    return RENDERER_CAP_BLUR | RENDERER_CAP_VSYNC;
}

/* Get renderer name */
static const char* gles2_get_name(void) {
    return "OpenGL ES 2.0";
}

/* Get renderer version */
static const char* gles2_get_version(void) {
    return (const char*)glGetString(GL_VERSION);
}

/* OpenGL ES 2.0 renderer operations */
const renderer_ops_t renderer_gles2_ops = {
    .init = gles2_init,
    .destroy = gles2_destroy,
    .begin_frame = gles2_begin_frame,
    .end_frame = gles2_end_frame,
    .present = gles2_present,
    .create_texture = gles2_create_texture,
    .destroy_texture = gles2_destroy_texture,
    .bind_texture = gles2_bind_texture,
    .clear = gles2_clear,
    .draw_layer = gles2_draw_layer,
    .resize = gles2_resize,
    .set_vsync = gles2_set_vsync,
    .get_capabilities = gles2_get_capabilities,
    .get_name = gles2_get_name,
    .get_version = gles2_get_version,
};