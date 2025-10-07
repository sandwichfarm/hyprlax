# IPC Overview

Hyprlax exposes a runtime IPC interface that allows you to control layers and settings without restarting the daemon.
Use the integrated command `hyprlax ctl` for human-friendly control and scripting.

For the full command reference, see: reference/ipc-commands.md.

## Typical Workflows

```bash
# Add a layer
hyprlax ctl add /path/to/img.png shift_multiplier=1.2 opacity=0.9 z=0

# Modify layer properties
hyprlax ctl modify 1 opacity 0.5
hyprlax ctl modify 1 shift_multiplier 0.8

# UV pan (normalized): shifts the sampled texture
# Typical range: -0.10 .. 0.10 (1.00 = full texture width/height)
hyprlax ctl modify 1 x 0.05
hyprlax ctl modify 1 y -0.02

# Z-order (ascending): lower z draws first (behind)
hyprlax ctl modify 1 z 10

# Blur amount
hyprlax ctl modify 1 blur 3.0

# Fit/align/content scale
hyprlax ctl modify 1 fit cover
hyprlax ctl modify 1 align.x 0.5
hyprlax ctl modify 1 align.y 0.5
hyprlax ctl modify 1 content_scale 1.2

# Overflow/tiling/margins
hyprlax ctl modify 1 overflow none
hyprlax ctl modify 1 tile.x true
hyprlax ctl modify 1 tile.y false
hyprlax ctl modify 1 margin_px.x 24
hyprlax ctl modify 1 margin_px.y 24

# Remove / list / clear
hyprlax ctl remove 1
hyprlax ctl list              # compact
hyprlax ctl list --long       # detailed
hyprlax ctl list --json       # JSON
hyprlax ctl list --filter path~=sunset  # filter by substring
hyprlax ctl list --filter id=2
hyprlax ctl list --filter hidden=true
hyprlax ctl clear

# Z-order convenience
hyprlax ctl front 3   # bring to front (highest z)
hyprlax ctl back  1   # send to back (lowest z)
hyprlax ctl up    2   # move forward by one step
hyprlax ctl down  2   # move backward by one step
```

## Settings

```bash
# Canonical dotted keys (aliases like fps/shift/duration/easing are accepted):
hyprlax ctl set render.fps 120
hyprlax ctl set parallax.shift_pixels 200
hyprlax ctl set animation.duration 1.2
hyprlax ctl set animation.easing cubic

hyprlax ctl get render.fps
hyprlax ctl get animation.duration
```

Structured properties are also available (examples):

```bash
hyprlax ctl set parallax.input workspace,cursor:0.3
hyprlax ctl set parallax.sources.cursor.weight 0.5  # legacy, still accepted
hyprlax ctl set render.overflow none
hyprlax ctl set render.tile.x true
hyprlax ctl get render.margin_px.x
```

> Configure sources with `parallax.input` (e.g., `workspace,cursor:0.3`). Legacy `parallax.mode` is removed.

## Understanding x/y (UV Pan)

- `x` and `y` are normalized UV offsets applied to the layer’s texture coordinates before parallax.
- Values are in the texture domain: `1.0` corresponds to the full width/height of the texture.
- Recommended range: `-0.10 .. 0.10` for subtle panning without visible tiling/clamp artifacts.
- Behavior with tiling:
  - When tiling is enabled (repeat), UVs wrap seamlessly.
  - With overflow=none/clamp, out-of-range UVs may be masked.

If you need pixel-accurate positioning in screen space, consider using per-pixel offsets when they are introduced (future addition).

## Status & Reload

```bash
hyprlax ctl status           # add --json for machine-readable output
hyprlax ctl reload           # reload config (TOML only; legacy paths will print a conversion hint)
```
