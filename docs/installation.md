# Installation Guide

This guide covers all installation methods for hyprlax.

## Quick Install

The easiest (but also least secure) way to install hyprlax is with the one-liner  

```bash
curl -sSL https://hyprlax.com/install.sh | bash
```

The next easiest (and more secure) is to checkout the source and run the install script 

```bash
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
./install.sh        # Install for current user (~/.local/bin)
```

For system-wide installation:
```bash
./install.sh -s     # Requires sudo, installs to /usr/local/bin
```

The installer will:
- Build hyprlax with optimizations
- Install the binary to the appropriate location
- Set up your PATH if needed
- Restart hyprlax if it's already running (for upgrades)

## Installing from Release

Download pre-built binaries from the [releases page](https://github.com/sandwichfarm/hyprlax/releases):

### For x86_64:
```bash
wget https://github.com/sandwichfarm/hyprlax/releases/latest/download/hyprlax-x86_64
chmod +x hyprlax-x86_64
sudo mv hyprlax-x86_64 /usr/local/bin/hyprlax
```

### For ARM64/aarch64:
```bash
wget https://github.com/sandwichfarm/hyprlax/releases/latest/download/hyprlax-aarch64
chmod +x hyprlax-aarch64
sudo mv hyprlax-aarch64 /usr/local/bin/hyprlax
```

## Building from Source

### Dependencies
_Only Arch Linux has been thoroughly tested. If you find issues with dependency installations on your system, please open an issue_

#### Core Dependencies

hyprlax now supports both Wayland and X11. Install the dependencies for your platform:

##### Arch Linux
```bash
# Wayland + X11 support (recommended)
sudo pacman -S base-devel wayland wayland-protocols mesa libx11 libxext

# Wayland only
sudo pacman -S base-devel wayland wayland-protocols mesa

# X11 only
sudo pacman -S base-devel libx11 libxext mesa
```

##### Ubuntu/Debian
```bash
# Wayland + X11 support (recommended)
sudo apt update
sudo apt install build-essential libwayland-dev wayland-protocols \
                 libegl1-mesa-dev libgles2-mesa-dev pkg-config \
                 libx11-dev libxext-dev

# Wayland only
sudo apt install build-essential libwayland-dev wayland-protocols \
                 libegl1-mesa-dev libgles2-mesa-dev pkg-config

# X11 only
sudo apt install build-essential libx11-dev libxext-dev \
                 libegl1-mesa-dev libgles2-mesa-dev pkg-config
```

##### Fedora
```bash
# Wayland + X11 support (recommended)
sudo dnf install gcc make wayland-devel wayland-protocols-devel \
                 mesa-libEGL-devel mesa-libGLES-devel pkg-config \
                 libX11-devel libXext-devel

# Wayland only
sudo dnf install gcc make wayland-devel wayland-protocols-devel \
                 mesa-libEGL-devel mesa-libGLES-devel pkg-config

# X11 only
sudo dnf install gcc make libX11-devel libXext-devel \
                 mesa-libEGL-devel mesa-libGLES-devel pkg-config
```

##### openSUSE
```bash
# Wayland + X11 support (recommended)
sudo zypper install gcc make wayland-devel wayland-protocols-devel \
                     Mesa-libEGL-devel Mesa-libGLES-devel pkg-config \
                     libX11-devel libXext-devel

# Wayland only
sudo zypper install gcc make wayland-devel wayland-protocols-devel \
                     Mesa-libEGL-devel Mesa-libGLES-devel pkg-config

# X11 only
sudo zypper install gcc make libX11-devel libXext-devel \
                     Mesa-libEGL-devel Mesa-libGLES-devel pkg-config
```

##### Void Linux
```bash
# Wayland + X11 support (recommended)
sudo xbps-install base-devel wayland wayland-protocols \
                  MesaLib-devel pkg-config \
                  libX11-devel libXext-devel

# Wayland only
sudo xbps-install base-devel wayland wayland-protocols \
                  MesaLib-devel pkg-config

# X11 only
sudo xbps-install base-devel libX11-devel libXext-devel \
                  MesaLib-devel pkg-config
```

##### NixOS
```nix
# In configuration.nix or shell.nix
environment.systemPackages = with pkgs; [
  # Build tools
  gcc gnumake pkg-config
  
  # Wayland support
  wayland wayland-protocols
  
  # X11 support
  xorg.libX11 xorg.libXext
  
  # OpenGL
  mesa libGL libGLU
];
```

#### Compositor-Specific Dependencies

Some compositors may require additional packages:

##### For X11 Window Managers
If using X11 window managers (i3, bspwm, awesome, etc.), you may want a compositor for transparency and blur effects:

```bash
# Arch Linux
sudo pacman -S picom

# Ubuntu/Debian
sudo apt install picom

# Fedora
sudo dnf install picom

# Others
# Build picom from source: https://github.com/yshui/picom
```

#### Optional Dependencies for Development

##### Testing Framework (Check)
Required for running the test suite:

```bash
# Arch Linux
sudo pacman -S check

# Ubuntu/Debian
sudo apt-get install check

# Fedora
sudo dnf install check-devel

# openSUSE
sudo zypper install check-devel

# Void Linux
sudo xbps-install check-devel
```

##### Memory Leak Detection (Valgrind)
Optional but recommended for development:

```bash
# Most distributions
sudo pacman -S valgrind     # Arch
sudo apt-get install valgrind  # Ubuntu/Debian
sudo dnf install valgrind   # Fedora
sudo zypper install valgrind   # openSUSE
sudo xbps-install valgrind  # Void

# Arch Linux: For valgrind to work properly, you may need:
sudo pacman -S debuginfod
export DEBUGINFOD_URLS="https://debuginfod.archlinux.org"

# Note: If valgrind fails with "unrecognised instruction", rebuild without -march=native:
# make clean-tests && CFLAGS="-Wall -Wextra -O2 -Isrc" make test
```

### Build Process

```bash
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
make
```

### Installation Options

#### User Installation (no sudo required)
```bash
make install-user   # Installs to ~/.local/bin
```

Make sure `~/.local/bin` is in your PATH:
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

#### System Installation
```bash
sudo make install   # Installs to /usr/local/bin
```

#### Custom Installation
```bash
make PREFIX=/custom/path install
```

## Verifying Installation

Check that hyprlax is installed correctly:

```bash
hyprlax --version
```

You should see:
```
hyprlax 1.3.0
Dynamic parallax wallpaper engine with multi-compositor support
Detected compositor: Hyprland  (or your current compositor)
Platform: Wayland  (or X11)
```

To check supported features:
```bash
# Check compositor detection
hyprlax --detect-compositor

# List capabilities
hyprlax --capabilities
```

## Upgrading

If you already have hyprlax installed, the installer will detect it and perform an upgrade:

```bash
cd hyprlax
git pull
./install.sh  # Will backup existing installation and upgrade
```

## Uninstallation

### If installed via script
```bash
# User installation
rm ~/.local/bin/hyprlax

# System installation
sudo rm /usr/local/bin/hyprlax
```

### If installed via make
```bash
cd hyprlax
make uninstall-user  # For user installation
# OR
sudo make uninstall  # For system installation
```

## Next Steps

- [Configure hyprlax](configuration.md) in your Hyprland config
- Learn about [multi-layer parallax](multi-layer.md)
- Explore [animation options](animation.md)
