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
#include "../include/defaults.h"

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
    "uniform vec2 u_mask_outside;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_tint_strength;\n"
    "void main() {\n"
    "    if ((u_mask_outside.x > 0.5 && (v_texcoord.x < 0.0 || v_texcoord.x > 1.0)) ||\n"
    "        (u_mask_outside.y > 0.5 && (v_texcoord.y < 0.0 || v_texcoord.y > 1.0))) discard;\n"
    "    vec4 color = texture2D(u_texture, v_texcoord);\n"
    "    vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
    "    vec3 rgb = color.rgb * effective;\n"
    "    // Premultiply alpha for correct blending\n"
    "    float final_alpha = color.a * u_opacity;\n"
    "    gl_FragColor = vec4(rgb * final_alpha, final_alpha);\n"
    "}\n";

/* Solid color fragment shader (for fullscreen fades/trails) */
const char *shader_fragment_fill =
    "precision highp float;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

/* Variant with texcoord offset uniform in vertex shader */
const char *shader_vertex_basic_offset =
    "precision highp float;\n"
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "uniform vec2 u_offset;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord + u_offset;\n"
    "}\n";

/* Shader constants */
#define BLUR_KERNEL_SIZE HYPRLAX_BLUR_KERNEL_SIZE
#define BLUR_WEIGHT_FALLOFF HYPRLAX_BLUR_WEIGHT_FALLOFF
#define SHADER_BUFFER_SIZE HYPRLAX_SHADER_BUFFER_SIZE

/* Blur shader template */
static const char *shader_fragment_blur_template =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_blur_amount;\n"
    "uniform vec2 u_mask_outside;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_tint_strength;\n"
    "\n"
    "void main() {\n"
    "    if ((u_mask_outside.x > 0.5 && (v_texcoord.x < 0.0 || v_texcoord.x > 1.0)) ||\n"
    "        (u_mask_outside.y > 0.5 && (v_texcoord.y < 0.0 || v_texcoord.y > 1.0))) discard;\n"
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
    "    vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
    "    vec3 rgb = result.rgb * effective;\n"
    "    float final_alpha = result.a * u_opacity;\n"
    "    gl_FragColor = vec4(rgb * final_alpha, final_alpha);\n"
    "}\n";

/* Separable blur fragment shader (directional) */
static const char *shader_fragment_blur_separable =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_blur_amount;\n"
    "uniform vec2 u_direction;\n"
    "uniform vec2 u_mask_outside;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_tint_strength;\n"
    "\n"
    "void main() {\n"
    "    if ((u_mask_outside.x > 0.5 && (v_texcoord.x < 0.0 || v_texcoord.x > 1.0)) ||\n"
    "        (u_mask_outside.y > 0.5 && (v_texcoord.y < 0.0 || v_texcoord.y > 1.0))) discard;\n"
    "    vec2 texel = 1.0 / u_resolution;\n"
    "    float spread = max(u_blur_amount, 0.001);\n"
    "    vec4 sum = vec4(0.0);\n"
    "    float total = 0.0;\n"
    "    float sigma = 2.0;\n"
    "    float denom = 2.0 * sigma * sigma;\n"
    "    for (int i = -4; i <= 4; i++) {\n"
    "        float fi = float(i) * spread;\n"
    "        float w = exp(-(fi*fi) / denom);\n"
    "        sum += texture2D(u_texture, v_texcoord + u_direction * texel * fi) * w;\n"
    "        total += w;\n"
    "    }\n"
    "    vec4 result = sum / total;\n"
    "    vec3 effective = mix(vec3(1.0), u_tint, clamp(u_tint_strength, 0.0, 1.0));\n"
    "    vec3 rgb = result.rgb * effective;\n"
    "    float final_alpha = result.a * u_opacity;\n"
    "    gl_FragColor = vec4(rgb * final_alpha, final_alpha);\n"
    "}\n";

/* Create a new shader program */
shader_program_t* shader_create_program(const char *name) {
    shader_program_t *program = calloc(1, sizeof(shader_program_t));
    if (!program) return NULL;

    program->name = name ? strdup(name) : strdup("unnamed");
    program->id = 0;
    program->compiled = false;
    program->cache_ready = false;
    program->loc_pos_attrib = -1;
    program->loc_tex_attrib = -1;
    program->loc_u_texture = -1;
    program->loc_u_opacity = -1;
    program->loc_u_blur_amount = -1;
    program->loc_u_resolution = -1;

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
    /* Populate cached locations for common uniforms/attributes */
    program->loc_pos_attrib = glGetAttribLocation(program->id, "a_position");
    program->loc_tex_attrib = glGetAttribLocation(program->id, "a_texcoord");
    program->loc_u_texture = glGetUniformLocation(program->id, "u_texture");
    program->loc_u_opacity = glGetUniformLocation(program->id, "u_opacity");
    program->loc_u_blur_amount = glGetUniformLocation(program->id, "u_blur_amount");
    program->loc_u_resolution = glGetUniformLocation(program->id, "u_resolution");
    program->loc_u_offset = glGetUniformLocation(program->id, "u_offset");
    program->loc_u_mask_outside = glGetUniformLocation(program->id, "u_mask_outside");
    program->cache_ready = true;

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

/* Compile separable blur shader */
int shader_compile_separable_blur(shader_program_t *program) {
    if (!program) return HYPRLAX_ERROR_INVALID_ARGS;
    return shader_compile(program, shader_vertex_basic, shader_fragment_blur_separable);
}

int shader_compile_separable_blur_with_vertex(shader_program_t *program, const char *vertex_src) {
    if (!program || !vertex_src) return HYPRLAX_ERROR_INVALID_ARGS;
    return shader_compile(program, vertex_src, shader_fragment_blur_separable);
}

/* Use shader program */
void shader_use(const shader_program_t *program) {
    static uint32_t s_last_program = 0;
    uint32_t id = (program && program->id) ? program->id : 0;
    if (id == s_last_program) {
        return; /* avoid redundant glUseProgram */
    }
    glUseProgram(id);
    s_last_program = id;
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

/* Fast accessors */
int shader_get_attrib_location(const shader_program_t *program, const char *name) {
    if (!program || !program->id || !name) return -1;
    if (program->cache_ready) {
        if (strcmp(name, "a_position") == 0) return program->loc_pos_attrib;
        if (strcmp(name, "a_texcoord") == 0) return program->loc_tex_attrib;
    }
    return glGetAttribLocation(program->id, name);
}

int shader_get_uniform_location(const shader_program_t *program, const char *name) {
    if (!program || !program->id || !name) return -1;
    if (program->cache_ready) {
        if (strcmp(name, "u_texture") == 0) return program->loc_u_texture;
        if (strcmp(name, "u_opacity") == 0) return program->loc_u_opacity;
        if (strcmp(name, "u_blur_amount") == 0) return program->loc_u_blur_amount;
        if (strcmp(name, "u_resolution") == 0) return program->loc_u_resolution;
        if (strcmp(name, "u_offset") == 0) return program->loc_u_offset;
    }
    return glGetUniformLocation(program->id, name);
}

/* Compile blur shader with a provided vertex shader */
int shader_compile_blur_with_vertex(shader_program_t *program, const char *vertex_src) {
    if (!program) return HYPRLAX_ERROR_INVALID_ARGS;
    char *blur_fragment_src = shader_build_blur_fragment(5.0f, BLUR_KERNEL_SIZE);
    if (!blur_fragment_src) return HYPRLAX_ERROR_NO_MEMORY;
    int result = shader_compile(program, vertex_src, blur_fragment_src);
    free(blur_fragment_src);
    return result;
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
