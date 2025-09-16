# hyprlax

Dynamic parallax wallpaper engine with multi-compositor support for Linux.

<p align="center">
  <img src="https://img.shields.io/badge/Wayland-Native-green" alt="Wayland Native">
  <img src="https://img.shields.io/badge/X11-EWMH-blue" alt="X11 EWMH">
  <img src="https://img.shields.io/badge/GPU-Accelerated-orange" alt="GPU Accelerated">
  <img src="https://img.shields.io/badge/Multi--Compositor-Support-purple" alt="Multi-Compositor">
</p>

## Features

- 🎬 **Buttery smooth animations** - GPU-accelerated rendering with configurable FPS
- 🖼️ **Parallax effect** - Wallpaper shifts as you switch workspaces
- 🌌 **Multi-layer parallax** - Create depth with multiple layers moving at different speeds
- 🔍 **Depth-of-field blur** - Realistic depth perception with per-layer blur effects
- 🖥️ **Multi-compositor support** - Works with Hyprland, Sway, River, Wayfire, Niri, X11 WMs, and more
- ⚡ **Lightweight** - Native client using appropriate protocols for each platform
- 🎨 **Customizable** - Per-layer easing functions, delays, and animation parameters
- 🔄 **Seamless transitions** - Interrupts and chains animations smoothly
- 🎮 **Dynamic layer management** - Add, remove, and modify layers at runtime via IPC

## Supported Compositors

### Wayland Compositors
| Compositor | Workspace Model | Special Features | Status |
|------------|----------------|------------------|---------|
| **Hyprland** | Linear workspaces | Full IPC, blur effects, animations | ✅ Full Support |
| **Sway** | i3-compatible workspaces | i3 IPC protocol | ✅ Full Support |
| **River** | Tag-based system | Tag workspace model | ✅ Full Support |
| **Wayfire** | 2D workspace grid | Horizontal & vertical parallax | ✅ Full Support |
| **Niri** | Scrollable workspaces | Smooth scrolling | ✅ Full Support |
| **Generic Wayland** | Basic workspaces | Any wlr-layer-shell compositor | ✅ Basic Support |

### X11 Window Managers
| Window Manager | Protocol | Features | Status |
|---------------|----------|----------|---------|
| **i3** | EWMH + i3 IPC | Workspace switching | ✅ Full Support |
| **bspwm** | EWMH | Desktop switching | ✅ Full Support |
| **awesome** | EWMH | Tag system | ✅ Full Support |
| **xmonad** | EWMH | Workspace switching | ✅ Full Support |
| **dwm** | EWMH | Tag system | ✅ Full Support |
| **Others** | EWMH | Basic desktop switching | ✅ Basic Support |

## Installation

### Quick Install

The easiest (but also least secure) method to install hyprlax is with the one-liner:

```bash
curl -sSL https://hyprlax.com/install.sh | bash
```

The next easiest (and more secure) method is to checkout the source and run the install script:

```bash
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
./install.sh        # Install for current user (~/.local/bin)
```

### Other Methods

- **System-wide**: `./install.sh -s` (requires sudo)
- **From release**: Download from [releases page](https://github.com/sandwichfarm/hyprlax/releases)
- **Manual build**: See [installation guide](docs/installation.md)

### Dependencies

#### Wayland
- Wayland, wayland-protocols, Mesa (EGL/GLES)

#### X11
- libX11, libXext, Mesa (EGL/GLES)

Full dependency list: [installation guide](docs/installation.md#dependencies)

## Quick Start

### Basic Usage

```bash
# Single wallpaper (auto-detects compositor)
hyprlax ~/Pictures/wallpaper.jpg

# Multi-layer parallax
hyprlax --layer background.jpg:0.3:1.0:expo:0:1.0:3.0 \
        --layer foreground.png:1.0:0.7

# Using config file
hyprlax --config ~/.config/hyprlax/parallax.conf

# Force specific compositor
HYPRLAX_COMPOSITOR=sway hyprlax ~/Pictures/wallpaper.jpg
```

### Key Options

- `-s, --shift` - Pixels to shift per workspace (default: 200)
- `-d, --duration` - Animation duration in seconds (default: 1.0)
- `-e, --easing` - Animation curve: linear, sine, expo, elastic, etc.
- `--layer` - Add layer: `image:shift:opacity[:easing[:delay[:duration[:blur]]]]`
- `--config` - Load from config file

**Full documentation:** [Configuration Guide](docs/configuration.md)

## Compositor Configuration

### Hyprland
Add to `~/.config/hypr/hyprland.conf`:
```bash
exec-once = pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg
```

### Sway/i3
Add to `~/.config/sway/config` or `~/.config/i3/config`:
```bash
exec_always pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg
```

### River
Add to your River init script:
```bash
riverctl spawn "pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg"
```

### Other Compositors
See [Compositor Configuration Guide](docs/compositors.md) for specific setup instructions.

## Runtime Control

Control layers and settings at runtime using the integrated `ctl` subcommand:

### Layer Management
```bash
# Add a new layer
hyprlax ctl add /path/to/image.png 1.5 0.8 10

# Modify layer properties
hyprlax ctl modify 1 opacity 0.5

# Remove a layer
hyprlax ctl remove 1

# List all layers
hyprlax ctl list

# Clear all layers
hyprlax ctl clear
```

### Runtime Settings
```bash
# Change FPS
hyprlax ctl set fps 120

# Change animation duration
hyprlax ctl set duration 2.0

# Change easing function
hyprlax ctl set easing elastic

# Query current settings
hyprlax ctl get fps
hyprlax ctl get duration

# Get daemon status
hyprlax ctl status
```

**Full guide:** [Dynamic Layer Management](docs/IPC.md)

## Documentation

### 📚 User Guides
- [Installation](docs/installation.md) - Detailed installation instructions
- [Configuration](docs/configuration.md) - All configuration options
- [Compositor Support](docs/compositors.md) - Compositor-specific features
- [Multi-Layer Parallax](docs/multi-layer.md) - Creating depth with layers
- [Animation](docs/animation.md) - Easing functions and timing
- [Examples](docs/examples.md) - Ready-to-use configurations

### 🔧 Developer Guides
- [Architecture](docs/architecture.md) - Modular design and components
- [Development](docs/development.md) - Building and contributing
- [Adding Compositors](docs/development.md#adding-compositor-support) - Extending compositor support

### ❓ Help
- [Troubleshooting](docs/troubleshooting.md) - Common issues and solutions
- [Dynamic Layers](docs/IPC.md) - Runtime layer control via IPC

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for a detailed list of changes in each release.

## Development

### Architecture

hyprlax uses a modular architecture with clear separation of concerns:
- **Platform abstraction** - Wayland and X11 backends
- **Compositor adapters** - Specific compositor implementations
- **Renderer abstraction** - OpenGL ES 2.0 rendering
- **Core engine** - Animation and configuration management

See [Architecture Documentation](docs/architecture.md) for details.

### Testing

hyprlax uses the [Check](https://libcheck.github.io/check/) framework for unit testing. Tests cover core functionality including animations, configuration parsing, IPC, shaders, and more.

#### Running Tests

```bash
# Run all tests
make test

# Run tests with memory leak detection
make memcheck

# Run individual test suites
./tests/test_blur
./tests/test_config
./tests/test_animation
```

### Code Quality Tools

```bash
# Run linter to check for issues
make lint

# Auto-fix formatting issues
make lint-fix

# Setup git hooks for automatic pre-commit checks
./scripts/setup-hooks.sh
```

## Contributing

Pull requests are welcome! Please read [RELEASE.md](RELEASE.md) for the release process.

See [Development Guide](docs/development.md) for:
- Building from source
- Project architecture
- Adding new features
- Code style guidelines

## License

MIT

## Roadmap

- [x] Multi-compositor support (Wayland & X11)
- [x] Dynamic layer loading/unloading
- [ ] Multi-monitor support
- [ ] Video wallpaper support
- [ ] Integration with wallpaper managers