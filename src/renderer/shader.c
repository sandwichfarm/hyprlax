/*
 * shader.c - Shader management implementation
 *
 * Handles shader compilation, linking, and uniform management for OpenGL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include "../include/shader.h"
#include "../include/hyprlax_internal.h"

/* Built-in shader sources */
const char *shader_vertex_basic =
    "precision highp float;\n"
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

const char *shader_fragment_basic =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    "    vec4 color = texture2D(u_texture, v_texcoord);\n"
    "    // Premultiply alpha for correct blending\n"
    "    float final_alpha = color.a * u_opacity;\n"
    "    gl_FragColor = vec4(color.rgb * final_alpha, final_alpha);\n"
    "}\n";

/* Shader constants */
#define BLUR_KERNEL_SIZE 5.0f
#define BLUR_WEIGHT_FALLOFF 0.15f
#define SHADER_BUFFER_SIZE 2048

/* Blur shader template */
static const char *shader_fragment_blur_template =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_blur_amount;\n"
    "\n"
    "void main() {\n"
    "    vec2 texel_size = 1.0 / u_resolution;\n"
    "    vec4 result = vec4(0.0);\n"
    "    float total_weight = 0.0;\n"
    "    float blur_size = u_blur_amount * %.1f;\n"  /* BLUR_KERNEL_SIZE */
    "    \n"
    "    for (float x = -blur_size; x <= blur_size; x += 1.0) {\n"
    "        for (float y = -blur_size; y <= blur_size; y += 1.0) {\n"
    "            vec2 offset = vec2(x, y) * texel_size;\n"
    "            float distance = length(offset);\n"
    "            float weight = exp(-distance * distance / (2.0 * %.3f * %.3f));\n"  /* BLUR_WEIGHT_FALLOFF */
    "            result += texture2D(u_texture, v_texcoord + offset) * weight;\n"
    "            total_weight += weight;\n"
    "        }\n"
    "    }\n"
    "    \n"
    "    result /= total_weight;\n"
    "    float final_alpha = result.a * u_opacity;\n"
    "    gl_FragColor = vec4(result.rgb * final_alpha, final_alpha);\n"
    "}\n";

/* Create a new shader program */
shader_program_t* shader_create_program(const char *name) {
    shader_program_t *program = calloc(1, sizeof(shader_program_t));
    if (!program) return NULL;

    program->name = name ? strdup(name) : strdup("unnamed");
    program->id = 0;
    program->compiled = false;

    return program;
}

/* Destroy shader program */
void shader_destroy_program(shader_program_t *program) {
    if (!program) return;

    if (program->id) {
        glDeleteProgram(program->id);
    }

    if (program->name) {
        free(program->name);
    }

    free(program);
}

/* Compile shader from source */
static GLuint compile_shader(const char *source, GLenum type) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char *info_log = malloc(info_len);
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            fprintf(stderr, "Shader compilation failed: %s\n", info_log);
            free(info_log);
        }

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

/* Compile and link shader program */
int shader_compile(shader_program_t *program,
                  const char *vertex_src,
                  const char *fragment_src) {
    if (!program || !vertex_src || !fragment_src) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }

    GLuint vertex_shader = compile_shader(vertex_src, GL_VERTEX_SHADER);
    if (!vertex_shader) {
        return HYPRLAX_ERROR_GL_INIT;
    }

    GLuint fragment_shader = compile_shader(fragment_src, GL_FRAGMENT_SHADER);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return HYPRLAX_ERROR_GL_INIT;
    }

    GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return HYPRLAX_ERROR_GL_INIT;
    }

    glAttachShader(prog, vertex_shader);
    glAttachShader(prog, fragment_shader);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);

    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char *info_log = malloc(info_len);
            glGetProgramInfoLog(prog, info_len, NULL, info_log);
            fprintf(stderr, "Program linking failed: %s\n", info_log);
            free(info_log);
        }

        glDeleteProgram(prog);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return HYPRLAX_ERROR_GL_INIT;
    }

    /* Clean up shaders (they're linked into the program now) */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    program->id = prog;
    program->compiled = true;

    return HYPRLAX_SUCCESS;
}

/* Compile blur shader with dynamic generation */
int shader_compile_blur(shader_program_t *program) {
    if (!program) return HYPRLAX_ERROR_INVALID_ARGS;

    /* Build the blur fragment shader dynamically */
    char *blur_fragment_src = shader_build_blur_fragment(5.0f, BLUR_KERNEL_SIZE);
    if (!blur_fragment_src) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }

    /* Compile with the basic vertex shader and blur fragment shader */
    int result = shader_compile(program, shader_vertex_basic, blur_fragment_src);

    free(blur_fragment_src);
    return result;
}

/* Use shader program */
void shader_use(const shader_program_t *program) {
    if (program && program->id) {
        glUseProgram(program->id);
    }
}

/* Set uniform float */
void shader_set_uniform_float(const shader_program_t *program,
                             const char *name, float value) {
    if (!program || !program->id || !name) return;

    GLint location = glGetUniformLocation(program->id, name);
    if (location != -1) {
        glUniform1f(location, value);
    }
}

/* Set uniform vec2 */
void shader_set_uniform_vec2(const shader_program_t *program,
                           const char *name, float x, float y) {
    if (!program || !program->id || !name) return;

    GLint location = glGetUniformLocation(program->id, name);
    if (location != -1) {
        glUniform2f(location, x, y);
    }
}

/* Set uniform int */
void shader_set_uniform_int(const shader_program_t *program,
                          const char *name, int value) {
    if (!program || !program->id || !name) return;

    GLint location = glGetUniformLocation(program->id, name);
    if (location != -1) {
        glUniform1i(location, value);
    }
}

/* Build dynamic blur shader */
char* shader_build_blur_fragment(float blur_amount, int kernel_size) {
    (void)kernel_size; /* Currently using fixed BLUR_KERNEL_SIZE */

    if (blur_amount <= 0.001f) {
        /* No blur needed, return basic shader */
        return strdup(shader_fragment_basic);
    }

    /* Allocate buffer for shader source */
    char *shader = malloc(SHADER_BUFFER_SIZE);
    if (!shader) return NULL;

    /* Build shader with specific blur parameters */
    snprintf(shader, SHADER_BUFFER_SIZE, shader_fragment_blur_template,
             BLUR_KERNEL_SIZE,      /* Kernel size */
             BLUR_WEIGHT_FALLOFF, BLUR_WEIGHT_FALLOFF);  /* Weight falloff */

    return shader;
}