# Animation Guide

Master the animation system to create smooth, natural parallax effects.

## Parallax Inputs

Hyprlax combines one or more **input sources** to drive layer movement:
- `workspace`: driven by workspace changes (original behavior)
- `cursor`: driven by smoothed cursor motion
- `window`: tracks the active window center (implemented for Hyprland; other compositors ignore it gracefully until native support is added)

Configure sources via CLI or TOML:

```bash
# CLI examples
hyprlax --input workspace                      # workspace only
hyprlax --input workspace,cursor:0.3           # workspace + cursor blend

# TOML (preferred)
[global.parallax]
input = ["workspace", "cursor:0.3"]

[global.parallax.invert.cursor]
x = true  # invert X for increased depth feeling
y = false

[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 0.25
deadzone_px = 3
ema_alpha = 0.25
```

> Use `parallax.input` (or `--input` on CLI) to select sources and optional weights.

### Per-Layer Axis Control

Fine-tune depth with per-axis multipliers on each layer:

```toml
[[global.layers]]
path = "images/foreground.png"
shift_multiplier = { x = 1.0, y = 0.0 }  # disable Y parallax for this layer
opacity = 1.0
```

This is useful to keep a subject anchored vertically (e.g., ensuring the bottom of a foreground element never rises above the screen), while still allowing the background to subtly move in Y for a bokeh-like feel.

## Animation Basics

Hyprlax animates wallpaper movement when you switch workspaces. The animation system controls:
- How fast the transition happens (duration)
- The acceleration curve (easing)
- When the animation starts (delay)
- How smooth it appears (frame rate)

## Easing Functions

Easing functions control the acceleration and deceleration of animations, making them feel more natural.

### Available Easing Types

| Type | Description | Best For |
|------|-------------|----------|
| `linear` | Constant speed | Mechanical, robotic feel |
| `quad` | Gentle acceleration (x²) | Subtle, soft movements |
| `cubic` | Moderate acceleration (x³) | Balanced, natural feel |
| `quart` | Strong acceleration (x⁴) | Emphatic movements |
| `quint` | Very strong acceleration (x⁵) | Dramatic effects |
| `sine` | Smooth sine wave curve | Very natural, organic |
| `expo` | Exponential ease-out | Snappy, modern |
| `circ` | Circular function curve | Unique, slightly bouncy |
| `back` | Slight overshoot at end | Playful, energetic |
| `elastic` | Bouncy spring effect | Fun, attention-grabbing |
| `snap` | Custom snappy curve | Extra responsive feel |

### Visualizing Easing Curves

```
Linear:         ────────────────
                ╱
               ╱
              ╱

Expo Out:      ──╱
                ╱
               ╱
              ╱

Elastic:       ─┐ ╱─╱─
                └╱
               ╱

Back:          ───╱──┐
                 ╱    └─
                ╱
```

## Animation Parameters

### Duration

Controls how long the animation takes to complete.

```bash
# Fast transition (0.3 seconds)
hyprlax -d 0.3 wallpaper.jpg

# Slow, dramatic (2 seconds)
hyprlax -d 2.0 wallpaper.jpg

# Default (1 second)
hyprlax wallpaper.jpg
```

**Recommendations:**
- `0.2-0.4s` - Snappy, responsive
- `0.5-0.8s` - Balanced, comfortable
- `1.0-1.5s` - Smooth, relaxed
- `1.5-2.0s` - Slow, dramatic

### Delay

Note: A global animation delay is not currently configurable from the CLI.

### Frame Rate

Controls animation smoothness.

```bash
# High-end system (144 FPS)
hyprlax --fps 144 wallpaper.jpg

# Power saving (30 FPS)
hyprlax --fps 30 wallpaper.jpg

# Cinematic (24 FPS)
hyprlax --fps 24 wallpaper.jpg
```

**Performance guide:**
- 144+ FPS: High-end gaming monitors
- 60 FPS: Standard smooth animation
- 30 FPS: Power efficient, still smooth
- 24 FPS: Cinematic, may feel less responsive

## Multi-Layer Parameters

When adding layers via CLI, use:

```bash
--layer image:shift:opacity[:blur]
```

Examples:
```bash
--layer bg.jpg:0.3:1.0:3.0   # slow, heavily blurred background
--layer mg.png:0.6:0.8:1.5   # midground with light blur
--layer fg.png:1.0:0.7       # foreground, no blur
```

## Animation Presets

### Smooth and Relaxed
```bash
hyprlax -d 1.5 -e sine -s 200 wallpaper.jpg
```
Perfect for: Calm, professional environments

### Snappy and Responsive
```bash
hyprlax -d 0.3 -e expo -s 150 wallpaper.jpg
```
Perfect for: Gaming setups, high-energy workflows

### Playful and Bouncy
```bash
hyprlax -d 0.8 -e elastic -s 180 wallpaper.jpg
```
Perfect for: Creative workspaces, fun themes

### Dramatic and Cinematic
```bash
hyprlax -d 2.0 -e quint -s 300 wallpaper.jpg
```
Perfect for: Showcases, presentations

## Advanced Animation Techniques

### Acceleration Profiles

Combine easing with duration for different feels:

```bash
# Quick start, slow finish
hyprlax -e expo -d 1.2 wallpaper.jpg

# Slow start, quick finish  
hyprlax -e sine -d 0.8 wallpaper.jpg

# Uniform speed
hyprlax -e linear -d 1.0 wallpaper.jpg
```

### Workspace-Aware Animation

Adjust shift amount based on workspace count:

```bash
# 10 workspaces, small shifts
hyprlax -s 100 wallpaper.jpg

# 4 workspaces, large shifts
hyprlax -s 400 wallpaper.jpg
```

### Multi-Layer Choreography

Create complex animations with careful timing:

```bash
# Wave effect – vary speeds and blur per layer
hyprlax --layer l1.png:0.3:1.0:2.0 \
        --layer l2.png:0.5:0.9:1.5 \
        --layer l3.png:0.7:0.8:1.0 \
        --layer l4.png:0.9:0.7:0.5 \
        --layer l5.png:1.1:0.6
```

## Performance Considerations

### Animation Smoothness

Factors affecting smoothness:
1. **Frame rate** - Higher is smoother but uses more GPU
2. **Layer count** - More layers = more processing
3. **Blur amount** - Blur effects are computationally expensive
4. **Image resolution** - Higher resolution = more pixels to process

### Optimization Tips

For smooth animations on lower-end systems:

```bash
# Reduce FPS
hyprlax --fps 30 wallpaper.jpg

# Avoid enabling vsync (default is off)

# Use simpler easing
hyprlax -e linear wallpaper.jpg

# Shorter duration
hyprlax -d 0.5 wallpaper.jpg
```

## Troubleshooting Animation Issues

### Stuttering
- Lower FPS: `--fps 30`
- Try toggling vsync: add or remove `--vsync` (default is off)
- Use fewer layers
- Reduce blur amounts

### Tearing
- Enable vsync: add `--vsync`
- Match monitor refresh rate: `--fps 60` (for 60Hz display)

### Delayed Response
- Global delay configuration is not available; reduce duration instead
- Use shorter duration: `-d 0.3`
- Try snappier easing: `-e expo`

### Too Fast/Slow
- Adjust duration: `-d 1.5` (slower) or `-d 0.5` (faster)
- Change easing function for different feel
- Modify shift amount: `-s 250` (more dramatic)

## Next Steps

- See [multi-layer guide](multi-layer.md) for complex animations
- Check [examples](examples.md) for real-world configurations
- Review [troubleshooting](troubleshooting.md) for common issues
