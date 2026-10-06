# hyprlax integration

Verified against checkout `6aaf881` on 2026-10-06. Recheck implementation for other versions; prefer codebase-memory-mcp discovery when available.

## Renderer implications

- `src/core/render_core.c:hyprlax_render_monitor` combines workspace/cursor/window inputs and sends normalized offsets to `draw_layer_ex`. Do not assume cursor maximum settings bound arbitrary workspace travel or all mixed-input combinations.
- `load_texture` in the same file decodes RGBA without premultiplication. `src/renderer/shader.c:shader_fragment_basic` premultiplies final alpha; `src/renderer/gles2.c:gles2_init` uses `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`. Export ordinary straight-alpha PNGs; premultiplying the files again darkens edges.
- `gles2.c:compute_fit_params` fits full texture dimensions, including transparent space. Shared canvas, `fit`, `scale`, `align`, and crop preserve registration. Tight-cropped cutouts and different per-plane scales do not.
- Use explicit `fit = "cover"` for an aspect-preserving shared zoomed crop. Current `stretch` supports centered scale but can distort aspect; older versions ignored its scale. `contain` can leave gaps and does not act like an unbounded overscaled quad.
- `gles2_draw_layer_internal` normally offsets UV by `x/scale, -y/scale`. With matching image/viewport aspect, centered cover fit, no additional margins/base UV offsets, and the default uniform-offset path, screen motion magnitude matches input pixel offset. Other configurations require actual sampled-region checks.
- `margin_px` shrinks the UV window; it creates no artwork. `overflow = "none"` can add automatic safe-area cropping and masks out-of-range sampling. Neither reconstructs hidden content. Preview combined crop settings rather than blindly stacking scale and margins.
- Edge clamping may conceal missing artwork as stretched bands. Explicit `tile.x/y` drives `GL_REPEAT` here; do not assume `overflow = "repeat_x"` alone guarantees repetition. Tile only seamless assets.
- Static clamped non-power-of-two images work; `load_texture` avoids mipmap filtering for them. Do not distort the common canvas merely to obtain power-of-two sizes. ES 2.0 NPOT repetition needs extension support or compatible tile dimensions. Check the target's actual `GL_MAX_TEXTURE_SIZE`.

## Finite registered example

For a 1920×1080 viewport and shared 2112×1188 PNGs, every plane uses the same 1.1 cover crop. Cursor bounds ±80×±40 with multipliers up to 1 fit its 96×54 per-side reserve under the assumptions above. Blur is disabled during alignment checks. Recalculate for other monitor aspects.

```toml
[global]
fps = 60
vsync = true
duration = 0.0
easing = "cubic"

[global.parallax]
input = "cursor"

[global.parallax.max_offset_px]
x = 80
y = 40

[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 1.0
animation_duration = 0.3
easing = "cubic"

[global.render]
overflow = "repeat_edge"
tile = false
margin_px = { x = 0, y = 0 }

[[global.layers]]
path = "00-background.png"
shift_multiplier = 0.0
opacity = 1.0
blur = 0.0
fit = "cover"
scale = 1.1
align = { x = 0.5, y = 0.5 }

[[global.layers]]
path = "10-midground.png"
shift_multiplier = 0.35
opacity = 1.0
blur = 0.0
fit = "cover"
scale = 1.1
align = { x = 0.5, y = 0.5 }

[[global.layers]]
path = "20-foreground.png"
shift_multiplier = 1.0
opacity = 1.0
blur = 0.0
fit = "cover"
scale = 1.1
align = { x = 0.5, y = 0.5 }
```

TOML planes are specified back to front. Paths resolve relative to the config directory (`src/core/config_toml.c:resolve_relative_path`); save config beside images. Quote string values, including easing (some older examples use invalid bare strings).

On a supported Wayland host, use `hyprlax --config /path/to/parallax.toml --debug` and exercise configured inputs. This launches the wallpaper, not a headless validator. Elsewhere, check TOML syntax and image files separately and report live renderer testing as unperformed.

Base decoded texture memory is approximately `4 * width * height * layer_count` bytes. Mipmaps, decoding, blur targets, and compositor surfaces add overhead. Transparent pixels still consume texture memory; reduce unnecessary planes before sacrificing required hidden content or registration.
