# Hyprlax — Smooth Parallax Wallpapers for Hyprland

Hyprlax is a high‑performance parallax wallpaper system for Wayland compositors, designed
for buttery‑smooth animations on Hyprland. It creates depth by moving multiple image layers
at different speeds, with a modern renderer, runtime control, and strong configurability.

## Highlights

- Multi‑layer parallax with depth and per‑layer controls
- OpenGL ES 2.0 renderer + Wayland layer‑shell
- 144 FPS target with optimized render loop
- Runtime control via `hyprlax ctl` (add/modify/list at runtime)
- Works across multiple compositors: Hyprland, Sway, Wayfire, Niri, River
- Simple legacy config and rich TOML configuration

## Quick Start

Already installed? Try these in a terminal:

```bash
# Single image (quick smoke test)
hyprlax ~/Pictures/wall.jpg

# Multi‑layer (foreground + background)
hyprlax --debug \
  --layer ~/Pictures/bg.jpg:0.3:1.0 \
  --layer ~/Pictures/fg.png:1.0:0.9

# Runtime control (add a layer while running)
hyprlax ctl add ~/Pictures/overlay.png opacity=0.8 scale=1.2
```

New to hyprlax? Start with the guides below.

- Getting started: [Quick Start](getting-started/quick-start.md) • [Installation](getting-started/installation.md) • [Compatibility](getting-started/compatibility.md)

## Configure

Hyprlax supports two configuration styles:

- Modern TOML: [TOML Reference](configuration/toml-reference.md)
- Legacy `.conf`: [Legacy Format](configuration/legacy-format.md)

Moving from legacy to TOML? See the [Migration Guide](configuration/migration-guide.md). You can
convert existing configs with:

```bash
hyprlax ctl convert-config ~/.config/hyprlax/parallax.conf ~/.config/hyprlax/hyprlax.toml --yes
```

Environment variables are also supported: see [Environment](configuration/environment.md).

## Learn by Examples and Guides

- Core topics: [Animation](guides/animation.md) • [Multi‑layer](guides/multi-layer.md) • [Cursor Tracking](guides/cursor-tracking.md)
 - Inputs: [Parallax Inputs](guides/inputs.md)
 - Performance tuning: [Performance](guides/performance.md)
- Real scenes to copy: [Examples Gallery](guides/examples.md)
- Runtime control overview: [IPC Overview](guides/ipc-overview.md)
- Help when stuck: [Troubleshooting](guides/troubleshooting.md)

## Reference

- CLI options: [CLI](reference/cli.md)
- Runtime commands: [IPC Commands](reference/ipc-commands.md)
- Easing catalog: [Easing Functions](reference/easing-functions.md)
- Environment variables: [Environment Variables](reference/environment-vars.md)

## Development

- Project overview: [Development Overview](development/README.md)
- Build from source: [Building](development/building.md)
- Testing: [Testing](development/testing.md)
- Contributing: [Contributing](development/contributing.md)
- Architecture: [Architecture](development/architecture.md)
