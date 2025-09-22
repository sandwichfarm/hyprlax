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
hyprlax ctl add <image_path> [scale=..] [opacity=..] [x=..] [y=..] [z=..]
```

Optional keys:

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `scale` | float | 1.0 | 0.1-5.0 | Scale factor |
| `opacity` | float | 1.0 | 0.0-1.0 | Transparency |
| `x` | float | 0.0 | any | X position offset (px) |
| `y` | float | 0.0 | any | Y position offset (px) |
| `z` | int | next | 0-31 | Z-order (layer stack position) |

**Example:**
```bash
hyprlax ctl add ~/walls/sunset.jpg opacity=0.9 scale=1.2 z=10
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
| `scale` | float | 0.1-5.0 | Scale factor |
| `opacity` | float | 0.0-1.0 | Transparency |
| `x` | int | any | X position offset |
| `y` | int | any | Y position offset |
| `z` | int | 0-31 | Z-order (layer stack position) |
| `visible` | bool | true/false, 1/0 | Visibility toggle |

**Examples:**
```bash
hyprlax ctl modify 1 opacity 0.5
hyprlax ctl modify 2 visible false
hyprlax ctl modify 3 scale 1.2
```

### list
Show all active layers.

```bash
hyprlax ctl list
```

**Output format:**
```
ID: 1 | Path: image.jpg | Scale: 1.00 | Opacity: 1.00 | Position: (0.00, 0.00) | Z: 0 | Visible: yes
ID: 2 | Path: overlay.png | Scale: 1.20 | Opacity: 0.90 | Position: (40.00, 20.00) | Z: 10 | Visible: yes
```

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

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `fps` | int | 30-240 | Target frame rate |
| `shift` | float | 0-1000 | Base parallax shift (pixels) |
| `duration` | float | 0.1-10.0 | Animation duration (seconds) |
| `easing` | string | see list | Easing function name |
| `blur_passes` | int | 0-5 | Number of blur passes |
| `blur_size` | int | 3-31 (odd) | Blur kernel size |
| `debug` | bool | true/false | Debug output toggle |

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
hyprlax ctl status
```

**Output includes:**
- Process ID
- Compositor type
- Active layers count
- Current FPS setting (if available)

### reload
Reload configuration file.

```bash
hyprlax ctl reload
```

Reloads the configuration file specified at startup.

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
    hyprlax ctl set blur_passes 1
else
    hyprlax ctl set fps 144
    hyprlax ctl set blur_passes 3
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

- **Location**: `/tmp/hyprlax-$USER.sock`
- **Permissions**: `0600` (user read/write only)
- **Protocol**: Unix domain socket

## Error Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Command failed |
| 2 | Invalid arguments |
| 3 | Layer not found |
| 4 | Property not found |
| 5 | Connection error |

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
