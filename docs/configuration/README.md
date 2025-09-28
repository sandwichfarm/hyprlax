# Configuration Overview

hyprlax supports two configuration formats:

1. **TOML** (recommended) - Modern, feature-rich configuration
2. **Legacy** (.conf) - Simple text-based format

## Configuration Methods

### 1. Command Line
Quick configuration without files:
```bash
hyprlax --shift 200 --duration 1.5 --fps 60 image.jpg
```

### 2. TOML Configuration (Recommended)
Advanced features with structured configuration:
```bash
hyprlax --config ~/.config/hyprlax/hyprlax.toml
```

See [TOML Reference](toml-reference.md) for complete documentation.

### 3. Legacy Configuration
Simple text-based format:
```bash
hyprlax --config ~/.config/hyprlax/parallax.conf
```

See [Legacy Format](legacy-format.md) for syntax.

## Quick Comparison

| Feature | Command Line | Legacy Config | TOML Config |
|---------|-------------|---------------|-------------|
| Basic settings | ✅ | ✅ | ✅ |
| Multi-layer | ✅ (--layer) | ✅ | ✅ |
| Per-layer controls | Limited | Limited | ✅ Full |
| Cursor parallax | ✅ | ❌ | ✅ Full |
| Content fitting | ❌ | ❌ | ✅ |
| Advanced easing | ✅ (global) | ❌ | ✅ |
| Parallax modes | ✅ | ❌ | ✅ |

## Configuration Priority

Settings are applied in this order (later overrides earlier):
1. Built-in defaults
2. Configuration file (TOML)
3. Command-line arguments
4. Runtime IPC commands

## File Locations

hyprlax searches for configs in:
1. Path specified with `--config`
2. `$XDG_CONFIG_HOME/hyprlax/` (usually `~/.config/hyprlax/`)
3. Current directory

## Migrating to TOML

Moving from legacy format? See [Migration Guide](migration-guide.md).

## Examples

Ready-to-use configurations are available in [examples/](examples/):
- [Basic parallax](examples/basic.toml)
- [Cursor tracking](examples/cursor-parallax.toml)  
- [Multi-layer depth](examples/multi-layer.toml)

## Getting Started

1. **New users**: Start with [TOML Reference](toml-reference.md)
2. **Legacy users**: See [Migration Guide](migration-guide.md)
3. **Quick setup**: Copy an [example](examples/) and customize

## Validation

Test your configuration:
```bash
hyprlax --config your-config.toml --debug
```

This will report any configuration errors before starting.
