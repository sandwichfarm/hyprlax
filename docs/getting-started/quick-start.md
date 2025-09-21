# Quick Start Guide

Get hyprlax running in under 5 minutes!

## Prerequisites

- Wayland compositor (Hyprland, River, Niri, or Sway)
- hyprlax installed ([see installation guide](installation.md))

## Basic Setup

### 1. Single Wallpaper

The simplest way to start:

```bash
hyprlax ~/Pictures/wallpaper.jpg
```

### 2. Add to Compositor Config

#### Hyprland
Add to `~/.config/hypr/hyprland.conf`:
```bash
exec-once = hyprlax ~/Pictures/wallpaper.jpg
```

#### River
Add to your River init:
```bash
riverctl spawn "hyprlax ~/Pictures/wallpaper.jpg"
```

#### Niri
Add to `~/.config/niri/config.kdl`:
```kdl
spawn-at-startup "hyprlax" "~/Pictures/wallpaper.jpg"
```

## First Parallax Effect

Create a simple two-layer parallax:

```bash
hyprlax --layer ~/walls/background.jpg:0.5:1.0 \
        --layer ~/walls/foreground.png:1.0:0.9
```

This creates:
- Background layer: moves slower (0.5x speed)
- Foreground layer: normal speed with slight transparency

## Test Your Setup

1. Start hyprlax with debug output:
```bash
hyprlax --debug ~/Pictures/test.jpg
```

2. Switch workspaces to see the parallax effect

3. If using cursor mode, move your mouse to see layers shift

## Next Steps

- [Configure with TOML](../configuration/toml-reference.md) for advanced features
- [Add multiple layers](../guides/multi-layer.md) for depth
- [Enable cursor tracking](../guides/cursor-tracking.md) for interactive parallax
- [Optimize performance](../guides/performance.md) for your system

## Common Issues

### Nothing appears
- Check compositor compatibility in [compatibility guide](compatibility.md)
- Run with `--debug` to see errors

### Poor performance
- Lower FPS: `hyprlax --fps 60 image.jpg`
- Avoid enabling vsync (default is off)

### Wrong compositor detected
- Force compositor: `hyprlax --compositor hyprland image.jpg`

For more help, see the [troubleshooting guide](../guides/troubleshooting.md).
