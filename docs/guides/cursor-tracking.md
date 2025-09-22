# Cursor Tracking Guide

Create interactive parallax effects that respond to mouse movement.

## Overview

Cursor-driven parallax makes layers shift based on mouse position, creating an immersive depth effect that follows user focus.

## Quick Start

### Basic Setup

```toml
[global.parallax]
mode = "cursor"

[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 1.0

[[global.layers]]
path = "background.jpg"
shift_multiplier = 0.5

[[global.layers]]
path = "foreground.png"
shift_multiplier = 1.0
```

## Parallax Modes

### Cursor-Only Mode
Layers move only with mouse movement:

```toml
[global.parallax]
mode = "cursor"

[global.parallax.sources]
workspace.weight = 0.0
cursor.weight = 1.0
```

### Hybrid Mode
Combine workspace and cursor movement:

```toml
[global.parallax]
mode = "hybrid"

[global.parallax.sources]
workspace.weight = 0.7    # 70% workspace influence
cursor.weight = 0.3       # 30% cursor influence
```

### Workspace-Only Mode (Traditional)
Disable cursor tracking:

```toml
[global.parallax]
mode = "workspace"
```

## Cursor Input Settings

### Sensitivity
Control how much layers move relative to cursor:

```toml
[global.input.cursor]
sensitivity_x = 1.0    # 1.0 = normal, 2.0 = double movement
sensitivity_y = 0.5    # Reduce vertical sensitivity
```

### Dead Zone
Ignore small cursor movements:

```toml
[global.input.cursor]
deadzone_px = 5    # Ignore movements under 5 pixels
```

### Smoothing
Smooth cursor movement with exponential moving average:

```toml
[global.input.cursor]
ema_alpha = 0.25    # 0.0 = max smoothing, 1.0 = no smoothing
```

Lower values create smoother, more delayed movement.

### Animation
Animate cursor-driven changes:

```toml
[global.input.cursor]
animation_duration = 3.0    # Seconds to animate position changes
easing = "ease_out"         # Easing function for cursor animation
```

## Normalization

Note: Cursor normalization targets are currently fixed; configuration of `normalize_to` is not supported.

## Movement Control

### Invert Movement
Make layers move opposite to cursor:

```toml
[global.parallax.invert.cursor]
x = true     # Layers move left when cursor moves right
y = false    # Normal vertical movement
```

### Limit Movement Range
Prevent excessive parallax:

```toml
[global.parallax.max_offset_px]
x = 300    # Maximum 300px horizontal shift
y = 150    # Maximum 150px vertical shift
```

## Per-Layer Control

### Different Speeds per Axis
```toml
[[global.layers]]
path = "clouds.png"
shift_multiplier = { x = 2.0, y = 0.5 }    # Fast horizontal, slow vertical
```

### Layer-Specific Settings
```toml
[[global.layers]]
path = "background.jpg"
shift_multiplier = 0.3    # Slow movement
scale = 1.3               # Overscale to hide edges during movement

[[global.layers]]
path = "midground.png"
shift_multiplier = 0.7
scale = 1.15

[[global.layers]]
path = "foreground.png"
shift_multiplier = 1.2    # Moves faster than cursor
scale = 1.0
```

## Effect Examples

### Depth Focus Effect
Objects appear to shift based on depth:

```toml
[global.parallax]
mode = "cursor"

[global.input.cursor]
sensitivity_x = 0.8
sensitivity_y = 0.6
ema_alpha = 0.2

[[global.layers]]
path = "far_mountains.jpg"
shift_multiplier = 0.2
blur = 3.0

[[global.layers]]
path = "trees.png"
shift_multiplier = 0.6
blur = 1.0

[[global.layers]]
path = "character.png"
shift_multiplier = 1.0
blur = 0.0
```

### Floating Elements
UI elements that subtly follow cursor:

```toml
[global.input.cursor]
sensitivity_x = 0.3
sensitivity_y = 0.3
ema_alpha = 0.1    # Very smooth
animation_duration = 5.0
easing = "sine"

[[global.layers]]
path = "ui_element.png"
shift_multiplier = { x = 1.0, y = 0.5 }
```

### Tilt Effect
Simulate 3D card tilt:

```toml
[global.parallax.invert.cursor]
x = true
y = true

[global.input.cursor]
sensitivity_x = 0.5
sensitivity_y = 0.5
normalize_to = "monitor"

[[global.layers]]
path = "card_shadow.png"
shift_multiplier = { x = -0.3, y = -0.3 }
opacity = 0.5

[[global.layers]]
path = "card.png"
shift_multiplier = 0.0    # Card itself doesn't move

[[global.layers]]
path = "card_shine.png"
shift_multiplier = { x = 0.5, y = 0.5 }
opacity = 0.7
```

## Performance Optimization

### Reduce Smoothing
Less smoothing = less calculation:
```toml
ema_alpha = 0.5    # Less smooth but more responsive
```

### Limit Refresh Rate
```toml
[global]
fps = 60    # Lower FPS for battery savings
```

### Disable Animation
```toml
animation_duration = 0.0    # Instant movement
```

## Multi-Monitor Considerations

### Consistent Behavior
Use canvas normalization for uniform behavior:

```toml
[global.input.cursor]
normalize_to = "canvas"
follow_global = true
```

### Per-Monitor Settings
Different sensitivities based on monitor size:

```toml
# For 4K monitor (needs less sensitivity)
sensitivity_x = 0.5
sensitivity_y = 0.5

# For 1080p monitor (needs more sensitivity)
sensitivity_x = 1.2
sensitivity_y = 1.2
```

## Troubleshooting

### Cursor Not Tracked
- Check compositor supports cursor position reporting
- Verify `mode` includes cursor: `"cursor"` or `"hybrid"`
- Run with `--debug` to see cursor position

### Jittery Movement
- Increase smoothing: `ema_alpha = 0.1`
- Add dead zone: `deadzone_px = 3`
- Increase animation duration

### Too Much/Little Movement
- Adjust sensitivity values
- Modify shift_multiplier per layer
- Set max_offset_px limits

### CPU Usage High
- Lower FPS: `fps = 30`
- Reduce smoothing: `ema_alpha = 0.5`
- Use fewer layers

## Testing Configuration

```bash
# Test with debug output
HYPRLAX_DEBUG=1 hyprlax --config cursor-config.toml --debug
```

## Complete Example

```toml
# Interactive desktop with cursor parallax
[global]
fps = 144
vsync = true
debug = false

[global.parallax]
mode = "cursor"

[global.parallax.sources]
workspace.weight = 0.0
cursor.weight = 1.0

[global.parallax.invert.cursor]
x = false
y = false

[global.parallax.max_offset_px]
x = 400
y = 200

[global.input.cursor]
follow_global = true
normalize_to = "monitor"
sensitivity_x = 1.0
sensitivity_y = 0.7
deadzone_px = 2
ema_alpha = 0.25
animation_duration = 2.5
easing = "ease_out"

# Sky - barely moves
[[global.layers]]
path = "~/wallpapers/sky.jpg"
shift_multiplier = 0.1
scale = 1.3
blur = 2.0
fit = "cover"

# Mountains - slow movement
[[global.layers]]
path = "~/wallpapers/mountains.png"
shift_multiplier = 0.3
scale = 1.2
blur = 1.0
fit = "cover"

# Forest - medium movement
[[global.layers]]
path = "~/wallpapers/forest.png"
shift_multiplier = 0.6
scale = 1.15
blur = 0.5
fit = "cover"

# Foreground - full movement
[[global.layers]]
path = "~/wallpapers/foreground.png"
shift_multiplier = 1.0
scale = 1.1
blur = 0.0
fit = "cover"

# UI overlay - moves opposite for depth
[[global.layers]]
path = "~/wallpapers/ui_overlay.png"
shift_multiplier = { x = -0.2, y = -0.1 }
opacity = 0.8
fit = "contain"
align = { x = 0.5, y = 0.5 }
```
