# Building from Source

Complete guide for compiling hyprlax from source code.

## Prerequisites

### Required Tools
- **C Compiler**: GCC (≥9) or Clang (≥10)
- **Build System**: GNU Make
- **Package Config**: pkg-config
- **Wayland Scanner**: wayland-scanner (for protocol generation)

### Required Libraries

#### Debian/Ubuntu
```bash
sudo apt install \
    build-essential \
    pkg-config \
    libwayland-dev \
    wayland-protocols \
    libegl-dev \
    libgles2-mesa-dev \
    libtoml-dev
```

#### Fedora
```bash
sudo dnf install \
    gcc make pkg-config \
    wayland-devel \
    wayland-protocols-devel \
    mesa-libEGL-devel \
    mesa-libGLES-devel \
    toml-c-devel
```

#### Arch Linux
```bash
sudo pacman -S \
    base-devel \
    wayland \
    wayland-protocols \
    mesa \
    tomlc99
```

#### Alpine Linux
```bash
sudo apk add \
    build-base \
    pkgconfig \
    wayland-dev \
    wayland-protocols \
    mesa-dev \
    tomlc99-dev
```

## Getting the Source

### Clone Repository
```bash
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
```

### Specific Branch
```bash
# Development branch
git clone -b feature/refactor https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
```

### Specific Release
```bash
# Latest stable
git clone --branch v2.0.0 https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
```

## Build Commands

### Standard Build
```bash
make
```
Produces optimized binary at `./hyprlax`

### Verbose/Custom Builds
Use environment or make variables to tweak flags:
```bash
make VERBOSE=1                 # Show full compiler commands
make CFLAGS="-Og -g"           # Debug-friendly flags
make CFLAGS="-O3 -march=native"  # Performance build
```

### Clean Build
```bash
make clean && make
```
Removes all artifacts and rebuilds

### Verbose Build
```bash
make VERBOSE=1
```
Shows full compiler commands

### Parallel Build
```bash
make -j$(nproc)
```
Uses all CPU cores for faster compilation

## Build Options

### Compiler Selection
```bash
# Use Clang
make CC=clang

# Use specific GCC version
make CC=gcc-12

# Cross-compile
make CC=aarch64-linux-gnu-gcc
```

### Optimization Levels
```bash
# Maximum optimization
make CFLAGS="-O3 -march=native"

# Size optimization
make CFLAGS="-Os"

# Debug-friendly
make CFLAGS="-Og -g"
```

### Architecture-Specific
```bash
# ARM64/AArch64
make ARCH=aarch64

# ARM 32-bit
make ARCH=armv7l

# x86 32-bit
make ARCH=i686
```

### Custom Paths
```bash
# Custom installation prefix
make PREFIX=/usr/local install

# Custom library path
make LDFLAGS="-L/opt/wayland/lib"

# Custom include path
make CFLAGS="-I/opt/wayland/include"
```

## Installation

### System-wide Install
```bash
sudo make install
```
Default locations:
- Binary: `/usr/local/bin/hyprlax`
- Docs: `/usr/local/share/doc/hyprlax/`
- Examples: `/usr/local/share/hyprlax/examples/`

### Custom Prefix
```bash
# Install to home directory
make PREFIX=$HOME/.local install

# Install to /opt
sudo make PREFIX=/opt/hyprlax install
```

### Uninstall
```bash
sudo make uninstall
```

## Build Targets

| Target | Description |
|--------|-------------|
| `all` | Build hyprlax binary (default) |
| `clean` | Remove build artifacts |
| `install` | Install to system |
| `uninstall` | Remove from system |
| `test` | Run test suite |
| `memcheck` | Run tests under valgrind |
| `bench` | Run benchmark helper script |
| `bench-perf` | Detailed performance benchmark |
| `bench-30fps` | Power consumption benchmark |
| `lint` | Run lint script (if available) |
| `lint-fix` | Auto-fix lint issues (if available) |

## Dependencies Resolution

### Missing Wayland Protocols
```bash
# Find protocols path
pkg-config --variable=pkgdatadir wayland-protocols

# Set manually if needed
export PKG_CONFIG_PATH=/usr/share/wayland-protocols:$PKG_CONFIG_PATH
```

### Missing EGL/GLES
```bash
# Verify Mesa installation
pkg-config --libs egl glesv2

# Install Mesa development files
sudo apt install libgl1-mesa-dev  # Debian/Ubuntu
```

### TOML Library Issues
No external TOML library is required; a vendor copy is included and built automatically.

## Troubleshooting

### Compilation Errors

#### "wayland-scanner not found"
```bash
# Install wayland-scanner
sudo apt install wayland-scanner  # Debian/Ubuntu
sudo dnf install wayland-scanner  # Fedora
```

#### "fatal error: GLES2/gl2.h: No such file"
```bash
# Install OpenGL ES headers
sudo apt install libgles2-mesa-dev  # Debian/Ubuntu
sudo dnf install mesa-libGLES-devel  # Fedora
```

#### Linking errors
```bash
# Check library paths
pkg-config --libs wayland-client egl glesv2

# Force static linking
make LDFLAGS="-static"
```

### Runtime Issues

#### "error while loading shared libraries"
```bash
# Update library cache
sudo ldconfig

# Check library dependencies
ldd hyprlax

# Set library path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

## Development Builds

### Sanitizers
```bash
# Address sanitizer
make CFLAGS="-fsanitize=address -g" LDFLAGS="-fsanitize=address"

# Thread sanitizer
make CFLAGS="-fsanitize=thread -g" LDFLAGS="-fsanitize=thread"

# Undefined behavior sanitizer
make CFLAGS="-fsanitize=undefined -g" LDFLAGS="-fsanitize=undefined"
```

### Static Analysis
```bash
# Using clang-tidy
make check

# Using cppcheck
cppcheck --enable=all src/

# Using scan-build
scan-build make
```

### Profile-Guided Optimization
```bash
# Step 1: Build with profiling
make CFLAGS="-fprofile-generate"

# Step 2: Run typical workload
./hyprlax --config examples/multi-layer.toml

# Step 3: Rebuild with profile data
make clean
make CFLAGS="-fprofile-use"
```

## Platform-Specific Notes

### NixOS
```nix
# shell.nix
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config
    wayland
    wayland-protocols
    mesa
    tomlc99
  ];
}
```

### Docker Build
```dockerfile
FROM debian:bookworm
RUN apt-get update && apt-get install -y \
    build-essential \
    pkg-config \
    libwayland-dev \
    wayland-protocols \
    libegl-dev \
    libgles2-mesa-dev
COPY . /build
WORKDIR /build
RUN make
```

### Cross-Compilation
```bash
# For Raspberry Pi
make CC=arm-linux-gnueabihf-gcc ARCH=armv7l

# For Pine64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
```
