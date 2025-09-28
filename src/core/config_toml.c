/*
 * config_toml.c - TOML configuration loader
 *
 * Minimal scaffolding to parse core [global] values and [[global.layers]]
 * using tomlc99. Designed to be forward-compatible with Issue #28.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>

#include "../include/log.h"
#include "../include/core.h"
#include "../include/hyprlax.h"
#include "../include/config_toml.h"
#include "../vendor/toml.h"

/* Resolve a path relative to the TOML config file's directory. */
static char* resolve_relative_path(const char *config_path, const char *rel)
{
    if (!rel) return NULL;
    if (rel[0] == '/') {
        return strdup(rel);
    }

    char *config_copy = strdup(config_path);
    if (!config_copy) return NULL;

    char *dir = dirname(config_copy);
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/%s", dir ? dir : ".", rel);
    free(config_copy);

    char *resolved = realpath(buf, NULL);
    if (resolved) return resolved;
    return strdup(buf);
}

static void parse_global_table(toml_table_t *global, config_t *cfg)
{
    if (!global || !cfg) return;

    toml_datum_t d;

    /* Legacy flat keys (kept for compatibility) */
    d = toml_int_in(global, "fps"); if (d.ok) cfg->target_fps = (int)d.u.i;
    d = toml_double_in(global, "duration"); if (d.ok) cfg->animation_duration = (float)d.u.d;
    d = toml_double_in(global, "shift"); if (d.ok) cfg->shift_pixels = (float)d.u.d;
    d = toml_string_in(global, "easing"); if (d.ok) { cfg->default_easing = easing_from_string(d.u.s); free(d.u.s); }

    /* Canonical nested keys */
    toml_table_t *animation = toml_table_in(global, "animation");
    if (animation) {
        d = toml_double_in(animation, "duration"); if (d.ok) cfg->animation_duration = (float)d.u.d;
        d = toml_string_in(animation, "easing"); if (d.ok) { cfg->default_easing = easing_from_string(d.u.s); free(d.u.s); }
    }

    d = toml_bool_in(global, "debug");
    if (d.ok) cfg->debug = d.u.b;

    d = toml_bool_in(global, "vsync");
    if (d.ok) cfg->vsync = d.u.b;

    d = toml_double_in(global, "idle_poll_rate");
    if (d.ok) cfg->idle_poll_rate = (float)d.u.d;

    /* Parallax: [global.parallax] */
    toml_table_t *parallax = toml_table_in(global, "parallax");
    if (parallax) {
        input_source_selection_t selection;
        input_source_selection_init(&selection);
        d = toml_string_in(parallax, "mode");
        if (d.ok) {
            cfg->parallax_mode = parallax_mode_from_string(d.u.s);
            free(d.u.s);
            /* Set default weights if not overridden later */
            if (cfg->parallax_mode == PARALLAX_WORKSPACE) {
                cfg->parallax_workspace_weight = 1.0f;
                cfg->parallax_cursor_weight = 0.0f;
            } else if (cfg->parallax_mode == PARALLAX_CURSOR) {
                cfg->parallax_workspace_weight = 0.0f;
                cfg->parallax_cursor_weight = 1.0f;
            } else {
                if (cfg->parallax_workspace_weight == 1.0f && cfg->parallax_cursor_weight == 0.0f) {
                    cfg->parallax_workspace_weight = 0.7f;
                    cfg->parallax_cursor_weight = 0.3f;
                }
            }
        }

        /* Canonical: shift_pixels inside parallax */
        d = toml_double_in(parallax, "shift_pixels");
        if (d.ok) cfg->shift_pixels = (float)d.u.d;

        toml_table_t *sources = toml_table_in(parallax, "sources");
        if (sources) {
            toml_table_t *ws = toml_table_in(sources, "workspace");
            if (ws) {
                d = toml_double_in(ws, "weight");
                if (d.ok) {
                    cfg->parallax_workspace_weight = (float)d.u.d;
                    char buf[32]; snprintf(buf, sizeof(buf), "workspace:%.6f", (float)d.u.d);
                    input_source_selection_add_spec(&selection, buf);
                }
            }
            toml_table_t *cur = toml_table_in(sources, "cursor");
            if (cur) {
                d = toml_double_in(cur, "weight");
                if (d.ok) {
                    cfg->parallax_cursor_weight = (float)d.u.d;
                    char buf[32]; snprintf(buf, sizeof(buf), "cursor:%.6f", (float)d.u.d);
                    input_source_selection_add_spec(&selection, buf);
                }
            }
        }

        toml_array_t *input_arr = toml_array_in(parallax, "input");
        if (input_arr) {
            int input_count = toml_array_nelem(input_arr);
            for (int i = 0; i < input_count; ++i) {
                toml_datum_t val = toml_string_at(input_arr, i);
                if (val.ok && val.u.s) {
                    input_source_selection_add_spec(&selection, val.u.s);
                    free(val.u.s);
                }
            }
        } else {
            toml_datum_t input_str = toml_string_in(parallax, "input");
            if (input_str.ok && input_str.u.s) {
                input_source_selection_add_spec(&selection, input_str.u.s);
                free(input_str.u.s);
            }
        }

        toml_table_t *invert = toml_table_in(parallax, "invert");
        if (invert) {
            toml_table_t *iws = toml_table_in(invert, "workspace");
            if (iws) {
                d = toml_bool_in(iws, "x");
                if (d.ok) cfg->invert_workspace_x = d.u.b;
                d = toml_bool_in(iws, "y");
                if (d.ok) cfg->invert_workspace_y = d.u.b;
            }
            toml_table_t *icur = toml_table_in(invert, "cursor");
            if (icur) {
                d = toml_bool_in(icur, "x");
                if (d.ok) cfg->invert_cursor_x = d.u.b;
                d = toml_bool_in(icur, "y");
                if (d.ok) cfg->invert_cursor_y = d.u.b;
            }
        }

        if (input_source_selection_modified(&selection)) {
            input_source_selection_commit(&selection, cfg);
        }

        toml_table_t *maxoff = toml_table_in(parallax, "max_offset_px");
        if (maxoff) {
            d = toml_double_in(maxoff, "x");
            if (d.ok) cfg->parallax_max_offset_x = (float)d.u.d;
            d = toml_double_in(maxoff, "y");
            if (d.ok) cfg->parallax_max_offset_y = (float)d.u.d;
        }
    }

    /* Render: [global.render] */
    toml_table_t *render = toml_table_in(global, "render");
    if (render) {
        toml_datum_t acc = toml_bool_in(render, "accumulate");
        if (acc.ok) cfg->render_accumulate = acc.u.b;
        toml_datum_t ts = toml_double_in(render, "trail_strength");
        if (ts.ok) {
            float v = (float)ts.u.d; if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
            cfg->render_trail_strength = v;
        }
        toml_datum_t o = toml_string_in(render, "overflow");
        if (o.ok && o.u.s) {
            if (strcmp(o.u.s, "repeat_edge") == 0 || strcmp(o.u.s, "clamp") == 0) cfg->render_overflow_mode = 0;
            else if (strcmp(o.u.s, "repeat") == 0 || strcmp(o.u.s, "tile") == 0) cfg->render_overflow_mode = 1;
            else if (strcmp(o.u.s, "repeat_x") == 0 || strcmp(o.u.s, "tilex") == 0) cfg->render_overflow_mode = 2;
            else if (strcmp(o.u.s, "repeat_y") == 0 || strcmp(o.u.s, "tiley") == 0) cfg->render_overflow_mode = 3;
            else if (strcmp(o.u.s, "none") == 0 || strcmp(o.u.s, "off") == 0) cfg->render_overflow_mode = 4;
            free(o.u.s);
        }
        /* tile can be bool or table */
        toml_datum_t tb = toml_bool_in(render, "tile");
        if (tb.ok) {
            cfg->render_tile_x = tb.u.b;
            cfg->render_tile_y = tb.u.b;
        } else {
            toml_table_t *tt = toml_table_in(render, "tile");
            if (tt) {
                toml_datum_t tx = toml_bool_in(tt, "x"); if (tx.ok) cfg->render_tile_x = tx.u.b;
                toml_datum_t ty = toml_bool_in(tt, "y"); if (ty.ok) cfg->render_tile_y = ty.u.b;
            }
        }
        toml_table_t *m = toml_table_in(render, "margin_px");
        if (m) {
            toml_datum_t mx = toml_double_in(m, "x");
            if (mx.ok) cfg->render_margin_px_x = (float)mx.u.d;
            toml_datum_t my = toml_double_in(m, "y");
            if (my.ok) cfg->render_margin_px_y = (float)my.u.d;
        }
    }

    /* Input: [global.input.cursor] */
    toml_table_t *input = toml_table_in(global, "input");
    if (input) {
        toml_table_t *cursor = toml_table_in(input, "cursor");
        if (cursor) {
            d = toml_double_in(cursor, "sensitivity_x");
            if (d.ok) cfg->cursor_sensitivity_x = (float)d.u.d;
            d = toml_double_in(cursor, "sensitivity_y");
            if (d.ok) cfg->cursor_sensitivity_y = (float)d.u.d;
            d = toml_double_in(cursor, "deadzone_px");
            if (d.ok) cfg->cursor_deadzone_px = (float)d.u.d;
            d = toml_double_in(cursor, "ema_alpha");
            if (d.ok) cfg->cursor_ema_alpha = (float)d.u.d;
            d = toml_double_in(cursor, "animation_duration");
            if (d.ok) cfg->cursor_anim_duration = d.u.d;
            d = toml_string_in(cursor, "easing");
            if (d.ok) {
                cfg->cursor_easing = easing_from_string(d.u.s);
                free(d.u.s);
            }
            toml_datum_t fg = toml_bool_in(cursor, "follow_global");
            if (fg.ok) cfg->cursor_follow_global = fg.u.b;
        }

        toml_table_t *window = toml_table_in(input, "window");
        if (window) {
            d = toml_double_in(window, "sensitivity_x");
            if (d.ok) cfg->window_sensitivity_x = (float)d.u.d;
            d = toml_double_in(window, "sensitivity_y");
            if (d.ok) cfg->window_sensitivity_y = (float)d.u.d;
            d = toml_double_in(window, "deadzone_px");
            if (d.ok) cfg->window_deadzone_px = (float)d.u.d;
            d = toml_double_in(window, "ema_alpha");
            if (d.ok) cfg->window_ema_alpha = (float)d.u.d;
        }
    }
}

int config_load_toml(config_t *cfg, const char *path)
{
    if (!cfg || !path) return HYPRLAX_ERROR_INVALID_ARGS;

    FILE *fp = fopen(path, "r");
    if (!fp) return HYPRLAX_ERROR_FILE_NOT_FOUND;

    char errbuf[256];
    toml_table_t *doc = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!doc) {
        LOG_ERROR("TOML parse error: %s", errbuf);
        return HYPRLAX_ERROR_LOAD_FAILED;
    }

    toml_table_t *global = toml_table_in(doc, "global");
    if (global) {
        parse_global_table(global, cfg);
    }

    toml_free(doc);
    return HYPRLAX_SUCCESS;
}

int config_apply_toml_to_context(hyprlax_context_t *ctx, const char *path)
{
    if (!ctx || !path) return HYPRLAX_ERROR_INVALID_ARGS;
    if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] TOML: apply to context start: %s\n", path);

    /* Load globals into ctx->config first */
    int rc = config_load_toml(&ctx->config, path);
    if (rc != HYPRLAX_SUCCESS) return rc;
    if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] TOML: globals loaded\n");

    /* Re-open to parse layers (keep separation of concerns above) */
    FILE *fp = fopen(path, "r");
    if (!fp) return HYPRLAX_ERROR_FILE_NOT_FOUND;

    char errbuf[256];
    toml_table_t *doc = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!doc) {
        LOG_ERROR("TOML parse error: %s", errbuf);
        return HYPRLAX_ERROR_LOAD_FAILED;
    }
    if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] TOML: parsed file\n");

    toml_table_t *global = toml_table_in(doc, "global");
    if (global) {
        toml_array_t *layers = toml_array_in(global, "layers");
        if (layers) {
            int n = toml_array_nelem(layers);
            if (getenv("HYPRLAX_INIT_TRACE")) fprintf(stderr, "[INIT_TRACE] TOML: layers count=%d\n", n);
            for (int i = 0; i < n; i++) {
                toml_table_t *lt = toml_table_at(layers, i);
                if (!lt) continue;

                const char *image = NULL;
                float shift = 1.0f;
                float shift_x = -1.0f; /* -1 means unset; will fallback to shift */
                float shift_y = -1.0f;
                float opacity = 1.0f;
                float blur = 0.0f;

                toml_datum_t d;
                d = toml_string_in(lt, "path");
                if (d.ok) {
                    image = d.u.s; /* remember to free later */
                }
                d = toml_double_in(lt, "shift_multiplier");
                if (d.ok) shift = (float)d.u.d;
                /* Optional per-axis table: shift_multiplier = { x = .., y = .. } */
                toml_table_t *smt = toml_table_in(lt, "shift_multiplier");
                if (smt) {
                    d = toml_double_in(smt, "x");
                    if (d.ok) shift_x = (float)d.u.d;
                    d = toml_double_in(smt, "y");
                    if (d.ok) shift_y = (float)d.u.d;
                }
                d = toml_double_in(lt, "opacity");
                if (d.ok) opacity = (float)d.u.d;
                d = toml_double_in(lt, "blur");
                if (d.ok) blur = (float)d.u.d;

                /* Optional tint */
                float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, tint_strength = 0.0f;
                toml_datum_t tc = toml_string_in(lt, "tint_color");
                if (tc.ok && tc.u.s) {
                    const char *s = tc.u.s;
                    if (s[0] == '#' && strlen(s) == 7) {
                        char cr[3] = {s[1], s[2], 0};
                        char cg[3] = {s[3], s[4], 0};
                        char cb[3] = {s[5], s[6], 0};
                        char *end = NULL; long rv = strtol(cr, &end, 16); long gv = strtol(cg, &end, 16); long bv = strtol(cb, &end, 16);
                        tint_r = (float)rv / 255.0f; tint_g = (float)gv / 255.0f; tint_b = (float)bv / 255.0f;
                    }
                    free(tc.u.s);
                }
                d = toml_double_in(lt, "tint_strength");
                if (d.ok) { tint_strength = (float)d.u.d; if (tint_strength < 0.0f) tint_strength = 0.0f; if (tint_strength > 1.0f) tint_strength = 1.0f; }

                if (image && *image) {
                    char *resolved = resolve_relative_path(path, image);
                    if (!resolved) {
                        LOG_WARN("Failed to resolve path: %s", image);
                    } else {
                        hyprlax_add_layer(ctx, resolved, shift, opacity, blur);
                        /* Apply per-axis multipliers and per-layer inversion if provided */
                        parallax_layer_t *last = ctx->layers;
                        if (last) {
                            while (last->next) last = last->next;
                            /* Apply tint if provided */
                            last->tint_r = tint_r; last->tint_g = tint_g; last->tint_b = tint_b; last->tint_strength = tint_strength;
                            if (shift_x >= 0.0f) last->shift_multiplier_x = shift_x;
                            if (shift_y >= 0.0f) last->shift_multiplier_y = shift_y;
                            /* Optional per-layer inversion */
                            toml_table_t *inv = toml_table_in(lt, "invert");
                            if (inv) {
                                toml_table_t *iws = toml_table_in(inv, "workspace");
                                if (iws) {
                                    d = toml_bool_in(iws, "x");
                                    if (d.ok) last->invert_workspace_x = d.u.b;
                                    d = toml_bool_in(iws, "y");
                                    if (d.ok) last->invert_workspace_y = d.u.b;
                                }
                                toml_table_t *icur = toml_table_in(inv, "cursor");
                                if (icur) {
                                    d = toml_bool_in(icur, "x");
                                    if (d.ok) last->invert_cursor_x = d.u.b;
                                    d = toml_bool_in(icur, "y");
                                    if (d.ok) last->invert_cursor_y = d.u.b;
                                }
                            }
                            /* Content fit mode */
                            d = toml_string_in(lt, "fit");
                            if (d.ok && d.u.s) {
                                if (strcmp(d.u.s, "stretch") == 0) last->fit_mode = LAYER_FIT_STRETCH;
                                else if (strcmp(d.u.s, "cover") == 0) last->fit_mode = LAYER_FIT_COVER;
                                else if (strcmp(d.u.s, "contain") == 0) last->fit_mode = LAYER_FIT_CONTAIN;
                                else if (strcmp(d.u.s, "fit_width") == 0 || strcmp(d.u.s, "fit_x") == 0) last->fit_mode = LAYER_FIT_WIDTH;
                                else if (strcmp(d.u.s, "fit_height") == 0 || strcmp(d.u.s, "fit_y") == 0) last->fit_mode = LAYER_FIT_HEIGHT;
                                free(d.u.s);
                            }
                            /* Overflow mode */
                            d = toml_string_in(lt, "overflow");
                            if (d.ok && d.u.s) {
                                if (strcmp(d.u.s, "repeat_edge") == 0 || strcmp(d.u.s, "clamp") == 0) last->overflow_mode = 0;
                                else if (strcmp(d.u.s, "repeat") == 0 || strcmp(d.u.s, "tile") == 0) last->overflow_mode = 1;
                                else if (strcmp(d.u.s, "repeat_x") == 0 || strcmp(d.u.s, "tilex") == 0) last->overflow_mode = 2;
                                else if (strcmp(d.u.s, "repeat_y") == 0 || strcmp(d.u.s, "tiley") == 0) last->overflow_mode = 3;
                                else if (strcmp(d.u.s, "none") == 0 || strcmp(d.u.s, "off") == 0) last->overflow_mode = 4;
                                free(d.u.s);
                            }
                            /* Tile: bool or table */
                            toml_datum_t tb = toml_bool_in(lt, "tile");
                            if (tb.ok) { last->tile_x = tb.u.b ? 1 : 0; last->tile_y = tb.u.b ? 1 : 0; }
                            else {
                                toml_table_t *tt = toml_table_in(lt, "tile");
                                if (tt) {
                                    toml_datum_t tx = toml_bool_in(tt, "x"); if (tx.ok) last->tile_x = tx.u.b ? 1 : 0;
                                    toml_datum_t ty = toml_bool_in(tt, "y"); if (ty.ok) last->tile_y = ty.u.b ? 1 : 0;
                                }
                            }
                            /* Margins */
                            toml_table_t *mp = toml_table_in(lt, "margin_px");
                            if (mp) {
                                toml_datum_t mx = toml_double_in(mp, "x");
                                if (mx.ok) last->margin_px_x = (float)mx.u.d;
                                toml_datum_t my = toml_double_in(mp, "y");
                                if (my.ok) last->margin_px_y = (float)my.u.d;
                            }
                            /* Additional scale */
                            d = toml_double_in(lt, "scale");
                            if (d.ok) last->content_scale = (float)d.u.d;
                            /* Alignment for cover/crop */
                            toml_table_t *align = toml_table_in(lt, "align");
                            if (align) {
                                toml_datum_t ax = toml_double_in(align, "x");
                                if (ax.ok) last->align_x = (float)ax.u.d;
                                else {
                                    toml_datum_t axs = toml_string_in(align, "x");
                                    if (axs.ok) {
                                        if (strcmp(axs.u.s, "left") == 0) last->align_x = 0.0f;
                                        else if (strcmp(axs.u.s, "center") == 0) last->align_x = 0.5f;
                                        else if (strcmp(axs.u.s, "right") == 0) last->align_x = 1.0f;
                                        free(axs.u.s);
                                    }
                                }
                                toml_datum_t ay = toml_double_in(align, "y");
                                if (ay.ok) last->align_y = (float)ay.u.d;
                                else {
                                    toml_datum_t ays = toml_string_in(align, "y");
                                    if (ays.ok) {
                                        if (strcmp(ays.u.s, "top") == 0) last->align_y = 0.0f;
                                        else if (strcmp(ays.u.s, "center") == 0) last->align_y = 0.5f;
                                        else if (strcmp(ays.u.s, "bottom") == 0) last->align_y = 1.0f;
                                        free(ays.u.s);
                                    }
                                }
                            }
                            /* Initial UV offset */
                            toml_table_t *uvo = toml_table_in(lt, "uv_offset");
                            if (uvo) {
                                d = toml_double_in(uvo, "x");
                                if (d.ok) last->base_uv_x = (float)d.u.d;
                                d = toml_double_in(uvo, "y");
                                if (d.ok) last->base_uv_y = (float)d.u.d;
                            }
                        }
                        free(resolved);
                    }
                } else {
                    LOG_WARN("Layer entry missing 'path'");
                }

                if (image) free((void*)image);
            }
        }
    }

    toml_free(doc);
    return HYPRLAX_SUCCESS;
}
