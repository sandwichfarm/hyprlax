/*
 * main.c - Application entry point
 * 
 * Simple entry point that uses the modular hyprlax system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "include/hyprlax.h"
#include "include/hyprlax_internal.h"

/* Global context for signal handling */
static hyprlax_context_t *g_ctx = NULL;

/* Signal handler for clean shutdown */
static void signal_handler(int sig) {
    (void)sig;
    if (g_ctx) {
        g_ctx->running = false;
    }
}

int main(int argc, char **argv) {
    /* Check for help/version early */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS] [--layer <image:shift:opacity:blur>...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -h, --help                Show this help message\n");
            printf("  -v, --version             Show version information\n");
            printf("  -f, --fps <rate>          Target FPS (default: 60)\n");
            printf("  -s, --shift <pixels>      Shift amount per workspace (default: 150)\n");
            printf("  -d, --duration <seconds>  Animation duration (default: 1.0)\n");
            printf("  -e, --easing <type>       Easing function (default: cubic)\n");
            printf("  -c, --config <file>       Load configuration from file\n");
            printf("  -D, --debug               Enable debug output\n");
            printf("  -r, --renderer <backend>  Renderer backend (gles2, auto)\n");
            printf("  -p, --platform <backend>  Platform backend (wayland, x11, auto)\n");
            printf("  -C, --compositor <backend> Compositor (hyprland, sway, generic, auto)\n");
            printf("\nEasing types:\n");
            printf("  linear, quad, cubic, quart, quint, sine, expo, circ,\n");
            printf("  back, elastic, bounce, snap\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("hyprlax %s\n", HYPRLAX_VERSION);
            printf("Buttery-smooth parallax wallpaper daemon with support for multiple compositors, platforms and renderers\n");
            return 0;
        }
    }
    
    /* Create application context */
    hyprlax_context_t *ctx = hyprlax_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create application context\n");
        return 1;
    }
    
    /* Set up signal handlers */
    g_ctx = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize application */
    int ret = hyprlax_init(ctx, argc, argv);
    if (ret != 0) {
        hyprlax_destroy(ctx);
        return 1;
    }
    
    /* Run main loop */
    ret = hyprlax_run(ctx);
    
    /* Clean up */
    hyprlax_destroy(ctx);
    g_ctx = NULL;
    
    return ret;
}