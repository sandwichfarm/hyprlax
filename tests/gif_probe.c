#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/vendor/gifdec.h"

#define EXPECTED_WIDTH 576
#define EXPECTED_HEIGHT 324
#define MAX_FRAMES 8
#define GUARD_BYTES 16

struct gif_metadata {
    int frame_count;
    uint16_t delays[MAX_FRAMES];
    uint8_t transparency[MAX_FRAMES];
};

static int report_failure(const char *path, const char *message)
{
    fprintf(stderr, "gif_probe: %s: %s\n", path, message);
    return -1;
}

static int render_is_bounded(gd_GIF *gif, uint8_t *buffer, size_t render_size)
{
    memset(buffer, 0xa5, render_size + GUARD_BYTES);
    gd_render_frame(gif, buffer);

    for (size_t index = render_size; index < render_size + GUARD_BYTES; index++) {
        if (buffer[index] != 0xa5) {
            return 0;
        }
    }
    return 1;
}

static int read_frames(
    gd_GIF *gif,
    const char *path,
    uint8_t *buffer,
    size_t render_size,
    struct gif_metadata *metadata,
    const struct gif_metadata *expected
)
{
    int result;
    int saw_transparency = 0;

    memset(metadata, 0, sizeof(*metadata));
    while ((result = gd_get_frame(gif)) == 1) {
        int index = metadata->frame_count;

        if (index >= MAX_FRAMES) {
            return report_failure(path, "frame count exceeds 8");
        }
        if (gif->gce.delay == 0) {
            return report_failure(path, "frame delay must be positive");
        }
        if (!render_is_bounded(gif, buffer, render_size)) {
            return report_failure(path, "frame render exceeded output buffer");
        }

        metadata->delays[index] = gif->gce.delay;
        metadata->transparency[index] = (uint8_t) gif->gce.transparency;
        saw_transparency |= gif->gce.transparency;
        metadata->frame_count++;
    }

    if (result < 0) {
        return report_failure(path, "gifdec rejected a frame");
    }
    if (metadata->frame_count == 0) {
        return report_failure(path, "GIF must contain at least one frame");
    }
    if (!saw_transparency) {
        return report_failure(path, "GIF must contain transparent frame metadata");
    }
    if (expected != NULL) {
        if (metadata->frame_count != expected->frame_count) {
            return report_failure(path, "rewind changed frame count");
        }
        for (int index = 0; index < metadata->frame_count; index++) {
            if (metadata->delays[index] != expected->delays[index]
                || metadata->transparency[index] != expected->transparency[index]) {
                return report_failure(path, "rewind changed frame metadata");
            }
        }
    }
    return 0;
}

static int probe_gif(const char *path)
{
    gd_GIF *gif = gd_open_gif(path);
    uint8_t *buffer;
    size_t render_size;
    struct gif_metadata first_pass;
    struct gif_metadata second_pass;
    int result = -1;

    if (gif == NULL) {
        return report_failure(path, "could not open GIF");
    }
    if (gif->width != EXPECTED_WIDTH || gif->height != EXPECTED_HEIGHT) {
        gd_close_gif(gif);
        return report_failure(path, "dimensions must be 576x324");
    }

    render_size = (size_t) gif->width * gif->height * 3;
    buffer = malloc(render_size + GUARD_BYTES);
    if (buffer == NULL) {
        gd_close_gif(gif);
        return report_failure(path, "could not allocate render buffer");
    }

    gif->loop_count = UINT16_MAX;
    if (read_frames(gif, path, buffer, render_size, &first_pass, NULL) != 0) {
        goto cleanup;
    }
    if (gif->loop_count == UINT16_MAX) {
        report_failure(path, "GIF is missing a Netscape loop extension");
        goto cleanup;
    }
    if (gif->loop_count != 0) {
        report_failure(path, "GIF must loop forever");
        goto cleanup;
    }

    gd_rewind(gif);
    gif->loop_count = UINT16_MAX;
    if (read_frames(gif, path, buffer, render_size, &second_pass, &first_pass) != 0) {
        goto cleanup;
    }
    if (gif->loop_count != 0) {
        report_failure(path, "rewind did not reread infinite loop metadata");
        goto cleanup;
    }

    printf(
        "gif_probe: %s: %dx%d, %d frame%s, render/rewind ok\n",
        path,
        gif->width,
        gif->height,
        first_pass.frame_count,
        first_pass.frame_count == 1 ? "" : "s"
    );
    result = 0;

cleanup:
    free(buffer);
    gd_close_gif(gif);
    return result;
}

int main(int argc, char **argv)
{
    int failed = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s GIF_PATH [GIF_PATH ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int index = 1; index < argc; index++) {
        if (probe_gif(argv[index]) != 0) {
            failed = 1;
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
