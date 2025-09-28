# Multi-Layer Parallax Guide

Create stunning depth effects with multiple image layers moving at different speeds.

## Understanding Multi-Layer Parallax

Multi-layer parallax simulates depth by moving different layers at varying speeds as you switch workspaces. Distant objects move slower, while near objects move faster, creating a realistic 3D effect.

## Basic Concepts

### Layer Properties

Each layer has several properties that control its appearance and behavior:

1. **Shift Multiplier** - Controls movement speed relative to workspace changes
2. **Opacity** - Transparency level of the layer
3. **Blur Amount** - Simulates depth-of-field with blur effects

### Layer Order

Layers are rendered in the order they're specified:
- First layer = backmost (rendered first)
- Last layer = frontmost (rendered on top)

## Creating Multi-Layer Wallpapers

### Method 1: Command Line

```bash
hyprlax --layer background.jpg:0.3:1.0:3.0 \
        --layer midground.png:0.6:0.8:1.5 \
        --layer foreground.png:1.0:0.6
```

### Method 2: Configuration File

Create `~/.config/hyprlax/hyprlax.toml`:

```bash
# Background layer - distant mountains
layer /path/to/mountains.jpg 0.3 1.0 3.0

# Midground - trees
layer /path/to/trees.png 0.6 0.8 1.5

# Foreground - grass
layer /path/to/grass.png 1.0 0.7 0.0

# Animation settings
duration 1.2
shift 250
easing expo
```

Then run:
```bash
hyprlax --config ~/.config/hyprlax/hyprlax.toml
```

## Layer Parameters Explained

### Shift Multiplier (Movement Speed)

The shift multiplier determines how fast a layer moves relative to workspace changes:

| Value | Effect | Use Case |
|-------|--------|----------|
| 0.0 | Static (no movement) | Fixed backgrounds, stars |
| 0.1-0.3 | Very slow | Distant mountains, sky |
| 0.4-0.6 | Slow | Hills, distant buildings |
| 0.7-0.9 | Medium | Midground elements |
| 1.0 | Normal | Standard parallax speed |
| 1.1-1.5 | Fast | Foreground elements |
| 1.5+ | Very fast | Extreme foreground |

### Opacity (Transparency)

Controls how transparent a layer is:

| Value | Effect | Use Case |
|-------|--------|----------|
| 1.0 | Fully opaque | Background layers |
| 0.7-0.9 | Slightly transparent | Midground elements |
| 0.4-0.6 | Semi-transparent | Overlay effects |
| 0.1-0.3 | Mostly transparent | Subtle accents |

### Blur Amount (Depth Simulation)

Creates depth-of-field effects:

| Value | Effect | Use Case |
|-------|--------|----------|
| 0.0 | No blur | Sharp foreground |
| 0.5-1.0 | Subtle blur | Near midground |
| 1.5-2.5 | Moderate blur | Far midground |
| 3.0-4.0 | Heavy blur | Background |
| 5.0+ | Extreme blur | Very distant/atmospheric |

## Creating Layer Images

### Using GIMP

1. Open your source image
2. Use selection tools to isolate elements
3. Create separate layers for each depth level
4. Export each layer as PNG with transparency

### Using Photoshop

1. Open image and duplicate background
2. Use Quick Selection or Magic Wand
3. Layer via Cut (Ctrl+Shift+J) for each element
4. Save layers as separate PNG files

### Using ImageMagick (Command Line)

```bash
# Extract sky (assuming blue-ish colors)
convert source.jpg -fuzz 20% -transparent "rgb(135,206,235)" foreground.png

# Create blurred background
convert source.jpg -blur 0x8 background.jpg

# Extract specific color range
convert source.jpg -colorspace HSV -separate +channel \
        -threshold 30% -combine -negate mask.png
```

## Example Configurations

### Cityscape

```bash
# Distant skyline - heavy blur, slow movement
layer city-skyline.jpg 0.2 1.0 4.0

# Buildings - moderate blur, medium movement  
layer buildings.png 0.5 0.9 2.0

# Street level - sharp, normal movement
layer street.png 1.0 0.8 0.0
```

### Nature Scene

```bash
# Sky and clouds - very slow, blurred
layer sky.jpg 0.15 1.0 3.5

# Mountains - slow, slightly blurred
layer mountains.png 0.3 0.95 2.0

# Trees - medium speed, soft blur
layer trees.png 0.6 0.9 1.0

# Grass - fast, sharp
layer grass.png 1.2 0.8 0.0
```

### Abstract

```bash
# Background gradient
layer gradient.jpg 0.0 1.0 5.0

# Floating shapes - various speeds
layer shapes1.png 0.4 0.6 2.0
layer shapes2.png 0.7 0.5 1.0
layer shapes3.png 1.1 0.4 0.0
```

## Performance Optimization

### Layer Count
- 2-3 layers: Smooth on all systems
- 4-5 layers: Good for modern GPUs
- 6+ layers: May impact performance

### Image Resolution
- Background layers: Can be lower resolution (blurred anyway)
- Foreground layers: Should match or exceed screen resolution
- PNG compression: Use tools like `pngquant` to reduce file size

### Blur Optimization
- Heavily blurred layers don't need high resolution
- Pre-blur backgrounds in image editor for better performance
- Limit real-time blur to 2-3 layers maximum

## Advanced Techniques

### Staggered Animation

Create natural movement with animation delays:

```bash
hyprlax --layer bg.jpg:0.3:1.0:3.0 \
        --layer mg.png:0.6:0.8:1.5 \
        --layer fg.png:1.0:0.7
```

### Parallax Scrolling Effects

Combine different shift multipliers for complex movement:

```bash
# Clouds moving opposite direction
layer clouds.png -0.1 0.3 2.0

# Static stars
layer stars.png 0.0 0.8 0.0

# Normal landscape
layer landscape.jpg 1.0 1.0 0.0
```

### Dynamic Depth

Use opacity and blur together for atmospheric perspective:

```bash
# Distant layer - low opacity, high blur
layer distant.jpg 0.2 0.6 5.0

# Medium layer - medium opacity, medium blur
layer medium.png 0.5 0.8 2.5

# Near layer - high opacity, no blur
layer near.png 1.0 1.0 0.0
```

## Troubleshooting

### Layers Not Visible
- Check image paths are correct
- Ensure opacity is not 0.0
- Verify PNG transparency is preserved

### Performance Issues
- Reduce number of layers
- Lower resolution for blurred layers
- Decrease blur amounts
- Reduce FPS: `--fps 60`

### Visual Artifacts
- Check for PNG transparency issues
- Ensure layers are in correct order
- Verify opacity values add up correctly

## Next Steps

- Explore [animation options](animation.md) for per-layer effects
- See [example configurations](examples.md) for inspiration
- Check [troubleshooting](troubleshooting.md) for common issues
