# hyprlax Runtime Control

hyprlax supports dynamic layer management and runtime configuration through an integrated IPC (Inter-Process Communication) interface using Unix domain sockets.

## Overview

The runtime control interface allows you to:
- Add new image layers without restarting hyprlax
- Remove existing layers
- Modify layer properties (opacity, scale, position, visibility)
- List all active layers
- Clear all layers
- **Change runtime settings** (FPS, animation, blur, etc.)
- Query hyprlax status and configuration

## Using hyprlax ctl

The `ctl` subcommand is integrated directly into the hyprlax binary for runtime control.

### Commands

#### Add a layer
```bash
hyprlax ctl add <image_path> [shift] [opacity] [blur]
```

Arguments:
- `image_path` - Path to the image file
- `shift` - Parallax shift multiplier (default: 1.0)
- `opacity` - Opacity 0.0-1.0 (default: 1.0)
- `blur` - Blur amount (default: 0.0)

Example:
```bash
hyprlax ctl add /path/to/image.png 1.5 0.8 10
```

#### Remove a layer
```bash
hyprlax ctl remove <layer_id>
```

#### Modify a layer
```bash
hyprlax ctl modify <layer_id> <property> <value>
```

Properties:
- `scale` - Scale factor
- `opacity` - Opacity (0.0-1.0)
- `x` - X offset
- `y` - Y offset
- `z` - Z-index
- `visible` - true/false or 1/0

Example:
```bash
hyprlax ctl modify 1 opacity 0.5
hyprlax ctl modify 2 visible false
```

#### List layers
```bash
hyprlax ctl list
```

#### Clear all layers
```bash
hyprlax ctl clear
```

#### Get status
```bash
hyprlax ctl status
```

### Runtime Settings Commands

#### Set a property
```bash
hyprlax ctl set <property> <value>
```

Properties:
- `fps` - Target framerate (30-240)
- `shift` - Parallax shift amount in pixels
- `duration` - Animation duration in seconds
- `easing` - Animation easing function
- `blur_passes` - Number of blur passes (0-5)
- `blur_size` - Blur kernel size (3-31, must be odd)
- `debug` - Enable/disable debug output (true/false)

Examples:
```bash
hyprlax ctl set fps 120
hyprlax ctl set duration 2.0
hyprlax ctl set easing elastic
```

#### Get a property
```bash
hyprlax ctl get <property>
```

Example:
```bash
hyprlax ctl get fps
hyprlax ctl get easing
```

#### Reload configuration
```bash
hyprlax ctl reload
```

## Socket Location

The IPC socket is created at `/tmp/hyprlax-$USER.sock` where `$USER` is your username.

## Security

The socket is created with permissions 0600 (user read/write only) to prevent unauthorized access.

## Scripting Examples

### Slideshow
```bash
#!/bin/bash
# Simple slideshow script
images=(/path/to/images/*.jpg)
for img in "${images[@]}"; do
    hyprlax ctl clear
    hyprlax ctl add "$img" 1.0 1.0 0
    sleep 10
done
```

### Fade transition
```bash
#!/bin/bash
# Fade between two images
hyprlax ctl add image1.jpg 1.0 1.0
hyprlax ctl add image2.jpg 1.0 0.0

# Fade out image1, fade in image2
for i in {10..0}; do
    hyprlax ctl modify 1 opacity "0.$i"
    hyprlax ctl modify 2 opacity "0.$((10-i))"
    sleep 0.1
done
```

### Performance tuning
```bash
#!/bin/bash
# Adjust performance based on system load
load=$(uptime | awk -F'load average:' '{print $2}' | cut -d, -f1 | xargs)
if (( $(echo "$load > 2.0" | bc -l) )); then
    hyprlax ctl set fps 30
    hyprlax ctl set blur_passes 1
else
    hyprlax ctl set fps 60
    hyprlax ctl set blur_passes 2
fi
```

### Dynamic blur based on time
```bash
#!/bin/bash
# Increase blur during work hours
hour=$(date +%H)
if [ "$hour" -ge 9 ] && [ "$hour" -lt 17 ]; then
    hyprlax ctl set blur_size 21
    hyprlax ctl set blur_passes 3
else
    hyprlax ctl set blur_size 7
    hyprlax ctl set blur_passes 1
fi
```

## Limitations

- Maximum 32 layers by default (configurable in source)
- Images must be accessible by hyprlax process
- Changes are applied immediately but may take a frame to become visible
- Memory usage increases with each loaded layer

## Troubleshooting

If `hyprlax ctl` cannot connect:
1. Ensure hyprlax daemon is running
2. Check the socket exists: `ls -la /tmp/hyprlax-*.sock`
3. Verify permissions on the socket file
4. Make sure you're using the same user that started hyprlax
5. Check that IPC server initialized successfully (run with `-D` flag to see debug output)