/*
 * main.c - Application entry point
 * 
 * Simple entry point that uses the modular hyprlax system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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
    /* Ensure stdout/stderr are valid - reopen to /dev/null ONLY if closed */
    /* Check if file descriptors are actually closed (fcntl will fail with EBADF) */
    if (fcntl(STDOUT_FILENO, F_GETFD) == -1 && errno == EBADF) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            if (fd != STDOUT_FILENO) {
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }
    }
    
    if (fcntl(STDERR_FILENO, F_GETFD) == -1 && errno == EBADF) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            if (fd != STDERR_FILENO) {
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
    }
    
    /* Log startup immediately to see if we're even running */
    FILE *startup_log = fopen("/tmp/hyprlax-exec.log", "a");
    if (startup_log) {
        fprintf(startup_log, "[%ld] hyprlax started with %d args\n", (long)time(NULL), argc);
        for (int i = 0; i < argc; i++) {
            fprintf(startup_log, "  arg[%d]: %s\n", i, argv[i]);
        }
        fprintf(startup_log, "  stdin: %s\n", isatty(0) ? "tty" : "not-tty");
        fprintf(startup_log, "  stdout: %s\n", isatty(1) ? "tty" : "not-tty");
        fprintf(startup_log, "  stderr: %s\n", isatty(2) ? "tty" : "not-tty");
        fclose(startup_log);
    }
    
    /* Check for ctl subcommand first */
    if (argc >= 2 && strcmp(argv[1], "ctl") == 0) {
        return hyprlax_ctl_main(argc - 1, argv + 1);
    }
    
    /* Check for help/version early */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS] [--layer <image:shift:opacity:blur>...]\n", argv[0]);
            printf("       %s ctl <command> [args...]\n", argv[0]);
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
            printf("\nMulti-monitor options:\n");
            printf("  --primary-only            Only use primary monitor\n");
            printf("  --monitor <name>          Use specific monitor(s)\n");
            printf("  --disable-monitor <name>  Exclude specific monitor\n");
            printf("\nControl Commands:\n");
            printf("  ctl add <image> [shift] [opacity] [blur]  Add a layer\n");
            printf("  ctl remove <id>                           Remove a layer\n");
            printf("  ctl modify <id> <property> <value>        Modify a layer\n");
            printf("  ctl list                                  List all layers\n");
            printf("  ctl clear                                 Clear all layers\n");
            printf("  ctl set <property> <value>                Set runtime property\n");
            printf("  ctl get <property>                        Get runtime property\n");
            printf("  ctl status                                Show daemon status\n");
            printf("  ctl reload                                Reload configuration\n");
            printf("\nRuntime Properties:\n");
            printf("  fps, shift, duration, easing, blur_passes, blur_size, debug\n");
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
    fprintf(stderr, "[MAIN] Creating application context\n");
    hyprlax_context_t *ctx = hyprlax_create();
    if (!ctx) {
        fprintf(stderr, "[MAIN] Failed to create application context\n");
        return 1;
    }
    
    /* Set up signal handlers */
    fprintf(stderr, "[MAIN] Setting up signal handlers\n");
    g_ctx = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize application */
    fprintf(stderr, "[MAIN] Starting initialization\n");
    int ret = hyprlax_init(ctx, argc, argv);
    if (ret != 0) {
        fprintf(stderr, "[MAIN] Initialization failed with code %d\n", ret);
        hyprlax_destroy(ctx);
        return 1;
    }
    fprintf(stderr, "[MAIN] Initialization complete\n");
    
    /* Run main loop */
    ret = hyprlax_run(ctx);
    
    /* Clean up */
    hyprlax_destroy(ctx);
    g_ctx = NULL;
    
    return ret;
}