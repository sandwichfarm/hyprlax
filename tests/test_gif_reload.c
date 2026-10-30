/* Exercise the production loader and decoders without a compositor or GPU. */
#include <check.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/include/hyprlax.h"
#include "../src/vendor/gifdec.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../src/stb_image.h"

static char fixture_dir[] = "/tmp/hyprlax-gif-reload-XXXXXX";
static const char *fixture_names[] = {
    "animated.gif", "legacy.gif", "local.gif", "bad-eof.gif", "bad-trailer.gif", "red.png",
    "no-palette.gif"
};
static unsigned char live_textures[64], texture_pixels[64][4];
static GLuint next_texture, bound_texture;
static int decoder_calls, fail_call, fail_result, decoder_closes;
static int comment_calls, plain_text_calls, application_calls;
static struct block_case {
    const char *name;
    size_t length;
    int patch_offset;
    unsigned char patch_value;
} block_cases[32];
static size_t block_case_count;

int __real_gd_get_frame(gd_GIF *gif);
void __real_gd_close_gif(gd_GIF *gif);

int __wrap_gd_get_frame(gd_GIF *gif) {
    decoder_calls++;
    ck_assert_msg(decoder_calls < 100, "loader kept decoding after an error");
    if (decoder_calls == fail_call) return fail_result;
    return __real_gd_get_frame(gif);
}

void __wrap_gd_close_gif(gd_GIF *gif) {
    decoder_closes++;
    __real_gd_close_gif(gif);
}

void glGenTextures(GLsizei count, GLuint *textures) {
    for (int i = 0; i < count; i++) {
        ck_assert_uint_lt(next_texture, 64);
        textures[i] = next_texture++;
        live_textures[textures[i]] = 1;
    }
}

void glDeleteTextures(GLsizei count, const GLuint *textures) {
    for (int i = 0; i < count; i++) {
        if (textures[i] == 0) continue;
        ck_assert_uint_lt(textures[i], 64);
        ck_assert_msg(live_textures[textures[i]], "texture deleted twice");
        live_textures[textures[i]] = 0;
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    ck_assert_uint_eq(target, GL_TEXTURE_2D);
    bound_texture = texture;
}

void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                 GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    (void)level;
    (void)internal_format;
    (void)border;
    ck_assert_uint_eq(target, GL_TEXTURE_2D);
    ck_assert_int_eq(width, 1);
    ck_assert_int_eq(height, 1);
    ck_assert_uint_eq(format, GL_RGBA);
    ck_assert_uint_eq(type, GL_UNSIGNED_BYTE);
    ck_assert_ptr_nonnull(pixels);
    ck_assert(live_textures[bound_texture]);
    memcpy(texture_pixels[bound_texture], pixels, 4);
}

void glGenerateMipmap(GLenum target) { (void)target; }
void glTexParameteri(GLenum target, GLenum name, GLint value) {
    (void)target;
    (void)name;
    (void)value;
}

static void fixture_path(char path[256], int index) {
    snprintf(path, 256, "%s/%s", fixture_dir, fixture_names[index]);
}

static void fixture_check(int success) {
    if (!success) {
        perror("GIF reload fixture");
        exit(EXIT_FAILURE);
    }
}

static void write_gif(int index, int legacy, int local, int malformed) {
    char path[256];
    fixture_path(path, index);
    FILE *file = fopen(path, "wb");
    fixture_check(file != NULL);
    unsigned char header[] = {
        'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0
    };
    unsigned char palette[] = {0, 0, 0, 255, 0, 0};
    unsigned char descriptor[] = {0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0};
    const unsigned char data[] = {2, 2, 0x4c, 0x01, 0};
    if (legacy) header[4] = '7';
    if (local) header[10] = 0;
    fwrite(header, 1, sizeof(header), file);
    if (!local) fwrite(palette, 1, sizeof(palette), file);
    for (int frame = 0; frame < (legacy || malformed ? 1 : 2); frame++) {
        unsigned char control[] = {0x21, 0xf9, 4, 0, 5, 0, 0, 0};
        control[4] += frame * 5;
        if (!legacy) fwrite(control, 1, sizeof(control), file);
        descriptor[9] = local == 1 ? 0x80 : 0;
        fwrite(descriptor, 1, sizeof(descriptor), file);
        if (local == 1) {
            palette[3] = frame ? 0 : 255;
            palette[4] = frame ? 255 : 0;
            fwrite(palette, 1, sizeof(palette), file);
        }
        fwrite(data, 1, sizeof(data), file);
    }
    if (malformed) fputc(0, file);
    if (malformed != 1) fputc(';', file);
    fixture_check(fclose(file) == 0);
}

static void block_fixture_path(char path[256], int index) {
    snprintf(path, 256, "%s/blocks-%s.gif", fixture_dir,
             index < 0 ? "valid" : block_cases[index].name);
}

static void create_block_fixtures(void) {
    char path[256];
    fixture_path(path, 0);
    FILE *file = fopen(path, "rb");
    unsigned char original[66], bytes[256];
    fixture_check(file != NULL);
    fixture_check(fread(original, 1, sizeof(original), file) == sizeof(original));
    fixture_check(fclose(file) == 0);
    /* Insert extensions between the two complete frames. */
    size_t length = 42;
    memcpy(bytes, original, length);
    const unsigned char comment[] = {0x21, 0xfe, 3, 'a', 'b', 'c', 2, 'd', 'e', 0};
    const unsigned char plain[] = {
        0x21, 1, 12, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 'x', 0
    };
    const unsigned char app[] = {
        0x21, 0xff, 11, 'T', 'E', 'S', 'T', 'A', 'P', 'P', 'L', '0', '0', '1',
        3, 'a', 'p', 'p', 2, 'o', 'k', 0
    };
    const unsigned char netscape[] = {
        0x21, 0xff, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
        3, 1, 7, 0, 0
    };
    size_t comment_start = length;
    memcpy(bytes + length, comment, sizeof(comment));
    length += sizeof(comment);
    size_t plain_start = length;
    memcpy(bytes + length, plain, sizeof(plain));
    length += sizeof(plain);
    size_t app_start = length;
    memcpy(bytes + length, app, sizeof(app));
    length += sizeof(app);
    size_t netscape_start = length;
    memcpy(bytes + length, netscape, sizeof(netscape));
    length += sizeof(netscape);
    memcpy(bytes + length, original + 42, sizeof(original) - 42);
    length += sizeof(original) - 42;

    struct block_case cases[] = {
        {"image-size", 38, -1, 0},
        {"image-payload", 40, -1, 0},
        {"image-terminator", 41, -1, 0},
        {"extension-label", comment_start + 1, -1, 0},
        {"comment-size", comment_start + 2, -1, 0},
        {"comment-payload", comment_start + 4, -1, 0},
        {"comment-terminator", comment_start + sizeof(comment) - 1, -1, 0},
        {"plain-header", plain_start + 5, -1, 0},
        {"plain-payload", plain_start + 16, -1, 0},
        {"plain-terminator", plain_start + sizeof(plain) - 1, -1, 0},
        {"plain-header-size", length, (int)plain_start + 2, 11},
        {"app-header", app_start + 8, -1, 0},
        {"app-payload", app_start + 16, -1, 0},
        {"app-terminator", app_start + sizeof(app) - 1, -1, 0},
        {"app-header-size", length, (int)app_start + 2, 10},
        {"netscape-header", netscape_start + 8, -1, 0},
        {"netscape-size", netscape_start + 14, -1, 0},
        {"netscape-payload", netscape_start + 17, -1, 0},
        {"netscape-terminator", netscape_start + sizeof(netscape) - 1, -1, 0},
        {"netscape-block-size", length, (int)netscape_start + 14, 2},
        {"netscape-block-id", length, (int)netscape_start + 15, 2},
        {"netscape-bad-terminator", length, (int)netscape_start + 18, 1},
        {"gce-header", 21, -1, 0},
        {"gce-fields", 24, -1, 0},
        {"gce-terminator", 26, -1, 0},
        {"gce-header-size", length, 21, 3},
        {"gce-bad-terminator", length, 26, 1},
    };
    block_case_count = sizeof(cases) / sizeof(cases[0]);
    memcpy(block_cases, cases, sizeof(cases));
    for (int index = -1; index < (int)block_case_count; index++) {
        unsigned char fixture[sizeof(bytes)];
        memcpy(fixture, bytes, length);
        size_t output_length = index < 0 ? length : block_cases[index].length;
        if (index >= 0 && block_cases[index].patch_offset >= 0) {
            fixture[block_cases[index].patch_offset] = block_cases[index].patch_value;
        }
        block_fixture_path(path, index);
        file = fopen(path, "wb");
        fixture_check(file != NULL);
        fixture_check(fwrite(fixture, 1, output_length, file) == output_length);
        fixture_check(fclose(file) == 0);
    }
}

static void create_fixtures(void) {
    fixture_check(mkdtemp(fixture_dir) != NULL);
    write_gif(0, 0, 0, 0);
    write_gif(1, 1, 0, 0);
    write_gif(2, 0, 1, 0);
    write_gif(3, 0, 0, 1);
    write_gif(4, 0, 0, 2);
    write_gif(6, 0, 2, 0);
    const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0,
        0x1f, 0x15, 0xc4, 0x89, 0, 0, 0, 0x0d, 0x49, 0x44, 0x41, 0x54,
        0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0, 5, 0, 1, 0xff,
        0x89, 0x99, 0x3d, 0x1d, 0, 0, 0, 0, 0x49, 0x45, 0x4e, 0x44,
        0xae, 0x42, 0x60, 0x82
    };
    char path[256];
    fixture_path(path, 5);
    FILE *file = fopen(path, "wb");
    fixture_check(file != NULL);
    fixture_check(fwrite(png, 1, sizeof(png), file) == sizeof(png));
    fixture_check(fclose(file) == 0);
    create_block_fixtures();
}

static void reset_state(void) {
    memset(live_textures, 0, sizeof(live_textures));
    memset(texture_pixels, 0, sizeof(texture_pixels));
    next_texture = 1;
    bound_texture = 0;
    decoder_calls = fail_call = fail_result = decoder_closes = 0;
    comment_calls = plain_text_calls = application_calls = 0;
}

static int live_count(void) {
    int count = 0;
    for (int i = 0; i < 64; i++) count += live_textures[i];
    return count;
}

static void load_fixture(hyprlax_context_t *ctx, parallax_layer_t *layer, int index) {
    char path[256];
    fixture_path(path, index);
    layer->image_path = strdup(path);
    ck_assert_ptr_nonnull(layer->image_path);
    ck_assert_int_eq(hyprlax_load_layer_texture(ctx, layer), 1);
}

static void release_layer(parallax_layer_t *layer) {
    hyprlax_release_layer_textures(layer);
    free(layer->image_path);
    layer->image_path = NULL;
}

START_TEST(test_malformed_gif_preserves_layer) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    load_fixture(&ctx, &layer, 0);
    parallax_layer_t original = layer;
    char path[256];
    fixture_path(path, _i == 2 ? 6 : 3 + _i);
    decoder_calls = decoder_closes = 0;
    ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), -1);
    ck_assert_mem_eq(&layer, &original, sizeof(layer));
    ck_assert_int_eq(live_count(), 2);
    if (_i != 2) ck_assert_int_eq(decoder_closes, 1);
    release_layer(&layer);
    ck_assert_int_eq(live_count(), 0);
}
END_TEST

START_TEST(test_malformed_gif_startup_stays_empty) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    char path[256];
    fixture_path(path, _i == 2 ? 6 : 3 + _i);
    layer.image_path = path;
    ck_assert_int_eq(hyprlax_load_layer_texture(&ctx, &layer), 0);
    ck_assert_uint_eq(layer.texture_id, 0);
    ck_assert_ptr_null(layer.gif_data);
    ck_assert_int_eq(live_count(), 0);
    if (_i != 2) ck_assert_int_eq(decoder_closes, 1);
}
END_TEST

START_TEST(test_second_pass_failure_cleans_partial_load) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    load_fixture(&ctx, &layer, 0);
    parallax_layer_t original = layer;
    decoder_calls = decoder_closes = 0;
    /* Fail after one uploaded frame, or when checking the final trailer. */
    fail_call = _i < 2 ? 5 : 6;
    fail_result = _i % 2 ? (_i < 2 ? 0 : 1) : -1;
    ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, layer.image_path), -1);
    ck_assert_mem_eq(&layer, &original, sizeof(layer));
    ck_assert_uint_eq(next_texture, _i < 2 ? 4 : 5);
    ck_assert_int_eq(live_count(), 2);
    ck_assert_int_eq(decoder_closes, 1);
    release_layer(&layer);
    ck_assert_int_eq(live_count(), 0);
}
END_TEST

START_TEST(test_compatible_gif_formats) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    char path[256];
    int local = _i % 2;
    fixture_path(path, 1 + local);
    /* These are formats accepted by the previous runtime STB loader. */
    int width, height;
    GLuint old_texture = load_texture(path, &width, &height);
    ck_assert_uint_ne(old_texture, 0);
    glDeleteTextures(1, &old_texture);
    if (_i < 2) {
        load_fixture(&ctx, &layer, 1 + local);
    } else {
        load_fixture(&ctx, &layer, 5);
        GLuint previous = layer.texture_id;
        ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), 0);
        ck_assert_int_eq(live_textures[previous], 0);
    }
    ck_assert(layer.is_gif);
    ck_assert_int_eq(layer.frame_count, local ? 2 : 1);
    ck_assert_int_eq(layer.width, 1);
    ck_assert_int_eq(layer.height, 1);
    const unsigned char red[] = {255, 0, 0, 255};
    ck_assert_mem_eq(texture_pixels[layer.gif_textures[0]], red, 4);
    if (local) {
        const unsigned char green[] = {0, 255, 0, 255};
        ck_assert_mem_eq(texture_pixels[layer.gif_textures[1]], green, 4);
        ck_assert_int_eq(layer.gif_delays[0], 50);
        ck_assert_int_eq(layer.gif_delays[1], 100);
    }
    release_layer(&layer);
    ck_assert_int_eq(live_count(), 0);
}
END_TEST

START_TEST(test_png_gif_reload_ownership) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    load_fixture(&ctx, &layer, 5);
    GLuint png_texture = layer.texture_id;
    char path[256];
    fixture_path(path, 0);
    ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), 0);
    ck_assert(layer.is_gif);
    ck_assert_int_eq(layer.frame_count, 2);
    ck_assert_int_eq(live_textures[png_texture], 0);
    ck_assert_int_eq(live_count(), 2);
    GLuint first = layer.gif_textures[0], second = layer.gif_textures[1];
    layer.current_frame = 1;
    layer.texture_id = second;
    fixture_path(path, 5);
    ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), 0);
    ck_assert(!layer.is_gif);
    ck_assert_int_eq(live_textures[first], 0);
    ck_assert_int_eq(live_textures[second], 0);
    ck_assert_int_eq(live_count(), 1);
    release_layer(&layer);
    hyprlax_release_layer_textures(&layer);
    ck_assert_int_eq(live_count(), 0);
}
END_TEST

START_TEST(test_malformed_subblocks_preserve_layer) {
    hyprlax_context_t ctx = {0};
    parallax_layer_t layer = {0};
    load_fixture(&ctx, &layer, 0);
    parallax_layer_t original = layer;
    char path[256];
    block_fixture_path(path, _i);
    decoder_calls = decoder_closes = 0;
    ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), -1);
    ck_assert_mem_eq(&layer, &original, sizeof(layer));
    ck_assert_int_eq(live_count(), 2);
    ck_assert_int_eq(decoder_closes, 1);
    release_layer(&layer);
    ck_assert_int_eq(live_count(), 0);
}
END_TEST

static void read_callback_block(gd_GIF *gif, const char *expected) {
    unsigned char size, bytes[255];
    ck_assert_int_eq(read(gif->fd, &size, 1), 1);
    ck_assert_uint_eq(size, strlen(expected));
    ck_assert_int_eq(read(gif->fd, bytes, size), size);
    ck_assert_mem_eq(bytes, expected, size);
}

static void comment_callback(gd_GIF *gif) {
    comment_calls++;
    read_callback_block(gif, "abc");
}

static void plain_text_callback(gd_GIF *gif, uint16_t tx, uint16_t ty,
                                uint16_t tw, uint16_t th, uint8_t cw, uint8_t ch,
                                uint8_t fg, uint8_t bg) {
    plain_text_calls++;
    ck_assert_int_eq(tx, 0);
    ck_assert_int_eq(ty, 0);
    ck_assert_int_eq(tw, 1);
    ck_assert_int_eq(th, 1);
    ck_assert_int_eq(cw, 1);
    ck_assert_int_eq(ch, 1);
    ck_assert_int_eq(fg, 1);
    ck_assert_int_eq(bg, 0);
    read_callback_block(gif, "x");
}

static void application_callback(gd_GIF *gif, char id[8], char auth[3]) {
    application_calls++;
    ck_assert_mem_eq(id, "TESTAPPL", 8);
    ck_assert_mem_eq(auth, "001", 3);
    read_callback_block(gif, "app");
}

START_TEST(test_valid_subblocks_and_callbacks) {
    char path[256];
    block_fixture_path(path, -1);
    const unsigned char red[] = {255, 0, 0, 255};
    if (!_i) {
        hyprlax_context_t ctx = {0};
        parallax_layer_t layer = {0};
        load_fixture(&ctx, &layer, 5);
        ck_assert_int_eq(hyprlax_reload_layer_texture(&ctx, &layer, path), 0);
        ck_assert_int_eq(layer.frame_count, 2);
        for (int frame = 0; frame < 2; frame++) {
            ck_assert_mem_eq(texture_pixels[layer.gif_textures[frame]], red, 4);
            ck_assert_int_eq(layer.gif_delays[frame], (frame + 1) * 50);
        }
        ck_assert_int_eq(((gd_GIF *)layer.gif_data)->loop_count, 7);
        release_layer(&layer);
        ck_assert_int_eq(live_count(), 0);
    } else {
        gd_GIF *gif = gd_open_gif(path);
        ck_assert_ptr_nonnull(gif);
        gif->comment = comment_callback;
        gif->plain_text = plain_text_callback;
        gif->application = application_callback;
        for (int frame = 0; frame < 2; frame++) {
            unsigned char pixel[3];
            ck_assert_int_eq(gd_get_frame(gif), 1);
            gd_render_frame(gif, pixel);
            ck_assert_mem_eq(pixel, red, 3);
            ck_assert_int_eq(gif->gce.delay, (frame + 1) * 5);
        }
        ck_assert_int_eq(gd_get_frame(gif), 0);
        ck_assert_int_eq(gif->loop_count, 7);
        ck_assert_int_eq(comment_calls, 1);
        ck_assert_int_eq(plain_text_calls, 1);
        ck_assert_int_eq(application_calls, 1);
        gd_close_gif(gif);
    }
}
END_TEST

int main(void) {
    create_fixtures();
    Suite *suite = suite_create("GIF layer reload");
    TCase *test_case = tcase_create("production loader");
    tcase_set_timeout(test_case, 3);
    tcase_add_checked_fixture(test_case, reset_state, NULL);
    tcase_add_loop_test(test_case, test_malformed_gif_preserves_layer, 0, 3);
    tcase_add_loop_test(test_case, test_malformed_gif_startup_stays_empty, 0, 3);
    tcase_add_loop_test(test_case, test_second_pass_failure_cleans_partial_load, 0, 4);
    tcase_add_loop_test(test_case, test_compatible_gif_formats, 0, 4);
    tcase_add_test(test_case, test_png_gif_reload_ownership);
    tcase_add_loop_test(test_case, test_malformed_subblocks_preserve_layer, 0, block_case_count);
    tcase_add_loop_test(test_case, test_valid_subblocks_and_callbacks, 0, 2);
    suite_add_tcase(suite, test_case);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    for (size_t i = 0; i < sizeof(fixture_names) / sizeof(fixture_names[0]); i++) {
        char path[256];
        fixture_path(path, i);
        unlink(path);
    }
    for (int i = -1; i < (int)block_case_count; i++) {
        char path[256];
        block_fixture_path(path, i);
        unlink(path);
    }
    rmdir(fixture_dir);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
