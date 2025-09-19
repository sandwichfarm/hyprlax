/*
 * shader.h - Shader management interface
 *
 * Handles shader compilation, linking, and uniform management.
 */

#ifndef HYPRLAX_SHADER_H
#define HYPRLAX_SHADER_H

#include <stdbool.h>
#include <stdint.h>

/* Shader types */
typedef enum {
    SHADER_TYPE_VERTEX,
    SHADER_TYPE_FRAGMENT,
    SHADER_TYPE_COMPUTE,  /* For future use */
} shader_type_t;

/* Shader program handle */
typedef struct shader_program {
    uint32_t id;
    char *name;
    bool compiled;
} shader_program_t;

/* Uniform location cache */
typedef struct {
    int position;
    int texcoord;
    int u_texture;
    int u_opacity;
    int u_blur_amount;
    int u_resolution;
    int u_offset;
} shader_uniforms_t;

/* Shader management functions */
shader_program_t* shader_create_program(const char *name);
void shader_destroy_program(shader_program_t *program);

int shader_compile(shader_program_t *program,
                  const char *vertex_src,
                  const char *fragment_src);

int shader_compile_blur(shader_program_t *program);

void shader_use(const shader_program_t *program);
void shader_set_uniform_float(const shader_program_t *program,
                             const char *name, float value);
void shader_set_uniform_vec2(const shader_program_t *program,
                           const char *name, float x, float y);
void shader_set_uniform_int(const shader_program_t *program,
                          const char *name, int value);

/* Get cached uniform locations */
shader_uniforms_t* shader_get_uniforms(shader_program_t *program);

/* Built-in shader sources */
extern const char *shader_vertex_basic;
extern const char *shader_fragment_basic;
extern const char *shader_fragment_blur;

/* Shader builder for dynamic blur shaders */
char* shader_build_blur_fragment(float blur_amount, int kernel_size);

#endif /* HYPRLAX_SHADER_H */