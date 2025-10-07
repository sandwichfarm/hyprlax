/*
 * config_legacy.c - Legacy config reader and TOML converter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/config_legacy.h"

static char* xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void cfg_layers_append(legacy_cfg_t *cfg, legacy_layer_cfg_t *layer) {
    if (cfg->layers_count == cfg->layers_cap) {
        int nc = cfg->layers_cap ? cfg->layers_cap * 2 : 4;
        legacy_layer_cfg_t *nl = (legacy_layer_cfg_t*)realloc(cfg->layers, sizeof(*nl) * nc);
        if (!nl) return; /* OOM: drop silently; caller should check count */
        cfg->layers = nl; cfg->layers_cap = nc;
    }
    cfg->layers[cfg->layers_count++] = *layer;
}

static int ensure_parent_dir(const char *path) {
    /* Create parent directories of path (very simple: only one-level mkdir -p). */
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (!slash) return 0;
    *slash = '\0';
    /* mkdir -p like: try, ignore EEXIST */
    struct stat st;
    if (stat(tmp, &st) == 0) return 0;
    /* recursively make one more level if needed */
    char *p = tmp;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(tmp) > 0) mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0755);
    return 0;
}

static char* resolve_relative_to(const char *base_file, const char *maybe_rel) {
    if (!maybe_rel) return NULL;
    if (maybe_rel[0] == '/') return xstrdup(maybe_rel);
    char base[PATH_MAX];
    strncpy(base, base_file, sizeof(base)-1); base[sizeof(base)-1] = '\0';
    char *dir = strrchr(base, '/');
    if (!dir) return xstrdup(maybe_rel);
    *dir = '\0';
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, maybe_rel);
    char *rp = realpath(full, NULL);
    if (rp) return rp;
    return xstrdup(full);
}

static int path_is_under(const char *path, const char *dir) {
    size_t dlen = strlen(dir);
    return strncmp(path, dir, dlen) == 0 && (path[dlen] == '/' || path[dlen] == '\0');
}

static const char* filename_only(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Attempt to make a relative path from dst_dir to path. If not possible, return strdup(path). */
static char* relativize_to_dir(const char *path, const char *dst_dir) {
    char real_path[PATH_MAX];
    char real_dir[PATH_MAX];
    if (!realpath(path, real_path)) {
        strncpy(real_path, path, sizeof(real_path)-1); real_path[sizeof(real_path)-1] = '\0';
    }
    if (!realpath(dst_dir, real_dir)) {
        strncpy(real_dir, dst_dir, sizeof(real_dir)-1); real_dir[sizeof(real_dir)-1] = '\0';
    }
    size_t dl = strlen(real_dir);
    if (strncmp(real_path, real_dir, dl) == 0 && (real_path[dl] == '/' || real_path[dl] == '\0')) {
        const char *rest = real_path + dl;
        if (*rest == '/') rest++;
        if (*rest == '\0') return xstrdup(filename_only(path));
        return xstrdup(rest);
    }
    return xstrdup(real_path);
}

static void strip_inline_comment(char *s) {
    if (!s) return;
    /* Remove comments starting with '#' when at BOL or preceded by whitespace. */
    char *p = s;
    while (*p) {
        if (*p == '#') {
            int at_bol = (p == s);
            int prev_ws = (!at_bol && (p[-1] == ' ' || p[-1] == '\t'));
            if (at_bol || prev_ws) { *p = '\0'; break; }
        }
        p++;
    }
}

static int parse_line(char *line, int *lineno, legacy_cfg_t *cfg) {
    (void)lineno;
    /* Strip inline comments first */
    strip_inline_comment(line);
    /* Strip leading spaces */
    char *p = line; while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r') return 0;
    char *cmd = strtok(p, " \t\r\n");
    if (!cmd) return 0;
    if (strcmp(cmd, "layer") == 0) {
        const char *img = strtok(NULL, " \t\r\n");
        const char *shift = strtok(NULL, " \t\r\n");
        const char *opacity = strtok(NULL, " \t\r\n");
        const char *blur = strtok(NULL, " \t\r\n");
        if (!img) return 0;
        legacy_layer_cfg_t l = {0};
        l.path = resolve_relative_to(cfg->source_path, img);
        l.shift_multiplier = shift ? (float)atof(shift) : 1.0f;
        l.opacity = opacity ? (float)atof(opacity) : 1.0f;
        l.blur = blur ? (float)atof(blur) : 0.0f;
        cfg_layers_append(cfg, &l);
        return 0;
    }
    if (strcmp(cmd, "duration") == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_duration=1; cfg->duration=atof(v);} return 0; }
    if (strcmp(cmd, "shift")    == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_shift=1; cfg->shift=(float)atof(v);} return 0; }
    if (strcmp(cmd, "fps")      == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_fps=1; cfg->fps=atoi(v);} return 0; }
    if (strcmp(cmd, "vsync")    == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_vsync=1; cfg->vsync=atoi(v)!=0;} return 0; }
    if (strcmp(cmd, "easing")   == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_easing=1; strncpy(cfg->easing,v,sizeof(cfg->easing)-1);} return 0; }
    if (strcmp(cmd, "idle_poll_rate") == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_idle=1; cfg->idle_hz=(float)atof(v);} return 0; }
    if (strcmp(cmd, "scale")    == 0) { const char *v = strtok(NULL, " \t\r\n"); if (v) { cfg->have_scale=1; cfg->scale=(float)atof(v);} return 0; }
    return 0;
}

int legacy_config_read(const char *legacy_path, legacy_cfg_t *out_cfg, char *err, size_t err_sz) {
    if (err && err_sz) err[0] = '\0';
    if (!legacy_path || !out_cfg) return -1;
    FILE *f = fopen(legacy_path, "r");
    if (!f) { if (err) snprintf(err, err_sz, "open %s: %s", legacy_path, strerror(errno)); return -1; }
    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->source_path = xstrdup(legacy_path);
    char buf[1024]; int lineno = 0;
    while (fgets(buf, sizeof(buf), f)) {
        lineno++;
        parse_line(buf, &lineno, out_cfg);
    }
    fclose(f);
    return 0;
}

int legacy_config_write_toml(const legacy_cfg_t *cfg, const char *dst_path, char *err, size_t err_sz) {
    if (err && err_sz) err[0] = '\0';
    if (!cfg || !dst_path) return -1;
    ensure_parent_dir(dst_path);
    FILE *fp = fopen(dst_path, "w");
    if (!fp) { if (err) snprintf(err, err_sz, "write %s: %s", dst_path, strerror(errno)); return -1; }

    fprintf(fp, "# Converted from legacy hyprlax config\n\n[global]\n");
    if (cfg->have_fps)      fprintf(fp, "fps = %d\n", cfg->fps);
    if (cfg->have_duration) fprintf(fp, "duration = %.3f\n", cfg->duration);
    if (cfg->have_scale)    fprintf(fp, "scale = %.3f\n", cfg->scale);
    if (cfg->have_shift)    fprintf(fp, "shift = %.3f\n", cfg->shift);
    if (cfg->have_easing && cfg->easing[0]) fprintf(fp, "easing = \"%s\"\n", cfg->easing);
    if (cfg->have_vsync)    fprintf(fp, "vsync = %s\n", cfg->vsync ? "true" : "false");
    if (cfg->have_idle)     fprintf(fp, "idle_poll_rate = %.3f\n", cfg->idle_hz);
    fprintf(fp, "\n");

    /* Layers */
    /* Destination dir for relative conversion */
    char dst_dir[PATH_MAX];
    strncpy(dst_dir, dst_path, sizeof(dst_dir)-1); dst_dir[sizeof(dst_dir)-1] = '\0';
    char *slash = strrchr(dst_dir, '/'); if (slash) *slash = '\0'; else strcpy(dst_dir, ".");

    for (int i = 0; i < cfg->layers_count; i++) {
        const legacy_layer_cfg_t *l = &cfg->layers[i];
        char *rel = relativize_to_dir(l->path, dst_dir);
        fprintf(fp, "[[global.layers]]\n");
        fprintf(fp, "path = \"%s\"\n", rel ? rel : (l->path ? l->path : ""));
        fprintf(fp, "shift_multiplier = %.3f\n", l->shift_multiplier == 0.0f ? 1.0f : l->shift_multiplier);
        fprintf(fp, "opacity = %.3f\n", l->opacity == 0.0f ? 1.0f : l->opacity);
        fprintf(fp, "blur = %.3f\n", l->blur);
        if (cfg->have_scale) fprintf(fp, "scale = %.3f\n", cfg->scale);
        fprintf(fp, "\n");
        free(rel);
    }
    fclose(fp);
    return 0;
}

void legacy_config_free(legacy_cfg_t *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->layers_count; i++) {
        free(cfg->layers[i].path);
    }
    free(cfg->layers); cfg->layers = NULL;
    cfg->layers_count = cfg->layers_cap = 0;
    free(cfg->source_path); cfg->source_path = NULL;
}

int legacy_paths_default(char *legacy_path, size_t lsz, char *toml_path, size_t tsz) {
    const char *home = getenv("HOME");
    if (!home) return -1;
    if (legacy_path && lsz > 0)
        snprintf(legacy_path, lsz, "%s/.config/hyprlax/parallax.conf", home);
    if (toml_path && tsz > 0)
        snprintf(toml_path, tsz, "%s/.config/hyprlax/hyprlax.toml", home);
    return 0;
}
