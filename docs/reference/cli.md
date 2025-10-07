# CLI Reference

Quick reference for the hyprlax command-line interface.

## Usage

```bash
hyprlax [OPTIONS] [--layer <image:shift:opacity:blur[:#RRGGBB[:strength]]> ...]
hyprlax ctl <COMMAND> [ARGS...]
```

## Global Options

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-h` | `--help` | flag | - | Show help message |
| `-v` | `--version` | flag | - | Show version information |
| `-D` | `--debug` | flag | false | Enable debug output |
| `-L` | `--debug-log[=FILE]` | flag/str | - | Write debug log to file (implies debug) |
| | `--trace` | flag | false | Enable trace-level logging |
| `-c` | `--config` | path | - | Load configuration file (.toml or legacy .conf) |
| `-C` | `--compositor` | string | auto | Force compositor: `hyprland`, `sway`, `generic`, `auto` |
| `-r` | `--renderer` | string | auto | Renderer backend: `gles2`, `auto` |
| `-p` | `--platform` | string | auto | Platform backend: `wayland`, `auto` |
| | `--verbose` | level | - | Log level: `error|warn|info|debug|trace` or `0..4` |
| | `--primary-only` | flag | off | Use only the primary monitor |
| | `--monitor` | string | - | Include specific monitor (repeatable) |
| | `--disable-monitor` | string | - | Exclude a specific monitor (repeatable) |

## Display/Timing

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-f` | `--fps` | int | 60 | Target frame rate (30-240) |
| `-V` | `--vsync` | flag | off | Enable vertical sync |
| | `--idle-poll-rate` | float | 2.0 | Idle polling rate in Hz |

## Animation

| Short | Long | Type | Default | Description |
|-------|------|------|---------|-------------|
| `-d` | `--duration` | float | 1.0 | Animation duration (seconds) |
| `-s` | `--shift` | float | 150 | Base parallax shift (pixels) |
| `-e` | `--easing` | string | cubic | Easing function (see below) |

## Parallax

| Long | Type | Default | Description |
|------|------|---------|-------------|
| `--input` | string | workspace | Comma list of sources (`workspace`, `cursor`, `window`; e.g. `workspace,cursor:0.3`) |

> Tip: Use `--input` (or the TOML/IPC equivalent `parallax.input`) to configure active sources. The `window` source currently requires Hyprland; other compositors will ignore it gracefully.

## Render Options

| Long | Type | Default | Description |
|------|------|---------|-------------|
| `--overflow` | string | repeat_edge | `repeat_edge|repeat|repeat_x|repeat_y|none` |
| `--tile-x` | flag | off | Enable tiling on X axis |
| `--tile-y` | flag | off | Enable tiling on Y axis |
| `--no-tile-x` | flag | off | Disable tiling on X axis |
| `--no-tile-y` | flag | off | Disable tiling on Y axis |
| `--margin-px-x` | float | 0 | Extra horizontal safe margin (px) |
| `--margin-px-y` | float | 0 | Extra vertical safe margin (px) |
| `--accumulate` | flag | off | Enable trails effect (accumulate frames) |
| `--trail-strength` | float | 0.12 | Per-frame fade when accumulating (0..1) |

## Layers

| Short | Long | Type | Description |
|-------|------|------|-------------|
| | `--layer` | string | Add layer: `image:shift:opacity[:blur]` |

### Layer Format
```bash
--layer /path/to/image.png:1.0:0.9:2.0:#AABBCC:0.5
#       └─ image path ─────┘ │   │   │    │      └─ tint strength (0.0-1.0, optional)
#                            │   │   │    └─ tint color (#RRGGBB, optional; use 'none' to disable)
#                            │   │   └─ blur amount (optional)
#                            │   └─ opacity (0.0-1.0)
#                            └─ shift multiplier (0.0-2.0)
```

## Easing Functions

Available values for `-e` / `--easing`:

| Type | Description |
|------|-------------|
| `linear` | Constant speed |
| `quad` | Quadratic ease-out |
| `cubic` | Cubic ease-out (default) |
| `quart` | Quartic ease-out |
| `quint` | Quintic ease-out |
| `sine` | Sinusoidal ease-out |
| `expo` | Exponential ease-out |
| `circ` | Circular ease-out |
| `back` | Overshoot and return |
| `elastic` | Elastic bounce |
| `bounce` | Bounce effect |
| `snap` | Custom snappy ease-out |

## Control Commands

Runtime control via `hyprlax ctl`:

| Command | Arguments | Description |
|---------|-----------|-------------|
| `add` | `<image> [shift_multiplier=..] [opacity=..] [uv_offset.x=..] [uv_offset.y=..] [z=..]` | Add new layer (IPC overlay) |
| `remove` | `<layer_id>` | Remove layer |
| `modify` | `<layer_id> <property> <value>` | Modify layer property (see IPC reference for full set) |
| `list` | `[--long|-l] [--json|-j] [--filter <expr>]` | List layers; support filters id=, hidden=, path~= |
| `clear` | - | Remove all layers |
| `status` | - | Show status |
| `set` | `<property> <value>` | Set runtime property |
| `get` | `<property>` | Get runtime property |
| `reload` | - | Reload configuration |

See [IPC Overview](../guides/ipc-overview.md) for workflows and [IPC Commands](ipc-commands.md) for the full reference.

## Examples

### Basic Usage
```bash
# Single image
hyprlax ~/Pictures/wallpaper.jpg

# With options
hyprlax --fps 60 --shift 300 ~/Pictures/wallpaper.jpg

# Debug mode
hyprlax -D ~/Pictures/test.jpg
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
hyprlax --config ~/.config/hyprlax/hyprlax.toml

# Convert legacy to TOML (non-interactive)
hyprlax ctl convert-config ~/.config/hyprlax/parallax.conf ~/.config/hyprlax/hyprlax.toml --yes

# Override config settings
hyprlax --config config.toml --fps 30 -D
```

### Compositor Override
```bash
# Force Hyprland mode
hyprlax --compositor hyprland image.jpg

# Use generic Wayland
hyprlax --compositor generic image.jpg

# Manual selection accepts: hyprland, niri, river, sway, generic.
```

### Runtime Control
```bash
# Add overlay layer at runtime (positioned UI element)
hyprlax ctl add ~/new-image.png opacity=0.8 x=40 y=20 z=10

# Change FPS
hyprlax ctl set fps 60

# List layers
hyprlax ctl list
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Invalid arguments |
| 3 | Configuration error |
| 4 | Platform/compositor error |
| 5 | IPC connection error |
