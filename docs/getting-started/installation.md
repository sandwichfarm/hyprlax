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

## Install from AUR (Arch Linux)

If you’re on Arch or an Arch-based distribution, you can install from the AUR:

```bash
# Using an AUR helper (recommended)
yay -S hyprlax-git

# Manual AUR build
git clone https://aur.archlinux.org/hyprlax-git.git
cd hyprlax-git
makepkg -si
```

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

hyprlax supports Wayland compositors. Install the required dependencies:

##### Arch Linux
```bash
sudo pacman -S base-devel wayland wayland-protocols mesa
```

##### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential libwayland-dev wayland-protocols \
                 libegl1-mesa-dev libgles2-mesa-dev pkg-config
```

##### Fedora
```bash
sudo dnf install gcc make wayland-devel wayland-protocols-devel \
                 mesa-libEGL-devel mesa-libGLES-devel pkg-config
```

##### openSUSE
```bash
sudo zypper install gcc make wayland-devel wayland-protocols-devel \
                     Mesa-libEGL-devel Mesa-libGLES-devel pkg-config
```

##### Void Linux
```bash
sudo xbps-install base-devel wayland wayland-protocols \
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
  
  # OpenGL
  mesa libGL libGLU
];
```

#### Compositor-Specific Dependencies

Some compositors may require additional packages:

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

You should see output similar to:
```
hyprlax <version>
Buttery-smooth parallax wallpaper daemon with support for multiple compositors, platforms and renderers
```

To inspect runtime behavior and auto-detection, run with debug enabled:
```bash
HYPRLAX_DEBUG=1 hyprlax --debug ~/Pictures/test.jpg
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
