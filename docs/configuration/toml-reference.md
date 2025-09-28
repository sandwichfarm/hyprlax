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
- `ease` - Smooth start and end
- `ease_in` - Slow start
- `ease_out` - Slow end  
- `ease_in_out` - Slow start and end
- `expo` - Exponential (default)
- `cubic` - Cubic curve
- `quart` - Quartic curve
- `quint` - Quintic curve
- `sine` - Sinusoidal
- `circ` - Circular
- `elastic` - Elastic bounce
- `back` - Overshoot and return
- `bounce` - Bounce effect

## Parallax Settings

### [global.parallax]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `mode` | string | "workspace" | Parallax mode: "workspace", "cursor", "hybrid" |

### [global.parallax.sources]

Control input source influence in hybrid mode:

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
| `fit` | string | "cover" | Content fit mode |
| `align` | table | center | Layer alignment |
| `margin_px` | table | 0 | Layer margins |

### Content Fit Modes

- `cover` - Cover entire area (may crop)
- `contain` - Fit entirely visible (may letterbox)
- `fill` - Stretch to fill (may distort)
- `scale-down` - Like contain but never upscale
- `none` - No scaling

### Alignment

```toml
align = { x = 0.5, y = 0.5 }  # Center (default)
```

Values:
- `0.0` = left/top
- `0.5` = center
- `1.0` = right/bottom
- Negative values for offset beyond edge

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
duration = 0.0          # Not used in cursor mode
vsync = true
easing = "expo"

[global.parallax]
mode = "cursor"         # Enable cursor tracking

[global.parallax.sources]
workspace.weight = 0.0
cursor.weight = 1.0

[global.parallax.invert.cursor]
x = true               # Layers move opposite to cursor
y = false

[global.parallax.max_offset_px]
x = 300
y = 150

[global.input.cursor]
follow_global = true
normalize_to = "monitor"
sensitivity_x = 1.2
sensitivity_y = 0.8
deadzone_px = 5
ema_alpha = 0.3        # Smooth cursor movement
animation_duration = 2.0
easing = "ease_out"

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

Example:

```toml
[global.render]
overflow = "repeat_edge"
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
