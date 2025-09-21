# Custom Build Configuration

Hyprlax supports creating optimized, minimal builds with only the features you need. By default, all platforms, renderers, and compositor backends are included. You can customize your build to reduce binary size and dependencies.

## Default Build

By default, all features are enabled:
```bash
make        # Builds with all features
make all    # Same as above
```

## Custom Builds

### Minimal Hyprland-only Build
Build with only Wayland platform and Hyprland compositor support:
```bash
make clean
make ENABLE_SWAY=0 ENABLE_WAYFIRE=0 ENABLE_NIRI=0 ENABLE_RIVER=0 ENABLE_GENERIC_WAYLAND=0
```

### Sway/Wayfire Build (i3/wlroots compositors)
```bash
make clean
make ENABLE_HYPRLAND=0 ENABLE_NIRI=0 ENABLE_RIVER=0
```

## Build Flags

### Platform Flags
- `ENABLE_WAYLAND` (default: 1) - Wayland platform support

### Renderer Flags
- `ENABLE_GLES2` (default: 1) - OpenGL ES 2.0 renderer

### Compositor Flags
- `ENABLE_HYPRLAND` (default: 1) - Hyprland compositor
- `ENABLE_SWAY` (default: 1) - Sway/i3 compositor
- `ENABLE_WAYFIRE` (default: 1) - Wayfire compositor (2D workspace grid)
- `ENABLE_NIRI` (default: 1) - Niri compositor (scrollable workspaces)
- `ENABLE_RIVER` (default: 1) - River compositor (tag-based)
- `ENABLE_GENERIC_WAYLAND` (default: 1) - Generic Wayland fallback

## Binary Size Comparison

Typical binary sizes with different configurations:
- Full build (all features): ~200KB
- Hyprland-only: ~120KB
- Single compositor: ~80-100KB

## Dependencies

Custom builds will automatically adjust package dependencies:
- Wayland builds require: `wayland-client`, `wayland-protocols`, `wayland-egl`
- GLES2 renderer requires: `egl`, `glesv2`

## Verification

After building, you can verify which features are included:
```bash
# Check binary size
ls -lh hyprlax

# Check linked libraries
ldd hyprlax

# Test detection (will show available backends)
./hyprlax --debug
```

## Installation

Install your custom build:
```bash
sudo make install
```

## Examples

### Embedded/Minimal System
For resource-constrained systems, build with only essential features:
```bash
make ENABLE_SWAY=0 ENABLE_WAYFIRE=0 ENABLE_NIRI=0 ENABLE_RIVER=0 ENABLE_GENERIC_WAYLAND=0
```

### Distribution Package
For distribution packages, keep all features enabled (default):
```bash
make
```

### Development/Testing
Test a specific compositor implementation:
```bash
# Test only Sway implementation
make ENABLE_HYPRLAND=0 ENABLE_WAYFIRE=0 ENABLE_NIRI=0 ENABLE_RIVER=0 ENABLE_GENERIC_WAYLAND=0
```

## Troubleshooting

### Build Errors
If you get "not available in this build" errors at runtime, ensure the required features were enabled during compilation.

### Missing Dependencies
The build will fail if required packages are missing. Install only the dependencies needed for your configuration:
```bash
# For Wayland-only
pkg-config --libs wayland-client wayland-egl egl glesv2

```

### Runtime Detection
If auto-detection fails, the binary will fall back to the first available backend compiled in. You can override detection:
```bash
# Force specific platform (if compiled in)
WAYLAND_DISPLAY=wayland-0 ./hyprlax
```