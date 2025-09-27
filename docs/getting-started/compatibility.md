# Compositor Compatibility

## Support Matrix

| Compositor | Status | Multi-Monitor | Workspace Tracking | Cursor Parallax | Blur Support | Notes |
|------------|--------|---------------|-------------------|-----------------|--------------|-------|
| **Hyprland** | ✅ Full | ✅ Yes | ✅ Full IPC | ✅ Yes | ✅ Yes | Primary target, all features supported |
| **River** | ✅ Stable | ⚠️ Single tested | ✅ Yes | ✅ Yes | ✅ Yes | Multi-monitor untested |
| **Niri** | ✅ Stable | ⚠️ Single tested | ✅ Yes | ✅ Yes | ✅ Yes | Multi-monitor untested |
| **Sway** | ⚠️ Partial | ✅ Yes | ⚠️ Basic | ✅ Yes | ✅ Yes | i3 IPC compatibility mode |
| **Wayfire** | ❌ Blocked | - | - | - | - | Renderer issues (#40) |
| **KDE Plasma** | 🔄 Planned | - | - | - | - | Future support |
| **GNOME** | 🔄 Planned | - | - | - | - | Future support |

### Legend
- ✅ Full support
- ⚠️ Partial/Limited support  
- ❌ Not working
- 🔄 Planned
- `-` Not applicable

## Platform Requirements

### Wayland
- **Required**: wlr-layer-shell protocol
- **Required**: EGL support
- **Required**: OpenGL ES 2.0
- **Optional**: zwlr-layer-shell-unstable-v1 for better layer control

### Removed Support
- **X11**: Dropped to reduce scope and focus on Wayland

## Feature Compatibility

### Core Features
All supported compositors have:
- Basic parallax wallpaper
- Multi-layer support
- Configuration file support
- IPC runtime control

### Advanced Features

#### Workspace Tracking
- **Full**: Hyprland (via IPC)
- **Protocol-based**: River, Niri, Sway
- Requires compositor to expose workspace information

#### Cursor Parallax
- Works on all supported compositors
- Requires pointer device access
- Can be CPU-intensive on older hardware

#### Blur Effects
- OpenGL ES 2.0 shader-based
- Works on all compositors with EGL support
- Performance varies by GPU

## Auto-Detection

hyprlax automatically detects your compositor using:
1. `HYPRLAND_INSTANCE_SIGNATURE` environment variable
2. `SWAYSOCK` environment variable  
3. `WAYLAND_DISPLAY` content parsing
4. Process detection

### Manual Override

Force a specific compositor:
```bash
hyprlax --compositor hyprland image.jpg
hyprlax --compositor sway image.jpg
hyprlax --compositor generic image.jpg
# Note: River, Niri and Wayfire are auto-detected; manual selection currently
# accepts only: hyprland, sway, generic.
```

## Testing Your Compositor

### Basic Test
```bash
hyprlax --debug --compositor auto ~/Pictures/test.jpg
```

### Feature Test
```bash
# Test workspace switching
hyprlax --debug --shift 200 test.jpg

# Test cursor tracking
hyprlax --config examples/mouse-parallax/hyprlax.toml

# Test blur support
hyprlax --layer test.jpg:1.0:1.0:10.0
```

## Known Issues

### River
- Multi-monitor setups untested
- Workspace events may lag slightly

### Niri  
- Multi-monitor setups untested
- Requires recent Niri version

### Sway
- Limited IPC compared to native Hyprland support
- Some animation features may not work

## Reporting Compatibility

Found hyprlax working (or not) on your compositor? Please report:
1. Compositor name and version
2. What works/doesn't work
3. Debug output: `hyprlax --debug test.jpg 2>&1`
4. Open issue at: https://github.com/sandwichfarm/hyprlax/issues
