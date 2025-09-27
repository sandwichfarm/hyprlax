# Legacy Configuration Format

The original hyprlax configuration format using simple text files.

> **Note**: For new configurations, we recommend using [TOML format](toml-reference.md) which supports more features.

## File Format

Legacy configuration files use a simple line-based format:

```bash
# Comments start with #
# Commands are: layer, duration, shift, easing, delay, fps

# Add layers (required for multi-layer mode)
layer <image_path> <shift> <opacity> [blur]

# Global settings (optional)
duration <seconds>
shift <pixels>
easing <type>
delay <seconds>
fps <rate>
```

## Commands

### layer
Add an image layer to the parallax stack:
```bash
layer <image_path> <shift> <opacity> [blur]
```

Parameters:
- `image_path` - Path to image file (required)
- `shift` - Movement multiplier 0.0-2.0 (required)
- `opacity` - Layer transparency 0.0-1.0 (required)
- `blur` - Blur amount 0.0-10.0 (optional)

### duration
Set animation duration in seconds:
```bash
duration 1.5
```

### shift
Set base parallax shift in pixels:
```bash
shift 200
```

### easing
Set animation easing function:
```bash
easing expo
```

Available: linear, ease, ease_in, ease_out, ease_in_out, expo, cubic, quart, quint, sine, circ, elastic, back, bounce

### delay
Set animation delay in seconds:
```bash
delay 0.2
```

### fps
Set target frame rate:
```bash
fps 60
```

## Example Configuration

`~/.config/hyprlax/parallax.conf`:
```bash
# Three-layer parallax wallpaper

# Background - slow movement, heavy blur
layer /home/user/walls/sky.jpg 0.3 1.0 3.0

# Midground - medium speed, light blur  
layer /home/user/walls/mountains.png 0.6 0.9 1.0

# Foreground - normal speed, no blur
layer /home/user/walls/trees.png 1.0 0.8 0.0

# Animation settings
duration 1.2
shift 250
easing expo
delay 0
fps 144
```

## Shift Multiplier Guide

- `0.0` - Static (no movement)
- `0.1-0.3` - Very slow (far background)
- `0.4-0.6` - Slow (background)
- `0.7-0.9` - Medium (midground)
- `1.0` - Normal (standard parallax)
- `1.1-1.5` - Fast (foreground)
- `1.5-2.0` - Very fast (extreme foreground)

## Blur Recommendations

- `0.0` - No blur (sharp, foreground elements)
- `0.5-1.5` - Subtle blur (midground elements)
- `2.0-3.0` - Moderate blur (background elements)
- `3.0-5.0` - Heavy blur (distant background)
- `5.0+` - Extreme blur (atmospheric effects)

## Usage

### With Config File
```bash
hyprlax --config ~/.config/hyprlax/parallax.conf
```

### Override Settings
Command-line arguments override config file settings:
```bash
hyprlax --config parallax.conf --fps 30 --duration 2.0
```

## Limitations

The legacy format cannot configure:
- Per-layer content fitting
- Cursor parallax modes
- Per-axis shift multipliers
- Layer alignment and margins
- Input sensitivity settings
- Parallax source weights

For these features, use [TOML configuration](toml-reference.md).

## Migration to TOML

See the [Migration Guide](migration-guide.md) for converting legacy configs to TOML format.