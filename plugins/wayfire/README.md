# Hyprlax Wayfire Plugin

Native Wayfire plugin that provides proper workspace persistence for hyprlax wallpapers.

## Problem

Wayfire's vswitch plugin includes all layer-shell surfaces (including wallpapers) in workspace switching animations, causing the background to move with the workspace instead of remaining static.

## Solution

This plugin creates native Wayfire views that:
- Persist across workspace switches
- Handle workspace grid changes
- Communicate with hyprlax via IPC for frame updates

## Building

### Prerequisites

- Wayfire development headers (`wayfire-dev` or similar package)
- wlroots development headers (`wlroots-dev` or `wlroots0.19-dev`)
- GLM headers (`glm-dev` or `libglm-dev`)
- Same compiler used to build Wayfire (usually g++ or clang++)

Install dependencies on common distributions:
```bash
# Arch Linux
sudo pacman -S wayfire wlroots glm

# Ubuntu/Debian
sudo apt install libwayfire-dev libwlroots-dev libglm-dev

# Fedora
sudo dnf install wayfire-devel wlroots-devel glm-devel
```

### Build Steps

```bash
make
```

To check if dependencies are installed:
```bash
make check-deps
```

## Installation

### System-wide installation:
```bash
sudo make install
```

### User installation:
```bash
make install PREFIX=$HOME/.local PLUGIN_DIR=$HOME/.local/lib/wayfire
```

## Configuration

1. Add the plugin to your `wayfire.ini`:
```ini
[core]
plugins = ... hyprlax-wayfire-plugin ...
```

2. Start hyprlax with Wayfire plugin support:
```bash
hyprlax --wayfire-plugin
```

## How it Works

1. **Plugin loads** in Wayfire at startup
2. **Creates Unix socket** at `$XDG_RUNTIME_DIR/hyprlax-wayfire.sock`
3. **Hyprlax connects** to the socket when `--wayfire-plugin` is specified
4. **Hyprlax sends** rendered frames via shared memory file descriptors
5. **Plugin displays** frames as native Wayfire views that persist across workspaces

## Troubleshooting

### Plugin not loading
- Check Wayfire logs: `wayfire -d 2>&1 | grep hyprlax`
- Verify plugin is in correct directory: `ls $(pkg-config --variable=plugindir wayfire)`

### Hyprlax not connecting
- Check socket exists: `ls $XDG_RUNTIME_DIR/hyprlax-wayfire.sock`
- Ensure hyprlax is started with `--wayfire-plugin` flag

### Performance issues
- The plugin uses shared memory for zero-copy frame transfer
- Ensure both hyprlax and Wayfire have GPU acceleration enabled

## Development

To enable debug logging:
```bash
WAYFIRE_DEBUG=hyprlax wayfire
```

## License

This plugin follows the main hyprlax project license.