/*
 * headless_renderer.c - Minimal offscreen renderer for headless mode
 *
 * Provides a simple offscreen rendering context for generating frames
 * that are sent to external renderers instead of being displayed directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "../include/renderer.h"
#include "../include/hyprlax_internal.h"
#include "../include/shared_buffer.h"
#include "../include/log.h"

typedef struct {
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    EGLConfig egl_config;
    
    /* Framebuffer for offscreen rendering */
    GLuint framebuffer;
    GLuint color_texture;
    GLuint depth_renderbuffer;
    
    /* Shader program for texture rendering */
    GLuint shader_program;
    GLuint vertex_buffer;
    GLint u_matrix;
    GLint u_texture;
    GLint u_opacity;
    
    int width;
    int height;
} headless_renderer_data_t;

static headless_renderer_data_t *g_headless_renderer = NULL;

/* Simple vertex shader for texture rendering */
static const char *vertex_shader_src = 
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "uniform mat4 u_matrix;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(position, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/* Simple fragment shader for texture rendering */
static const char *fragment_shader_src = 
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    "    vec4 color = texture2D(u_texture, v_texcoord);\n"
    "    gl_FragColor = vec4(color.rgb, color.a * u_opacity);\n"
    "}\n";

/* Compile shader helper */
static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char *log = malloc(len);
            glGetShaderInfoLog(shader, len, NULL, log);
            LOG_ERROR("Shader compilation failed: %s", log);
            free(log);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Initialize headless renderer */
int headless_renderer_init(int width, int height) {
    if (g_headless_renderer) {
        return HYPRLAX_SUCCESS; /* Already initialized */
    }
    
    g_headless_renderer = calloc(1, sizeof(headless_renderer_data_t));
    if (!g_headless_renderer) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    g_headless_renderer->width = width;
    g_headless_renderer->height = height;
    
    /* Get EGL display */
    g_headless_renderer->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_headless_renderer->egl_display == EGL_NO_DISPLAY) {
        LOG_ERROR("Failed to get EGL display for headless renderer");
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_NO_DISPLAY;
    }
    
    /* Initialize EGL */
    EGLint major, minor;
    if (!eglInitialize(g_headless_renderer->egl_display, &major, &minor)) {
        LOG_ERROR("Failed to initialize EGL");
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Choose config for pbuffer */
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    
    EGLint num_configs;
    if (!eglChooseConfig(g_headless_renderer->egl_display, config_attribs,
                        &g_headless_renderer->egl_config, 1, &num_configs) || num_configs == 0) {
        LOG_ERROR("Failed to choose EGL config");
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Create context */
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    g_headless_renderer->egl_context = eglCreateContext(g_headless_renderer->egl_display,
                                                      g_headless_renderer->egl_config,
                                                      EGL_NO_CONTEXT, context_attribs);
    if (g_headless_renderer->egl_context == EGL_NO_CONTEXT) {
        LOG_ERROR("Failed to create EGL context");
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Create pbuffer surface */
    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };
    
    g_headless_renderer->egl_surface = eglCreatePbufferSurface(g_headless_renderer->egl_display,
                                                             g_headless_renderer->egl_config,
                                                             pbuffer_attribs);
    if (g_headless_renderer->egl_surface == EGL_NO_SURFACE) {
        LOG_ERROR("Failed to create pbuffer surface");
        eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Make context current */
    if (!eglMakeCurrent(g_headless_renderer->egl_display, g_headless_renderer->egl_surface,
                       g_headless_renderer->egl_surface, g_headless_renderer->egl_context)) {
        LOG_ERROR("Failed to make context current");
        eglDestroySurface(g_headless_renderer->egl_display, g_headless_renderer->egl_surface);
        eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Create framebuffer for rendering */
    glGenFramebuffers(1, &g_headless_renderer->framebuffer);
    glGenTextures(1, &g_headless_renderer->color_texture);
    glGenRenderbuffers(1, &g_headless_renderer->depth_renderbuffer);
    
    /* Setup color texture */
    glBindTexture(GL_TEXTURE_2D, g_headless_renderer->color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    /* Setup depth renderbuffer */
    glBindRenderbuffer(GL_RENDERBUFFER, g_headless_renderer->depth_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    
    /* Attach to framebuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_headless_renderer->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_headless_renderer->color_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                             g_headless_renderer->depth_renderbuffer);
    
    /* Check framebuffer status */
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Framebuffer incomplete: 0x%x", status);
        glDeleteFramebuffers(1, &g_headless_renderer->framebuffer);
        glDeleteTextures(1, &g_headless_renderer->color_texture);
        glDeleteRenderbuffers(1, &g_headless_renderer->depth_renderbuffer);
        eglDestroySurface(g_headless_renderer->egl_display, g_headless_renderer->egl_surface);
        eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    /* Compile and link shader program */
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    
    if (!vertex_shader || !fragment_shader) {
        LOG_ERROR("Failed to compile shaders");
        glDeleteFramebuffers(1, &g_headless_renderer->framebuffer);
        glDeleteTextures(1, &g_headless_renderer->color_texture);
        glDeleteRenderbuffers(1, &g_headless_renderer->depth_renderbuffer);
        eglDestroySurface(g_headless_renderer->egl_display, g_headless_renderer->egl_surface);
        eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    g_headless_renderer->shader_program = glCreateProgram();
    glAttachShader(g_headless_renderer->shader_program, vertex_shader);
    glAttachShader(g_headless_renderer->shader_program, fragment_shader);
    glBindAttribLocation(g_headless_renderer->shader_program, 0, "position");
    glBindAttribLocation(g_headless_renderer->shader_program, 1, "texcoord");
    glLinkProgram(g_headless_renderer->shader_program);
    
    GLint linked;
    glGetProgramiv(g_headless_renderer->shader_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOG_ERROR("Failed to link shader program");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        glDeleteProgram(g_headless_renderer->shader_program);
        glDeleteFramebuffers(1, &g_headless_renderer->framebuffer);
        glDeleteTextures(1, &g_headless_renderer->color_texture);
        glDeleteRenderbuffers(1, &g_headless_renderer->depth_renderbuffer);
        eglDestroySurface(g_headless_renderer->egl_display, g_headless_renderer->egl_surface);
        eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        eglTerminate(g_headless_renderer->egl_display);
        free(g_headless_renderer);
        g_headless_renderer = NULL;
        return HYPRLAX_ERROR_GL_INIT;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    /* Get uniform locations */
    g_headless_renderer->u_matrix = glGetUniformLocation(g_headless_renderer->shader_program, "u_matrix");
    g_headless_renderer->u_texture = glGetUniformLocation(g_headless_renderer->shader_program, "u_texture");
    g_headless_renderer->u_opacity = glGetUniformLocation(g_headless_renderer->shader_program, "u_opacity");
    
    /* Create vertex buffer with quad vertices */
    float vertices[] = {
        /* x, y, u, v */
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    
    glGenBuffers(1, &g_headless_renderer->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, g_headless_renderer->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    LOG_INFO("Headless renderer initialized (%dx%d)", width, height);
    
    /* Set viewport */
    glViewport(0, 0, width, height);
    
    /* Enable blending */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
    return HYPRLAX_SUCCESS;
}

/* Begin frame rendering */
void headless_renderer_begin_frame(void) {
    if (!g_headless_renderer) return;
    
    /* Bind framebuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_headless_renderer->framebuffer);
    glViewport(0, 0, g_headless_renderer->width, g_headless_renderer->height);
    
    /* Clear */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* End frame and capture to shared buffer */
shared_buffer_t* headless_renderer_capture_frame(void) {
    if (!g_headless_renderer) return NULL;
    
    /* Make sure rendering is complete */
    glFinish();
    
    /* Create shared buffer */
    shared_buffer_t *buffer = shared_buffer_create(g_headless_renderer->width,
                                                   g_headless_renderer->height,
                                                   GL_RGBA);
    if (!buffer) {
        LOG_ERROR("Failed to create shared buffer");
        return NULL;
    }
    
    /* Read pixels from framebuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_headless_renderer->framebuffer);
    glReadPixels(0, 0, g_headless_renderer->width, g_headless_renderer->height,
                GL_RGBA, GL_UNSIGNED_BYTE, buffer->pixels);
    
    /* Pixels already contain the rendered framebuffer from glReadPixels above */
    
    /* Update buffer header */
    buffer->header->frame_number++;
    
    return buffer;
}

/* Render a texture to the framebuffer */
void headless_renderer_render_texture(GLuint texture, float x, float y, float width, float height, float opacity) {
    if (!g_headless_renderer) return;
    
    /* Use shader program */
    glUseProgram(g_headless_renderer->shader_program);
    
    /* Set up orthographic projection matrix */
    float matrix[16] = {
        width/g_headless_renderer->width, 0, 0, 0,
        0, height/g_headless_renderer->height, 0, 0,
        0, 0, 1, 0,
        x/g_headless_renderer->width, y/g_headless_renderer->height, 0, 1
    };
    
    glUniformMatrix4fv(g_headless_renderer->u_matrix, 1, GL_FALSE, matrix);
    glUniform1i(g_headless_renderer->u_texture, 0);
    glUniform1f(g_headless_renderer->u_opacity, opacity);
    
    /* Bind texture */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    /* Set up vertex attributes */
    glBindBuffer(GL_ARRAY_BUFFER, g_headless_renderer->vertex_buffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    /* Draw quad */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    /* Clean up */
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

/* Cleanup plugin renderer */
void headless_renderer_destroy(void) {
    if (!g_headless_renderer) return;
    
    if (g_headless_renderer->framebuffer) {
        glDeleteFramebuffers(1, &g_headless_renderer->framebuffer);
    }
    if (g_headless_renderer->color_texture) {
        glDeleteTextures(1, &g_headless_renderer->color_texture);
    }
    if (g_headless_renderer->depth_renderbuffer) {
        glDeleteRenderbuffers(1, &g_headless_renderer->depth_renderbuffer);
    }
    
    if (g_headless_renderer->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_headless_renderer->egl_display, EGL_NO_SURFACE, 
                      EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_headless_renderer->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(g_headless_renderer->egl_display, g_headless_renderer->egl_surface);
        }
        if (g_headless_renderer->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(g_headless_renderer->egl_display, g_headless_renderer->egl_context);
        }
        eglTerminate(g_headless_renderer->egl_display);
    }
    
    free(g_headless_renderer);
    g_headless_renderer = NULL;
}
