# IPC Commands Reference

Quick reference for hyprlax runtime control commands.

## Command Format

```bash
hyprlax ctl <command> [arguments...]
```

## Layer Management

### add
Add a new image layer (IPC overlay). Optional parameters are key=value pairs.

```bash
hyprlax ctl add <image_path> [shift_multiplier=..] [opacity=..] [x=..] [y=..] [z=..]
```

Optional keys:

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `shift_multiplier` | float | 1.0 | 0.0-5.0 | Per-layer parallax multiplier (0.0=static, 1.0=normal) |
| `opacity` | float | 1.0 | 0.0-1.0 | Transparency |
| `x` | float | 0.0 | any | UV pan X offset (normalized; typical -0.10..0.10). Alias: `uv_offset.x` |
| `y` | float | 0.0 | any | UV pan Y offset (normalized; typical -0.10..0.10). Alias: `uv_offset.y` |
| `z` | int | next | 0-31 | Z-order (layer stack position) |

**Example:**
```bash
hyprlax ctl add ~/walls/sunset.jpg opacity=0.9 shift_multiplier=1.2 z=10
```

### remove
Remove a layer by ID.

```bash
hyprlax ctl remove <layer_id>
```

**Example:**
```bash
hyprlax ctl remove 2
```

### modify
Change layer properties.

```bash
hyprlax ctl modify <layer_id> <property> <value>
```

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `shift_multiplier` | float | 0.0-5.0 | Per-layer parallax multiplier (0.0 disables movement) |
| `opacity` | float | 0.0-1.0 | Transparency |
| `x` | float | any | UV pan X offset (normalized; typical -0.10..0.10). Alias: `uv_offset.x` |
| `y` | float | any | UV pan Y offset (normalized; typical -0.10..0.10). Alias: `uv_offset.y` |
| `z` | int | 0-31 | Z-order (layer stack position) |
| `visible` | bool | true/false, 1/0 | Visibility toggle |
| `hidden` | bool | true/false | Deprecated; prefer `visible` |
| `blur` | float | >=0 | Per-layer blur amount |
| `fit` | string | stretch/cover/contain/fit_width/fit_height | Content fit mode |
| `content_scale` | float | >0 | Content scale multiplier |
| `align.x` | float | 0..1 | Horizontal alignment (0 left, 0.5 center, 1 right) |
| `align.y` | float | 0..1 | Vertical alignment (0 top, 0.5 center, 1 bottom) |
| `overflow` | string | inherit/repeat_edge/repeat/repeat_x/repeat_y/none | Overflow behavior |
| `tile.x` | bool | true/false | Tiling on X |
| `tile.y` | bool | true/false | Tiling on Y |
| `margin_px.x` | float | px | Safe margin X (px) when overflow none |
| `margin_px.y` | float | px | Safe margin Y (px) when overflow none |
| `path` | string | file path | Image path; reloads texture |
| `tint` | string | - | `#RRGGBB[:strength]` or `none` |

**Examples:**
```bash
hyprlax ctl modify 1 opacity 0.5
hyprlax ctl modify 2 visible false
hyprlax ctl modify 3 shift_multiplier 1.2
```

### list
Show all active layers.

```bash
hyprlax ctl list [--long|-l] [--json|-j] [--filter <expr>]
```

Formats:
- Compact (default): one line per layer with id, z, opacity, shift_multiplier, blur, vis, path
- Long (`--long`): detailed properties including UV Offset, fit, align, Content Scale, overflow, tile, Margin Px
- JSON (`--json`): machine-readable array of layer objects (canonical keys)

Filters:
- `id=<id>`
- `hidden=true|false`
- `path~=substr`

### clear
Remove all layers.

```bash
hyprlax ctl clear
```

## Global Settings

### set
Change runtime settings.

```bash
hyprlax ctl set <property> <value>
```

| Property (canonical) | Type | Range | Description |
|----------------------|------|-------|-------------|
| `render.fps` | int | 30-240 | Target frame rate |
| `parallax.shift_pixels` | float | 0-1000 | Base parallax shift (pixels) |
| `animation.duration` | float | 0.1-10.0 | Animation duration (seconds) |
| `animation.easing` | string | see list | Easing function name |
| `render.accumulate` | bool | true/false | Enable trails effect |
| `render.trail_strength` | float | 0.0-1.0 | Per-frame fade when accumulating |
| `render.overflow` | string | repeat_edge/repeat/repeat_x/repeat_y/none | Texture overflow mode |
| `render.tile.x` | bool | true/false | Tiling on X |
| `render.tile.y` | bool | true/false | Tiling on Y |
| `render.margin_px.x` | float | px | Safe margin X (px) when overflow none |
| `render.margin_px.y` | float | px | Safe margin Y (px) when overflow none |
| `debug` | bool | true/false | Debug output toggle |

Aliases (kept for compatibility): `fps`, `shift`, `duration`, `easing`.

Additional structured keys:

- `parallax.input` — comma/array list of enabled sources (e.g. `workspace,cursor:0.3`)
- `parallax.sources.workspace.weight`, `parallax.sources.cursor.weight` — individual source weights
- `parallax.sources.window.weight` — weight for the window source (Hyprland only at present)
- `input.window.sensitivity_x/y`, `input.window.deadzone_px`, `input.window.ema_alpha` — window provider tuning knobs

**Examples:**
```bash
hyprlax ctl set fps 120
hyprlax ctl set duration 2.0
hyprlax ctl set easing elastic
hyprlax ctl set debug true
```

### get
Query current settings.

```bash
hyprlax ctl get <property>
```

**Examples:**
```bash
hyprlax ctl get fps
# Output: fps=144

hyprlax ctl get easing
# Output: easing=expo
```

## System Commands

### status
Show hyprlax status information.

```bash
hyprlax ctl status [--json|-j] [--long|-l]
```

**Output includes:**
- Default (text): running state, layers, target FPS, FPS, parallax inputs, monitors count, compositor, socket
- `--json`: machine-readable object with keys including:
  - `running`, `layers`, `target_fps`, `fps`
- `parallax_input` (enabled sources)
  - `compositor`, `socket`, `vsync`, `debug`
  - `caps` (compositor capability flags)
  - `monitors[]` with `name`, `size`, `pos`, `scale`, `refresh`, `caps`

### reload
Reload configuration file.

```bash
hyprlax ctl reload
```

Reloads the configuration file specified at startup. Runtime reload now supports TOML only. If a legacy `.conf` path was used, hyprlax will refuse to reload and print a conversion hint.

## Quick Examples

### Image Slideshow
```bash
#!/bin/bash
images=(~/walls/*.jpg)
for img in "${images[@]}"; do
    hyprlax ctl clear
    hyprlax ctl add "$img" 1.0 1.0 0
    sleep 30
done
```

### Fade Transition
```bash
#!/bin/bash
# Add second image invisible
hyprlax ctl add image2.jpg opacity=0.0 z=1

# Fade between images
for i in {10..0}; do
    hyprlax ctl modify 0 opacity "0.$i"
    hyprlax ctl modify 1 opacity "0.$((10-i))"
    sleep 0.1
done
```

### Dynamic Performance
```bash
#!/bin/bash
# Lower quality when on battery
if [[ $(cat /sys/class/power_supply/AC/online) == "0" ]]; then
    hyprlax ctl set fps 30
else
    hyprlax ctl set fps 144
fi
```

### Time-Based Wallpaper
```bash
#!/bin/bash
hour=$(date +%H)
hyprlax ctl clear

if [ "$hour" -ge 6 ] && [ "$hour" -lt 12 ]; then
    hyprlax ctl add ~/walls/morning.jpg
elif [ "$hour" -ge 12 ] && [ "$hour" -lt 18 ]; then
    hyprlax ctl add ~/walls/afternoon.jpg
elif [ "$hour" -ge 18 ] && [ "$hour" -lt 22 ]; then
    hyprlax ctl add ~/walls/evening.jpg
else
    hyprlax ctl add ~/walls/night.jpg
fi
```

## Socket Information

- Location (preferred): `$XDG_RUNTIME_DIR/hyprlax-$USER-$HYPRLAND_INSTANCE_SIGNATURE.sock`
- Location (fallback): `/tmp/hyprlax-$USER.sock`
- Permissions: `0600` (user read/write only)
- Protocol: Unix domain socket

## Status JSON Fields

When running `hyprlax ctl status --json`, a compact JSON object is returned. Fields include:

- `running`: boolean
- `layers`: number
- `target_fps`: number
- `fps`: number
- `parallax_input`: string (e.g., `workspace,cursor:0.3`)
- `compositor`: string
- `socket`: string
- `vsync`: boolean
- `debug`: boolean
- `caps`: object with compositor capability flags
- `monitors`: array of monitor objects with `name`, `size`, `pos`, `scale`, `refresh`, `caps`

## IPC Error Codes (optional)

- Enable structured codes with `HYPRLAX_IPC_ERROR_CODES=1`. When disabled (default), errors are plain strings starting with `Error:`.
- Codes are stable within a major release and grouped by area.

| Code | Meaning |
|------|---------|
| 1000 | No command specified |
| 1002 | Unknown command |
| 1003 | Token too long (e.g., property/value/filter) |
| 1100 | Image path required (add) |
| 1101 | Invalid or missing layer ID |
| 1102 | Layer not found |
| 1110 | Failed to add layer |
| 1200 | modify usage: requires `<id> <property> <value>` |
| 1201 | Invalid property (or layer not found/invalid property) |
| 1202 | set usage: requires `<property> <value>` |
| 1203 | get usage: requires `<property>` |
| 1210 | Invalid fps |
| 1211 | fps out of range (30..240) |
| 1212 | Invalid shift |
| 1213 | shift out of range (0..1000) |
| 1214 | Invalid duration |
| 1215 | duration out of range (0.1..10.0) |
| 1216 | Unknown/invalid property (set) |
| 1217 | Unknown property (get) |
| 1250 | scale out of range (0.1..5.0) |
| 1251 | opacity out of range (0.0..1.0) |
| 1253 | content_scale must be > 0 |
| 1254 | invalid fit value |
| 1255 | invalid overflow value |
| 1256 | margin.x must be >= 0 |
| 1257 | margin.y must be >= 0 |
| 1258 | blur must be >= 0 |
| 1260 | invalid z |
| 1261 | z out of range (0..31) |
| 1300 | Runtime context/settings unavailable |
| 1400 | No configuration path set |
| 1401 | Failed to reload configuration |

Example
```bash
HYPRLAX_IPC_ERROR_CODES=1 hyprlax ctl modify 1 opacity 2.0
# Error(1251): opacity out of range (0.0..1.0)
```

## Troubleshooting

### Cannot connect to socket
```bash
# Check if hyprlax is running
pgrep hyprlax

# Check socket exists
ls -la /tmp/hyprlax-*.sock

# Start hyprlax if needed
hyprlax ~/image.jpg &
```

### Command not working
```bash
# Enable debug mode
hyprlax ctl set debug true

# Check status
hyprlax ctl status
```
