# Easing Functions Reference

Visual guide to hyprlax animation easing functions.

## Overview

Easing functions control the acceleration and deceleration of animations, creating natural-looking motion.

## Function Types

The engine implements "ease-out" variants for non-linear curves. Supported values:

### linear
Constant speed throughout the animation
```
Speed: ────────────────────
Usage: Mechanical, robotic movements
```

### quad
Quadratic ease-out (gentle deceleration)
```
Speed: ╱──────────
Usage: Subtle motions
```

### cubic (default)
Cubically eased deceleration
```
Speed: ╱─────────╲
Usage: Smooth, predictable motion
```
- Moderate acceleration curve
- Best for: Standard animations

### quart
**Quartic curve acceleration**
```
Speed: ╱═════════╲
Usage: Heavier feel than cubic
```
- Stronger acceleration than cubic
- Best for: Heavier elements

### quint
**Quintic curve acceleration**
```
Speed: ╱═════════╲
Usage: Very smooth, heavy motion
```
- Very strong acceleration curve
- Best for: Large elements, dramatic shifts

### sine
**Sinusoidal curve**
```
Speed: ∿∿∿∿∿∿∿∿∿
Usage: Natural, wave-like motion
```
- Sine wave acceleration
- Best for: Natural movements, oscillations

### circ
**Circular curve**
```
Speed: ◜═════════◝
Usage: Circular motion feel
```
- Based on circular arc
- Best for: Rotational hints, orbits

### expo
Exponential ease-out (snappy start)
```
Speed: ──╱
Usage: Modern, responsive motion
```

### elastic
**Elastic overshoot and settle**
```
Speed: ╱──◠◡◠──╲
Usage: Playful, bouncy motion
```
- Overshoots then settles
- Best for: Playful UI, notifications

### back
**Slight backup before moving**
```
Speed: ◀╱────────╲
Usage: Anticipation effect
```
- Pulls back before advancing
- Best for: Emphasis, anticipation

### bounce
**Bouncing settle effect**
```
Speed: ╱──╯╰╯╰─
Usage: Impact simulation
```
- Bounces at the end
- Best for: Landings, impacts

### snap
Custom snappy ease-out
```
Speed: ─╱──
Usage: Extra responsive feel
```

## Usage Examples

### Command Line
```bash
# Default exponential
hyprlax --easing cubic image.jpg

# Smooth natural motion
hyprlax --easing sine --duration 1.5 image.jpg

# Playful bounce
hyprlax --easing bounce --duration 2.0 image.jpg

# Quick and snappy
hyprlax --easing expo --duration 0.5 image.jpg
```

### TOML Configuration
```toml
[global]
easing = "cubic"         # Global default

[global.input.cursor]
easing = "expo"           # Cursor-specific
```

### IPC Runtime
```bash
# Change easing at runtime
hyprlax ctl set easing elastic
```

## Choosing the Right Easing

### For Workspace Switching
- **Recommended**: `expo`, `cubic`, `sine`
- **Duration**: 0.5 - 1.5 seconds
- Natural, not distracting

### For Cursor Parallax
- **Recommended**: `expo`, `cubic`, `linear`
- **Duration**: 2.0 - 4.0 seconds
- Smooth following, no bounce

### For Dramatic Effects
- **Recommended**: `elastic`, `bounce`, `back`
- **Duration**: 1.5 - 3.0 seconds
- Attention-grabbing

### For Subtle Background
- **Recommended**: `sine`, `ease`, `ease_in_out`
- **Duration**: 2.0 - 5.0 seconds
- Gentle, barely noticeable

## Performance Notes

- Simple easings (`linear`, `ease`) have minimal CPU impact
- Complex easings (`elastic`, `bounce`) require more calculations
- Longer durations spread calculations over more frames

## Testing Easings

Compare different easings:
```bash
# Test each easing for 2 seconds
for easing in linear ease ease_in ease_out expo elastic bounce; do
    echo "Testing: $easing"
    hyprlax ctl set easing $easing
    hyprlax ctl set duration 2.0
    sleep 3
done
```

## Mathematical Basis

| Function | Formula Type |
|----------|--------------|
| linear | `t` |
| ease | Cubic Bezier |
| cubic | `t³` |
| quart | `t⁴` |
| quint | `t⁵` |
| sine | `sin(t × π/2)` |
| circ | `√(1 - t²)` |
| expo | `2^(10×(t-1))` |
| elastic | Damped sine wave |
| back | Cubic with overshoot |
| bounce | Simulated physics |

## Custom Duration Guidelines

| Easing | Short (0.2-0.5s) | Medium (0.5-1.5s) | Long (1.5-3.0s) |
|--------|------------------|-------------------|-----------------|
| linear | ✅ Good | ✅ Good | ⚠️ May feel slow |
| ease | ✅ Good | ✅ Good | ✅ Good |
| expo | ✅ Snappy | ✅ Smooth | ⚠️ May drag |
| elastic | ❌ Too fast | ✅ Playful | ✅ Noticeable |
| bounce | ❌ Too fast | ✅ Fun | ⚠️ May annoy |
