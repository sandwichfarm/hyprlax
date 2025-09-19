# Animation Guide

Master the animation system to create smooth, natural parallax effects.

## Parallax Modes

Hyprlax supports multiple sources of parallax and ways to combine them:
- workspace: Driven only by workspace changes (original behavior)
- cursor: Driven only by mouse/cursor movement
- hybrid: Workspace-driven with cursor influence blended in

You can select a mode via CLI or TOML:

```bash
# CLI
hyprlax --parallax workspace   # or cursor, hybrid
hyprlax --mouse-weight 0.3 --workspace-weight 0.7

# TOML (preferred)
[global.parallax]
mode = "hybrid"

[global.parallax.sources]
workspace.weight = 0.7
cursor.weight = 0.3

[global.parallax.invert.cursor]
x = true  # invert X for increased depth feeling
y = false

[global.input.cursor]
sensitivity_x = 1.0
sensitivity_y = 0.25
deadzone_px = 3
ema_alpha = 0.25
```

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
| `expo` | Exponential acceleration | **Default** - Snappy, modern |
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

Adds a pause before the animation starts.

```bash
# Start immediately (default)
hyprlax --delay 0 wallpaper.jpg

# Wait 0.5 seconds before animating
hyprlax --delay 0.5 wallpaper.jpg
```

**Use cases:**
- Sync with other desktop animations
- Create staggered effects with multiple layers
- Allow time for workspace switch to register

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

## Per-Layer Animation (Multi-Layer Mode)

Each layer can have independent animation settings for complex effects.

### Command Line Syntax

```bash
--layer image:shift:opacity:easing:delay:duration:blur
```

### Examples

#### Staggered Timing
```bash
# Background starts first
--layer bg.jpg:0.3:1.0:expo:0:1.0:3.0

# Midground follows
--layer mg.png:0.6:0.8:expo:0.1:1.0:1.5

# Foreground last
--layer fg.png:1.0:0.7:expo:0.2:1.0:0
```

#### Mixed Easing Functions
```bash
# Smooth background
--layer bg.jpg:0.3:1.0:sine:0:1.5:3.0

# Snappy midground
--layer mg.png:0.6:0.8:expo:0:0.8:1.5

# Bouncy foreground
--layer fg.png:1.0:0.7:elastic:0:1.2:0
```

#### Different Durations
```bash
# Slow background (2 seconds)
--layer bg.jpg:0.3:1.0:expo:0:2.0:3.0

# Medium midground (1 second)
--layer mg.png:0.6:0.8:expo:0:1.0:1.5

# Fast foreground (0.5 seconds)
--layer fg.png:1.0:0.7:expo:0:0.5:0
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
# Wave effect - each layer starts slightly after previous
hyprlax --layer l1.png:0.3:1.0:expo:0.0:1.0:2.0 \
        --layer l2.png:0.5:0.9:expo:0.1:1.0:1.5 \
        --layer l3.png:0.7:0.8:expo:0.2:1.0:1.0 \
        --layer l4.png:0.9:0.7:expo:0.3:1.0:0.5 \
        --layer l5.png:1.1:0.6:expo:0.4:1.0:0.0
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

# Disable vsync
hyprlax --vsync 0 wallpaper.jpg

# Use simpler easing
hyprlax -e linear wallpaper.jpg

# Shorter duration
hyprlax -d 0.5 wallpaper.jpg
```

## Troubleshooting Animation Issues

### Stuttering
- Lower FPS: `--fps 30`
- Disable vsync: `--vsync 0`
- Use fewer layers
- Reduce blur amounts

### Tearing
- Enable vsync: `--vsync 1`
- Match monitor refresh rate: `--fps 60` (for 60Hz display)

### Delayed Response
- Reduce animation delay: `--delay 0`
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
