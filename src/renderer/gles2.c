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

    /* Current surface for multi-monitor support */
    EGLSurface current_surface;

    /* Shaders */
    shader_program_t *basic_shader;
    shader_program_t *blur_shader;
    shader_program_t *blur_sep_shader;

    /* Vertex buffer for quad rendering */
    GLuint vbo;
    GLuint ebo;
    GLuint position_attrib;
    GLuint texcoord_attrib;

    /* Current state */
    int width;
    int height;
    bool vsync_enabled;
    /* Separable blur target */
    GLuint blur_fbo;
    GLuint blur_tex;
    int blur_downscale; /* 0/1 = full res; >1 = downscale factor */
    int blur_w;
    int blur_h;
} gles2_renderer_data_t;

/* Global instance */
static gles2_renderer_data_t *g_gles2_data = NULL;
static void gles2_create_blur_target(int width, int height);

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
    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Compiling basic shader\n");
    }
    data->basic_shader = shader_create_program("basic");
    const char *use_uniform_offset = getenv("HYPRLAX_UNIFORM_OFFSET");
    const char *vertex_src = (use_uniform_offset && *use_uniform_offset) ?
                              shader_vertex_basic_offset : shader_vertex_basic;
    if (shader_compile(data->basic_shader, vertex_src,
                      shader_fragment_basic) != HYPRLAX_SUCCESS) {
        fprintf(stderr, "Failed to compile basic shader\n");
        /* Continue anyway - we need at least basic rendering */
    } else if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Basic shader compiled successfully, id=%u\n", data->basic_shader->id);
    }

    /* Compile blur shader */
    if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Compiling blur shader\n");
    }
    const char *use_separable = getenv("HYPRLAX_SEPARABLE_BLUR");
    const char *down_env = getenv("HYPRLAX_BLUR_DOWNSCALE");
    if (down_env && *down_env) {
        int f = atoi(down_env);
        if (f > 1 && f < 16) data->blur_downscale = f; /* sanity bounds */
    }
    if (use_separable && *use_separable) {
        data->blur_sep_shader = shader_create_program("blur_separable");
        /* Always compile separable blur with offset-capable vertex shader so u_offset is available */
        if (shader_compile_separable_blur_with_vertex(data->blur_sep_shader, shader_vertex_basic_offset) != HYPRLAX_SUCCESS) {
            fprintf(stderr, "Warning: Failed to compile separable blur shader\n");
            shader_destroy_program(data->blur_sep_shader);
            data->blur_sep_shader = NULL;
        } else if (getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] Separable blur shader compiled successfully, id=%u\n",
                    data->blur_sep_shader->id);
        }
        if (data->blur_sep_shader) {
            gles2_create_blur_target(config->width, config->height);
        }
    }

    /* Legacy single-pass blur as fallback */
    data->blur_shader = shader_create_program("blur");
    if ((use_uniform_offset && *use_uniform_offset) ?
        (shader_compile_blur_with_vertex(data->blur_shader, shader_vertex_basic_offset) != HYPRLAX_SUCCESS) :
        (shader_compile_blur(data->blur_shader) != HYPRLAX_SUCCESS)) {
        shader_destroy_program(data->blur_shader);
        data->blur_shader = NULL;
    } else if (getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Blur shader compiled successfully, id=%u\n",
                data->blur_shader->id);
    }

    /* Store configuration */
    data->width = config->width;
    data->height = config->height;
    data->vsync_enabled = config->vsync;

    /* Set vsync if requested (default off to prevent GPU blocking when idle) */
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
    if (g_gles2_data->blur_sep_shader) {
        shader_destroy_program(g_gles2_data->blur_sep_shader);
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

    if (g_gles2_data->blur_tex) {
        glDeleteTextures(1, &g_gles2_data->blur_tex);
    }
    if (g_gles2_data->blur_fbo) {
        glDeleteFramebuffers(1, &g_gles2_data->blur_fbo);
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

    /* Use current surface for multi-monitor support */
    EGLSurface surface = g_gles2_data->current_surface ?
                        g_gles2_data->current_surface :
                        g_gles2_data->egl_surface;

    /* Allow skipping glFinish via env for performance testing */
    const char *no_finish = getenv("HYPRLAX_NO_GLFINISH");
    if (!no_finish || strcmp(no_finish, "0") == 0) {
        glFinish();
    }
    eglSwapBuffers(g_gles2_data->egl_display, surface);
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

    /* Track last active unit and bound texture to avoid redundant state changes */
    enum { MAX_TRACKED_UNITS = 8 };
    static int s_active_unit = -1;
    static GLuint s_bound_tex[MAX_TRACKED_UNITS] = {0};

    if (unit < 0 || unit >= MAX_TRACKED_UNITS) unit = 0;

    if (s_active_unit != unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        s_active_unit = unit;
    }
    if (s_bound_tex[unit] != texture->id) {
        glBindTexture(GL_TEXTURE_2D, texture->id);
        s_bound_tex[unit] = texture->id;
    }
}

/* Draw layer */
static void gles2_draw_layer(const texture_t *texture, float x, float y,
                            float opacity, float blur_amount) {
    static int draw_count = 0;
    if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] gles2_draw_layer %d: tex=%u, x=%.3f, opacity=%.3f, blur=%.3f\n",
                draw_count, texture ? texture->id : 0, x, opacity, blur_amount);
    }

    if (!texture || !g_gles2_data || !g_gles2_data->basic_shader) {
        if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] gles2_draw_layer: Missing %s\n",
                    !texture ? "texture" : !g_gles2_data ? "gles2_data" : "shader");
        }
        draw_count++;
        return;
    }

    /* Setup vertices for a fullscreen quad */
    GLfloat vertices[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,  /* Bottom-left */
         1.0f, -1.0f,  1.0f, 1.0f,  /* Bottom-right */
        -1.0f,  1.0f,  0.0f, 0.0f,  /* Top-left */
         1.0f,  1.0f,  1.0f, 0.0f   /* Top-right */
    };

    /* Adjust coordinates based on mode */
    const char *use_uniform_offset = getenv("HYPRLAX_UNIFORM_OFFSET");
    /* Don't mutate texcoords when using separable blur; offsets will be applied via uniforms */
    /* no-op placeholder removed */
    if (0) {}

    if (!(use_uniform_offset && *use_uniform_offset) /* legacy single-pass path, updated below after sep check */) {
        /* Legacy path: encode offset into texcoords */
        vertices[2] = x;          /* Bottom-left U */
        vertices[3] = 1.0f - y;   /* Bottom-left V (inverted) */
        vertices[6] = 1.0f + x;   /* Bottom-right U */
        vertices[7] = 1.0f - y;   /* Bottom-right V (inverted) */
        vertices[10] = x;         /* Top-left U */
        vertices[11] = 0.0f - y;  /* Top-left V (inverted) */
        vertices[14] = 1.0f + x;  /* Top-right U */
        vertices[15] = 0.0f - y;  /* Top-right V (inverted) */
    }

    /* Choose shader based on blur amount */
    shader_program_t *shader = g_gles2_data->basic_shader;
    int use_sep_blur = 0;
    if (blur_amount > 0.01f) {
        if (g_gles2_data->blur_sep_shader && g_gles2_data->blur_fbo && getenv("HYPRLAX_SEPARABLE_BLUR")) {
            shader = g_gles2_data->blur_sep_shader;
            use_sep_blur = 1;
            /* Re-initialize vertices to default texcoords for separable path */
            GLfloat full_vertices[] = {
                -1.0f, -1.0f,  0.0f, 1.0f,
                 1.0f, -1.0f,  1.0f, 1.0f,
                -1.0f,  1.0f,  0.0f, 0.0f,
                 1.0f,  1.0f,  1.0f, 0.0f,
            };
            memcpy(vertices, full_vertices, sizeof(full_vertices));
        } else if (g_gles2_data->blur_shader) {
            shader = g_gles2_data->blur_shader;
        }
        if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
            fprintf(stderr, "[DEBUG] Using %s blur (amount=%.3f)\n",
                    use_sep_blur ? "separable" : (shader == g_gles2_data->blur_shader ? "single-pass" : "none"),
                    blur_amount);
        }
    }

    /* Use selected shader */
    shader_use(shader);

    /* Set sampler uniform only when program changes */
    {
        static uint32_t s_sampler_prog = 0;
        if (shader && shader->id != s_sampler_prog) {
            GLint loc = shader_get_uniform_location(shader, "u_texture");
            if (loc != -1) {
                glUniform1i(loc, 0);
            }
            s_sampler_prog = shader ? shader->id : 0;
        }
    }

    /* Set uniforms */
    shader_set_uniform_float(shader, "u_opacity", opacity);

    /* Legacy blur uniforms */
    if (shader == g_gles2_data->blur_shader) {
        shader_set_uniform_float(shader, "u_blur_amount", blur_amount);
        shader_set_uniform_vec2(shader, "u_resolution",
                               (float)g_gles2_data->width, (float)g_gles2_data->height);
    }

    /* Separable blur path: two passes (horizontal to FBO, vertical to default) */
    if (use_sep_blur) {
        GLint loc_amt = shader_get_uniform_location(shader, "u_blur_amount");
        if (loc_amt != -1) glUniform1f(loc_amt, blur_amount);
        GLint loc_res = shader_get_uniform_location(shader, "u_resolution");
        /* First pass samples the source layer texture: use texture resolution */
        if (loc_res != -1) glUniform2f(loc_res, (float)texture->width, (float)texture->height);
        GLint loc_dir = shader_get_uniform_location(shader, "u_direction");
        /* Always apply layer offset via u_offset for separable blur pass 1 */
        {
            GLint u_off = shader_get_uniform_location(shader, "u_offset");
            if (u_off != -1) glUniform2f(u_off, x, -y);
        }
        /* Save viewport and blend state */
        GLint prev_viewport[4];
        glGetIntegerv(GL_VIEWPORT, prev_viewport);
        GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
        if (blend_was_enabled) glDisable(GL_BLEND);

        /* First pass: horizontal to downscaled FBO (always use default texcoords + u_offset) */
        if (loc_dir != -1) glUniform2f(loc_dir, 1.0f, 0.0f);
        glBindFramebuffer(GL_FRAMEBUFFER, g_gles2_data->blur_fbo);
        glViewport(0, 0, g_gles2_data->blur_w, g_gles2_data->blur_h);
        gles2_bind_texture(texture, 0);

        /* Setup vertex data */
        const char *persist_vbo = getenv("HYPRLAX_PERSISTENT_VBO");
        GLuint vbo = 0;
        if (persist_vbo && *persist_vbo && g_gles2_data->vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, g_gles2_data->vbo);
            /* For separable path, keep default texcoords; offsets via u_offset */
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        } else {
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        }

        GLint pos_attrib = shader_get_attrib_location(shader, "a_position");
        GLint tex_attrib = shader_get_attrib_location(shader, "a_texcoord");
        if (pos_attrib >= 0) {
            glEnableVertexAttribArray(pos_attrib);
            glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        }
        if (tex_attrib >= 0) {
            glEnableVertexAttribArray(tex_attrib);
            glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        /* Second pass: vertical to default framebuffer (upsampling) */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        /* Restore full-screen viewport & blend before drawing to default framebuffer */
        glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
        if (blend_was_enabled) glEnable(GL_BLEND);
        /* Vertical sampling resolution: use downscaled if enabled, else screen */
        if (loc_res != -1) {
            if (g_gles2_data->blur_downscale > 1)
                glUniform2f(loc_res, (float)g_gles2_data->blur_w, (float)g_gles2_data->blur_h);
            else
                glUniform2f(loc_res, (float)g_gles2_data->width, (float)g_gles2_data->height);
        }
        if (loc_dir != -1) glUniform2f(loc_dir, 0.0f, 1.0f);
        /* Ensure we don't apply layer offset again on the second pass */
        {
            GLint u_off = shader_get_uniform_location(shader, "u_offset");
            if (u_off != -1) glUniform2f(u_off, 0.0f, 0.0f);
        }
        texture_t tmp = { .id = g_gles2_data->blur_tex };
        gles2_bind_texture(&tmp, 0);
        /* Rebind vertex data and attrib pointers to be explicit for second pass */
        if (persist_vbo && *persist_vbo && g_gles2_data->vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, g_gles2_data->vbo);
            /* For separable path, sample the FBO texture; if orientation is inverted on your stack,
               flip V here to compensate. We'll flip V to 1.0 - V for the second pass. */
            GLfloat full_vertices_flip_v[] = {
                -1.0f, -1.0f,  0.0f, 0.0f,  /* Bottom-left maps to V=0 */
                 1.0f, -1.0f,  1.0f, 0.0f,
                -1.0f,  1.0f,  0.0f, 1.0f,  /* Top-left maps to V=1 */
                 1.0f,  1.0f,  1.0f, 1.0f,
            };
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(full_vertices_flip_v), full_vertices_flip_v);
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            GLfloat full_vertices_flip_v[] = {
                -1.0f, -1.0f,  0.0f, 0.0f,
                 1.0f, -1.0f,  1.0f, 0.0f,
                -1.0f,  1.0f,  0.0f, 1.0f,
                 1.0f,  1.0f,  1.0f, 1.0f,
            };
            glBufferData(GL_ARRAY_BUFFER, sizeof(full_vertices_flip_v), full_vertices_flip_v, GL_STATIC_DRAW);
        }
        if (pos_attrib >= 0) {
            glEnableVertexAttribArray(pos_attrib);
            glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        }
        if (tex_attrib >= 0) {
            glEnableVertexAttribArray(tex_attrib);
            glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (pos_attrib >= 0) glDisableVertexAttribArray(pos_attrib);
        if (tex_attrib >= 0) glDisableVertexAttribArray(tex_attrib);
        if (!(persist_vbo && *persist_vbo)) {
            glDeleteBuffers(1, &vbo);
        }

        goto done;
    }

    /* Bind texture (sampler already set to unit 0 if needed) */
    gles2_bind_texture(texture, 0);

    /* If uniform-offset mode, set u_offset */
    if (use_uniform_offset && *use_uniform_offset) {
        GLint u_off = shader_get_uniform_location(shader, "u_offset");
        if (u_off != -1) {
            glUniform2f(u_off, x, -y);
        }
    }

    /* Setup vertex data */
    const char *persist_vbo = getenv("HYPRLAX_PERSISTENT_VBO");
    GLuint vbo = 0;
    if (persist_vbo && *persist_vbo && g_gles2_data->vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g_gles2_data->vbo);
        /* If using uniform-offset or separable blur, keep default texcoords; otherwise update texcoords */
        if (!(use_uniform_offset && *use_uniform_offset) && !use_sep_blur) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        }
    } else {
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }

    GLint pos_attrib = shader_get_attrib_location(shader, "a_position");
    GLint tex_attrib = shader_get_attrib_location(shader, "a_texcoord");

    if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] Attrib locations: pos=%d, tex=%d\n", pos_attrib, tex_attrib);
    }

    if (pos_attrib >= 0) {
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
    }

    if (tex_attrib >= 0) {
        glEnableVertexAttribArray(tex_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
    }

    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Check for GL errors */
    if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr, "[DEBUG] GL Error after draw: 0x%x\n", err);
        }
    }

    /* Cleanup */
    if (pos_attrib >= 0) glDisableVertexAttribArray(pos_attrib);
    if (tex_attrib >= 0) glDisableVertexAttribArray(tex_attrib);
    if (!(persist_vbo && *persist_vbo)) {
        glDeleteBuffers(1, &vbo);
    }

    if (draw_count < 5 && getenv("HYPRLAX_DEBUG")) {
        fprintf(stderr, "[DEBUG] gles2_draw_layer %d: Complete\n", draw_count);
    }
done:
    draw_count++;
}

/* Resize viewport */
static void gles2_resize(int width, int height) {
    glViewport(0, 0, width, height);
    /* Store new size in private data */
    if (g_gles2_data) {
        g_gles2_data->width = width;
        g_gles2_data->height = height;
        if (g_gles2_data->blur_sep_shader) {
            gles2_create_blur_target(width, height);
        }
    }
}

/* Set vsync */
static void gles2_set_vsync(bool enabled) {
    if (g_gles2_data && g_gles2_data->egl_display != EGL_NO_DISPLAY) {
        eglSwapInterval(g_gles2_data->egl_display, enabled ? 1 : 0);
        g_gles2_data->vsync_enabled = enabled;
    }
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

/* Create EGL surface for a monitor */
EGLSurface gles2_create_monitor_surface(void *native_window) {
    if (!g_gles2_data || !native_window) {
        return EGL_NO_SURFACE;
    }

    EGLSurface surface = eglCreateWindowSurface(g_gles2_data->egl_display,
                                                g_gles2_data->egl_config,
                                                (EGLNativeWindowType)native_window,
                                                NULL);
    return surface;
}

/* Make a monitor's EGL surface current */
int gles2_make_current(EGLSurface surface) {
    if (!g_gles2_data) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    if (!eglMakeCurrent(g_gles2_data->egl_display, surface, surface, g_gles2_data->egl_context)) {
        return HYPRLAX_ERROR_GL_INIT;
    }

    /* Track current surface for present */
    g_gles2_data->current_surface = surface;

    return HYPRLAX_SUCCESS;
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
/* Create or recreate separable blur render target */
static void gles2_create_blur_target(int width, int height) {
    if (!g_gles2_data) return;
    if (!g_gles2_data->blur_fbo) {
        glGenFramebuffers(1, &g_gles2_data->blur_fbo);
    }
    if (g_gles2_data->blur_tex) {
        glDeleteTextures(1, &g_gles2_data->blur_tex);
        g_gles2_data->blur_tex = 0;
    }
    glGenTextures(1, &g_gles2_data->blur_tex);
    glBindTexture(GL_TEXTURE_2D, g_gles2_data->blur_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    int factor = g_gles2_data->blur_downscale > 1 ? g_gles2_data->blur_downscale : 1;
    g_gles2_data->blur_w = width / factor;
    g_gles2_data->blur_h = height / factor;
    if (g_gles2_data->blur_w < 1) g_gles2_data->blur_w = 1;
    if (g_gles2_data->blur_h < 1) g_gles2_data->blur_h = 1;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_gles2_data->blur_w, g_gles2_data->blur_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gles2_data->blur_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_gles2_data->blur_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
