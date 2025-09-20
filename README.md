> **hyprlax is presently under heavy development**, follow progress [here](https://github.com/sandwichfarm/hyprlax/issues/13)
> 
> New version will include Multi-Monitor Support, Robust Configuration and support for additional Compositors such as Niri and River (though still works best with _hyprland_)

# hyprlax

Smooth parallax wallpaper animations for Hyprland.

<p align="center">
  <img src="https://img.shields.io/badge/Hyprland-Compatible-blue" alt="Hyprland Compatible">
  <img src="https://img.shields.io/badge/Wayland-Native-green" alt="Wayland Native">
  <img src="https://img.shields.io/badge/GPU-Accelerated-orange" alt="GPU Accelerated">
</p>

## Features

- 🎬 **Buttery smooth animations** - GPU-accelerated rendering with configurable FPS.
- 🖼️ **Parallax effect** - Wallpaper shifts as you switch workspaces
- 🌌 **Multi-layer parallax** - Create depth with multiple layers moving at different speeds
- 🔍 **Depth-of-field blur** - Realistic depth perception with per-layer blur effects
- ⚡ **Lightweight** - Native Wayland client using layer-shell protocol
- 🎨 **Customizable** - Per-layer easing functions, delays, and animation parameters
- 🔄 **Seamless transitions** - Interrupts and chains animations smoothly
- 🎮 **Dynamic layer management** - Add, remove, and modify layers at runtime via IPC (NEW in v1.3.0)

## Installation

### Quick Install

The easiest (but also least secure) method to install hyprlax is with the one-liner  

```bash
curl -sSL https://hyprlax.com/install.sh | bash
```

The next easiest (and more secure) method is to checkout the source and run the install script 

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

- Wayland, wayland-protocols, Mesa (EGL/GLES)
- Full dependency list: [installation guide](docs/installation.md#dependencies)

## Quick Start

### Basic Usage

```bash
# Single wallpaper
hyprlax ~/Pictures/wallpaper.jpg

# Multi-layer parallax
hyprlax --layer background.jpg:0.3:1.0:expo:0:1.0:3.0 \
        --layer foreground.png:1.0:0.7

# Using config file
hyprlax --config ~/.config/hyprlax/parallax.conf
```

### Key Options

- `-s, --shift` - Pixels to shift per workspace (default: 200)
- `-d, --duration` - Animation duration in seconds (default: 1.0)
- `-e, --easing` - Animation curve: linear, sine, expo, elastic, etc.
- `--layer` - Add layer: `image:shift:opacity[:easing[:delay[:duration[:blur]]]]`
- `--config` - Load from config file

**Full documentation:** [Configuration Guide](docs/configuration.md)


## Hyprland Configuration

Add to `~/.config/hypr/hyprland.conf`:

```bash
# Kill existing wallpaper daemons
exec-once = pkill swww-daemon; pkill hyprpaper; pkill hyprlax

# Start hyprlax
exec-once = hyprlax ~/Pictures/wallpaper.jpg

# Or with multi-layer config
exec-once = hyprlax --config ~/.config/hyprlax/parallax.conf
```

**Full setup guide:** [Configuration Guide](docs/configuration.md)

## Dynamic Layer Management

Control layers at runtime using `hyprlax-ctl`:

```bash
# Add a new layer
hyprlax-ctl add /path/to/image.png scale=1.5 opacity=0.8

# Modify layer properties
hyprlax-ctl modify 1 opacity 0.5

# Remove a layer
hyprlax-ctl remove 1

# List all layers
hyprlax-ctl list
```

**Full guide:** [Dynamic Layer Management](docs/IPC.md)

## Documentation

### 📚 Guides
- [Installation](docs/installation.md) - Detailed installation instructions
- [Configuration](docs/configuration.md) - All configuration options
- [Multi-Layer Parallax](docs/multi-layer.md) - Creating depth with layers
- [Dynamic Layer Management](docs/IPC.md) - Runtime layer control via IPC
- [Animation](docs/animation.md) - Easing functions and timing
- [Examples](docs/examples.md) - Ready-to-use configurations
- [Troubleshooting](docs/troubleshooting.md) - Common issues and solutions
- [Development](docs/development.md) - Building and contributing

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for a detailed list of changes in each release.

## License

MIT

## Development

### Testing

hyprlax uses the [Check](https://libcheck.github.io/check/) framework for unit testing. Tests cover core functionality including animations, configuration parsing, IPC, shaders, and more.

#### Test Dependencies

```bash
# Arch Linux
sudo pacman -S check

# Ubuntu/Debian
sudo apt-get install check

# Fedora
sudo dnf install check-devel
```

Optional for memory leak detection:
```bash
# Install valgrind (most distros)
sudo pacman -S valgrind  # Arch
sudo apt-get install valgrind  # Ubuntu/Debian

# Arch Linux users may also need debug symbols:
sudo pacman -S debuginfod
# Then set environment variable:
export DEBUGINFOD_URLS="https://debuginfod.archlinux.org"
```

#### Running Tests

```bash
# Run all tests
make test

# Run tests with memory leak detection
make memcheck

# Note: If valgrind reports "unhandled instruction", rebuild tests without -march=native:
# make clean-tests && CFLAGS="-Wall -Wextra -O2" make test

# Run individual test suites
./tests/test_blur
./tests/test_config
./tests/test_animation
```

### Code Quality Tools

We provide linting tools to catch common issues before they reach CI:

```bash
# Run linter to check for issues
make lint

# Auto-fix formatting issues
make lint-fix

# Setup git hooks for automatic pre-commit checks
./scripts/setup-hooks.sh
```

The linter checks for:
- Trailing whitespace
- Missing newlines at end of files
- Compilation errors
- Common security issues
- Static analysis (if cppcheck is installed)

## Contributing

Pull requests are welcome! Please read [RELEASE.md](RELEASE.md) for the release process.

## Roadmap

- [ ] Dynamic layer loading/unloading ([#1](https://github.com/sandwichfarm/hyprlax/issues/1))
- [ ] Multi-monitor support
- [ ] Video wallpaper support
- [ ] Integration with wallpaper managers
