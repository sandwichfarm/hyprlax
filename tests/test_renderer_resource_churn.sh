#!/bin/sh
set -eu

FILE="src/renderer/gles2.c"

awk '
    /^static void gles2_fade_frame/ {
        fn = "gles2_fade_frame";
    }
    /^static void gles2_draw_layer_internal/ {
        fn = "gles2_draw_layer_internal";
    }
    /^static void compute_fit_params/ || /^static void gles2_draw_layer\(/ {
        fn = "";
    }
    fn && /glGenBuffers/ {
        printf("%s:%d: glGenBuffers in hot render path (%s)\n", FILENAME, FNR, fn);
        bad = 1;
    }
    END {
        exit bad;
    }
' "$FILE"
