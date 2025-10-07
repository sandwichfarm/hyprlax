# Migration Guide: Legacy to TOML

Converting your legacy `.conf` files to modern TOML format.

## Why Migrate?

TOML configuration provides:
- ✅ Cursor-driven parallax
- ✅ Per-layer content fitting
- ✅ Advanced input controls
- ✅ Per-axis shift multipliers
- ✅ Better validation and error messages
- ✅ Cleaner, more maintainable syntax

## Quick Conversion

### Basic Example

**Legacy (`parallax.conf`):**
```bash
layer /home/user/bg.jpg 0.5 1.0 2.0
layer /home/user/fg.png 1.0 0.9
duration 1.5
shift 200
easing expo
fps 60
```

**TOML (`hyprlax.toml`):**
```toml
[global]
duration = 1.5
shift = 200
easing = "expo"
fps = 60

[[global.layers]]
path = "/home/user/bg.jpg"
shift_multiplier = 0.5
opacity = 1.0
blur = 2.0

[[global.layers]]
path = "/home/user/fg.png"
shift_multiplier = 1.0
opacity = 0.9
```

## Conversion Reference

| Legacy Command | TOML Equivalent |
|----------------|-----------------|
| `duration 1.5` | `[global]`<br>`duration = 1.5` |
| `shift 200` | `[global]`<br>`shift = 200` |
| `easing expo` | `[global]`<br>`easing = "expo"` |
| `fps 60` | `[global]`<br>`fps = 60` |

| `layer path shift opacity blur` | `[[global.layers]]`<br>`path = "path"`<br>`shift_multiplier = shift`<br>`opacity = opacity`<br>`blur = blur` |

## Advanced Features

### Add Cursor Parallax

TOML allows cursor-driven parallax:

```toml
[global.parallax]
input = "cursor"  # New feature!

[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 0.5
ema_alpha = 0.25  # Smoothing
```

### Per-Layer Content Fitting

Control how images scale (supported values): `stretch`, `cover`, `contain`, `fit_width`, `fit_height`

```toml
[[global.layers]]
path = "image.png"
fit = "contain"    # or "cover", "stretch", "fit_width", "fit_height"
align = { x = 0.5, y = 0.8 }  # Bottom-center
```

### Per-Axis Movement

Different speeds for X and Y:

```toml
[[global.layers]]
path = "clouds.png"
shift_multiplier = { x = 1.5, y = 0.3 }  # Fast horizontal, slow vertical
```

## Step-by-Step Migration

### 1. Create TOML Structure
Start with basic global settings:
```toml
[global]
fps = 144
vsync = true
debug = false
```

### 2. Convert Global Settings
Map each legacy command:
```toml
duration = 1.5    # from: duration 1.5
shift = 200       # from: shift 200
easing = "expo"   # from: easing expo (add quotes!)
```

### 3. Convert Layers
Each `layer` command becomes a `[[global.layers]]` section:
```toml
[[global.layers]]
path = "/path/to/image.jpg"
shift_multiplier = 0.5
opacity = 1.0
blur = 2.0
```

### 4. Add New Features (Optional)
Enhance with TOML-only features:
```toml
# Layer enhancements
fit = "cover"
scale = 1.1
margin_px = { x = 50, y = 50 }

# Cursor parallax
[global.parallax]
input = "cursor"

[global.input.cursor]
sensitivity_x = 1.2
```

## Testing Your Migration

1. **Run both configs** to compare:
```bash
# Legacy
hyprlax --config parallax.conf

# TOML
hyprlax --config hyprlax.toml
```

2. **Check for errors**:
```bash
hyprlax --config hyprlax.toml --debug
```

3. **Fine-tune** new features as needed

## Common Pitfalls

### 1. Missing Quotes
❌ `easing = expo`
✅ `easing = "expo"`

### 2. Wrong Layer Format
❌ `layer = "/path/image.jpg 0.5 1.0"`
✅ 
```toml
[[global.layers]]
path = "/path/image.jpg"
shift_multiplier = 0.5
opacity = 1.0
```

### 3. Invalid TOML Syntax
Use a TOML validator: https://www.toml-lint.com/

## Complete Migration Example

**Original Legacy Config:**
```bash
# ~/.config/hyprlax/parallax.conf
layer ~/walls/stars.jpg 0.2 1.0 5.0
layer ~/walls/nebula.png 0.5 0.8 2.0
layer ~/walls/planet.png 1.0 1.0 0.0
duration 2.0
shift 300
easing sine
fps 120
```

**New TOML Config:**
```toml
# ~/.config/hyprlax/hyprlax.toml
[global]
duration = 2.0
shift = 300
easing = "sine"
fps = 120
vsync = true
debug = false

# Optional: Add cursor parallax
[global.parallax]
input = ["workspace", "cursor:0.3"]  # Both workspace and cursor

[global.input.cursor]
sensitivity_x = 0.8
sensitivity_y = 0.6
ema_alpha = 0.2

# Background: stars
[[global.layers]]
path = "~/walls/stars.jpg"
shift_multiplier = 0.2
opacity = 1.0
blur = 5.0
fit = "cover"

# Midground: nebula
[[global.layers]]
path = "~/walls/nebula.png"
shift_multiplier = 0.5
opacity = 0.8
blur = 2.0
fit = "cover"
scale = 1.1  # Slight overscale for parallax margins

# Foreground: planet
[[global.layers]]
path = "~/walls/planet.png"
shift_multiplier = 1.0
opacity = 1.0
blur = 0.0
fit = "contain"
align = { x = 0.8, y = 0.7 }  # Position planet
```

## Getting Help

- See [TOML Reference](toml-reference.md) for all options
- Check [Examples](../guides/examples.md) for working configs
- Report issues: https://github.com/sandwichfarm/hyprlax/issues
### Canonical Keys and Naming

Hyprlax now uses canonical dotted, snake_case keys consistently across Config (TOML), CLI/IPC, and ENV. Use these forms going forward; legacy aliases remain accepted for compatibility.

- Global keys
  - `render.fps` (ENV: `HYPRLAX_RENDER_FPS`) — alias: `fps`
  - `parallax.shift_pixels` (ENV: `HYPRLAX_PARALLAX_SHIFT_PIXELS`) — alias: `shift`
  - `animation.duration` (ENV: `HYPRLAX_ANIMATION_DURATION`) — alias: `duration`
  - `animation.easing` (ENV: `HYPRLAX_ANIMATION_EASING`) — alias: `easing`

- Per-layer keys (add/modify)
  - `shift_multiplier` — alias: `scale`
  - `uv_offset.x`, `uv_offset.y` — alias: `x`, `y`
  - `align.x`, `align.y` — alias: `align_x`, `align_y`
  - `margin_px.x`, `margin_px.y` — alias: `margin.x`, `margin.y`
  - `visible` — alias: `hidden` (inverse semantics)
  - `fit`, `overflow`, `tile.x`, `tile.y`, `opacity`, `blur`, `z`, `path`

`hyprlax ctl list --json` now uses `shift_multiplier`, `uv_offset`, `content_scale`, `margin_px`, and `visible`.
