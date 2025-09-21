# Troubleshooting Guide

Solutions to common issues with hyprlax.

## Installation Issues

### Build Errors

#### Missing Dependencies
```
fatal error: wayland-client.h: No such file or directory
```

**Solution:** Install Wayland development packages:
```bash
# Arch
sudo pacman -S wayland wayland-protocols

# Ubuntu/Debian
sudo apt install libwayland-dev wayland-protocols

# Fedora
sudo dnf install wayland-devel wayland-protocols-devel
```

#### OpenGL/EGL Errors
```
fatal error: EGL/egl.h: No such file or directory
```

**Solution:** Install Mesa development packages:
```bash
# Arch
sudo pacman -S mesa

# Ubuntu/Debian
sudo apt install libegl1-mesa-dev libgles2-mesa-dev

# Fedora
sudo dnf install mesa-libEGL-devel mesa-libGLES-devel
```

#### Wayland Scanner Not Found
```
make: wayland-scanner: Command not found
```

**Solution:** Install wayland-scanner:
```bash
# Arch
sudo pacman -S wayland

# Ubuntu/Debian
sudo apt install wayland-scanner

# Fedora
sudo dnf install wayland-devel
```

### Installation Path Issues

#### Command Not Found After Install
```bash
hyprlax: command not found
```

**Solution:** Add installation directory to PATH:
```bash
# If installed to ~/.local/bin
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify
which hyprlax
```

## Runtime Issues

### Black Screen

#### Issue: Wallpaper doesn't appear, screen is black

**Common Causes & Solutions:**

1. **Wrong image path**
   ```bash
   # Use absolute path
   hyprlax /home/username/Pictures/wallpaper.jpg
   
   # Not relative path
   # hyprlax ../wallpaper.jpg  # May not work
   ```

2. **Image format not supported**
   - Supported: JPEG, PNG
   - Not supported: WebP, AVIF, SVG
   - Convert unsupported formats:
   ```bash
   convert image.webp image.jpg
   ```

3. **Another wallpaper daemon running**
   ```bash
   # Kill other daemons
   pkill swww-daemon
   pkill hyprpaper
   pkill swaybg
   
   # Then start hyprlax
   hyprlax wallpaper.jpg
   ```

4. **Permissions issue**
   ```bash
   # Check file permissions
   ls -la /path/to/wallpaper.jpg
   
   # Fix if needed
   chmod 644 /path/to/wallpaper.jpg
   ```

### Animation Issues

#### Stuttering or Lag

**Solutions:**

1. **Reduce frame rate**
   ```bash
   hyprlax --fps 30 wallpaper.jpg
   ```

2. **Disable vsync**
   ```bash
   hyprlax --vsync 0 wallpaper.jpg
   ```

3. **Use simpler easing**
   ```bash
   hyprlax -e linear wallpaper.jpg
   ```

4. **Reduce layer count (multi-layer mode)**
   ```bash
   # Instead of 5 layers, use 3
   hyprlax --layer bg.jpg:0.3:1.0 \
           --layer fg.png:1.0:0.8
   ```

#### Animation Not Working

**Check if workspace changes are detected:**

1. **Verify Hyprland IPC**
   ```bash
   # Check if socket exists
   ls -la /tmp/hypr/
   
   # Should show .socket2
   ```

2. **Test workspace switching**
   ```bash
   # Run with debug mode
   hyprlax --debug wallpaper.jpg
   
   # Switch workspaces and check output
   ```

3. **Check permissions**
   ```bash
   # Ensure socket is readable
   ls -la /tmp/hypr/.socket2
   ```

### Multi-Layer Issues

#### Layers Not Visible

1. **Check image paths**
   ```bash
   # All paths must be absolute
   hyprlax --layer /home/user/layer1.png:0.3:1.0 \
           --layer /home/user/layer2.png:1.0:0.8
   ```

2. **Verify PNG transparency**
   ```bash
   # Check if PNG has alpha channel
   identify -verbose image.png | grep "Channel"
   ```

3. **Check layer order**
   - First layer = background (bottommost)
   - Last layer = foreground (topmost)

#### Performance Issues with Blur

1. **Reduce blur amounts**
   ```bash
   # Instead of blur:5.0, use blur:2.0
   --layer bg.jpg:0.3:1.0:expo:0:1.0:2.0
   ```

2. **Pre-blur backgrounds**
   ```bash
   # Blur image beforehand
   convert input.jpg -blur 0x8 output.jpg
   
   # Then use without runtime blur
   --layer output.jpg:0.3:1.0
   ```

3. **Limit blur to fewer layers**
   - Only blur 1-2 background layers
   - Keep foreground layers sharp

## Graphics Issues

### GPU/Driver Problems

#### OpenGL Errors
```
Failed to create EGL context
```

**Solutions:**

1. **Check GPU drivers**
   ```bash
   # Check renderer
   glxinfo | grep "OpenGL renderer"
   
   # For NVIDIA
   nvidia-smi
   
   # For AMD
   glxinfo | grep AMD
   ```

2. **Try software rendering**
   ```bash
   LIBGL_ALWAYS_SOFTWARE=1 hyprlax wallpaper.jpg
   ```

3. **Update drivers**
   ```bash
   # Arch - NVIDIA
   sudo pacman -S nvidia nvidia-utils
   
   # Arch - AMD
   sudo pacman -S mesa xf86-video-amdgpu
   ```

### Wayland Issues

#### Layer Shell Not Supported
```
Error: wlr-layer-shell not supported
```

**Solution:** Hyprlax requires a compositor with layer-shell support. Compatible compositors:
- Hyprland (recommended)
- Sway
- River
- Wayfire

Not compatible:
- GNOME Wayland
- KDE Wayland (without layer-shell)

## Configuration Issues

### Config File Not Loading

1. **Check file path**
   ```bash
   # Verify file exists
   ls -la ~/.config/hyprlax/parallax.conf
   ```

2. **Validate syntax**
   ```bash
   # Check for syntax errors
   cat ~/.config/hyprlax/parallax.conf
   
   # Common issues:
   # - Missing image paths
   # - Invalid numbers
   # - Wrong parameter count
   ```

3. **Debug mode**
   ```bash
   hyprlax --debug --config ~/.config/hyprlax/parallax.conf
   ```

### Invalid Parameters

#### Command Line
```
Error: Invalid layer specification
```

**Fix format:**
```bash
# Correct: image:shift:opacity
--layer /path/to/image.jpg:0.5:1.0

# Wrong: missing parameters
--layer image.jpg:0.5  # Missing opacity
```

## Common Error Messages

### "Failed to connect to Wayland display"

**Causes:**
- Not running under Wayland
- WAYLAND_DISPLAY not set

**Solutions:**
```bash
# Check if running Wayland
echo $WAYLAND_DISPLAY

# Should output: wayland-0 or wayland-1

# If empty, not running Wayland
```

### "Failed to connect to Hyprland IPC"

**Causes:**
- Hyprland not running
- Socket permissions issue

**Solutions:**
```bash
# Check if Hyprland is running
pgrep Hyprland

# Check socket
ls -la /tmp/hypr/.socket2

# Restart Hyprland if needed
```

### "Image too large"

**Cause:** Image exceeds GPU texture limits

**Solution:**
```bash
# Resize image
convert large.jpg -resize 3840x2160 resized.jpg

# Use resized version
hyprlax resized.jpg
```

## Getting Help

### Debug Information

Collect this information when reporting issues:

```bash
# Hyprlax version
hyprlax --version

# System info
uname -a
lsb_release -a

# GPU info
glxinfo | grep -E "OpenGL renderer|OpenGL version"

# Wayland info
echo $WAYLAND_DISPLAY
echo $XDG_SESSION_TYPE

# Debug run
hyprlax --debug wallpaper.jpg 2>&1 | tee hyprlax_debug.log
```

### Reporting Issues

Report issues at: https://github.com/sandwichfarm/hyprlax/issues

Include:
1. Debug output
2. System information
3. Steps to reproduce
4. Expected vs actual behavior
5. Configuration used

## Next Steps

- Review [configuration guide](configuration.md) for setup help
- See [examples](examples.md) for working configurations
- Check [multi-layer guide](multi-layer.md) for advanced features