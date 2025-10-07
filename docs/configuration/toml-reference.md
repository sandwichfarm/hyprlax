# TOML Configuration Reference

Complete reference for hyprlax TOML configuration format.

## File Structure

```toml
[global]                    # Global settings
[global.parallax]          # Parallax behavior
[global.parallax.sources]  # Input source weights
[global.parallax.invert]   # Inversion settings
[global.input.cursor]      # Cursor input settings
[[global.layers]]          # Layer definitions (array)
```

## Global Settings

### [global]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `fps` | integer | 60 | Target frame rate (30-240) |
| `debug` | boolean | false | Enable debug output |
| `duration` | float | 1.0 | Workspace animation duration (seconds) |
| `shift` | integer | 150 | Legacy parallax shift (pixels) |
| `vsync` | boolean | false | Enable vertical sync |
| `easing` | string | "cubic" | Default easing function |

### Easing Functions
- `linear` - Constant speed
- `quad` - Quadratic ease-out
- `cubic` - Cubic ease-out (default)
- `quart` - Quartic ease-out
- `quint` - Quintic ease-out
- `sine` - Sinusoidal ease-out
- `expo` - Exponential ease-out
- `circ` - Circular ease-out
- `elastic` - Elastic overshoot
- `back` - Slight pull-back before moving
- `bounce` - Bouncing settle
- `snap` - Fast start with sharp deceleration

## Parallax Settings

### [global.parallax]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `input` | string/array | "workspace" | Active sources (e.g., "workspace,cursor:0.3" or ["workspace", "cursor:0.3"]) |

### [global.parallax.sources]

Control input source influence via weights:

| Key | Type | Range | Description |
|-----|------|-------|-------------|
| `workspace.weight` | float | 0.0-1.0 | Workspace influence |
| `cursor.weight` | float | 0.0-1.0 | Cursor influence |

### [global.parallax.invert.cursor]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `x` | boolean | false | Invert X-axis movement |
| `y` | boolean | false | Invert Y-axis movement |

### [global.parallax.max_offset_px]

Clamp maximum parallax offset:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `x` | integer | unlimited | Max X offset in pixels |
| `y` | integer | unlimited | Max Y offset in pixels |

## Cursor Input Settings

### [global.input.cursor]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `follow_global` | boolean | true | Use global cursor position |
| `normalize_to` | string | - | Not currently configurable |
| `sensitivity_x` | float | 1.0 | X-axis sensitivity multiplier |
| `sensitivity_y` | float | 1.0 | Y-axis sensitivity multiplier |
| `deadzone_px` | integer | 0 | Dead zone radius in pixels |
| `ema_alpha` | float | 0.25 | Smoothing factor (0.0-1.0) |
| `animation_duration` | float | 3.0 | Cursor animation duration |
| `easing` | string | "expo" | Cursor animation easing |

## Layer Configuration

### [[global.layers]]

Array of layer definitions, rendered back to front:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `path` | string | required | Image file path |
| `shift_multiplier` | table/float | 1.0 | Parallax speed multiplier |
| `scale` | float | 1.0 | Layer scale factor |
| `opacity` | float | 1.0 | Layer opacity (0.0-1.0) |
| `blur` | float | 0.0 | Blur amount |
| `fit` | string | "stretch" | Content fit mode |
| `align` | table | center | Layer alignment |
| `margin_px` | table | 0 | Layer margins |
| `overflow` | string | inherit | Texture overflow mode (overrides `[global.render]`) |
| `tile` | bool/table | inherit | Enable tiling (`true`) or per-axis `{ x, y }` |
| `uv_offset` | table | `{ x=0, y=0 }` | Initial UV offset for sampling |
| `tint_color` | string | - | Hex RGB like `#RRGGBB` |
| `tint_strength` | float | 0.0 | 0.0..1.0 strength of tint |

### Content Fit Modes

Accepted values (renderer tokens):
- `stretch` - Stretch to fill (may distort)
- `cover` - Cover entire area (may crop)
- `contain` - Fit entirely visible (may letterbox)
- `fit_width` (alias: `fit_x`) - Fit by width
- `fit_height` (alias: `fit_y`) - Fit by height

### Alignment

```toml
align = { x = 0.5, y = 0.5 }  # Center (default)
```

Values:
- `0.0` = left/top
- `0.5` = center
- `1.0` = right/bottom
- Negative values for offset beyond edge

String forms are also accepted per-axis:
- X: `left`, `center`, `right`
- Y: `top`, `center`, `bottom`

### Shift Multiplier

```toml
# Uniform shift
shift_multiplier = 1.5

# Per-axis shift
shift_multiplier = { x = 1.0, y = 0.5 }
```

## Complete Example

```toml
# Cursor-driven parallax with multiple layers
[global]
fps = 144
debug = false
duration = 0.0
vsync = true
easing = "expo"

[global.parallax]
input = "cursor"         # Enable cursor-only tracking

[global.parallax.invert.cursor]
x = true               # Layers move opposite to cursor
y = false

[global.parallax.max_offset_px]
x = 300
y = 150

[global.input.cursor]
follow_global = true
sensitivity_x = 1.2
sensitivity_y = 0.8
deadzone_px = 5
ema_alpha = 0.3        # Smooth cursor movement
animation_duration = 2.0
easing = "expo"

# Background layer - moves slowly
[[global.layers]]
path = "background.jpg"
shift_multiplier = 0.3
scale = 1.2
opacity = 0.8
blur = 5.0
fit = "cover"
align = { x = 0.5, y = 0.5 }

# Midground layer
[[global.layers]]
path = "midground.png"
shift_multiplier = { x = 0.6, y = 0.4 }
scale = 1.0
opacity = 0.9
blur = 1.0
fit = "cover"

# Foreground layer - full parallax
[[global.layers]]
path = "foreground.png"  
shift_multiplier = 1.0
scale = 0.9
opacity = 1.0
blur = 0.0
fit = "contain"
align = { x = 0.5, y = 0.8 }  # Align to bottom
margin_px = { x = 50, y = 30 }
```

## Render Settings

### [global.render]

Rendering behavior, including optional trails (frame accumulation).

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `overflow` | string | `repeat_edge` | Texture overflow mode |
| `tile` | bool/table | false | Enable tiling (`true`) or per-axis `{ x, y }` |
| `margin_px` | table | `{ x=0, y=0 }` | Extra safe margin in pixels |
| `accumulate` | bool | false | Accumulate frames to create motion trails |
| `trail_strength` | float | 0.12 | Per-frame fade when accumulating (0..1) |

#### Overflow Modes

Use to control sampling outside the image bounds (when panned/scaled):
- `repeat_edge` (alias: `clamp`) - Clamp to edge texels (default)
- `repeat` (alias: `tile`) - Repeat in both axes
- `repeat_x` (alias: `tilex`) - Repeat in X only
- `repeat_y` (alias: `tiley`) - Repeat in Y only
- `none` (alias: `off`) - No sampling outside; outside area may be masked

Notes:
- Per-layer `overflow` can be set in `[[global.layers]]` and overrides the global mode.
- When combined with `tile`, per-axis tiling takes precedence over the overflow mode for wrap behavior.

#### Tiling

Controls explicit repeating per axis. Two forms are accepted:
- Boolean: `tile = true` (enables repeat in both axes)
- Table: `tile = { x = true, y = false }` (per-axis)

Precedence and interaction:
- Per-layer `tile` overrides the global `tile`.
- If `tile.x` or `tile.y` is enabled, that axis repeats regardless of `overflow`.
- If `overflow = none` and tiling is disabled on an axis, the renderer masks outside that axis to avoid edge artifacts.

Example:

```toml
[global.render]
overflow = "repeat_x"         # repeat horizontally only
tile = { x = false, y = false }
margin_px = { x = 0, y = 0 }
accumulate = true
trail_strength = 0.12
```

## Validation

Run with `--debug` to validate configuration:
```bash
hyprlax --config your-config.toml --debug
```

Configuration errors will be reported before startup.
