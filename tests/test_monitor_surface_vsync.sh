#!/bin/sh
set -eu

file="src/renderer/gles2.c"

awk '
    /^int gles2_make_current\(EGLSurface surface\)/ { in_function = 1 }
    in_function && /eglSwapInterval\(g_gles2_data->egl_display, 0\)/ { found = 1 }
    in_function && /^\/\* Destroy a monitor.s EGL surface/ { in_function = 0 }
    END {
        if (!found) {
            print "monitor EGL surfaces do not receive nonblocking swap policy"
            exit 1
        }
    }
' "$file"
