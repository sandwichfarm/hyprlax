# Performance Guide

Optimize hyprlax for your system's performance and power requirements.

## Quick Optimization

### Battery/Power Saving
```bash
# Low power configuration
hyprlax --fps 30 --vsync image.jpg

# With environment flags
HYPRLAX_FRAME_CALLBACK=1 hyprlax --fps 30 image.jpg
```

### Balanced Performance
```bash
# Good visuals with reasonable performance
HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1 hyprlax image.jpg
```

### Maximum Performance
```bash
# All optimizations enabled
HYPRLAX_PERSISTENT_VBO=1 \
HYPRLAX_UNIFORM_OFFSET=1 \
HYPRLAX_NO_GLFINISH=1 \
hyprlax --fps 144 image.jpg
```

## Frame Rate Settings

### FPS Targets

| FPS | Use Case | Power Usage | Smoothness |
|-----|----------|-------------|------------|
| 30 | Battery saving | Low | Acceptable |
| 60 | Balanced | Moderate | Good |
| 90 | Gaming monitors | Higher | Very good |
| 120 | High refresh | High | Excellent |
| 144+ | Competitive | Highest | Perfect |

### Configuration
```toml
[global]
fps = 60        # Target frame rate
vsync = true    # Prevent tearing
```

### Runtime Adjustment
```bash
# Change FPS on the fly
hyprlax ctl set fps 30     # Battery mode
hyprlax ctl set fps 144    # Performance mode
```

## Blur Optimization

### Blur Performance Impact

Blur is one of the most expensive operations. Optimize with:

#### Separable Blur (Recommended)
```bash
# Two-pass blur, much faster
HYPRLAX_SEPARABLE_BLUR=1 hyprlax image.jpg
```

#### Blur Downscaling
```bash
# Render blur at lower resolution
HYPRLAX_BLUR_DOWNSCALE=2 hyprlax image.jpg  # 2x downscale
HYPRLAX_BLUR_DOWNSCALE=4 hyprlax image.jpg  # 4x downscale
```

#### Reduce Blur Passes
Use separable blur and downscale via environment variables:
```bash
export HYPRLAX_SEPARABLE_BLUR=1
export HYPRLAX_BLUR_DOWNSCALE=2   # 2x downscale (try 4 for more speed)
```

### Quality vs Performance

| Settings | Quality | Performance |
|----------|---------|-------------|
| `SEPARABLE_BLUR=0, passes=3` | Best | Slowest |
| `SEPARABLE_BLUR=1, DOWNSCALE=1` | Good | Fast |
| `SEPARABLE_BLUR=1, DOWNSCALE=2` | Acceptable | Faster |
| `SEPARABLE_BLUR=1, DOWNSCALE=4` | Lower | Fastest |

## GPU Optimization

### Persistent VBO
Reuse vertex buffer objects:
```bash
HYPRLAX_PERSISTENT_VBO=1 hyprlax image.jpg
```
- Reduces allocations
- Better GPU memory usage
- Recommended for all systems

### Uniform Offsets
Pass offsets via uniforms instead of modifying vertices:
```bash
HYPRLAX_UNIFORM_OFFSET=1 hyprlax image.jpg
```
- Reduces bandwidth
- Keeps geometry static
- Works best with persistent VBO

### Skip glFinish
Remove CPU/GPU synchronization:
```bash
HYPRLAX_NO_GLFINISH=1 hyprlax image.jpg
```
- Higher throughput
- May increase latency
- Best for high FPS targets

## Rendering Optimization

### Frame Callbacks
Use Wayland frame callbacks for timing:
```bash
HYPRLAX_FRAME_CALLBACK=1 hyprlax --fps 60 image.jpg
```
- Reduces idle CPU usage
- Better vsync handling
- Recommended for battery saving

### Layer Optimization

#### Reduce Layer Count
Fewer layers = better performance:
```toml
# Minimal setup
[[global.layers]]
path = "background.jpg"

# Instead of multiple semi-transparent layers
```

#### Optimize Image Sizes
- Use appropriate resolutions
- Compress PNG files
- Avoid unnecessarily large textures

#### Smart Blur Usage
```toml
# Only blur distant layers
[[global.layers]]
path = "far_background.jpg"
blur = 3.0  # Heavy blur

[[global.layers]]
path = "foreground.png"
blur = 0.0  # No blur on foreground
```

## Power Management

### Idle Optimization

Detect spurious rendering:
```bash
HYPRLAX_RENDER_DIAG=1 hyprlax --debug image.jpg
```

Look for `[RENDER_DIAG]` lines when idle - there shouldn't be any.

### Battery Mode Script
```bash
#!/bin/bash
# battery-mode.sh
if [[ $(cat /sys/class/power_supply/AC/online) == "0" ]]; then
    # On battery
    hyprlax ctl set fps 30
    hyprlax ctl set blur_passes 1
    export HYPRLAX_FRAME_CALLBACK=1
else
    # On AC power
    hyprlax ctl set fps 144
    hyprlax ctl set blur_passes 2
    unset HYPRLAX_FRAME_CALLBACK
fi
```

## Benchmarking

### Quick Benchmark
```bash
make bench
```

### Detailed Performance Test
```bash
make bench-perf
```

### Power Consumption Test
```bash
make bench-30fps
```

### Custom Benchmark
```bash
HYPRLAX_PROFILE=1 hyprlax --debug image.jpg 2>&1 | grep PROFILE
```

Output shows frame timings:
```
[PROFILE] draw=2.1ms present=0.3ms
```

## Monitoring Performance

### Real-time FPS
```bash
hyprlax --debug image.jpg 2>&1 | grep FPS
```

### GPU Usage (NVIDIA)
```bash
nvidia-smi dmon -s u
```

### GPU Usage (AMD)
```bash
radeontop
```

### CPU Usage
```bash
htop -p $(pgrep hyprlax)
```

## Configuration Templates

### Low-End System
```toml
[global]
fps = 30
vsync = true
debug = false

# Minimal blur
blur_passes = 1
blur_size = 5

# Single layer or minimal layers
[[global.layers]]
path = "wallpaper.jpg"
blur = 0.0
```

### Mid-Range System
```toml
[global]
fps = 60
vsync = true

# Balanced blur
blur_passes = 2
blur_size = 9

# Multiple layers OK
[[global.layers]]
path = "background.jpg"
blur = 2.0
shift_multiplier = 0.5

[[global.layers]]
path = "foreground.png"
blur = 0.0
shift_multiplier = 1.0
```

### High-End System
```toml
[global]
fps = 144
vsync = true

# Quality blur
blur_passes = 3
blur_size = 15

# Many layers with effects
[[global.layers]]
path = "layer1.jpg"
blur = 5.0
shift_multiplier = 0.2

[[global.layers]]
path = "layer2.png"
blur = 3.0
shift_multiplier = 0.5

[[global.layers]]
path = "layer3.png"
blur = 1.0
shift_multiplier = 0.8

[[global.layers]]
path = "layer4.png"
blur = 0.0
shift_multiplier = 1.2
```

## Troubleshooting Performance

### High CPU Usage
1. Lower FPS: `hyprlax ctl set fps 30`
2. Enable frame callbacks: `HYPRLAX_FRAME_CALLBACK=1`
3. Check for spurious renders: `HYPRLAX_RENDER_DIAG=1`

### High GPU Usage
1. Enable separable blur: `HYPRLAX_SEPARABLE_BLUR=1`
2. Reduce blur quality: `HYPRLAX_BLUR_DOWNSCALE=2`
3. Use fewer layers
4. Lower resolution images

### Stuttering
1. Try toggling vsync: enable with `--vsync` (default is off)
2. Use persistent VBO: `HYPRLAX_PERSISTENT_VBO=1`
3. Match monitor refresh rate

### Tearing
1. Enable vsync: add `--vsync`
2. Don't skip glFinish
3. Use frame callbacks

## Optimization Checklist

- [ ] Choose appropriate FPS for use case
- [ ] Enable persistent VBO
- [ ] Use uniform offsets
- [ ] Enable separable blur if using blur
- [ ] Optimize image sizes
- [ ] Minimize layer count
- [ ] Use frame callbacks for idle
- [ ] Test with benchmarks
- [ ] Monitor GPU/CPU usage
- [ ] Create battery/AC profiles
