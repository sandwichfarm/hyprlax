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
#include "include/config_legacy.h"

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
            printf("Usage: %s [OPTIONS] [--layer <image:shift:opacity:blur[:#RRGGBB[:strength]]>...]\n", argv[0]);
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
            printf("  -p, --platform <backend>  Platform backend (wayland, auto)\n");
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

    /* Early legacy config detection and conversion (before init) */
    char **argv_effective = argv; int argc_effective = argc; int argv_modified = 0;
    {
        const char *config_arg = NULL; const char *config_val = NULL;
        int yes = 0, do_continue = 0, do_convert = 0;
        /* Env overrides for automation */
        const char *assume_env = getenv("HYPRLAX_ASSUME_YES");
        const char *nonint_env = getenv("HYPRLAX_NONINTERACTIVE");
        if (assume_env && *assume_env && strcmp(assume_env, "0") != 0 && strcasecmp(assume_env, "false") != 0) yes = 1;
        int force_noninteractive = (nonint_env && *nonint_env && strcmp(nonint_env, "0") != 0 && strcasecmp(nonint_env, "false") != 0);
        for (int i = 1; i < argc; i++) {
            const char *a = argv[i];
            if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
            else if (!strcmp(a, "--continue")) do_continue = 1;
            else if (!strcmp(a, "--convert-config")) do_convert = 1;
            else if (!strcmp(a, "--non-interactive") || !strcmp(a, "--noninteractive") || !strcmp(a, "--batch")) { force_noninteractive = 1; setenv("HYPRLAX_NONINTERACTIVE", "1", 1); }
            else if (!strcmp(a, "-c") || !strcmp(a, "--config")) {
                config_arg = a;
                if (i + 1 < argc) config_val = argv[i+1];
            } else if (!strncmp(a, "--config=", 9)) {
                config_arg = "--config";
                config_val = a + 9;
            }
        }

        char def_legacy[512] = {0}, def_toml[512] = {0};
        legacy_paths_default(def_legacy, sizeof(def_legacy), def_toml, sizeof(def_toml));
        int have_default_legacy = (def_legacy[0] && access(def_legacy, R_OK) == 0);

        const char *legacy_src = NULL;
        if (config_val) {
            const char *ext = strrchr(config_val, '.');
            if (ext && (strcmp(ext, ".conf") == 0 || strcmp(ext, ".CONF") == 0)) legacy_src = config_val;
        } else if (have_default_legacy) {
            legacy_src = def_legacy;
        }

        if (do_convert || legacy_src) {
            /* Don't do interactive prompts in non-tty when not explicitly requested */
            if (!do_convert && (force_noninteractive || !isatty(0)) && legacy_src) {
                fprintf(stderr, "Found legacy config at %s. Convert with: hyprlax ctl convert-config %s %s --yes\n",
                        legacy_src, legacy_src, def_toml);
                return 3;
            }

            if (!legacy_src && do_convert) {
                /* Try default path or fail clearly */
                if (have_default_legacy) legacy_src = def_legacy;
                else {
                    fprintf(stderr, "No legacy config found. Usage: hyprlax ctl convert-config <legacy.conf> [dst.toml] [--yes]\n");
                    return 2;
                }
            }

            if (legacy_src) {
                legacy_cfg_t lcfg; char err[256];
                if (legacy_config_read(legacy_src, &lcfg, err, sizeof(err)) != 0) {
                    fprintf(stderr, "Failed to read legacy config: %s\n", err[0]?err:"unknown error");
                    return 2;
                }
                const char *dst = def_toml;
                if (!yes && !force_noninteractive && isatty(0)) {
                    fprintf(stderr, "Convert legacy config to TOML?\n  from: %s\n  to:   %s\nProceed? [y/N] ", legacy_src, dst);
                    fflush(stderr);
                    char line[16]; if (fgets(line, sizeof(line), stdin)) { if (line[0]=='y'||line[0]=='Y') yes=1; }
                }
                if (!yes && !do_convert) {
                    fprintf(stderr, "Conversion aborted. To convert non-interactively: hyprlax ctl convert-config %s %s --yes\n", legacy_src, dst);
                    legacy_config_free(&lcfg);
                    return 3;
                }
                if (legacy_config_write_toml(&lcfg, dst, err, sizeof(err)) != 0) {
                    fprintf(stderr, "Failed to write TOML: %s\n", err[0]?err:"unknown error");
                    legacy_config_free(&lcfg);
                    return 2;
                }
                legacy_config_free(&lcfg);
                fprintf(stderr, "Converted to: %s\nRun: hyprlax --config %s\n", dst, dst);
                if (!do_continue) return 0;
                /* Build a cleaned argv without conversion flags and with --config dst */
                char **alt = calloc((size_t)argc + 4, sizeof(char*));
                if (!alt) return 0; /* fallback: just exit */
                int na = 0; alt[na++] = argv[0];
                int have_config = 0;
                for (int i = 1; i < argc; i++) {
                    const char *a = argv[i];
                    if (!strcmp(a, "--convert-config") || !strcmp(a, "--continue") || !strcmp(a, "--yes") || !strcmp(a, "-y")) {
                        continue;
                    }
                    if (!strcmp(a, "-c") || !strcmp(a, "--config")) {
                        /* consume and replace value with dst */
                        alt[na++] = "--config";
                        if (i + 1 < argc) { i++; } /* skip original value */
                        alt[na++] = (char*)dst; have_config = 1; continue;
                    }
                    if (!strncmp(a, "--config=", 9)) {
                        alt[na++] = "--config"; alt[na++] = (char*)dst; have_config = 1; continue;
                    }
                    alt[na++] = argv[i];
                }
                if (!have_config) {
                    alt[na++] = "--config"; alt[na++] = (char*)dst;
                }
                argv_effective = alt; argc_effective = na; argv_modified = 1;
            }
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
    /* Use sigaction without SA_RESTART so epoll_wait/nanosleep are interrupted */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* do not set SA_RESTART */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);  /* Ignore SIGPIPE like bash does */

    /* Initialize application */
    if (startup_log) {
        fprintf(startup_log, "[MAIN] Starting initialization\n");
        fflush(startup_log);
    }
    int ret = hyprlax_init(ctx, argc_effective, argv_effective);
    if (ret != 0) {
        if (startup_log) {
            fprintf(startup_log, "[MAIN] ERROR: Initialization failed with code %d\n", ret);
            fclose(startup_log);
        }
        hyprlax_destroy(ctx);
        if (argv_modified) free(argv_effective);
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
    if (argv_modified) free(argv_effective);
    g_ctx = NULL;

    return ret;
}
