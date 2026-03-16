# Technology Stack

**Analysis Date:** 2026-03-16

## Languages

**Primary:**
- C (C99 standard) - Core engine, platform adapters, renderers, all application logic

**Secondary:**
- Bash - Installation scripts, testing framework, build tooling
- JavaScript/Svelte - Website documentation and demos only (not part of main binary)

## Runtime

**Environment:**
- Linux (Wayland) - Primary target, requires Wayland display server
- GCC - Compiler (supports cross-compilation via `ARCH` flag)

**Package Manager:**
- None - Statically linked or system packages only
- Dependencies resolved via `pkg-config` from system packages

## Frameworks

**Core:**
- Wayland (wayland-client, wayland-protocols) - Display server integration
- EGL (OpenGL ES 2.0) - Graphics rendering via Mesa
- GLES2 - GPU-accelerated rendering

**Protocol Support:**
- wlr-layer-shell (unstable-v1) - Layer shell protocol for wallpaper
- xdg-shell - XDG window protocol (via wayland-protocols)
- fractional-scale-v1 - Fractional scaling support (via wayland-protocols)
- viewporter - Viewport manipulation (via wayland-protocols)
- river-status-unstable-v1 - River compositor workspace events

**Configuration:**
- TOML (via tomlc99 vendor library) - Configuration file format
- Legacy `.conf` format - Deprecated, supported for migration

**Testing:**
- Check (libcheck) - Unit testing framework
- Valgrind - Memory leak detection (optional, in Makefile)

**Build/Dev:**
- Make - Build system
- wayland-scanner - Protocol code generation from XML
- pkg-config - Dependency resolution

## Key Dependencies

**Critical:**
- `wayland-client` - Wayland client library
- `wayland-protocols` - Standard Wayland protocol definitions
- `wayland-egl` - EGL integration with Wayland
- `glesv2` - OpenGL ES 2.0 library
- `egl` - OpenGL driver abstraction
- `libm` (math.h) - C math library (linked with `-lm`)

**Vendor Libraries (Embedded):**
- `tomlc99` (src/vendor/toml.c) - Embedded TOML parser
- `gifdec` (src/vendor/gifdec.c) - GIF decoder
- `stb_image` (src/stb_image.h) - Single-header image loading (PNG/JPG)

**Optional Testing:**
- `check` - Unit test framework (pkg-config discovery)
- `valgrind` - Memory debugging tool

## Configuration

**Environment:**
- No `.env` files required - configuration via TOML or environment variables
- Environment variable precedence: Command-line args > Environment vars > Config file > Defaults

**Key Environment Variables:**
- `HYPRLAX_RENDER_FPS` - Override FPS setting
- `HYPRLAX_ANIMATION_DURATION` - Override animation duration
- `HYPRLAX_ANIMATION_EASING` - Override easing function
- `HYPRLAX_PARALLAX_SHIFT_PIXELS` - Override shift in pixels
- `HYPRLAX_PARALLAX_SHIFT_PERCENT` - Override shift as percentage of screen width
- `HYPRLAX_RENDER_VSYNC` - Enable/disable vsync
- `HYPRLAX_PARALLAX_INPUT` - Input source for parallax (workspace, cursor, window)
- `HYPRLAX_PARALLAX_SOURCES_CURSOR_WEIGHT` - Cursor parallax weight
- `HYPRLAX_PARALLAX_SOURCES_WORKSPACE_WEIGHT` - Workspace parallax weight
- `HYPRLAX_DEBUG` - Enable debug logging to stderr
- `HYPRLAX_VERBOSE` - Logging verbosity level (0-4)
- `HYPRLAX_INIT_TRACE` - Initialization tracing to stderr
- `HYPRLAX_TRACE` - General tracing (varies by context)
- `HYPRLAX_ASSUME_YES` - Non-interactive mode
- `HYPRLAX_NONINTERACTIVE` - Non-interactive mode (alias)
- `HYPRLAX_FRAME_CALLBACK` - Wayland frame callback mode override
- `HYPRLAX_COMPOSITOR` - Force specific compositor: hyprland, sway, generic, etc.
- `HYPRLAX_SOCKET_SUFFIX` - IPC socket naming suffix (for testing)
- `WAYLAND_DISPLAY` - Wayland display identifier (system)
- `XDG_RUNTIME_DIR` - Runtime directory for sockets (system)
- `HYPRLAND_INSTANCE_SIGNATURE` - Hyprland instance ID (Hyprland-specific)
- `HOME` - Home directory for config path resolution

**Configuration Files:**
- Primary: `~/.config/hyprlax/` - User configuration directory
- Supported: TOML format (required, preferred)
- Legacy: `.conf` format (deprecated, warnings shown)
- Example: `/home/sandwich/.config/hyprlax/pixel-city/parallax.toml`

**Build Configuration:**
- Makefile variables for conditional compilation:
  - `ENABLE_WAYLAND` (default 1) - Wayland support
  - `ENABLE_HYPRLAND` (default 1) - Hyprland adapter
  - `ENABLE_SWAY` (default 1) - Sway adapter
  - `ENABLE_WAYFIRE` (default 1) - Wayfire adapter
  - `ENABLE_NIRI` (default 1) - Niri adapter
  - `ENABLE_RIVER` (default 1) - River adapter
  - `ENABLE_GENERIC_WAYLAND` (default 1) - Generic Wayland fallback
  - `ENABLE_GLES2` (default 1) - OpenGL ES 2.0 rendering
  - `CI` - CI mode (generic arch, O2 optimization)
  - `ARCH` - Cross-compilation target (e.g., `aarch64`)

**Optimization Flags:**
- Development: `-O3 -march=native -flto` (Native architecture, link-time optimization)
- CI/Release: `-O2` (Portable binary)
- Always: `-Wall -Wextra` (Strict warnings)

## Platform Requirements

**Development:**
- GCC compiler with C99 support
- pkg-config for dependency resolution
- wayland-protocols package installed
- Make build tool
- Standard POSIX development tools

**Runtime:**
- Linux with Wayland display server (not X11)
- Wayland libraries installed (wayland-client, wayland-protocols)
- Mesa/GPU drivers providing EGL and GLES2
- XDG_RUNTIME_DIR must be set (standard on modern Linux)

**Production:**
- Deployment target: Linux distributions with Wayland support
- Binary installation: `/usr/local/bin/hyprlax` (system-wide, recommended)
- Alternative: `~/.local/bin/hyprlax` (user-only, requires full path in configs)
- Configuration path: `~/.config/hyprlax/` (user config directory)
- Socket path: `/tmp/hyprlax-<uid>-<suffix>` (IPC communication)

## Version Management

**Versioning:**
- Version embedded from git commit hash (if VERSION file missing)
- CI/CD overwrites with tag version
- Available as `HYPRLAX_VERSION` macro in compilation

**Build Artifacts:**
- Single binary: `hyprlax` (C executable)
- Size: ~500KB (typical, varies by architecture)
- Dependencies: Dynamically linked (system libraries)

---

*Stack analysis: 2026-03-16*
