# Parallax Inputs Guide

Understand and control how Hyprlax computes parallax movement using multiple input sources.

## Overview

Hyprlax blends one or more input sources into a single parallax offset:

- `workspace`: Movement derived from workspace changes or position (adapter-dependent)
- `cursor`: Smoothed cursor motion relative to the viewport
- `window`: Active window position/centroid (Hyprland only for now)

You select inputs and their weights using a concise spec. Higher weights increase a source’s influence. Sources with zero weight are disabled.

## Configuration

You can configure inputs via CLI, TOML, IPC, or environment variables.

### CLI

```bash
# Workspace-only (classic)
hyprlax --input workspace

# Workspace + cursor blend (30% cursor influence)
hyprlax --input workspace,cursor:0.3

# Include window source (Hyprland only), half-weight
hyprlax --input workspace,window:0.5
```

### TOML (preferred)

```toml
[global.parallax]
# String form with optional weights
input = "workspace,cursor:0.3"

# or array form
# input = ["workspace", "cursor:0.3"]

[global.parallax.invert.cursor]
x = true
y = false

[global.parallax.max_offset_px]
x = 300
y = 150
```

You can also set weights explicitly using nested tables (primarily for legacy compatibility):

```toml
[global.parallax.sources]
workspace.weight = 0.7
cursor.weight = 0.3

# Window source (Hyprland only; ignored by other compositors)
window.weight = 0.5
```

### IPC (runtime)

```bash
# Blend workspace + cursor
hyprlax ctl set parallax.input workspace,cursor:0.3

# Adjust a specific source
hyprlax ctl set parallax.sources.cursor.weight 0.4
hyprlax ctl set parallax.sources.workspace.weight 0.6
```

### Environment

```bash
export HYPRLAX_PARALLAX_INPUT="workspace,cursor:0.3"
# Or per-source
export HYPRLAX_PARALLAX_SOURCES_WORKSPACE_WEIGHT=0.7
export HYPRLAX_PARALLAX_SOURCES_CURSOR_WEIGHT=0.3
```

## Blending Rules

Hyprlax computes the final parallax offset per axis as a weighted sum of each enabled source:

```
offset = workspace * w_ws + cursor * w_cur + window * w_win
```

Weights: 0.0..1.0 (values are clamped). When using `parallax.input`:

- Explicit weights are used as-is (clamped).
- Unspecified weights get the remaining share split evenly.
- Special case: if both `workspace` and `cursor` are included without explicit weights, defaults apply:
  - workspace = 0.7, cursor = 0.3

If a source has weight 0.0, it is disabled.

## Source Details

### Workspace

- Derives movement from the compositor’s workspace model (linear, scrollable, tag-based), adapted per backend.
- Good default for traditional parallax during workspace switches.

### Cursor

- Uses smoothed cursor movement centered on the viewport/canvas.
- Configurable smoothing, sensitivity, and deadzone.

```toml
[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 0.5
deadzone_px   = 3
ema_alpha     = 0.25    # 0.0 = max smoothing, 1.0 = no smoothing

# Optional animation of cursor-driven changes
animation_duration = 2.0
easing = "expo"
```

### Window (Hyprland only)

- Tracks the active window center; useful for subtle foreground tracking.
- Tunables mirror the cursor provider:

```toml
[global.input.window]
sensitivity_x = 1.0
sensitivity_y = 1.0
deadzone_px   = 6
ema_alpha     = 0.25
```

Other compositors currently ignore the window source gracefully.

## Inversion & Limits

Invert per source to flip the perceived depth direction:

```toml
[global.parallax.invert.workspace]
x = false
y = false

[global.parallax.invert.cursor]
x = true
y = false
```

Limit maximum offset to avoid excessive motion:

```toml
[global.parallax.max_offset_px]
x = 300
y = 150
```

## Per-Layer Interaction

Each layer multiplies the global offset by its own `shift_multiplier` (per-axis supported):

```toml
[[global.layers]]
path = "background.jpg"
shift_multiplier = { x = 0.2, y = 0.1 }
scale = 1.2      # overscale to avoid edges during movement
```

Use larger multipliers on foreground layers and smaller on background layers to achieve realistic depth.

## Examples

### Cursor-Only

```toml
[global.parallax]
input = "cursor"
```

### Workspace + Cursor (30% cursor)

```toml
[global.parallax]
input = ["workspace", "cursor:0.3"]
```

### Workspace + Window (Hyprland)

```toml
[global.parallax]
input = "workspace,window:0.5"
```

## Troubleshooting

- Inputs not taking effect? Confirm the spec: `hyprlax ctl get parallax.input`.
- Cursor jitter? Increase smoothing (`ema_alpha`) or add a small `deadzone_px`.
- Too much movement? Lower per-layer `shift_multiplier` or set `max_offset_px`.
- No effect on non-Hyprland compositors when using `window`? That is expected — the source is ignored where unavailable.

