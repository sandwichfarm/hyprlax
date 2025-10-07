/*
 * IPC implementation for hyprlax
 * Handles runtime layer management via Unix sockets
 */

#include "ipc.h"
#include "include/hyprlax.h"
#include "include/core.h"
#include "include/log.h"
#include "compositor/workspace_models.h"
#include "include/config_toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <math.h>
#include "include/defaults.h"

/* stb_image prototypes (implementation is compiled in hyprlax_main.c) */
extern int stbi_info(const char *filename, int *x, int *y, int *comp);
extern const char *stbi_failure_reason(void);

/* Forward decls */
static void json_escape(const char *in, char *out, size_t out_sz);
static int layer_compare_qsort(const void* a, const void* b);

/* Forward decl for JSON escaping used in list output */
static void json_escape(const char *in, char *out, size_t out_sz);

/* Helpers shared by add/modify property handling */
/* Forward decls for helpers defined later in file */
static void ipc_errorf(char *out, size_t out_sz, int code, const char *fmt, ...);
static int token_check_len(const char *tok, size_t maxlen, const char *name,
                           char *response, size_t response_sz);
static int parse_int_range(const char *s, int minv, int maxv, int *out);

static int str_to_bool(const char *v) {
    if (!v) return 0;
    return (!strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcasecmp(v, "yes")) ? 1 : 0;
}

/* --- Tint parsing helpers --- */
static int parse_hex_rgb_ipc(const char *s, float *r, float *g, float *b) {
    if (!s || s[0] != '#' || strlen(s) != 7) return 0;
    char cr[3] = {s[1], s[2], 0};
    char cg[3] = {s[3], s[4], 0};
    char cb[3] = {s[5], s[6], 0};
    char *end = NULL;
    long rv = strtol(cr, &end, 16); if (end && *end) return 0;
    long gv = strtol(cg, &end, 16); if (end && *end) return 0;
    long bv = strtol(cb, &end, 16); if (end && *end) return 0;
    if (r) *r = (float)rv / 255.0f;
    if (g) *g = (float)gv / 255.0f;
    if (b) *b = (float)bv / 255.0f;
    return 1;
}
/* Accepts: 'none' or '#RRGGBB' or '#RRGGBB:0.5' or '#RRGGBB,0.5' */
static int parse_tint_value_ipc(const char *value, float *r, float *g, float *b, float *strength) {
    if (!value || !*value) return 0;
    if (!strcmp(value, "none")) {
        if (r) *r = 1.0f; if (g) *g = 1.0f; if (b) *b = 1.0f; if (strength) *strength = 0.0f; return 1;
    }
    char buf[128];
    if (strlen(value) >= sizeof(buf)) return 0;
    /* Safe copy: avoid flagged unsafe copy; value length already validated */
    snprintf(buf, sizeof(buf), "%s", value);
    char *sep = strchr(buf, ':'); if (!sep) sep = strchr(buf, ',');
    float tr=1.0f, tg=1.0f, tb=1.0f, ts=1.0f;
    if (sep) {
        *sep = '\0';
        if (!parse_hex_rgb_ipc(buf, &tr, &tg, &tb)) return 0;
        ts = (float)atof(sep + 1); if (ts < 0.0f) ts = 0.0f; if (ts > 1.0f) ts = 1.0f;
    } else {
        if (!parse_hex_rgb_ipc(buf, &tr, &tg, &tb)) return 0;
        ts = 1.0f;
    }
    if (r) *r = tr; if (g) *g = tg; if (b) *b = tb; if (strength) *strength = ts;
    return 1;
}
static int overflow_from_string(const char *s) {
    if (!s) return -2;
    if (!strcmp(s, "inherit")) return -1;
    if (!strcmp(s, "repeat_edge") || !strcmp(s, "clamp")) return 0;
    if (!strcmp(s, "repeat") || !strcmp(s, "tile")) return 1;
    if (!strcmp(s, "repeat_x") || !strcmp(s, "tilex")) return 2;
    if (!strcmp(s, "repeat_y") || !strcmp(s, "tiley")) return 3;
    if (!strcmp(s, "none") || !strcmp(s, "off")) return 4;
    return -2;
}

/* Apply a single property to a layer; returns 1 on success, 0 on error (with response filled) */
static int apply_layer_property(hyprlax_context_t *app, parallax_layer_t *layer,
                                const char *property, const char *value,
                                char *response, size_t response_sz) {
    if (!app || !layer || !property || !value) return 0;
    if (token_check_len(property, IPC_MAX_PROP_LEN, "property", response, response_sz)) return 0;
    if (token_check_len(value, IPC_MAX_VALUE_LEN, "value", response, response_sz)) return 0;

    if (strcmp(property, "scale") == 0 || strcmp(property, "shift_multiplier") == 0) {
        float v = atof(value); if (v < 0.0f || v > 5.0f) { ipc_errorf(response, response_sz, 1250, "scale out of range (0.0..5.0)\n"); return 0; }
        layer->shift_multiplier = v; layer->shift_multiplier_x = v; layer->shift_multiplier_y = v; return 1;
    } else if (strcmp(property, "opacity") == 0) {
        float v = atof(value); if (v < 0.0f || v > 1.0f) { ipc_errorf(response, response_sz, 1251, "opacity out of range (0.0..1.0)\n"); return 0; }
        layer->opacity = v; return 1;
    } else if (strcmp(property, "path") == 0) {
        char propbuf[64]; snprintf(propbuf, sizeof(propbuf), "layer.%u.path", layer->id);
        int rc = hyprlax_runtime_set_property(app, propbuf, value);
        if (rc == 0) return 1; ipc_errorf(response, response_sz, 1252, "failed to set path\n"); return 0;
    } else if (strcmp(property, "x") == 0 || strcmp(property, "uv_offset.x") == 0) {
        layer->base_uv_x = (float)atof(value); return 1;
    } else if (strcmp(property, "y") == 0 || strcmp(property, "uv_offset.y") == 0) {
        layer->base_uv_y = (float)atof(value); return 1;
    } else if (strcmp(property, "overflow") == 0) {
        int m = overflow_from_string(value);
        if (m == -2) { ipc_errorf(response, response_sz, 1255, "invalid overflow value\n"); return 0; }
        layer->overflow_mode = m; return 1;
    } else if (strcmp(property, "tile.x") == 0) {
        layer->tile_x = str_to_bool(value) ? 1 : 0; return 1;
    } else if (strcmp(property, "tile.y") == 0) {
        layer->tile_y = str_to_bool(value) ? 1 : 0; return 1;
    } else if (strcmp(property, "margin.x") == 0 || strcmp(property, "margin_px.x") == 0) {
        float v = atof(value); if (v < 0.0f) { ipc_errorf(response, response_sz, 1256, "margin.x must be >= 0\n"); return 0; } layer->margin_px_x = v; return 1;
    } else if (strcmp(property, "margin.y") == 0 || strcmp(property, "margin_px.y") == 0) {
        float v = atof(value); if (v < 0.0f) { ipc_errorf(response, response_sz, 1257, "margin.y must be >= 0\n"); return 0; } layer->margin_px_y = v; return 1;
    } else if (strcmp(property, "blur") == 0) {
        float v = atof(value); if (v < 0.0f) { ipc_errorf(response, response_sz, 1258, "blur must be >= 0\n"); return 0; } layer->blur_amount = v; return 1;
    } else if (strcmp(property, "fit") == 0) {
        if (!strcmp(value, "stretch")) layer->fit_mode = LAYER_FIT_STRETCH;
        else if (!strcmp(value, "cover")) layer->fit_mode = LAYER_FIT_COVER;
        else if (!strcmp(value, "contain")) layer->fit_mode = LAYER_FIT_CONTAIN;
        else if (!strcmp(value, "fit_width")) layer->fit_mode = LAYER_FIT_WIDTH;
        else if (!strcmp(value, "fit_height")) layer->fit_mode = LAYER_FIT_HEIGHT;
        else { ipc_errorf(response, response_sz, 1254, "invalid fit value\n"); return 0; }
        return 1;
    } else if (strcmp(property, "content_scale") == 0) {
        float v = atof(value); if (v <= 0.0f) { ipc_errorf(response, response_sz, 1253, "content_scale must be > 0\n"); return 0; }
        layer->content_scale = v; layer->scale_is_custom = true; return 1;
    } else if (strcmp(property, "align_x") == 0 || strcmp(property, "align.x") == 0) {
        layer->align_x = atof(value); if (layer->align_x < 0.0f) layer->align_x = 0.0f; if (layer->align_x > 1.0f) layer->align_x = 1.0f; return 1;
    } else if (strcmp(property, "align_y") == 0 || strcmp(property, "align.y") == 0) {
        layer->align_y = atof(value); if (layer->align_y < 0.0f) layer->align_y = 0.0f; if (layer->align_y > 1.0f) layer->align_y = 1.0f; return 1;
    } else if (strcmp(property, "z") == 0) {
        int zv = 0; int rc = parse_int_range(value, 0, 31, &zv);
        if (rc <= 0) { ipc_errorf(response, response_sz, rc==0?1260:1261, rc==0?"invalid z\n":"z out of range (0..31)\n"); return 0; }
        layer->z_index = zv; return 1;
    } else if (strcmp(property, "hidden") == 0) {
        layer->hidden = str_to_bool(value) ? true : false; return 1;
    } else if (strcmp(property, "visible") == 0) {
        bool vis = str_to_bool(value) ? true : false; layer->hidden = !vis; return 1;
    }
    else if (strcmp(property, "tint") == 0) {
        float tr, tg, tb, ts;
        if (!parse_tint_value_ipc(value, &tr, &tg, &tb, &ts)) { ipc_errorf(response, response_sz, 1259, "invalid tint value\n"); return 0; }
        layer->tint_r = tr; layer->tint_g = tg; layer->tint_b = tb; layer->tint_strength = ts; return 1;
    }
    ipc_errorf(response, response_sz, 1201, "Invalid property '%s'\n", property);
    return 0;
}

/* (Removed) Simple env debug check: use LOG_* levels instead */

/* Sanitize an input line: strip CR/LF and trailing spaces */
static void sanitize_line(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

/* Optional error code formatting. Enable with HYPRLAX_IPC_ERROR_CODES=1 */
static int ipc_error_codes_enabled(void) {
    const char *v = getenv("HYPRLAX_IPC_ERROR_CODES");
    if (!v || !*v) return 0;
    if (!strcmp(v, "0") || !strcasecmp(v, "false")) return 0;
    return 1;
}

static void ipc_errorf(char *out, size_t out_sz, int code, const char *fmt, ...) {
    if (!out || out_sz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    if (ipc_error_codes_enabled()) {
        int w = snprintf(out, out_sz, "Error(%d): ", code);
        if (w < 0 || (size_t)w >= out_sz) { va_end(ap); return; }
        vsnprintf(out + w, out_sz - (size_t)w, fmt, ap);
    } else {
        int w = snprintf(out, out_sz, "Error: ");
        if (w < 0 || (size_t)w >= out_sz) { va_end(ap); return; }
        vsnprintf(out + w, out_sz - (size_t)w, fmt, ap);
    }
    va_end(ap);
}

/* Validate token length; writes error to response on failure */
static int token_check_len(const char *tok, size_t maxlen, const char *name,
                           char *response, size_t response_sz) {
    if (!tok) return 0;
    size_t n = strlen(tok);
    if (n > maxlen) {
        ipc_errorf(response, response_sz, 1003, "%s too long (max %zu)\n", name, maxlen);
        return 1;
    }
    return 0;
}

/* Parse unsigned 32-bit integer from token; return 1 on success */
static int parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s || !out) return 0;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v > UINT32_MAX) return 0;
    *out = (uint32_t)v;
    return 1;
}

/* Parse int within [min,max]; returns 1 ok, 0 parse error, -1 out of range */
static int parse_int_range(const char *s, int minv, int maxv, int *out) {
    if (!s || !*s || !out) return 0;
    errno = 0; char *end = NULL; long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < minv || v > maxv) return -1;
    *out = (int)v; return 1;
}

/* Parse double within [min,max]; returns 1 ok, 0 parse error, -1 out of range */
static int parse_double_range(const char *s, double minv, double maxv, double *out) {
    if (!s || !*s || !out) return 0;
    errno = 0; char *end = NULL; double v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0' || !isfinite(v)) return 0;
    if (v < minv || v > maxv) return -1;
    *out = v; return 1;
}

/* Weak runtime bridge stubs. If the application provides real implementations,
 * the linker will bind to those instead. Signatures match hyprlax.h. */
__attribute__((weak)) int hyprlax_runtime_set_property(hyprlax_context_t *ctx, const char *property, const char *value) {
    (void)ctx; (void)property; (void)value; return -1;
}
__attribute__((weak)) int hyprlax_runtime_get_property(hyprlax_context_t *ctx, const char *property, char *out, size_t out_size) {
    (void)ctx; (void)property; (void)out; (void)out_size; return -1;
}

/* Additional weak stubs so unit tests can link src/ipc.c standalone */
__attribute__((weak)) int hyprlax_add_layer(hyprlax_context_t *ctx, const char *image_path, float shift, float opacity, float blur) {
    (void)ctx; (void)image_path; (void)shift; (void)opacity; (void)blur; return -1;
}
__attribute__((weak)) void hyprlax_remove_layer(hyprlax_context_t *ctx, uint32_t layer_id) {
    (void)ctx; (void)layer_id;
}
__attribute__((weak)) parallax_layer_t* layer_list_find(parallax_layer_t *head, uint32_t id) {
    (void)head; (void)id; return NULL;
}
__attribute__((weak)) parallax_layer_t* layer_list_sort_by_z(parallax_layer_t *head) {
    return head;
}
/* Legacy parallax mode helpers removed. */
__attribute__((weak)) const char* easing_to_string(easing_type_t type) {
    (void)type; return "cubic";
}
__attribute__((weak)) easing_type_t easing_from_string(const char *name) {
    (void)name; return EASE_CUBIC_OUT;
}
__attribute__((weak)) int config_apply_toml_to_context(hyprlax_context_t *ctx, const char *path) {
    (void)ctx; (void)path; return -1;
}
/* Weak stub for reload to satisfy unit tests linking ipc.c alone */
__attribute__((weak)) int hyprlax_reload_config(hyprlax_context_t *ctx) {
    (void)ctx; return -1;
}
/* Weak stub for capability detection used in status JSON */
__attribute__((weak)) bool workspace_detect_capabilities(int compositor_type,
                                  compositor_capabilities_t *caps) {
    (void)compositor_type; if (caps) { memset(caps, 0, sizeof(*caps)); }
    return false;
}

static void format_parallax_inputs(const config_t *cfg, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    size_t len = 0;
    if (cfg) {
        if (cfg->parallax_workspace_weight > 0.0f) {
            len += snprintf(out + len, out_sz - len, "%sworkspace:%.3f",
                            len ? "," : "", cfg->parallax_workspace_weight);
        }
        if (cfg->parallax_cursor_weight > 0.0f) {
            len += snprintf(out + len, out_sz - len, "%scursor:%.3f",
                            len ? "," : "", cfg->parallax_cursor_weight);
        }
    }
    if (len == 0) {
        snprintf(out, out_sz, "none");
    }
}

static void get_socket_path(char* buffer, size_t size) {
    const char* user = getenv("USER");
    if (!user) {
        struct passwd* pw = getpwuid(getuid());
        user = pw ? pw->pw_name : "unknown";
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
        /* Prefer runtime dir + signature to avoid session collisions */
        snprintf(buffer, size, "%s/hyprlax-%s-%s%s.sock", xdg, user, sig, suffix);
        return;
    }
    /* Fallback to /tmp for compatibility */
    snprintf(buffer, size, "%s%s%s.sock", IPC_SOCKET_PATH_PREFIX, user, suffix);
}

static ipc_command_t parse_command(const char* cmd) {
    if (strcmp(cmd, "add") == 0) return IPC_CMD_ADD_LAYER;
    if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "rm") == 0) return IPC_CMD_REMOVE_LAYER;
    if (strcmp(cmd, "modify") == 0 || strcmp(cmd, "mod") == 0) return IPC_CMD_MODIFY_LAYER;
    if (strcmp(cmd, "front") == 0 || strcmp(cmd, "raise") == 0) return IPC_CMD_LAYER_FRONT;
    if (strcmp(cmd, "back") == 0 || strcmp(cmd, "lower") == 0) return IPC_CMD_LAYER_BACK;
    if (strcmp(cmd, "up") == 0 || strcmp(cmd, "forward") == 0) return IPC_CMD_LAYER_UP;
    if (strcmp(cmd, "down") == 0 || strcmp(cmd, "backward") == 0) return IPC_CMD_LAYER_DOWN;
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) return IPC_CMD_LIST_LAYERS;
    if (strcmp(cmd, "clear") == 0) return IPC_CMD_CLEAR_LAYERS;
    if (strcmp(cmd, "reload") == 0) return IPC_CMD_RELOAD_CONFIG;
    if (strcmp(cmd, "status") == 0) return IPC_CMD_GET_STATUS;
    if (strcmp(cmd, "set") == 0) return IPC_CMD_SET_PROPERTY;
    if (strcmp(cmd, "get") == 0) return IPC_CMD_GET_PROPERTY;
    if (strcmp(cmd, "diag") == 0) return IPC_CMD_DIAG;
    if (strcmp(cmd, "computed") == 0 || strcmp(cmd, "calc") == 0 || strcmp(cmd, "calculate") == 0) return IPC_CMD_COMPUTED;
    return IPC_CMD_UNKNOWN;
}

ipc_context_t* ipc_init(void) {
    LOG_DEBUG("[IPC] Initializing IPC subsystem");

    ipc_context_t* ctx = calloc(1, sizeof(ipc_context_t));
    if (!ctx) { LOG_ERROR("[IPC] Failed to allocate IPC context"); return NULL; }

    get_socket_path(ctx->socket_path, sizeof(ctx->socket_path));
    LOG_DEBUG("[IPC] Socket path: %s", ctx->socket_path);

    // Check if another instance is already running by trying to connect to the socket
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        struct sockaddr_un test_addr;
        memset(&test_addr, 0, sizeof(test_addr));
        test_addr.sun_family = AF_UNIX;
        strncpy(test_addr.sun_path, ctx->socket_path, sizeof(test_addr.sun_path) - 1);

        if (connect(test_fd, (struct sockaddr*)&test_addr, sizeof(test_addr)) == 0) {
            // Successfully connected - another instance is running
            LOG_ERROR("[IPC] Another instance of hyprlax is already running");
            LOG_ERROR("[IPC] Socket: %s", ctx->socket_path);
            close(test_fd);
            free(ctx);
            return NULL;
        }
        close(test_fd);
        LOG_DEBUG("[IPC] No existing instance detected");
    }

    // Remove existing socket if it exists (stale from a crash)
    unlink(ctx->socket_path);

    // Create Unix domain socket with retries for early boot scenario
    int max_retries = IPC_SOCKET_CREATE_MAX_RETRIES;
    int retry_delay_ms = IPC_SOCKET_CREATE_RETRY_MS;
    ctx->socket_fd = -1;

    for (int i = 0; i < max_retries; i++) {
        ctx->socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (ctx->socket_fd >= 0) { LOG_TRACE("[IPC] Socket created successfully"); break; }
        if (i == 0) { LOG_WARN("[IPC] Failed to create socket: %s, retrying...", strerror(errno)); }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = retry_delay_ms * 1000000L;
        nanosleep(&ts, NULL);
    }
    if (ctx->socket_fd < 0) { LOG_ERROR("Failed to create IPC socket: %s", strerror(errno)); free(ctx); return NULL; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctx->socket_path, sizeof(addr.sun_path) - 1);

    LOG_TRACE("[IPC] Binding socket to %s", ctx->socket_path);
    if (bind(ctx->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("[IPC] Failed to bind IPC socket: %s", strerror(errno));
        close(ctx->socket_fd);
        free(ctx);
        return NULL;
    }

    LOG_TRACE("[IPC] Starting to listen on socket");
    if (listen(ctx->socket_fd, IPC_LISTEN_BACKLOG) < 0) {
        LOG_ERROR("[IPC] Failed to listen on IPC socket: %s", strerror(errno));
        close(ctx->socket_fd);
        unlink(ctx->socket_path);
        free(ctx);
        return NULL;
    }

    // Set socket permissions to user-only
    chmod(ctx->socket_path, 0600);

    ctx->active = true;
    ctx->next_layer_id = 1;

    LOG_DEBUG("[IPC] Socket successfully listening at: %s", ctx->socket_path);
    return ctx;
}

void ipc_cleanup(ipc_context_t* ctx) {
    if (!ctx) return;

    ctx->active = false;

    // Clear all layers
    ipc_clear_layers(ctx);

    // Close and remove socket
    if (ctx->socket_fd >= 0) {
        close(ctx->socket_fd);
        unlink(ctx->socket_path);
    }

    free(ctx);
}

bool ipc_process_commands(ipc_context_t* ctx) {
    if (!ctx || !ctx->active) return false;

    // Accept new connections
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(ctx->socket_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) { LOG_WARN("Failed to accept IPC connection: %s", strerror(errno)); }
        return false;
    }

    // Read command
    char buffer[IPC_MAX_MESSAGE_SIZE];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return false;
    }
    buffer[bytes] = '\0';
    sanitize_line(buffer);

    // Parse and execute command
    char* cmd = strtok(buffer, " \n");
    if (!cmd) {
        const char* error = ipc_error_codes_enabled()?"Error(1000): No command specified\n":"Error: No command specified\n";
        send(client_fd, error, strlen(error), 0);
        close(client_fd);
        return false;
    }

    ipc_command_t command = parse_command(cmd);
    if (getenv("HYPRLAX_DEBUG")) {
        hyprlax_context_t *app_dbg = (hyprlax_context_t*)ctx->app_context;
        void *head = app_dbg ? (void*)app_dbg->layers : NULL;
        int lcount = app_dbg ? app_dbg->layer_count : ctx->layer_count;
        fprintf(stderr, "[DEBUG] IPC command: %s (enum=%d), layers=%d head=%p\n", cmd, (int)command, lcount, head);
        if (head && app_dbg && app_dbg->layers) {
            fprintf(stderr, "[DEBUG]   head.id=%u head.next=%p\n", app_dbg->layers->id, (void*)app_dbg->layers->next);
        }
    }
    char response[IPC_MAX_MESSAGE_SIZE];
    bool success = false;

    /* Helper to renormalize z_index after reordering */
    auto void renorm_z(hyprlax_context_t *app) {
        if (!app) return;
        app->layers = layer_list_sort_by_z(app->layers);
        int zi = 0; for (parallax_layer_t *it = app->layers; it; it = it->next) it->z_index = zi++;
    }

    switch (command) {
        case IPC_CMD_ADD_LAYER: {
            char* path = strtok(NULL, " \n");
            if (!path) {
                ipc_errorf(response, sizeof(response), 1100, "Image path required\n");
                break;
            }
            /* If the first token looks like an option instead of a path, emit a clearer error */
            if (!strncmp(path, "scale=", 6) || !strncmp(path, "opacity=", 8) ||
                !strncmp(path, "x=", 2) || !strncmp(path, "y=", 2) || !strncmp(path, "z=", 2) ||
                !strcmp(path, "scale") || !strcmp(path, "opacity") || !strcmp(path, "x") || !strcmp(path, "y") || !strcmp(path, "z")) {
                snprintf(response, sizeof(response), "Error: Image path must be the first argument\n");
                break;
            }
            /* Optional: preflight check for readability to improve error clarity */
            if (access(path, R_OK) != 0) {
                /* Don't hard fail if app can load non-files, but provide a useful error */
                /* Continue; hyprlax_add_layer may still handle this (e.g., memory textures or after chdir) */
            }

            /* Parse arbitrary key[= ]value pairs and apply later */
            const int MAX_KV = 64;
            const char *keys[MAX_KV];
            const char *vals[MAX_KV];
            int kvn = 0; int parse_err = 0;
            char *pending = NULL;
            char *param;
            while (!parse_err && (param = strtok(NULL, " \n"))) {
                char *eq = strchr(param, '=');
                if (!eq) {
                    if (!pending) { pending = param; }
                    else {
                        if (kvn < MAX_KV) { keys[kvn] = pending; vals[kvn] = param; kvn++; }
                        pending = NULL;
                    }
                } else {
                    *eq = '\0';
                    const char *k = param; const char *v = eq + 1;
                    if (!*k) { snprintf(response, sizeof(response), "Error: empty key in parameter\n"); parse_err = 1; break; }
                    if (kvn < MAX_KV) { keys[kvn] = k; vals[kvn] = v; kvn++; }
                }
            }
            if (!parse_err && pending) { snprintf(response, sizeof(response), "Error: '%s' requires a value\n", pending); parse_err = 1; }
            if (parse_err) { break; }

            /* Bridge to application context */
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            if (!app) {
                ipc_errorf(response, sizeof(response), 1300, "Runtime context unavailable\n");
                break;
            }
            /* Capture previous max id */
            uint32_t prev_max_id = 0;
            for (parallax_layer_t *it = app->layers; it; it = it->next) {
                if (it->id > prev_max_id) prev_max_id = it->id;
            }
            int rc_add = hyprlax_add_layer(app, path, 1.0f, 1.0f, 0.0f);
            if (rc_add == 0) {
                /* Find the new layer and apply initial params */
                uint32_t new_id = 0; parallax_layer_t *new_layer = NULL;
                for (parallax_layer_t *it = app->layers; it; it = it->next) {
                    if (it->id > prev_max_id) { new_id = it->id; new_layer = it; }
                }
                if (new_layer) {
                    /* Apply all provided properties */
                    for (int i = 0; i < kvn; i++) {
                        if (!apply_layer_property(app, new_layer, keys[i], vals[i], response, sizeof(response))) {
                            /* On first error, report and stop */
                            break;
                        }
                    }
                    /* Re-sort after potential z changes */
                    app->layers = layer_list_sort_by_z(app->layers);
                }
                /* Maintain draw order by z */
                app->layers = layer_list_sort_by_z(app->layers);
                if (response[0] && strncmp(response, "Error", 5) == 0) { success = false; }
                else { snprintf(response, sizeof(response), new_layer ? "Layer added with ID: %u\n" : "Layer added\n", new_id); success = true; }
            } else {
                ipc_errorf(response, sizeof(response), 1110, "Failed to add layer\n");
            }
            break;
        }

        case IPC_CMD_REMOVE_LAYER: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) {
                ipc_errorf(response, sizeof(response), 1101, "Layer ID required\n");
                break;
            }

            uint32_t id = 0;
            if (!parse_u32(id_str, &id)) {
                ipc_errorf(response, sizeof(response), 1101, "Invalid layer ID\n");
                break;
            }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            if (app && layer_list_find(app->layers, id)) {
                hyprlax_remove_layer(app, id);
                snprintf(response, sizeof(response), "Layer %u removed\n", id);
                success = true;
            } else {
                ipc_errorf(response, sizeof(response), 1102, "Layer %u not found\n", id);
            }
            break;
        }

        case IPC_CMD_MODIFY_LAYER: {
            char* id_str = strtok(NULL, " \n");
            char* property = strtok(NULL, " \n");
            char* value = strtok(NULL, " \n");

            if (!id_str || !property || !value) {
                ipc_errorf(response, sizeof(response), 1200, "Usage: modify <id> <property> <value>\n");
                break;
            }

            if (token_check_len(property, IPC_MAX_PROP_LEN, "property", response, sizeof(response))) break;
            if (token_check_len(value, IPC_MAX_VALUE_LEN, "value", response, sizeof(response))) break;

            uint32_t id = 0;
            if (!parse_u32(id_str, &id)) {
                ipc_errorf(response, sizeof(response), 1101, "Invalid layer ID\n");
                break;
            }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            parallax_layer_t *layer = app ? layer_list_find(app->layers, id) : NULL;
            if (!layer) {
                ipc_errorf(response, sizeof(response), 1102, "Failed to modify layer %u\n", id);
                break;
            }
            if (apply_layer_property(app, layer, property, value, response, sizeof(response))) {
                /* Re-sort list by z if z was modified */
                if (strcmp(property, "z") == 0) app->layers = layer_list_sort_by_z(app->layers);
                snprintf(response, sizeof(response), "Layer %u modified\n", id); success = true;
            }
            break;
        }

        case IPC_CMD_LIST_LAYERS: {
            if (getenv("HYPRLAX_DEBUG")) {
                hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
                fprintf(stderr, "[DEBUG] IPC LIST entering: app=%p layers=%p count=%d\n", (void*)app, app? (void*)app->layers:NULL, app?app->layer_count:ctx->layer_count);
            }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            if (!app || !app->layers) { snprintf(response, sizeof(response), "No layers\n"); success = true; break; }

            /* Parse optional flags: --json, --long, --filter expr */
            bool json = false, longf = false;
            int filter_id = -1; int filter_hidden = -1; const char *filter_path = NULL;
            char *opt;
            while ((opt = strtok(NULL, " \n"))) {
                if (strcmp(opt, "--json") == 0 || strcmp(opt, "-j") == 0) json = true;
                else if (strcmp(opt, "--long") == 0 || strcmp(opt, "-l") == 0) longf = true;
                else if (strcmp(opt, "--filter") == 0 || strcmp(opt, "-f") == 0) {
                    char *expr = strtok(NULL, " \n");
                    if (expr) {
                        if (token_check_len(expr, 256, "filter", response, sizeof(response))) { success=false; break; }
                        if (!strncmp(expr, "id=", 3)) { filter_id = atoi(expr+3); }
                        else if (!strncmp(expr, "hidden=", 7)) {
                            const char *v = expr+7; filter_hidden = (!strcmp(v,"1")||!strcasecmp(v,"true")||!strcasecmp(v,"yes"))?1:0;
                        } else if (!strncmp(expr, "path~=", 6)) {
                            filter_path = expr + 6;
                        }
                    }
                }
            }

            size_t off = 0; response[0] = '\0';
            if (json) {
                off += snprintf(response + off, sizeof(response) - off, "[");
                bool first = true;
                int guard = 0; for (parallax_layer_t *it = app->layers; it && guard < (app->layer_count + 4); it = it->next, guard++) {
                    if (getenv("HYPRLAX_DEBUG") && guard == 0) {
                        fprintf(stderr, "[DEBUG]   LIST head: id=%u next=%p\n", it->id, (void*)it->next);
                    }
                    if (filter_id >= 0 && (int)it->id != filter_id) continue;
                    if (filter_hidden >= 0 && (it->hidden ? 1 : 0) != filter_hidden) continue;
                    if (filter_path && (!it->image_path || !strstr(it->image_path, filter_path))) continue;
                    if (!first) { if (off + 1 < sizeof(response)) response[off++] = ','; }
                    first = false;
                    int eff_over = (it->overflow_mode >= 0) ? it->overflow_mode : app->config.render_overflow_mode;
                    const char *over_s = (eff_over==0?"repeat_edge": eff_over==1?"repeat": eff_over==2?"repeat_x": eff_over==3?"repeat_y": eff_over==4?"none":"inherit");
                    int eff_tile_x;
                    int eff_tile_y;
                    if (it->tile_x >= 0) eff_tile_x = it->tile_x;
                    else {
                        if (eff_over == 1 || eff_over == 2) eff_tile_x = 1; else if (eff_over == 3) eff_tile_x = 0; else eff_tile_x = app->config.render_tile_x;
                    }
                    if (it->tile_y >= 0) eff_tile_y = it->tile_y;
                    else {
                        if (eff_over == 1 || eff_over == 3) eff_tile_y = 1; else if (eff_over == 2) eff_tile_y = 0; else eff_tile_y = app->config.render_tile_y;
                    }
                    float eff_mx = (it->margin_px_x != 0.0f || it->margin_px_y != 0.0f) ? it->margin_px_x : app->config.render_margin_px_x;
                    float eff_my = (it->margin_px_x != 0.0f || it->margin_px_y != 0.0f) ? it->margin_px_y : app->config.render_margin_px_y;
                    const char *fit_s = (it->fit_mode==LAYER_FIT_STRETCH?"stretch": it->fit_mode==LAYER_FIT_COVER?"cover": it->fit_mode==LAYER_FIT_CONTAIN?"contain": it->fit_mode==LAYER_FIT_WIDTH?"fit_width":"fit_height");
                    char esc[512]; json_escape(it->image_path ? it->image_path : "<memory>", esc, sizeof(esc));
                    int w = snprintf(response + off, sizeof(response) - off,
                        "{\"id\":%u,\"path\":\"%s\",\"shift\":%.3f,\"opacity\":%.3f,\"z\":%d,\"uv\":[%.4f,%.4f],\"fit\":\"%s\",\"align\":[%.3f,%.3f],\"content_scale\":%.3f,\"blur\":%.3f,\"overflow\":\"%s\",\"tile\":[%s,%s],\"margin\":[%.1f,%.1f],\"hidden\":%s,\"tint\":[%.3f,%.3f,%.3f,%.3f]}",
                        it->id, esc, it->shift_multiplier, it->opacity, it->z_index,
                        it->base_uv_x, it->base_uv_y, fit_s, it->align_x, it->align_y, it->content_scale, it->blur_amount,
                        over_s, eff_tile_x?"true":"false", eff_tile_y?"true":"false", eff_mx, eff_my, it->hidden?"true":"false",
                        it->tint_r, it->tint_g, it->tint_b, it->tint_strength);
                    if (w < 0 || off + (size_t)w >= sizeof(response)) { break; }
                    off += (size_t)w;
                }
                if (off + 2 < sizeof(response)) { response[off++] = ']'; response[off++]='\n'; response[off]='\0'; }
                success = true;
            } else if (longf) {
                for (parallax_layer_t *it = app->layers; it; it = it->next) {
                    if (filter_id >= 0 && (int)it->id != filter_id) continue;
                    if (filter_hidden >= 0 && (it->hidden ? 1 : 0) != filter_hidden) continue;
                    if (filter_path && (!it->image_path || !strstr(it->image_path, filter_path))) continue;
                    int eff_over = (it->overflow_mode >= 0) ? it->overflow_mode : app->config.render_overflow_mode;
                    const char *over_s = (eff_over==0?"repeat_edge": eff_over==1?"repeat": eff_over==2?"repeat_x": eff_over==3?"repeat_y": eff_over==4?"none":"inherit");
                    int eff_tile_x;
                    int eff_tile_y;
                    if (it->tile_x >= 0) eff_tile_x = it->tile_x;
                    else {
                        if (eff_over == 1 || eff_over == 2) eff_tile_x = 1; else if (eff_over == 3) eff_tile_x = 0; else eff_tile_x = app->config.render_tile_x;
                    }
                    if (it->tile_y >= 0) eff_tile_y = it->tile_y;
                    else {
                        if (eff_over == 1 || eff_over == 3) eff_tile_y = 1; else if (eff_over == 2) eff_tile_y = 0; else eff_tile_y = app->config.render_tile_y;
                    }
                    float eff_mx = (it->margin_px_x != 0.0f || it->margin_px_y != 0.0f) ? it->margin_px_x : app->config.render_margin_px_x;
                    float eff_my = (it->margin_px_x != 0.0f || it->margin_px_y != 0.0f) ? it->margin_px_y : app->config.render_margin_px_y;
                    const char *fit_s = (it->fit_mode==LAYER_FIT_STRETCH?"stretch": it->fit_mode==LAYER_FIT_COVER?"cover": it->fit_mode==LAYER_FIT_CONTAIN?"contain": it->fit_mode==LAYER_FIT_WIDTH?"fit_width":"fit_height");
                    int w = snprintf(response + off, sizeof(response) - off,
                                     "ID: %u | Path: %s | Shift Multiplier: %.2f | Opacity: %.2f | Z: %d | UV Offset: %.3f,%.3f | Fit: %s | Align: %.2f,%.2f | Content Scale: %.2f | Blur: %.2f | Overflow: %s | Tile: %s/%s | Margin Px: %.1f,%.1f | Visible: %s | Tex: %u | Size: %dx%d | Tint: #%02x%02x%02x:%.2f\n",
                                     it->id, it->image_path ? it->image_path : "<memory>",
                                     it->shift_multiplier, it->opacity, it->z_index,
                                     it->base_uv_x, it->base_uv_y,
                                     fit_s, it->align_x, it->align_y, it->content_scale,
                                     it->blur_amount,
                                     over_s,
                                     eff_tile_x?"true":"false", eff_tile_y?"true":"false",
                                     eff_mx, eff_my,
                                     it->hidden?"yes":"no",
                                     (unsigned int)it->texture_id, it->width, it->height,
                                     (int)(it->tint_r*255.0f+0.5f), (int)(it->tint_g*255.0f+0.5f), (int)(it->tint_b*255.0f+0.5f), it->tint_strength);
                    if (w < 0 || off + (size_t)w >= sizeof(response)) { break; }
                    off += (size_t)w;
                }
                success = true;
            } else {
                /* Compact */
                for (parallax_layer_t *it = app->layers; it; it = it->next) {
                    if (filter_id >= 0 && (int)it->id != filter_id) continue;
                    if (filter_hidden >= 0 && (it->hidden ? 1 : 0) != filter_hidden) continue;
                    if (filter_path && (!it->image_path || !strstr(it->image_path, filter_path))) continue;
                    int w = snprintf(response + off, sizeof(response) - off,
                                     "%u z=%d op=%.2f shift_multiplier=%.2f blur=%.2f vis=%s path=%s\n",
                                     it->id, it->z_index, it->opacity, it->shift_multiplier, it->blur_amount,
                                     it->hidden?"y":"n", it->image_path ? it->image_path : "<memory>");
                    if (w < 0 || off + (size_t)w >= sizeof(response)) { break; }
                    off += (size_t)w;
                }
                success = true;
            }
            break;
        }

        case IPC_CMD_LAYER_FRONT: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) { snprintf(response, sizeof(response), "Error: Layer ID required\n"); break; }
            uint32_t id = 0; if (!parse_u32(id_str, &id)) { snprintf(response, sizeof(response), "Error: Invalid layer ID\n"); break; }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            parallax_layer_t *layer = app ? layer_list_find(app->layers, id) : NULL;
            if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
            /* Find current max z */
            int maxz = layer->z_index;
            for (parallax_layer_t *it = app->layers; it; it = it->next)
                if (it->z_index > maxz) maxz = it->z_index;
            layer->z_index = maxz + 1;
            renorm_z(app);
            snprintf(response, sizeof(response), "Layer %u brought to front\n", id);
            success = true;
            break;
        }

        case IPC_CMD_LAYER_BACK: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) { snprintf(response, sizeof(response), "Error: Layer ID required\n"); break; }
            uint32_t id = 0; if (!parse_u32(id_str, &id)) { snprintf(response, sizeof(response), "Error: Invalid layer ID\n"); break; }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            parallax_layer_t *layer = app ? layer_list_find(app->layers, id) : NULL;
            if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
            /* Find current min z */
            int minz = layer->z_index;
            for (parallax_layer_t *it = app->layers; it; it = it->next)
                if (it->z_index < minz) minz = it->z_index;
            layer->z_index = minz - 1;
            renorm_z(app);
            snprintf(response, sizeof(response), "Layer %u sent to back\n", id);
            success = true;
            break;
        }

        case IPC_CMD_LAYER_UP: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) { snprintf(response, sizeof(response), "Error: Layer ID required\n"); break; }
            uint32_t id = 0; if (!parse_u32(id_str, &id)) { snprintf(response, sizeof(response), "Error: Invalid layer ID\n"); break; }
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            parallax_layer_t *layer = app ? layer_list_find(app->layers, id) : NULL;
            if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
            /* Find next higher z neighbor */
            parallax_layer_t *neighbor = NULL;
            int min_higher = INT_MAX;
            for (parallax_layer_t *it = app->layers; it; it = it->next) {
                if (it->z_index > layer->z_index && it->z_index < min_higher) { neighbor = it; min_higher = it->z_index; }
            }
            if (!neighbor) { snprintf(response, sizeof(response), "Layer %u already at front\n", id); success = true; break; }
            int tmp = layer->z_index; layer->z_index = neighbor->z_index; neighbor->z_index = tmp;
            renorm_z(app);
            snprintf(response, sizeof(response), "Layer %u moved up\n", id);
            success = true;
            break;
        }

        case IPC_CMD_LAYER_DOWN: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) { snprintf(response, sizeof(response), "Error: Layer ID required\n"); break; }
            uint32_t id = (uint32_t)atoi(id_str);
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            parallax_layer_t *layer = app ? layer_list_find(app->layers, id) : NULL;
            if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
            /* Find next lower z neighbor */
            parallax_layer_t *neighbor = NULL;
            int max_lower = INT_MIN;
            for (parallax_layer_t *it = app->layers; it; it = it->next) {
                if (it->z_index < layer->z_index && it->z_index > max_lower) { neighbor = it; max_lower = it->z_index; }
            }
            if (!neighbor) { snprintf(response, sizeof(response), "Layer %u already at back\n", id); success = true; break; }
            int tmp = layer->z_index; layer->z_index = neighbor->z_index; neighbor->z_index = tmp;
            renorm_z(app);
            snprintf(response, sizeof(response), "Layer %u moved down\n", id);
            success = true;
            break;
        }

        case IPC_CMD_CLEAR_LAYERS:
            {
                hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
                if (!app) { snprintf(response, sizeof(response), "Error: Runtime context unavailable\n"); break; }
                while (app->layers) { uint32_t id = app->layers->id; hyprlax_remove_layer(app, id); }
                snprintf(response, sizeof(response), "All layers cleared\n");
                success = true;
                break;
            }

        case IPC_CMD_GET_STATUS:
            {
                /* Parse optional --json */
                bool json = false;
                char *opt;
                while ((opt = strtok(NULL, " \n"))) {
                    if (strcmp(opt, "--json") == 0 || strcmp(opt, "-j") == 0) json = true;
                }

                hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
                int layers = app ? app->layer_count : 0;
                const char *comp = (app && app->compositor && app->compositor->ops && app->compositor->ops->get_name) ? app->compositor->ops->get_name() : "unknown";
                char parallax_inputs[64];
                format_parallax_inputs(app ? &app->config : NULL, parallax_inputs, sizeof(parallax_inputs));
                int monitors = (app && app->monitors) ? app->monitors->count : 0;
                int target_fps = app ? app->config.target_fps : 0;
                double fps = app ? app->fps : 0.0;
                bool vsync = app ? app->config.vsync : false;
                bool debug = app ? app->config.debug : false;
                if (json) {
                    size_t off = 0; response[0] = '\0';
                    /* Top-level compositor capabilities (detected) */
                    compositor_capabilities_t tcaps = {0};
                    int ctype = (app && app->compositor) ? app->compositor->type : COMPOSITOR_AUTO;
                    (void)workspace_detect_capabilities(ctype, &tcaps);

                    off += snprintf(response + off, sizeof(response) - off,
                        "{\"running\":true,\"layers\":%d,\"target_fps\":%d,\"fps\":%.2f,\"parallax_input\":\"%s\",\"compositor\":\"%s\",\"socket\":\"%s\",\"vsync\":%s,\"debug\":%s,\"caps\":{\"steal\":%s,\"move\":%s,\"split\":%s,\"wsets\":%s,\"tags\":%s,\"vstack\":%s},\"monitors\":[",
                        layers, target_fps, fps, parallax_inputs, comp, ctx->socket_path, vsync?"true":"false", debug?"true":"false",
                        tcaps.can_steal_workspace?"true":"false",
                        tcaps.supports_workspace_move?"true":"false",
                        tcaps.has_split_plugin?"true":"false",
                        tcaps.has_wsets_plugin?"true":"false",
                        tcaps.supports_tags?"true":"false",
                        tcaps.supports_vertical_stack?"true":"false");
                    /* Append monitors */
                    if (app && app->monitors) {
                        monitor_instance_t *m = app->monitors->head; bool first = true;
                        while (m && off + 64 < sizeof(response)) {
                            if (!first) { response[off++] = ','; }
                            first = false;
                            off += snprintf(response + off, sizeof(response) - off,
                                "{\"name\":\"%s\",\"size\":[%d,%d],\"pos\":[%d,%d],\"scale\":%d,\"refresh\":%d,\"caps\":{\"steal\":%s,\"move\":%s,\"split\":%s,\"wsets\":%s,\"tags\":%s,\"vstack\":%s}}",
                                m->name, m->width, m->height, m->global_x, m->global_y, m->scale, m->refresh_rate,
                                m->capabilities.can_steal_workspace?"true":"false",
                                m->capabilities.supports_workspace_move?"true":"false",
                                m->capabilities.has_split_plugin?"true":"false",
                                m->capabilities.has_wsets_plugin?"true":"false",
                                m->capabilities.supports_tags?"true":"false",
                                m->capabilities.supports_vertical_stack?"true":"false");
                            m = m->next;
                        }
                    }
                    if (off + 2 < sizeof(response)) { response[off++] = ']'; response[off++]='}'; response[off++]='\n'; response[off]='\0'; }
                } else {
                    snprintf(response, sizeof(response),
                             "Status: Active\nhyprlax running\nLayers: %d\nTarget FPS: %d\nFPS: %.1f\nParallax Inputs: %s\nMonitors: %d\nCompositor: %s\nSocket: %s\n",
                             layers, target_fps, fps, parallax_inputs, monitors, comp, ctx->socket_path);
                }
                success = true;
                break;
            }

        case IPC_CMD_RELOAD_CONFIG: {
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            if (!app) { ipc_errorf(response, sizeof(response), 1400, "No configuration path set\n"); success=false; break; }
            int rc = hyprlax_reload_config(app);
            if (rc == 0) { snprintf(response, sizeof(response), "Configuration reloaded\n"); success = true; }
            else { ipc_errorf(response, sizeof(response), 1401, "Failed to reload configuration\n"); success=false; }
            break;
        }

        case IPC_CMD_SET_PROPERTY: {
            char* property = strtok(NULL, " \n");
            char* value = strtok(NULL, " \n");

            if (!property || !value) {
                ipc_errorf(response, sizeof(response), 1202, "Usage: set <property> <value>\n");
                break;
            }

            if (token_check_len(property, IPC_MAX_PROP_LEN, "property", response, sizeof(response))) break;
            if (token_check_len(value, IPC_MAX_VALUE_LEN, "value", response, sizeof(response))) break;

            hyprlax_context_t *app_set = (hyprlax_context_t*)ctx->app_context;
            if (!app_set) { ipc_errorf(response, sizeof(response), 1300, "Runtime settings not available\n"); break; }
            bool handled = false;
            if (strcmp(property, "fps") == 0 || strcmp(property, "render.fps") == 0) {
                int iv = 0; int rc = parse_int_range(value, 30, 240, &iv);
                if (rc <= 0) { ipc_errorf(response, sizeof(response), rc==0?1210:1211, rc==0?"invalid fps\n":"fps out of range (30..240)\n"); break; }
                app_set->config.target_fps = iv; handled = true;
            }
            else if (strcmp(property, "render.content_scale") == 0 || strcmp(property, "content_scale_default") == 0 || strcmp(property, "content_scale") == 0) {
                double dv = 0.0; int rc = parse_double_range(value, 0.01, 10.0, &dv);
                if (rc <= 0) { ipc_errorf(response, sizeof(response), rc==0?1253:1253, "content_scale must be > 0\n"); break; }
                app_set->config.scale_factor = (float)dv;
                /* Update layers that still inherit global */
                for (parallax_layer_t *it = app_set->layers; it; it = it->next) {
                    if (!it->scale_is_custom) it->content_scale = app_set->config.scale_factor;
                }
                handled = true;
            }
            else if (strcmp(property, "shift") == 0 || strcmp(property, "shift_percent") == 0 || strcmp(property, "parallax.shift_percent") == 0) {
                double dv = 0.0; int rc = parse_double_range(value, 0.0, 100.0, &dv);
                if (rc <= 0) { ipc_errorf(response, sizeof(response), rc==0?1212:1213, rc==0?"invalid shift\n":"shift out of range (0..100)\n"); break; }
                /* Auto-detect percentage vs pixels */
                if (dv <= 10.0) {
                    app_set->config.shift_percent = (float)dv;
                    app_set->config.shift_pixels = 0.0f;
                } else {
                    /* Backwards compatibility: treat large values as pixels */
                    app_set->config.shift_pixels = (float)dv;
                    app_set->config.shift_percent = 0.0f;
                }
                handled = true;
            }
            else if (strcmp(property, "shift_pixels") == 0 || strcmp(property, "parallax.shift_pixels") == 0) {
                /* Deprecated: explicit pixel-based shift */
                double dv = 0.0; int rc = parse_double_range(value, 0.0, 1000.0, &dv);
                if (rc <= 0) { ipc_errorf(response, sizeof(response), rc==0?1212:1213, rc==0?"invalid shift\n":"shift out of range (0..1000)\n"); break; }
                app_set->config.shift_pixels = (float)dv;
                app_set->config.shift_percent = 0.0f;
                handled = true;
            }
            else if (strcmp(property, "duration") == 0 || strcmp(property, "animation.duration") == 0) {
                double dv = 0.0; int rc = parse_double_range(value, 0.1, 10.0, &dv);
                if (rc <= 0) { ipc_errorf(response, sizeof(response), rc==0?1214:1215, rc==0?"invalid duration\n":"duration out of range (0.1..10.0)\n"); break; }
                app_set->config.animation_duration = (float)dv; handled = true;
            }
            else if (strcmp(property, "easing") == 0 || strcmp(property, "animation.easing") == 0) { app_set->config.default_easing = easing_from_string(value); handled = true; }
            if (!handled) {
                int rc = hyprlax_runtime_set_property(app_set, property, value);
                if (rc == 0) { snprintf(response, sizeof(response), "OK\n"); success = true; break; }
                ipc_errorf(response, sizeof(response), 1216, "Unknown/invalid property '%s'\n", property);
                break;
            }
            snprintf(response, sizeof(response), "OK\n");
            success = true;
            break;
        }

        case IPC_CMD_GET_PROPERTY: {
            char* property = strtok(NULL, " \n");

            if (!property) {
                ipc_errorf(response, sizeof(response), 1203, "Usage: get <property>\n");
                break;
            }

            if (token_check_len(property, IPC_MAX_PROP_LEN, "property", response, sizeof(response))) break;

            hyprlax_context_t *app_get = (hyprlax_context_t*)ctx->app_context;
            if (!app_get) { ipc_errorf(response, sizeof(response), 1300, "Runtime settings not available\n"); break; }
            if (strcmp(property, "fps") == 0 || strcmp(property, "render.fps") == 0) { snprintf(response, sizeof(response), "%d\n", app_get->config.target_fps); success = true; break; }
            if (strcmp(property, "render.content_scale") == 0 || strcmp(property, "content_scale_default") == 0 || strcmp(property, "content_scale") == 0) {
                snprintf(response, sizeof(response), "%.3f\n", app_get->config.scale_factor);
                success = true; break;
            }
            if (strcmp(property, "shift") == 0 || strcmp(property, "shift_percent") == 0 || strcmp(property, "parallax.shift_percent") == 0) {
                /* Return percentage if set, otherwise convert pixels to approximate percentage */
                if (app_get->config.shift_percent > 0.0f) {
                    snprintf(response, sizeof(response), "%.2f\n", app_get->config.shift_percent);
                } else if (app_get->config.shift_pixels > 0.0f && app_get->monitors && app_get->monitors->head) {
                    /* Convert pixels to percentage for display */
                    float percent = (app_get->config.shift_pixels / app_get->monitors->head->width) * 100.0f;
                    snprintf(response, sizeof(response), "%.2f (from %.0f px)\n", percent, app_get->config.shift_pixels);
                } else {
                    snprintf(response, sizeof(response), "1.5\n");  /* Default */
                }
                success = true;
                break;
            }
            if (strcmp(property, "shift_pixels") == 0 || strcmp(property, "parallax.shift_pixels") == 0) {
                snprintf(response, sizeof(response), "%.1f\n", app_get->config.shift_pixels);
                success = true;
                break;
            }
            if (strcmp(property, "duration") == 0 || strcmp(property, "animation.duration") == 0) { snprintf(response, sizeof(response), "%.3f\n", app_get->config.animation_duration); success = true; break; }
            if (strcmp(property, "easing") == 0 || strcmp(property, "animation.easing") == 0) { snprintf(response, sizeof(response), "%s\n", easing_to_string(app_get->config.default_easing)); success = true; break; }
            if (hyprlax_runtime_get_property(app_get, property, response, sizeof(response)) == 0) {
                size_t len = strlen(response); if (len < sizeof(response) - 1) response[len++] = '\n', response[len] = '\0';
                success = true;
            } else {
                ipc_errorf(response, sizeof(response), 1217, "Unknown property '%s'\n", property);
            }
            break;
        }

        case IPC_CMD_DIAG: {
            char *sub = strtok(NULL, " \n");
            if (!sub) { snprintf(response, sizeof(response), "Error: Usage: diag <subcmd> ...\n"); break; }
            if (strcmp(sub, "texinfo") == 0) {
                char *id_str = strtok(NULL, " \n");
                if (!id_str) { snprintf(response, sizeof(response), "Error: Usage: diag texinfo <id>\n"); break; }
                uint32_t id = 0; if (!parse_u32(id_str, &id)) { snprintf(response, sizeof(response), "Error: Invalid layer ID\n"); break; }
                hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
                if (!app) { snprintf(response, sizeof(response), "Error: Runtime context unavailable\n"); break; }
                parallax_layer_t *layer = layer_list_find(app->layers, id);
                if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
                const char *path = layer->image_path ? layer->image_path : "";
                int access_ok = (access(path, R_OK) == 0);
                struct stat st; int stat_ok = (stat(path, &st) == 0);
                int w=0,h=0,comp=0; int info_ok = 0; const char *fail = NULL;
                if (access_ok) {
                    info_ok = stbi_info(path, &w, &h, &comp);
                    if (!info_ok) fail = stbi_failure_reason();
                }
                snprintf(response, sizeof(response),
                         "{\"id\":%u,\"path\":\"%s\",\"tex\":%u,\"size\":[%d,%d],\"access\":%s,\"stat\":{\"ok\":%s,\"size\":%lld,\"mtime\":%lld},\"stbi\":{\"ok\":%s,\"w\":%d,\"h\":%d,\"comp\":%d,\"err\":\"%s\"}}\n",
                         id, path, (unsigned int)layer->texture_id, layer->width, layer->height,
                         access_ok?"true":"false",
                         stat_ok?"true":"false",
                         stat_ok?(long long)st.st_size:0LL,
                         stat_ok?(long long)st.st_mtime:0LL,
                         info_ok?"true":"false", w, h, comp, fail?fail:"");
                success = true; break;
            } else if (strcmp(sub, "texload") == 0) {
                char *id_str = strtok(NULL, " \n");
                if (!id_str) { snprintf(response, sizeof(response), "Error: Usage: diag texload <id>\n"); break; }
                uint32_t id = 0; if (!parse_u32(id_str, &id)) { snprintf(response, sizeof(response), "Error: Invalid layer ID\n"); break; }
                hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
                if (!app) { snprintf(response, sizeof(response), "Error: Runtime context unavailable\n"); break; }
                parallax_layer_t *layer = layer_list_find(app->layers, id);
                if (!layer) { snprintf(response, sizeof(response), "Error: Layer %u not found\n", id); break; }
                char prop[64]; snprintf(prop, sizeof(prop), "layer.%u.path", id);
                int rc = hyprlax_runtime_set_property(app, prop, layer->image_path ? layer->image_path : "");
                if (rc == 0) { snprintf(response, sizeof(response), "OK\n"); success = true; }
                else { snprintf(response, sizeof(response), "Error: texload failed\n"); }
                break;
            } else {
                snprintf(response, sizeof(response), "Error: Unknown diag subcommand '%s'\n", sub);
            }
            break;
        }

        case IPC_CMD_COMPUTED: {
            hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
            if (!app) { snprintf(response, sizeof(response), "Error: Runtime context unavailable\n"); break; }

            monitor_instance_t *mon = (app->monitors && app->monitors->primary) ? app->monitors->primary : (app->monitors ? app->monitors->head : NULL);
            parallax_layer_t *layer = app->layers;
            int screen_w = mon ? mon->width : HYPRLAX_DEFAULT_MON_WIDTH;
            int screen_h = mon ? mon->height : HYPRLAX_DEFAULT_MON_HEIGHT;
            float screen_aspect = screen_h > 0 ? ((float)screen_w / (float)screen_h) : 1.7778f;
            float img_w = (layer && layer->width > 0) ? (float)layer->width : (float)screen_w;
            float img_h = (layer && layer->height > 0) ? (float)layer->height : (float)screen_h;
            float image_aspect = img_h > 0 ? (img_w / img_h) : screen_aspect;
            float scale = (layer && layer->content_scale > 0.0f) ? layer->content_scale : app->config.scale_factor;
            if (scale <= 0.0f) scale = HYPRLAX_DEFAULT_LAYER_SCALE;

            /* Visible UV window for cover fit */
            float uvw = 1.0f / scale; /* default for narrower images */
            if (image_aspect > screen_aspect) {
                uvw = (screen_aspect / image_aspect) * (1.0f / scale);
            }
            if (uvw > 1.0f) uvw = 1.0f; if (uvw < 0.0f) uvw = 0.0f;
            float margin_norm = 0.5f * (1.0f - uvw);
            float margin_px = margin_norm * scale * (float)screen_w;

            int wc = HYPRLAND_DEFAULT_WORKSPACE_COUNT;
            if (app->compositor && app->compositor->ops && app->compositor->ops->get_workspace_count) {
                int c = app->compositor->ops->get_workspace_count();
                if (c > 1 && c < 1000) wc = c;
            }
            if (wc <= 1) wc = 2;

            float fudge = 0.90f; const char *envf = getenv("HYPRLAX_SAFE_SHIFT_FACTOR");
            if (envf && *envf) { float f = atof(envf); if (f > 0.0f && f <= 1.0f) fudge = f; }
            float auto_shift_px = (margin_px / (float)(wc - 1)) * fudge;
            float auto_shift_pct = (auto_shift_px / (float)screen_w) * 100.0f;

            float cfg_shift_px = app->config.shift_pixels;
            float cfg_shift_pct = app->config.shift_percent;
            int used_auto = (cfg_shift_px <= 0.0f && cfg_shift_pct <= 0.0f) ? 1 : 0;

            int n = snprintf(response, sizeof(response),
                             "monitor %s %dx%d\nlayer %u size %dx%d\nfit cover\ncontent_scale %.3f\nworkspaces %d\nuv_width_frac %.6f\nmargin_px %.2f\nauto_shift_px %.2f\nauto_shift_percent %.4f\nconfigured_shift_px %.2f\nconfigured_shift_percent %.4f\nmode %s\n",
                             mon ? mon->name : "<none>", screen_w, screen_h,
                             layer ? layer->id : 0, (int)img_w, (int)img_h,
                             scale, wc, uvw, margin_px, auto_shift_px, auto_shift_pct, cfg_shift_px, cfg_shift_pct,
                             used_auto ? "auto" : "configured");
            (void)n; success = true; break;
        }

        default:
            ipc_errorf(response, sizeof(response), 1002, "Unknown command '%s'\n", cmd);
            break;
    }

    // Send response
    send(client_fd, response, strlen(response), 0);
    close(client_fd);

    return success;
}

uint32_t ipc_add_layer(ipc_context_t* ctx, const char* image_path, float scale, float opacity, float x_offset, float y_offset, int z_index) {
    if (!ctx || !image_path) return 0;
    /* Fallback to local array when no app context (unit tests) */
    if (!ctx->app_context) {
        if (ctx->layer_count >= IPC_MAX_LAYERS) return 0;
        if (access(image_path, R_OK) != 0) return 0;
        layer_t *layer = calloc(1, sizeof(layer_t)); if (!layer) return 0;
        layer->image_path = strdup(image_path);
        layer->scale = scale; layer->opacity = opacity;
        layer->x_offset = x_offset; layer->y_offset = y_offset;
        layer->z_index = z_index; layer->visible = true;
        layer->id = ctx->next_layer_id++;
        ctx->layers[ctx->layer_count++] = layer;
        if (ctx->layer_count > 1) qsort(ctx->layers, ctx->layer_count, sizeof(layer_t*), layer_compare_qsort);
        return layer->id;
    }
    /* Bridged path */
    if (access(image_path, R_OK) != 0) { LOG_WARN("Image file not found or not readable: %s", image_path); return 0; }
    hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
    uint32_t prev_max_id = 0;
    for (parallax_layer_t *it = app->layers; it; it = it->next) {
        if (it->id > prev_max_id) prev_max_id = it->id;
    }
    if (hyprlax_add_layer(app, image_path, scale, opacity, 0.0f) != 0) {
        return 0;
    }
    uint32_t new_id = 0;
    for (parallax_layer_t *it = app->layers; it; it = it->next) {
        if (it->id > prev_max_id) new_id = it->id;
    }
    return new_id;
}

bool ipc_remove_layer(ipc_context_t* ctx, uint32_t layer_id) {
    if (!ctx) return false;
    if (!ctx->app_context) {
        for (int i = 0; i < ctx->layer_count; i++) {
            if (ctx->layers[i] && ctx->layers[i]->id == layer_id) {
                free(ctx->layers[i]->image_path); free(ctx->layers[i]);
                for (int j = i; j < ctx->layer_count - 1; j++) ctx->layers[j] = ctx->layers[j+1];
                ctx->layers[--ctx->layer_count] = NULL;
                return true;
            }
        }
        return false;
    }
    hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
    if (!layer_list_find(app->layers, layer_id)) return false;
    hyprlax_remove_layer(app, layer_id);
    return true;
}

bool ipc_modify_layer(ipc_context_t* ctx, uint32_t layer_id, const char* property, const char* value) {
    if (!ctx || !property || !value) return false;
    if (!ctx->app_context) {
        layer_t *layer = NULL;
        for (int i = 0; i < ctx->layer_count; i++) if (ctx->layers[i] && ctx->layers[i]->id == layer_id) { layer = ctx->layers[i]; break; }
        if (!layer) return false;
        bool needs_sort = false;
        if (strcmp(property, "scale") == 0) { layer->scale = atof(value); }
        else if (strcmp(property, "opacity") == 0) { layer->opacity = atof(value); }
        else if (strcmp(property, "path") == 0) { free(layer->image_path); layer->image_path = strdup(value); }
        else if (strcmp(property, "x") == 0) { layer->x_offset = atof(value); }
        else if (strcmp(property, "y") == 0) { layer->y_offset = atof(value); }
        else if (strcmp(property, "z") == 0) { layer->z_index = atoi(value); needs_sort = true; }
        else if (strcmp(property, "visible") == 0) { layer->visible = (!strcmp(value,"true")||!strcmp(value,"1")); }
        else if (strcmp(property, "hidden") == 0) { layer->visible = (!strcmp(value,"true")||!strcmp(value,"1")) ? false : true; }
        else if (strcmp(property, "blur") == 0) { /* no-op in fallback */ }
        else if (strcmp(property, "fit") == 0 || strcmp(property, "content_scale") == 0 || strcmp(property, "align_x") == 0 || strcmp(property, "align_y") == 0 ||
                 strcmp(property, "overflow") == 0 || strcmp(property, "tile.x") == 0 || strcmp(property, "tile.y") == 0 || strcmp(property, "margin.x") == 0 || strcmp(property, "margin.y") == 0) { /* ignore in fallback */ }
        else return false;
        if (needs_sort && ctx->layer_count > 1) qsort(ctx->layers, ctx->layer_count, sizeof(layer_t*), layer_compare_qsort);
        return true;
    }
    /* Bridged mode */
    hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
    parallax_layer_t *layer = layer_list_find(app->layers, layer_id);
    if (!layer) return false;
    if (strcmp(property, "scale") == 0) {
        float v = atof(value); layer->shift_multiplier = v; layer->shift_multiplier_x = v; layer->shift_multiplier_y = v; return true;
    } else if (strcmp(property, "opacity") == 0) {
        layer->opacity = atof(value); return true;
    } else if (strcmp(property, "x") == 0) {
        layer->base_uv_x = atof(value); return true;
    } else if (strcmp(property, "y") == 0) {
        layer->base_uv_y = atof(value); return true;
    } else if (strcmp(property, "z") == 0) {
        layer->z_index = atoi(value); return true;
    } else if (strcmp(property, "visible") == 0) {
        bool vis = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0); if (!vis) layer->opacity = 0.0f; return true;
    }
    return false;
}

char* ipc_list_layers(ipc_context_t* ctx) {
    if (!ctx) return NULL;
    if (!ctx->app_context) {
        if (ctx->layer_count == 0) return NULL;
        char *result = malloc(IPC_MAX_MESSAGE_SIZE); if (!result) return NULL;
        size_t off = 0; result[0]='\0';
        for (int i = 0; i < ctx->layer_count; i++) {
            layer_t *layer = ctx->layers[i]; if (!layer) continue;
            int w = snprintf(result + off, IPC_MAX_MESSAGE_SIZE - off,
                "ID: %u | Path: %s | Shift Multiplier: %.2f | Opacity: %.2f | Position: (%.2f, %.2f) | Z: %d | Visible: %s\n",
                layer->id, layer->image_path, layer->scale, layer->opacity,
                layer->x_offset, layer->y_offset, layer->z_index,
                layer->visible ? "yes" : "no");
            if (w < 0 || off + (size_t)w >= IPC_MAX_MESSAGE_SIZE) { break; }
            off += (size_t)w;
        }
        return result;
    }
    hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
    if (!app->layers) return NULL;
    char *out = malloc(IPC_MAX_MESSAGE_SIZE); if (!out) return NULL; out[0] = '\0'; size_t off = 0;
                int guard2 = 0; for (parallax_layer_t *it = app->layers; it && guard2 < (app->layer_count + 4); it = it->next, guard2++) {
                    int w = snprintf(out + off, IPC_MAX_MESSAGE_SIZE - off,
                         "ID: %u | Path: %s | Shift: %.2f | Opacity: %.2f | Z: %d\n",
                         it->id, it->image_path ? it->image_path : "<memory>", it->shift_multiplier, it->opacity, it->z_index);
        if (w < 0 || off + (size_t)w >= IPC_MAX_MESSAGE_SIZE)
            break;
        off += (size_t)w;
    }
    return out;
}

void ipc_clear_layers(ipc_context_t* ctx) {
    if (!ctx) return;
    if (!ctx->app_context) {
        for (int i = 0; i < ctx->layer_count; i++) {
            if (ctx->layers[i]) { free(ctx->layers[i]->image_path); free(ctx->layers[i]); ctx->layers[i]=NULL; }
        }
        ctx->layer_count = 0; return;
    }
    hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
    while (app->layers) {
        uint32_t id = app->layers->id;
        hyprlax_remove_layer(app, id);
    }
}

layer_t* ipc_find_layer(ipc_context_t* ctx, uint32_t layer_id) {
    if (!ctx) return NULL;
    if (ctx->app_context) return NULL;
    for (int i = 0; i < ctx->layer_count; i++) if (ctx->layers[i] && ctx->layers[i]->id == layer_id) return ctx->layers[i];
    return NULL;
}

/* Legacy comparator removed in bridged mode */

void ipc_sort_layers(ipc_context_t* ctx) { if (!ctx) return; if (ctx->layer_count > 1) qsort(ctx->layers, ctx->layer_count, sizeof(layer_t*), layer_compare_qsort); }

// Request handling function for tests and IPC processing
int ipc_handle_request(ipc_context_t* ctx, const char* request, char* response, size_t response_size) {
    if (!ctx || !request || !response || response_size == 0) {
        return -1;
    }

    // Parse command
    char cmd[64] = {0};
    char args[256] = {0};
    sscanf(request, "%63s %255[^\n]", cmd, args);

    if (strlen(cmd) == 0) {
        snprintf(response, response_size, "Error: Empty command");
        return -1;
    }

    // Handle ADD command
    if (strcmp(cmd, "ADD") == 0) {
        char path[256];
        float scale = 1.0f, opacity = 1.0f, blur = 0.0f;
        int count = sscanf(args, "%255s %f %f %f", path, &scale, &opacity, &blur);
        if (count < 1) {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1100): ADD requires at least an image path");
            else snprintf(response, response_size, "Error: ADD requires at least an image path");
            return -1;
        }

        uint32_t id = ipc_add_layer(ctx, path, scale, opacity, 0.0f, blur, 0);
        if (id > 0) {
            snprintf(response, response_size, "Layer added with ID: %u", id);
            return 0;
        } else {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1110): Failed to add layer");
            else snprintf(response, response_size, "Error: Failed to add layer");
            return -1;
        }
    }

    // Handle REMOVE command
    else if (strcmp(cmd, "REMOVE") == 0) {
        uint32_t id = 0;
        if (sscanf(args, "%u", &id) != 1) {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1101): REMOVE requires a layer ID");
            else snprintf(response, response_size, "Error: REMOVE requires a layer ID");
            return -1;
        }

        if (ipc_remove_layer(ctx, id)) {
            snprintf(response, response_size, "Layer %u removed", id);
            return 0;
        } else {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1102): Layer %u not found", id);
            else snprintf(response, response_size, "Error: Layer %u not found", id);
            return -1;
        }
    }

    // Handle MODIFY command
    else if (strcmp(cmd, "MODIFY") == 0) {
        uint32_t id = 0;
        char property[64], value[64];
        if (sscanf(args, "%u %63s %63s", &id, property, value) != 3) {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1200): MODIFY requires ID, property, and value");
            else snprintf(response, response_size, "Error: MODIFY requires ID, property, and value");
            return -1;
        }

        if (ipc_modify_layer(ctx, id, property, value)) {
            snprintf(response, response_size, "Layer %u modified", id);
            return 0;
        } else {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1201): Layer %u not found or invalid property", id);
            else snprintf(response, response_size, "Error: Layer %u not found or invalid property", id);
            return -1;
        }
    }

    // Handle LIST command
    else if (strcmp(cmd, "LIST") == 0) {
        char* list = ipc_list_layers(ctx);
        if (list) {
            snprintf(response, response_size, "%s", list);
            free(list);
            return 0;
        } else {
            snprintf(response, response_size, "No layers");
            return 0;
        }
    }

    // Handle CLEAR command
    else if (strcmp(cmd, "CLEAR") == 0) {
        ipc_clear_layers(ctx);
        snprintf(response, response_size, "All layers cleared");
        return 0;
    }

    // Handle STATUS command
    else if (strcmp(cmd, "STATUS") == 0) {
        hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
        int layers = app ? app->layer_count : ctx->layer_count;
        const char *comp = (app && app->compositor && app->compositor->ops && app->compositor->ops->get_name) ? app->compositor->ops->get_name() : "unknown";
        int target_fps = app ? app->config.target_fps : 60;
        double fps = app ? app->fps : 0.0;
        char parallax_inputs[64];
        format_parallax_inputs(app ? &app->config : NULL, parallax_inputs, sizeof(parallax_inputs));
        snprintf(response, response_size,
                 "hyprlax running\nLayers: %d\nTarget FPS: %d\nFPS: %.1f\nParallax inputs: %s\nCompositor: %s",
                 layers, target_fps, fps, parallax_inputs, comp);
        return 0;
    }

    // Handle RELOAD command
    else if (strcmp(cmd, "RELOAD") == 0) {
        hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
        if (app && app->config.config_path) {
            int rc = config_apply_toml_to_context(app, app->config.config_path);
            if (rc == 0) { snprintf(response, response_size, "Configuration reloaded"); return 0; }
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1401): Failed to reload configuration"); else snprintf(response, response_size, "Error: Failed to reload configuration"); return -1;
        } else {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1400): No configuration path set"); else snprintf(response, response_size, "Error: No configuration path set");
            return -1;
        }
    }

    // Handle SET_PROPERTY command
    else if (strcmp(cmd, "SET_PROPERTY") == 0) {
        char property[64], value[64];
        if (sscanf(args, "%63s %63s", property, value) != 2) {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1202): SET_PROPERTY requires property and value");
            else snprintf(response, response_size, "Error: SET_PROPERTY requires property and value");
            return -1;
        }
        hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
        if (!app) {
            if (strcmp(property, "fps") == 0 || strcmp(property, "shift") == 0 ||
                strcmp(property, "duration") == 0 || strcmp(property, "easing") == 0) {
                snprintf(response, response_size, "OK"); return 0; /* accept in fallback */
            }
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1216): Unknown/invalid property '%s'", property); else snprintf(response, response_size, "Error: Unknown/invalid property '%s'", property); return -1;
        }
        if (strcmp(property, "fps") == 0 || strcmp(property, "render.fps") == 0) { app->config.target_fps = atoi(value); snprintf(response, response_size, "OK"); return 0; }
        if (strcmp(property, "shift") == 0 || strcmp(property, "parallax.shift_pixels") == 0) { app->config.shift_pixels = atof(value); snprintf(response, response_size, "OK"); return 0; }
        if (strcmp(property, "duration") == 0 || strcmp(property, "animation.duration") == 0) { app->config.animation_duration = atof(value); snprintf(response, response_size, "OK"); return 0; }
        if (strcmp(property, "easing") == 0 || strcmp(property, "animation.easing") == 0) { app->config.default_easing = easing_from_string(value); snprintf(response, response_size, "OK"); return 0; }
        {
            int rc = hyprlax_runtime_set_property(app, property, value);
            if (rc == 0) { snprintf(response, response_size, "OK"); return 0; }
        }
        snprintf(response, response_size, "Error: Unknown/invalid property '%s'", property); return -1;
    }

    // Handle GET_PROPERTY command
    else if (strcmp(cmd, "GET_PROPERTY") == 0) {
        char property[64];
        if (sscanf(args, "%63s", property) != 1) {
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1203): GET_PROPERTY requires property name");
            else snprintf(response, response_size, "Error: GET_PROPERTY requires property name");
            return -1;
        }
        hyprlax_context_t *app = (hyprlax_context_t*)ctx->app_context;
        if (!app) {
            if (strcmp(property, "fps") == 0) { snprintf(response, response_size, "60"); return 0; }
            if (strcmp(property, "shift") == 0) { snprintf(response, response_size, "1.5\n"); return 0; }
            if (strcmp(property, "duration") == 0) { snprintf(response, response_size, "1.000"); return 0; }
            if (strcmp(property, "easing") == 0) { snprintf(response, response_size, "cubic"); return 0; }
            if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1217): Unknown property '%s'", property); else snprintf(response, response_size, "Error: Unknown property '%s'", property); return -1;
        }
        if (strcmp(property, "fps") == 0 || strcmp(property, "render.fps") == 0) { snprintf(response, response_size, "%d", app->config.target_fps); return 0; }
        if (strcmp(property, "shift") == 0 || strcmp(property, "parallax.shift_pixels") == 0) { snprintf(response, response_size, "%.1f", app->config.shift_pixels); return 0; }
        if (strcmp(property, "duration") == 0 || strcmp(property, "animation.duration") == 0) { snprintf(response, response_size, "%.3f", app->config.animation_duration); return 0; }
        if (strcmp(property, "easing") == 0 || strcmp(property, "animation.easing") == 0) { snprintf(response, response_size, "%s", easing_to_string(app->config.default_easing)); return 0; }
        {
            int rc = hyprlax_runtime_get_property(app, property, response, response_size);
            if (rc == 0) return 0;
        }
        snprintf(response, response_size, "Error: Unknown property '%s'", property); return -1;
    }

    // Unknown command
    else {
        if (ipc_error_codes_enabled()) snprintf(response, response_size, "Error(1002): Unknown command '%s'", cmd); else snprintf(response, response_size, "Error: Unknown command '%s'", cmd);
        return -1;
    }
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

/* Fallback comparator for local IPC layer array */
static int layer_compare_qsort(const void* a, const void* b) {
    layer_t* la = *(layer_t* const*)a;
    layer_t* lb = *(layer_t* const*)b;
    if (!la || !lb) return (la ? -1 : lb ? 1 : 0);
    return la->z_index - lb->z_index;
}
