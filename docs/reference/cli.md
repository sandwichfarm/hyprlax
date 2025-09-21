# CLI Reference

Quick reference for all hyprlax command-line options.

## Usage

```bash
hyprlax [OPTIONS] [IMAGE]
hyprlax ctl [COMMAND] [ARGS...]
```

## Global Options

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-h` | `--help` | flag | - | Show help message |
| | `--version` | flag | - | Show version information |
| | `--debug` | flag | false | Enable debug output |
| | `--config` | path | - | Load configuration file (.toml or .conf) |
| | `--compositor` | string | auto | Force compositor: `hyprland`, `river`, `niri`, `sway`, `generic`, `auto` |

## Display Options

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-f` | `--scale` | float | auto | Image scale factor |
| `-v` | `--vsync` | 0\|1 | 1 | Enable/disable vertical sync |
| | `--fps` | int | 144 | Target frame rate (30-240) |

## Animation Options

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-d` | `--duration` | float | 1.0 | Animation duration (seconds) |
| `-s` | `--shift` | int | 200 | Parallax shift distance (pixels) |
| `-e` | `--easing` | string | expo | Easing function (see below) |
| | `--delay` | float | 0 | Animation start delay (seconds) |

## Layer Options

| Short | Long | Type | Description |
|-------|------|------|-------------|
| | `--layer` | string | Add layer: `image:shift:opacity[:blur]` |

### Layer Format
```bash
--layer /path/to/image.png:1.0:0.9:2.0
#       └─ image path ─────┘ │   │   └─ blur (optional)
#                           │   └─ opacity (0.0-1.0)
#                           └─ shift multiplier (0.0-2.0)
```

## Easing Functions

Available easing types for `-e` / `--easing`:

| Type | Description |
|------|-------------|
| `linear` | Constant speed |
| `ease` | Smooth start and end |
| `ease_in` | Slow start |
| `ease_out` | Slow end |
| `ease_in_out` | Slow start and end |
| `expo` | Exponential (default) |
| `cubic` | Cubic curve |
| `quart` | Quartic curve |
| `quint` | Quintic curve |
| `sine` | Sinusoidal |
| `circ` | Circular |
| `elastic` | Elastic bounce |
| `back` | Overshoot and return |
| `bounce` | Bounce effect |

## Control Commands

Runtime control via `hyprlax ctl`:

| Command | Arguments | Description |
|---------|-----------|-------------|
| `add` | `<image> [shift] [opacity] [blur]` | Add new layer |
| `remove` | `<layer_id>` | Remove layer |
| `modify` | `<layer_id> <property> <value>` | Modify layer property |
| `list` | - | List all layers |
| `clear` | - | Remove all layers |
| `status` | - | Show status |
| `set` | `<property> <value>` | Set global property |
| `get` | `<property>` | Get global property |
| `reload` | - | Reload configuration |

See [IPC Commands](ipc-commands.md) for detailed control documentation.

## Examples

### Basic Usage
```bash
# Single image
hyprlax ~/Pictures/wallpaper.jpg

# With options
hyprlax --fps 60 --shift 300 ~/Pictures/wallpaper.jpg

# Debug mode
hyprlax --debug ~/Pictures/test.jpg
```

### Multi-Layer
```bash
# Two layers with different speeds
hyprlax --layer ~/bg.jpg:0.5:1.0:2.0 \
        --layer ~/fg.png:1.0:0.9:0.0

# Three-layer depth
hyprlax --layer ~/sky.jpg:0.2:1.0:5.0 \
        --layer ~/mountains.png:0.6:0.95:2.0 \
        --layer ~/trees.png:1.0:1.0:0.0
```

### Configuration
```bash
# Load TOML config
hyprlax --config ~/.config/hyprlax/config.toml

# Load legacy config
hyprlax --config ~/.config/hyprlax/parallax.conf

# Override config settings
hyprlax --config config.toml --fps 30 --debug
```

### Compositor Override
```bash
# Force Hyprland mode
hyprlax --compositor hyprland image.jpg

# Use generic Wayland
hyprlax --compositor generic image.jpg
```

### Runtime Control
```bash
# Add layer at runtime
hyprlax ctl add ~/new-image.png 1.0 0.8 2.0

# Change FPS
hyprlax ctl set fps 60

# List layers
hyprlax ctl list
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `HYPRLAX_DEBUG` | Enable debug output | 0 |
| `HYPRLAX_CONFIG` | Default config path | - |

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Invalid arguments |
| 3 | Configuration error |
| 4 | Platform/compositor error |
| 5 | IPC connection error |