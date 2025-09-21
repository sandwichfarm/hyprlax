# Compositor Support Guide

## Overview

hyprlax supports a wide range of compositors and window managers through its modular adapter system. This guide details the features, configuration, and limitations of each supported compositor.

## Quick Compatibility Matrix

| Compositor | Platform | Auto-Detect | Workspace Model | Parallax | Blur | Animations | IPC |
|------------|----------|-------------|-----------------|----------|------|------------|-----|
| **Hyprland** | Wayland | ✅ | Linear | ✅ | ✅ | ✅ | ✅ Full |
| **Sway** | Wayland | ✅ | Linear | ✅ | ❌ | ❌ | ✅ i3-compatible |
| **River** | Wayland | ✅ | Tags | ✅ | ❌ | ❌ | ✅ Control |
| **Wayfire** | Wayland | ✅ | 2D Grid | ✅ H+V | ⚠️ | ✅ | ✅ Custom |
| **Niri** | Wayland | ✅ | Scrollable | ✅ Smooth | ❌ | ✅ | ✅ Custom |
| **Generic Wayland** | Wayland | ✅ | Basic | ✅ | ❌ | ❌ | ❌ |

**Legend:**
- ✅ Full Support
- ⚠️ Requires external compositor (e.g., picom)
- ❌ Not Supported
- H+V: Horizontal + Vertical

## Wayland Compositors

### Hyprland

**Detection:** `HYPRLAND_INSTANCE_SIGNATURE` environment variable

**Features:**
- Full IPC support with event streaming
- Native blur effects
- Smooth animations
- Per-workspace wallpaper offsets
- Dynamic workspace creation

**Configuration:**
```bash
# ~/.config/hypr/hyprland.conf
exec-once = pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg

# With blur effect
exec-once = hyprlax --layer bg.jpg:1.0:1.0:expo:0:1.0:5.0 \
                    --layer fg.png:1.5:0.8
```

**IPC Commands:**
```bash
hyprctl dispatch workspace +1   # Workspace change is handled by Hyprland
hyprlax ctl set blur_size 5     # Set blur amount for hyprlax
```

**Limitations:** None - Full feature support

---

### Sway

**Detection:** `SWAYSOCK` environment variable or `XDG_CURRENT_DESKTOP=sway`

**Features:**
- i3-compatible IPC protocol
- JSON message format
- Workspace change events
- Multi-monitor support

**Configuration:**
```bash
# ~/.config/sway/config
exec_always pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg

# Disable swaybg if using hyprlax
exec_always pkill swaybg
```

**IPC Integration:**
- Uses i3-compatible IPC protocol
- Subscribes to workspace events
- Can query workspace information

**Limitations:**
- No native blur support (Sway doesn't support blur)
- No animation easing (instant workspace switches)

---

### River

**Detection:** `XDG_CURRENT_DESKTOP=river` or control socket presence

**Features:**
- Tag-based workspace system
- Control socket for commands
- Minimalist design philosophy

**Configuration:**
```bash
# River init script
riverctl spawn "pkill hyprlax; hyprlax ~/Pictures/wallpaper.jpg"

# Map keys for workspace switching
riverctl map normal Super 1 set-focused-tags 1
riverctl map normal Super 2 set-focused-tags 2
```

**Tag System:**
- River uses tags instead of workspaces
- Tags are bitmasks (can show multiple tags)
- hyprlax maps first active tag to workspace number

**Limitations:**
- No blur support
- Basic event system
- Tag model may not map perfectly to linear workspaces
 - Caveat — Cursor follow: River does not expose a global cursor position via IPC. hyprlax falls back to the Wayland pointer, so cursor-driven parallax updates when the pointer is over the wallpaper surface. (This is expected; use workspace mode or hybrid on River if you want movement independent of pointer focus.) In debug mode, hyprlax logs a one‑time note about this fallback.

---

### Wayfire

**Detection:** `XDG_CURRENT_DESKTOP=wayfire` or `WAYFIRE_SOCKET`

**Features:**
- 2D workspace grid support
- Horizontal AND vertical parallax
- Plugin-based architecture
- DBus integration

**Configuration:**
```ini
# ~/.config/wayfire.ini
[autostart]
hyprlax = pkill hyprlax; hyprlax --config ~/.config/hyprlax/wayfire.conf

[core]
plugins = ... other plugins ...
```

**2D Workspace Grid:**
```bash
# Configure for 3x3 grid
hyprlax --grid 3x3 ~/Pictures/wallpaper.jpg

# Different shift for X and Y
hyprlax --shift-x 200 --shift-y 150 ~/Pictures/wallpaper.jpg
```

**Limitations:**
- Blur requires wayfire-plugins-extra
- Complex plugin interactions possible

---

### Niri

**Detection:** `XDG_CURRENT_DESKTOP=niri` or Niri socket

**Features:**
- Scrollable workspace model
- Smooth continuous scrolling
- Gesture support
- Modern Rust-based compositor

**Configuration:**
```kdl
// ~/.config/niri/config.kdl
spawn-at-startup "pkill" "hyprlax"
spawn-at-startup "hyprlax" "~/Pictures/wallpaper.jpg"
```

**Scrollable Workspaces:**
- Workspaces scroll continuously
- Parallax follows scroll position
- Smooth sub-pixel positioning

**Limitations:**
- No blur support (not implemented in Niri)
- Newer compositor, potential compatibility issues
 - Caveat — Cursor follow: Niri does not expose a global cursor position via IPC. hyprlax falls back to the Wayland pointer, so cursor-driven parallax updates when the pointer is over the wallpaper surface. Prefer workspace or hybrid mode if you want movement independent of pointer focus. In debug mode, hyprlax logs a one‑time note about this fallback.

---

### Generic Wayland

**Detection:** Fallback when `WAYLAND_DISPLAY` is set

**Features:**
- Basic wlr-layer-shell support
- Works with any wlr-roots based compositor
- Minimal configuration

**Supported Compositors:**
- labwc
- dwl
- japokwm
- Others with wlr-layer-shell

**Configuration:**
```bash
# Force generic Wayland mode
HYPRLAX_COMPOSITOR=generic hyprlax ~/Pictures/wallpaper.jpg
```

**Limitations:**
- No workspace detection
- No IPC support
- Manual workspace switching only
- No blur or advanced features

## Environment Variables

Control hyprlax behavior with environment variables:

```bash
# Force specific compositor
HYPRLAX_COMPOSITOR=sway hyprlax wallpaper.jpg

# Enable debug output
HYPRLAX_DEBUG=1 hyprlax wallpaper.jpg

# Set IPC socket path
HYPRLAX_IPC_SOCKET=/tmp/hyprlax.sock hyprlax wallpaper.jpg
```

## Compositor Detection Order

1. Check `HYPRLAX_COMPOSITOR` environment variable
2. Check compositor-specific environment variables
3. Check for compositor-specific sockets/files
4. Check `XDG_CURRENT_DESKTOP`
5. Check `XDG_SESSION_DESKTOP`
6. Probe for running processes
7. Fall back to generic implementation

## Adding Compositor Support

To add support for a new compositor:

1. **Create Adapter:** `src/compositor/newcomp.c`
2. **Implement Interface:** Implement all functions in `compositor_ops_t`
3. **Add Detection:** Implement `detect()` function
4. **Register:** Add to compositor registry
5. **Document:** Add to this guide

See [Development Guide](development.md#adding-compositor-support) for detailed instructions.

## Troubleshooting

### Compositor Not Detected

```bash
# Check detection
HYPRLAX_DEBUG=1 hyprlax --version 2>&1 | grep "Detected"

# Force compositor
HYPRLAX_COMPOSITOR=generic hyprlax wallpaper.jpg
```

### Features Not Working

Some features require compositor support:
- **Blur:** Compositor must support blur (Hyprland) or use external compositor (picom)
- **Transparency:** Requires compositor with alpha channel support
- **Animations:** Requires smooth workspace switching

### Wayland Specific Issues

For Wayland compositors:
```bash
# Check layer-shell support
WAYLAND_DEBUG=1 hyprlax wallpaper.jpg 2>&1 | grep layer

# Check compositor protocols
wayland-info | grep -E "zwlr_layer_shell|xdg_shell"
```

## Performance Tips

### By Compositor Type

**High Performance (Hardware Accelerated):**
- Hyprland - Full GPU acceleration
- Wayfire - OpenGL renderer
- Niri - GPU accelerated

**Moderate Performance:**
- Sway - Software rendering for some operations
- River - Minimal overhead

**Consider Optimizations:**
- Generic - Reduce layer count and effects

### Optimization Flags

```bash
# Reduce FPS for lower power usage
hyprlax --fps 30 wallpaper.jpg

# Disable vsync for testing
hyprlax --vsync 0 wallpaper.jpg

# Reduce animation duration
hyprlax -d 0.5 wallpaper.jpg
```

## Feature Requests

If your compositor isn't supported or lacks features:
1. Open an issue on [GitHub](https://github.com/sandwichfarm/hyprlax/issues)
2. Provide compositor name and version
3. Include output of `env | grep -E "DESKTOP|SESSION|DISPLAY"`
4. Describe desired features
