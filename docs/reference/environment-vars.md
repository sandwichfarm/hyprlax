# Environment Variables Reference

Environment variables that affect hyprlax behavior.

## hyprlax-Specific Variables

### HYPRLAX_DEBUG
Enable debug output without command-line flag.

**Values:** `0` (off), `1` (on)  
**Default:** `0`

```bash
export HYPRLAX_DEBUG=1
hyprlax image.jpg  # Debug enabled
```

### HYPRLAX_TRACE
Enable trace-level logging (very verbose).

**Values:** `0` (off), non-empty (on)

```bash
export HYPRLAX_TRACE=1
hyprlax --debug image.jpg
```

### HYPRLAX_VERBOSE
Numeric log level override.

**Values:** `0..4` where 0=error, 1=warn, 2=info, 3=debug, 4=trace

```bash
export HYPRLAX_VERBOSE=3  # debug
hyprlax image.jpg
```

### Rendering/Performance Tweaks

The renderer recognizes the following variables:

- `HYPRLAX_PERSISTENT_VBO=1` — reuse VBOs to reduce allocations
- `HYPRLAX_UNIFORM_OFFSET=1` — pass offsets via uniforms (keeps geometry static)
- `HYPRLAX_NO_GLFINISH=1` — skip glFinish to reduce CPU/GPU sync
- `HYPRLAX_SEPARABLE_BLUR=1` — enable separable blur path
- `HYPRLAX_BLUR_DOWNSCALE=<n>` — render blur at lower resolution (2, 4, ...)
- `HYPRLAX_FRAME_CALLBACK=1` — use Wayland frame callbacks for timing
- `HYPRLAX_RENDER_DIAG=1` — print render diagnostics when idle
- `HYPRLAX_PROFILE=1` — print frame timing/profile lines

## Compositor Detection

hyprlax checks these variables to auto-detect your compositor:

### HYPRLAND_INSTANCE_SIGNATURE
Indicates Hyprland is running.

**Set by:** Hyprland compositor  
**Example:** `v0.35.0-1-g12345abc_1234567890`

```bash
if [ -n "$HYPRLAND_INSTANCE_SIGNATURE" ]; then
    echo "Running on Hyprland"
fi
```

### SWAYSOCK
Indicates Sway is running.

**Set by:** Sway compositor  
**Example:** `/run/user/1000/sway-ipc.1000.12345.sock`

```bash
if [ -n "$SWAYSOCK" ]; then
    echo "Running on Sway"
fi
```

### WAYLAND_DISPLAY
Wayland session identifier.

**Set by:** All Wayland compositors  
**Example:** `wayland-0`, `wayland-1`

```bash
if [ -n "$WAYLAND_DISPLAY" ]; then
    echo "Running on Wayland"
fi
```

## Display Variables

### DISPLAY
X11 display (legacy, not used).

**Note:** X11 support has been removed. This variable is ignored.

### XDG_SESSION_TYPE
Session type indicator.

**Values:** `wayland`, `x11`, `tty`  
**Used for:** Confirming Wayland session

```bash
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    hyprlax image.jpg
fi
```

## Path Variables

### XDG_CONFIG_HOME
User configuration directory.

**Default:** `~/.config`  
**Used for:** Finding config files

```bash
# hyprlax looks for configs in:
$XDG_CONFIG_HOME/hyprlax/
```

### XDG_RUNTIME_DIR
Runtime directory for sockets.

**Default:** `/run/user/$UID`  
**Used for:** IPC socket location (preferred when available)

### HYPRLAX_SOCKET_SUFFIX
Append a suffix to the IPC socket filename for isolation (useful for tests, parallel runs, or multiple instances).

- Values: any short string with letters/digits/`-`/`_`
- Preferred path: `$XDG_RUNTIME_DIR/hyprlax-$USER-$HYPRLAND_INSTANCE_SIGNATURE-$SUFFIX.sock`
- Fallback path: `/tmp/hyprlax-$USER-$SUFFIX.sock`

Example:
```bash
export HYPRLAX_SOCKET_SUFFIX=tests
make test
```

Alias: HYPRLAX_TEST_SUFFIX is still accepted but considered deprecated; HYPRLAX_SOCKET_SUFFIX takes precedence when both are set.

### HOME
User home directory.

**Used for:** Expanding `~` in paths

## Graphics Variables

### WLR_RENDERER
Force wlroots renderer backend.

**Values:** `vulkan`, `gles2`, `pixman`  
**Note:** May affect performance

```bash
export WLR_RENDERER=gles2
hyprlax image.jpg
```

### __GLX_VENDOR_LIBRARY_NAME
NVIDIA proprietary driver indicator.

**Values:** `nvidia`  
**Used for:** Detecting NVIDIA GPUs

### LIBGL_ALWAYS_SOFTWARE
Force software rendering.

**Values:** `0` (off), `1` (on)  
**Warning:** Severely impacts performance

```bash
# For testing only
export LIBGL_ALWAYS_SOFTWARE=1
hyprlax --debug image.jpg
```

## Debug Variables

### WAYLAND_DEBUG
Enable Wayland protocol debugging.

**Values:** `0` (off), `1` (client), `2` (server), `3` (both)  
**Warning:** Very verbose output

```bash
export WAYLAND_DEBUG=1
hyprlax image.jpg 2>&1 | less
```

### EGL_LOG_LEVEL
EGL debugging verbosity.

**Values:** `debug`, `info`, `warning`, `error`, `fatal`  
**Default:** `warning`

```bash
export EGL_LOG_LEVEL=debug
hyprlax --debug image.jpg
```

## Usage Examples

### Development Setup
```bash
#!/bin/bash
# Development environment
export HYPRLAX_DEBUG=1
export HYPRLAX_CONFIG=./test-config.toml
export WAYLAND_DEBUG=1
hyprlax test-image.jpg
```

### Production Setup
```bash
#!/bin/bash
# Production environment
unset HYPRLAX_DEBUG
export HYPRLAX_CONFIG=~/.config/hyprlax/production.toml
hyprlax ~/wallpapers/current.jpg
```

### Compositor Override
```bash
#!/bin/bash
# Force generic mode regardless of compositor
unset HYPRLAND_INSTANCE_SIGNATURE
unset SWAYSOCK
hyprlax --compositor generic image.jpg
```

### Debugging Graphics Issues
```bash
#!/bin/bash
# Full graphics debugging
export HYPRLAX_DEBUG=1
export EGL_LOG_LEVEL=debug
export WAYLAND_DEBUG=1
hyprlax --debug test.jpg 2>&1 | tee debug.log
```

## Checking Your Environment

### List Relevant Variables
```bash
env | grep -E "(HYPRLAX|WAYLAND|HYPRLAND|SWAY|XDG|WLR|EGL|LIBGL)"
```

### hyprlax Environment Check
```bash
hyprlax --debug --compositor auto test.jpg 2>&1 | head -20
```

This shows which environment variables hyprlax detected.

## Priority Order

When multiple configuration methods exist:

1. Command-line arguments (highest priority)
2. Configuration file
3. Environment variables
4. Built-in defaults (lowest priority)

Example:
```bash
export HYPRLAX_DEBUG=1        # Enables debug
hyprlax --debug 0 image.jpg   # Command line overrides, debug disabled
```

## Security Notes

- IPC socket permissions are not affected by environment variables
- Path variables are sanitized before use
- Never put sensitive data in `HYPRLAX_DEBUG` output
