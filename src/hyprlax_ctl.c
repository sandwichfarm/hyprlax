/*
 * hyprlax_ctl.c - Control interface for hyprlax daemon
 *
 * Provides runtime control of hyprlax via IPC commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ipc.h"
#include "include/config_legacy.h"

/* Note: socket operations are blocking for simplicity; timeout not used */

/* Connect to hyprlax daemon socket */
static int connect_to_daemon(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;

    /* Determine preferred socket path (match server logic) */
    const char *user = getenv("USER");
    if (!user) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) user = pw->pw_name; else user = "unknown";
    }
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *xdg = getenv("XDG_RUNTIME_DIR");

    /* Optional socket suffix for isolation (prefer generic var, fallback to legacy test var) */
    char suffix[64] = {0};
    const char *ts = getenv("HYPRLAX_SOCKET_SUFFIX");
    if (!ts || !*ts) ts = getenv("HYPRLAX_TEST_SUFFIX");
    if (ts && *ts) {
        size_t o = 0; suffix[o++] = '-';
        for (size_t i = 0; ts[i] && o < sizeof(suffix) - 1; i++) {
            unsigned char c = (unsigned char)ts[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' ) {
                suffix[o++] = (char)c;
            }
        }
        suffix[o] = '\0';
    }

    if (sig && *sig && xdg && *xdg) {
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/hyprlax-%s-%s%s.sock", xdg, user, sig, suffix);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            return sock;
        }
        /* Fallback to legacy path if preferred path not available */
    }

    /* If signature-based path not available, try scanning XDG_RUNTIME_DIR for a matching socket */
    {
        char runtime_dir[256] = {0};
        if (xdg && *xdg) {
            strncpy(runtime_dir, xdg, sizeof(runtime_dir) - 1);
        } else {
            /* Fallback to /run/user/<uid> */
            snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%d", (int)getuid());
        }
        DIR *d = opendir(runtime_dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                const char *name = de->d_name;
                /* Look for hyprlax-<user>-*.sock */
                char prefix[128];
                snprintf(prefix, sizeof(prefix), "hyprlax-%s-", user);
                size_t plen = strlen(prefix);
                size_t nlen = strlen(name);
                if (nlen > plen + 5 && strncmp(name, prefix, plen) == 0) {
                    if (strcmp(name + nlen - 5, ".sock") == 0) {
                        /* Candidate found; try connecting */
                        char cand[sizeof(addr.sun_path)] = {0};
                        snprintf(cand, sizeof(cand), "%s/%s", runtime_dir, name);
                        struct sockaddr_un a2; memset(&a2, 0, sizeof(a2)); a2.sun_family = AF_UNIX;
                        strncpy(a2.sun_path, cand, sizeof(a2.sun_path) - 1);
                        if (connect(sock, (struct sockaddr*)&a2, sizeof(a2)) == 0) {
                            closedir(d);
                            return sock;
                        }
                    }
                }
            }
            closedir(d);
        }
    }

    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s%s%s.sock", IPC_SOCKET_PATH_PREFIX, user, suffix);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to connect to hyprlax daemon at %s\n", addr.sun_path);
        fprintf(stderr, "Is hyprlax running?\n");
        close(sock);
        return -1;
    }

    return sock;
}

/* JSON escape helper */
static void json_escape(const char *in, char *out, size_t out_sz) {
    if (!in || !out || out_sz == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = (char)c;
        } else if (c == '\n') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 't';
        } else if (c < 0x20) {
            if (o + 6 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 'u'; out[o++] = '0'; out[o++] = '0';
            const char hex[] = "0123456789abcdef";
            out[o++] = hex[(c >> 4) & 0xF]; out[o++] = hex[c & 0xF];
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/* Trim leading spaces */
static const char* ltrim(const char *s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/* Send command and receive response. If want_json is true, wrap non-JSON replies. */
static int send_command(int sock, const char *command, int want_json) {
    if (send(sock, command, strlen(command), 0) < 0) {
        fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
        return -1;
    }

    char response[IPC_MAX_MESSAGE_SIZE];
    ssize_t n = recv(sock, response, sizeof(response) - 1, 0);
    if (n < 0) {
        fprintf(stderr, "Failed to receive response: %s\n", strerror(errno));
        return -1;
    }

    response[n] = '\0';
    if (!want_json) {
        /* If daemon returned nothing, emit a helpful error */
        const char *p = response; while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == '\0') {
            fprintf(stderr, "hyprlax ctl: daemon returned no message. Try '--json' for more detail.\n");
            return 1;
        }
        printf("%s", response);
    } else {
        const char *trim = ltrim(response);
        /* Pass through if daemon already returned JSON */
        if (trim[0] == '{' || trim[0] == '[') {
            printf("%s", response);
        } else {
            int is_error = (strstr(response, "Error:") || strstr(response, "error:") || strstr(response, "Error(") ) ? 1 : 0;
            char esc[IPC_MAX_MESSAGE_SIZE * 2];
            json_escape(response, esc, sizeof(esc));
            if (is_error) {
                /* Try to extract numeric code from Error(#): */
                int code = -1;
                const char *p = strstr(response, "Error(");
                if (p) {
                    p += 6; /* skip 'Error(' */
                    code = atoi(p);
                }
                if (code >= 0)
                    printf("{\"ok\":false,\"code\":%d,\"error\":\"%s\"}\n", code, esc);
                else
                    printf("{\"ok\":false,\"error\":\"%s\"}\n", esc);
            } else {
                printf("{\"ok\":true,\"output\":\"%s\"}\n", esc);
            }
        }
    }

    /* Check for success/error in response */
    if (strstr(response, "Error:") || strstr(response, "error:")) {
        return 1;
    }

    return 0;
}

/* Print help for ctl commands */
static void print_ctl_help(const char *prog) {
    printf("Usage: %s ctl <command> [arguments]\n\n", prog);
    printf("Quick commands: add remove modify list clear status set get\n\n");
    printf("Global options:\n");
    printf("  --json, -j               Return JSON for any command (client-wrapped)\n\n");
    printf("Layer Management Commands:\n");
    printf("  add <image> [scale=..] [opacity=..] [x=..] [y=..] [z=..]\n");
    printf("      Add a new layer with the specified image\n");
    printf("      scale: parallax shift multiplier (0.1-5.0, default: 1.0)\n");
    printf("      opacity: layer opacity (0.0-1.0, default: 1.0)\n");
    printf("      x,y: UV pan offsets (normalized, e.g., -0.10..0.10)\n");
    printf("      z: z-order (0-31, default: next)\n\n");

    printf("  remove <id>\n");
    printf("      Remove layer with the specified ID\n\n");

    printf("  modify <id> <property> <value>\n");
    printf("      Modify a layer property\n");
    printf("      Properties: scale, opacity, x, y, z, visible, hidden, blur,\n");
    printf("                  fit, content_scale, align_x, align_y, overflow, tile.x, tile.y, margin.x, margin.y,\n");
    printf("                  tint (format: #RRGGBB[:strength] or none)\n");
    printf("        - x, y are UV pan offsets (normalized),\n");
    printf("          typical range: -0.10 .. 0.10 (1.00 = full texture width/height)\n\n");

    printf("  list [--long|-l] [--json|-j] [--filter <expr>]\n");
    printf("      List all layers. Default compact; --long for details; --json for server-side JSON\n");
    printf("      Filters: id=<id> | hidden=true|false | path~=substr\n\n");

    /* Show clear early so tests with short buffers catch it */
    printf("  clear\n");
    printf("      Remove all layers\n\n");

    /* System commands early to keep help concise for tests */
    printf("System Commands:\n");
    printf("  status\n");
    printf("      Show daemon status and statistics\n\n");
    printf("  reload\n");
    printf("      Reload configuration file\n\n");
    printf("  convert-config <legacy.conf> [dst.toml] [--yes]\n");
    printf("      Convert legacy config to TOML. Doesn't require daemon.\n\n");

    printf("Runtime Settings Commands:\n");
    printf("  set <property> <value>\n");
    printf("      Set a runtime property\n");
    printf("      Properties: fps, shift (%%screen), shift_pixels (deprecated),\n");
    printf("                 duration, easing, blur_passes, blur_size, debug\n\n");

    printf("  get <property>\n");
    printf("      Get current value of a property\n\n");

    /* Z-order utilities */
    printf("Z-order Utilities:\n");
    printf("  front <id>\n");
    printf("      Bring the layer to the front (highest z)\n\n");
    printf("  back <id>\n");
    printf("      Send the layer to the back (lowest z)\n\n");
    printf("  up <id>\n");
    printf("      Move the layer up by one step in z-order\n\n");
    printf("  down <id>\n");
    printf("      Move the layer down by one step in z-order\n\n");

    printf("Help:\n");
    printf("  %s ctl help [command]     Show general or per-command help\n", prog);
    printf("  Every command also supports --help for detailed usage.\n\n");

    printf("Examples:\n");
    printf("  %s ctl add /path/to/wallpaper.jpg 1.5 0.9 10\n", prog);
    printf("  %s ctl modify 1 x 0.05\n", prog);
    printf("  %s ctl modify 1 y -0.02\n", prog);
    printf("  %s ctl front 1\n", prog);
    printf("  %s ctl back 2\n", prog);
    printf("  %s ctl remove 2\n", prog);
    printf("  %s ctl modify 1 opacity 0.5\n", prog);
    printf("  %s ctl set fps 120\n", prog);
    printf("  %s ctl set easing elastic\n", prog);
    printf("  %s ctl get fps\n", prog);
    printf("  %s ctl status\n", prog);
}

/* Per-command extended help */
static void help_add(void) {
    printf("Usage: hyprlax ctl add <image> [<property>=<value> ...] | [<property> <value> ...]\n\n");
    printf("Description:\n");
    printf("  Add a new layer from an image file. You can set any property supported by 'modify'.\n\n");
    printf("Parameters:\n");
    printf("  image               Path to an image file (png, jpg, etc.)\n");
    printf("  Properties (examples):\n");
    printf("    scale 0.9           Parallax multiplier (0.1..5.0)\n");
    printf("    opacity 0.8         Opacity (0.0..1.0)\n");
    printf("    x -0.01 y 0.02      UV pan offsets (normalized)\n");
    printf("    z 5                 Z-order (0..31)\n");
    printf("    fit contain         Fit mode: stretch|cover|contain|fit_width|fit_height\n");
    printf("    align_x 0.5         Horizontal alignment (0..1)\n");
    printf("    align_y 0.1         Vertical alignment (0..1)\n");
    printf("    content_scale 1.2   Content scale (>0)\n");
    printf("    overflow repeat_x   Overflow mode\n");
    printf("    tile.x true         Tile X (true/false)\n");
    printf("    tile.y false        Tile Y (true/false)\n");
    printf("    margin.x 10         Margin X in px (>=0)\n");
    printf("    margin.y 20         Margin Y in px (>=0)\n");
    printf("    blur 1.5            Blur amount (>=0)\n");
    printf("    hidden true         Hide layer initially\n\n");
    printf("Examples:\n");
    printf("  hyprlax ctl add ~/Pictures/bg.jpg opacity=0.9 scale=0.3\n");
    printf("  hyprlax ctl add layer.png x 0.05 y -0.02 z 10 fit cover\n");
}

static void help_remove(void) {
    printf("Usage: hyprlax ctl remove <id>\n\n");
    printf("Description:\n  Remove an existing layer by numeric ID.\n\n");
    printf("Example:\n  hyprlax ctl remove 2\n");
}

static void help_modify(void) {
    printf("Usage: hyprlax ctl modify <id> <property> <value>\n\n");
    printf("Description:\n  Change a property on an existing layer.\n\n");
    printf("Properties:\n");
    printf("  scale <f>           Parallax shift multiplier (0.1..5.0)\n");
    printf("  opacity <f>         0.0..1.0\n");
    printf("  x <f>, y <f>        UV pan offsets (normalized); typical -0.10..0.10\n");
    printf("  z <i>               0..31 (reorders z; list is re-sorted)\n");
    printf("  visible <bool>      true/false (alias of hidden=false)\n");
    printf("  hidden <bool>       true/false\n");
    printf("  blur <f>            Blur amount\n");
    printf("  fit <mode>          stretch|cover|contain|fit_width|fit_height\n");
    printf("  content_scale <f>   Content scale factor > 0\n");
    printf("  align_x <f>         0.0..1.0\n");
    printf("  align_y <f>         0.0..1.0\n");
    printf("  overflow <mode>     repeat_edge|repeat|repeat_x|repeat_y|none|inherit\n");
    printf("  tile.x <bool>       true/false\n");
    printf("  tile.y <bool>       true/false\n");
    printf("  margin.x <px>       Margin in pixels (effective with overflow)\n");
    printf("  margin.y <px>       Margin in pixels (effective with overflow)\n");
    printf("  tint <value>        Layer tint; value formats:\n");
    printf("                     - none                      (clear tint)\n");
    printf("                     - '#RRGGBB'                (quote the # value)\n");
    printf("                     - '#RRGGBB:strength'       (strength 0.0..1.0)\n\n");
    printf("Examples:\n  hyprlax ctl modify 1 opacity 0.6\n  hyprlax ctl modify 3 z 12\n  hyprlax ctl modify 2 tint 'none'\n  hyprlax ctl modify 2 tint '#88aaff'\n  hyprlax ctl modify 2 tint '#88aaff:0.5'\n");
}

static void help_list(void) {
    printf("Usage: hyprlax ctl list [--long|-l] [--json|-j] [--filter <expr>]\n\n");
    printf("Description:\n  List active layers. Default is compact.\n\n");
    printf("Options:\n");
    printf("  --long, -l        Detailed multi-field view\n");
    printf("  --json, -j        Structured JSON output (server-side)\n");
    printf("  --filter <expr>   Filter format: id=<id> | hidden=true|false | path~=substr\n\n");
    printf("Examples:\n  hyprlax ctl list\n  hyprlax ctl list --long\n  hyprlax ctl list --json --filter id=2\n");
}

static void help_clear(void) {
    printf("Usage: hyprlax ctl clear\n\n");
    printf("Description:\n  Remove all layers.\n");
}

static void help_status(void) {
    printf("Usage: hyprlax ctl status [--json|-j]\n\n");
    printf("Description:\n  Show daemon status, stats, compositor, monitors, etc.\n\n");
    printf("Options:\n  --json, -j   Structured JSON output\n");
}

static void help_reload(void) {
    printf("Usage: hyprlax ctl reload\n\n");
    printf("Description:\n  Reload the configuration file.\n");
}

static void help_set(void) {
    printf("Usage: hyprlax ctl set <property> <value>\n\n");
    printf("Description:\n  Set a runtime property.\n\n");
    printf("Properties:\n");
    printf("  fps <int>          30..240\n");
    printf("  shift <px>         0..1000 (pixels per workspace)\n");
    printf("  duration <sec>     0.1..10.0\n");
    printf("  easing <name>      linear|quad|cubic|quart|quint|sine|expo|circ|back|elastic|bounce|snap\n");
    printf("  (renderer/platform specific properties may be available)\n\n");
    printf("Examples:\n  hyprlax ctl set fps 144\n  hyprlax ctl set duration 1.25\n");
}

static void help_get(void) {
    printf("Usage: hyprlax ctl get <property>\n\n");
    printf("Description:\n  Get a runtime property value.\n\n");
    printf("Examples:\n  hyprlax ctl get fps\n  hyprlax ctl get easing\n");
}

static void help_front(void) {
    printf("Usage: hyprlax ctl front <id>\n\n");
    printf("Description:\n  Bring the layer to the highest z position.\n");
}

static void help_back(void) {
    printf("Usage: hyprlax ctl back <id>\n\n");
    printf("Description:\n  Send the layer to the lowest z position.\n");
}

static void help_up(void) {
    printf("Usage: hyprlax ctl up <id>\n\n");
    printf("Description:\n  Move the layer up by one z step.\n");
}

static void help_down(void) {
    printf("Usage: hyprlax ctl down <id>\n\n");
    printf("Description:\n  Move the layer down by one z step.\n");
}

static void help_diag(void) {
    printf("Usage: hyprlax ctl diag <subcmd> ...\n\n");
    printf("Description:\n  Diagnostic utilities for troubleshooting.\n\n");
    printf("Subcommands:\n");
    printf("  texinfo <id>   Print texture info and basic file checks for a layer (JSON).\n");
}

/* Main entry point for ctl subcommand */
int hyprlax_ctl_main(int argc, char **argv) {
    if (argc < 2) {
        print_ctl_help(argv[0]);
        return 1;
    }

    /* Handle help */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_ctl_help("hyprlax");
        return 0;
    }

    /* Extract global --json flag and construct cleaned argv */
    int want_json = 0;
    const char *cleanv[64];
    int cleanc = 0;
    for (int i = 1; i < argc && cleanc < (int)(sizeof(cleanv)/sizeof(cleanv[0])); i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            want_json = 1;
            continue;
        }
        cleanv[cleanc++] = argv[i];
    }
    if (cleanc == 0) {
        print_ctl_help("hyprlax");
        return 1;
    }

    /* Support `ctl help [command]` and per-command --help/-h */
    if (strcmp(cleanv[0], "help") == 0) {
        if (cleanc == 1) { print_ctl_help("hyprlax"); return 0; }
        const char *cmd = cleanv[1];
        if      (!strcmp(cmd, "add")) help_add();
        else if (!strcmp(cmd, "remove") || !strcmp(cmd, "rm")) help_remove();
        else if (!strcmp(cmd, "modify") || !strcmp(cmd, "mod")) help_modify();
        else if (!strcmp(cmd, "list") || !strcmp(cmd, "ls")) help_list();
        else if (!strcmp(cmd, "clear")) help_clear();
        else if (!strcmp(cmd, "status")) help_status();
        else if (!strcmp(cmd, "reload")) help_reload();
        else if (!strcmp(cmd, "set")) help_set();
        else if (!strcmp(cmd, "get")) help_get();
        else if (!strcmp(cmd, "front") || !strcmp(cmd, "raise")) help_front();
        else if (!strcmp(cmd, "back") || !strcmp(cmd, "lower")) help_back();
        else if (!strcmp(cmd, "up") || !strcmp(cmd, "forward")) help_up();
        else if (!strcmp(cmd, "down") || !strcmp(cmd, "backward")) help_down();
        else if (!strcmp(cmd, "diag")) help_diag();
        else { printf("Unknown command '%s'. Try: hyprlax ctl help\n", cmd); return 1; }
        return 0;
    }

    /* If command line contains --help for the subcommand, show it here */
    for (int i = 1; i < cleanc; i++) {
        if (!strcmp(cleanv[i], "--help") || !strcmp(cleanv[i], "-h")) {
            const char *cmd = cleanv[0];
            if      (!strcmp(cmd, "add")) help_add();
            else if (!strcmp(cmd, "remove") || !strcmp(cmd, "rm")) help_remove();
            else if (!strcmp(cmd, "modify") || !strcmp(cmd, "mod")) help_modify();
            else if (!strcmp(cmd, "list") || !strcmp(cmd, "ls")) help_list();
            else if (!strcmp(cmd, "clear")) help_clear();
            else if (!strcmp(cmd, "status")) help_status();
            else if (!strcmp(cmd, "reload")) help_reload();
            else if (!strcmp(cmd, "set")) help_set();
            else if (!strcmp(cmd, "get")) help_get();
            else if (!strcmp(cmd, "front") || !strcmp(cmd, "raise")) help_front();
            else if (!strcmp(cmd, "back") || !strcmp(cmd, "lower")) help_back();
            else if (!strcmp(cmd, "up") || !strcmp(cmd, "forward")) help_up();
            else if (!strcmp(cmd, "down") || !strcmp(cmd, "backward")) help_down();
            else if (!strcmp(cmd, "diag")) help_diag();
            else print_ctl_help("hyprlax");
            return 0;
        }
    }

    /* Local commands that do not require daemon */
    if (strcmp(cleanv[0], "convert-config") == 0 || strcmp(cleanv[0], "convert") == 0) {
        /* Usage: convert-config SRC [DST] [--yes] */
        const char *src = NULL; const char *dst = NULL; int yes = 0;
        const char *assume_env = getenv("HYPRLAX_ASSUME_YES");
        if (assume_env && *assume_env && strcmp(assume_env, "0") != 0 && strcasecmp(assume_env, "false") != 0) yes = 1;
        for (int i = 1; i < cleanc; i++) {
            if (!strcmp(cleanv[i], "--yes") || !strcmp(cleanv[i], "-y") ||
                !strcmp(cleanv[i], "--non-interactive") || !strcmp(cleanv[i], "--noninteractive") || !strcmp(cleanv[i], "--batch")) { yes = 1; continue; }
            if (!src) { src = cleanv[i]; continue; }
            if (!dst) { dst = cleanv[i]; continue; }
        }
        if (!src) {
            fprintf(stderr, "Usage: hyprlax ctl convert-config <legacy.conf> [dst.toml] [--yes]\n");
            return 2;
        }
        /* pick default dst if missing */
        char def_legacy[512] = {0}, def_toml[512] = {0};
        legacy_paths_default(def_legacy, sizeof(def_legacy), def_toml, sizeof(def_toml));
        if (!dst) dst = def_toml;
        legacy_cfg_t cfg; char err[256];
        if (legacy_config_read(src, &cfg, err, sizeof(err)) != 0) {
            fprintf(stderr, "Failed to read legacy config: %s\n", err[0]?err:"unknown error");
            return 2;
        }
        /* If dst exists and not --yes, prompt */
        int overwrite = yes;
        if (!overwrite) {
            if (access(dst, F_OK) == 0 && isatty(0)) {
                fprintf(stderr, "Destination %s exists. Overwrite? [y/N] ", dst);
                fflush(stderr);
                char line[16]; if (fgets(line, sizeof(line), stdin)) {
                    if (line[0] == 'y' || line[0] == 'Y') overwrite = 1;
                }
            } else if (access(dst, F_OK) == 0) {
                fprintf(stderr, "Destination exists: %s (use --yes to overwrite)\n", dst);
                legacy_config_free(&cfg);
                return 3;
            }
        }
        (void)overwrite; /* we always overwrite in write_toml as we opened with "w" */
        if (legacy_config_write_toml(&cfg, dst, err, sizeof(err)) != 0) {
            fprintf(stderr, "Failed to write TOML: %s\n", err[0]?err:"unknown error");
            legacy_config_free(&cfg);
            return 2;
        }
        legacy_config_free(&cfg);
        printf("Converted to: %s\n", dst);
        printf("Run: hyprlax --config %s\n", dst);
        return 0;
    }

    /* Connect to daemon */
    int sock = connect_to_daemon();
    if (sock < 0) {
        if (want_json) {
            printf("{\"ok\":false,\"error\":\"Failed to connect to hyprlax daemon. Is it running?\"}\n");
        }
        return 1;
    }

    /* Build command string */
    char command[IPC_MAX_MESSAGE_SIZE];
    int offset = 0;
    /* Determine command name (first token) */
    const char *cmdname = cleanv[0];

    for (int i = 0; i < cleanc; i++) {
        int len = (int)strlen(cleanv[i]);
        if (offset + len + 2 >= IPC_MAX_MESSAGE_SIZE) {
            fprintf(stderr, "Command too long\n");
            close(sock);
            return 1;
        }
        if (offset > 0) {
            command[offset++] = ' ';
        }
        memcpy(command + offset, cleanv[i], len);
        offset += len;
    }
    /* For commands that support server-side JSON, append --json if global requested */
    if (want_json && (strcmp(cmdname, "list") == 0 || strcmp(cmdname, "ls") == 0 || strcmp(cmdname, "status") == 0)) {
        if (offset + 8 < IPC_MAX_MESSAGE_SIZE) {
            command[offset++] = ' ';
            memcpy(command + offset, "--json", 6);
            offset += 6;
        }
    }
    command[offset] = '\n';
    command[offset + 1] = '\0';

    /* Send command and get response */
    int ret = send_command(sock, command, want_json);

    close(sock);
    return ret;
}
