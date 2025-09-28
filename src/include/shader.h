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
    /* Cached locations for hot path (filled after link) */
    int loc_pos_attrib;
    int loc_tex_attrib;
    int loc_u_texture;
    int loc_u_opacity;
    int loc_u_blur_amount;
    int loc_u_resolution;
    int loc_u_offset;
    int loc_u_mask_outside;
    bool cache_ready;
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
int shader_compile_separable_blur(shader_program_t *program);
int shader_compile_separable_blur_with_vertex(shader_program_t *program, const char *vertex_src);

void shader_use(const shader_program_t *program);
void shader_set_uniform_float(const shader_program_t *program,
                             const char *name, float value);
void shader_set_uniform_vec2(const shader_program_t *program,
                           const char *name, float x, float y);
void shader_set_uniform_int(const shader_program_t *program,
                          const char *name, int value);

/* Blur shader compile variants */
int shader_compile_blur_with_vertex(shader_program_t *program, const char *vertex_src);

/* Fast accessors for cached locations (returns -1 if not present) */
int shader_get_attrib_location(const shader_program_t *program, const char *name);
int shader_get_uniform_location(const shader_program_t *program, const char *name);

/* Get cached uniform locations */
shader_uniforms_t* shader_get_uniforms(shader_program_t *program);

/* Built-in shader sources */
extern const char *shader_vertex_basic;
extern const char *shader_vertex_basic_offset;
extern const char *shader_fragment_basic;
extern const char *shader_fragment_fill;
extern const char *shader_fragment_blur;

/* Shader builder for dynamic blur shaders */
char* shader_build_blur_fragment(float blur_amount, int kernel_size);

#endif /* HYPRLAX_SHADER_H */
