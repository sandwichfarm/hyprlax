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
    /* Workaround for exec-once redirecting to /dev/null */
    /* Some libraries don't work properly when stderr is /dev/null */
    char target[256];
    ssize_t len;
    
    /* Check if stderr points to /dev/null */
    len = readlink("/proc/self/fd/2", target, sizeof(target)-1);
    if (len > 0) {
        target[len] = '\0';
        if (strcmp(target, "/dev/null") == 0) {
            /* Redirect stderr to a log file instead of /dev/null */
            int fd = open("/tmp/hyprlax-stderr.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
    }
    
    /* Log startup immediately to see if we're even running */
    FILE *startup_log = fopen("/tmp/hyprlax-exec.log", "a");
    if (startup_log) {
        fprintf(startup_log, "\n[%ld] === HYPRLAX STARTUP ===\n", (long)time(NULL));
        fprintf(startup_log, "  argc: %d\n", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(startup_log, "  arg[%d]: %s\n", i, argv[i]);
        }
        fprintf(startup_log, "  stdin: %s\n", isatty(0) ? "tty" : "not-tty");
        fprintf(startup_log, "  stdout: %s\n", isatty(1) ? "tty" : "not-tty");
        fprintf(startup_log, "  stderr: %s\n", isatty(2) ? "tty" : "not-tty");
        fprintf(startup_log, "  WAYLAND_DISPLAY: %s\n", getenv("WAYLAND_DISPLAY") ?: "NOT SET");
        fprintf(startup_log, "  XDG_RUNTIME_DIR: %s\n", getenv("XDG_RUNTIME_DIR") ?: "NOT SET");
        fprintf(startup_log, "  HYPRLAND_INSTANCE_SIGNATURE: %s\n", getenv("HYPRLAND_INSTANCE_SIGNATURE") ?: "NOT SET");
        fflush(startup_log);
    }
    
    /* Check for ctl subcommand first */
    if (argc >= 2 && strcmp(argv[1], "ctl") == 0) {
        if (startup_log) {
            fprintf(startup_log, "  Running ctl subcommand\n");
            fclose(startup_log);
        }
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
    if (startup_log) {
        fprintf(startup_log, "[MAIN] Creating application context\n");
        fflush(startup_log);
    }
    hyprlax_context_t *ctx = hyprlax_create();
    if (!ctx) {
        if (startup_log) {
            fprintf(startup_log, "[MAIN] ERROR: Failed to create application context\n");
            fclose(startup_log);
        }
        return 1;
    }
    
    /* Set up signal handlers */
    if (startup_log) {
        fprintf(startup_log, "[MAIN] Setting up signal handlers\n");
        fflush(startup_log);
    }
    g_ctx = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  /* Ignore SIGPIPE like bash does */
    
    /* Initialize application */
    if (startup_log) {
        fprintf(startup_log, "[MAIN] Starting initialization\n");
        fflush(startup_log);
    }
    int ret = hyprlax_init(ctx, argc, argv);
    if (ret != 0) {
        if (startup_log) {
            fprintf(startup_log, "[MAIN] ERROR: Initialization failed with code %d\n", ret);
            fclose(startup_log);
        }
        hyprlax_destroy(ctx);
        return 1;
    }
    if (startup_log) {
        fprintf(startup_log, "[MAIN] Initialization complete - entering main loop\n");
        fclose(startup_log);
        startup_log = NULL;
    }
    
    /* Run main loop */
    ret = hyprlax_run(ctx);
    
    /* Clean up */
    hyprlax_destroy(ctx);
    g_ctx = NULL;
    
    return ret;
}