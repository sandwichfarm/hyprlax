# Development Guide

Information for building, modifying, and contributing to hyprlax.

## Building from Source

### Prerequisites

Required tools:
- GCC or Clang (C compiler)
- GNU Make
- pkg-config
- Wayland scanner (for Wayland support)

Required libraries:
- Wayland client libraries
- Wayland protocols
- X11 libraries (for X11 support)
- EGL (OpenGL ES context creation)
- OpenGL ES 2.0
- Math library (libm)

### Getting the Source

```bash
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
```

### Build Commands

```bash
# Standard build (auto-detects platform)
make

# Debug build with symbols
make debug

# Clean build
make clean && make

# Build for specific architecture
make ARCH=aarch64

# Build without X11 support
make NO_X11=1

# Verbose build output
make VERBOSE=1
```

### Build Options

The Makefile supports various options:

```bash
# Custom compiler
make CC=clang

# Custom optimization
make CFLAGS="-O2 -march=native"

# Custom installation prefix
make PREFIX=/opt/hyprlax install

# Parallel build
make -j$(nproc)
```

## Project Structure

```
hyprlax/
├── src/
│   ├── main.c                    # Entry point
│   ├── hyprlax_main.c            # Main application logic
│   ├── ipc.c                     # IPC server implementation
│   ├── hyprlax-ctl.c            # IPC client tool
│   │
│   ├── core/                    # Core functionality
│   │   ├── animation.c          # Animation system
│   │   ├── config.c             # Configuration parser
│   │   ├── easing.c             # Easing functions
│   │   └── layer.c              # Layer management
│   │
│   ├── platform/                # Platform abstraction
│   │   ├── platform.c           # Platform interface
│   │   ├── wayland.c            # Wayland implementation
│   │   └── x11.c                # X11 implementation
│   │
│   ├── compositor/              # Compositor adapters
│   │   ├── compositor.c         # Compositor interface
│   │   ├── hyprland.c          # Hyprland adapter
│   │   ├── sway.c              # Sway adapter
│   │   ├── river.c             # River adapter
│   │   ├── wayfire.c           # Wayfire adapter
│   │   ├── niri.c              # Niri adapter
│   │   ├── x11_ewmh.c          # X11 EWMH adapter
│   │   └── generic_wayland.c   # Generic Wayland adapter
│   │
│   ├── renderer/                # Rendering backends
│   │   ├── renderer.c           # Renderer interface
│   │   ├── gles2.c             # OpenGL ES 2.0 renderer
│   │   └── shader.c            # Shader compilation
│   │
│   └── include/                 # Header files
│       ├── hyprlax_internal.h   # Internal definitions
│       ├── compositor.h         # Compositor interface
│       ├── platform.h           # Platform interface
│       └── renderer.h           # Renderer interface
│
├── protocols/                    # Wayland protocol files
│   ├── wlr-layer-shell-unstable-v1.xml
│   └── (generated files)
│
├── tests/                        # Test suites
│   ├── test_animation.c
│   ├── test_config.c
│   ├── test_blur.c
│   └── ...
│
├── docs/                         # Documentation
├── examples/                     # Example configurations
├── scripts/                      # Build and utility scripts
├── Makefile                      # Build configuration
├── install.sh                    # Installation script
└── README.md                     # Project documentation
```

## Architecture Overview

See [Architecture Documentation](architecture.md) for detailed system design.

### Key Components

1. **Platform Layer** - Abstracts Wayland/X11 differences
2. **Compositor Adapters** - Handle compositor-specific features
3. **Renderer** - OpenGL ES 2.0 rendering engine
4. **Core Engine** - Animation, configuration, and layer management
5. **IPC Server** - Runtime control interface

### Module Interfaces

Each module implements a well-defined interface:

```c
// Platform interface example
typedef struct platform_ops {
    int (*init)(void);
    void (*destroy)(void);
    int (*create_window)(window_config_t *config);
    int (*poll_events)(platform_event_t *event);
    // ... more operations
} platform_ops_t;

// Compositor interface example
typedef struct compositor_ops {
    int (*init)(void *platform_data);
    bool (*detect)(void);
    int (*get_current_workspace)(void);
    int (*poll_events)(compositor_event_t *event);
    // ... more operations
} compositor_ops_t;
```

## Adding Features

### Adding a New Easing Function

1. **Add enum value** in `src/core/easing.c`:
```c
typedef enum {
    // ... existing easings ...
    EASE_BOUNCE_OUT,  // New easing
} easing_type_t;
```

2. **Implement function** in `easing_apply()`:
```c
case EASE_BOUNCE_OUT: {
    if (t < 0.363636) {
        return 7.5625f * t * t;
    } else if (t < 0.727272) {
        t -= 0.545454f;
        return 7.5625f * t * t + 0.75f;
    }
    // ... more bounce logic
}
```

3. **Add string parsing** in `easing_from_string()`:
```c
else if (strcmp(str, "bounce") == 0) return EASE_BOUNCE_OUT;
```

### Adding Compositor Support

1. **Create adapter file** `src/compositor/newcomp.c`:
```c
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

static bool newcomp_detect(void) {
    // Check if running under this compositor
    return getenv("NEWCOMP_SOCKET") != NULL;
}

static int newcomp_init(void *platform_data) {
    // Initialize compositor adapter
    return HYPRLAX_SUCCESS;
}

// ... implement all ops ...

const compositor_ops_t compositor_newcomp_ops = {
    .detect = newcomp_detect,
    .init = newcomp_init,
    // ... all operations
};
```

2. **Add to registry** in `src/compositor/compositor.c`:
```c
// In compositor_detect()
if (compositor_newcomp_ops.detect && compositor_newcomp_ops.detect()) {
    return COMPOSITOR_NEWCOMP;
}

// In compositor_create()
case COMPOSITOR_NEWCOMP:
    adapter->ops = &compositor_newcomp_ops;
    break;
```

3. **Add type** to `src/include/compositor.h`:
```c
typedef enum {
    // ... existing types ...
    COMPOSITOR_NEWCOMP,
} compositor_type_t;

extern const compositor_ops_t compositor_newcomp_ops;
```

4. **Update Makefile**:
```makefile
COMPOSITOR_SRCS = ... src/compositor/newcomp.c
```

### Adding a New Platform

Similar process for platform modules in `src/platform/`.

### Adding a New Renderer

1. Create `src/renderer/newrenderer.c`
2. Implement `renderer_ops_t` interface
3. Add to renderer selection logic

## Debugging

### Debug Build

```bash
# Build with debug symbols and no optimization
make debug

# Run with gdb
gdb ./hyprlax
(gdb) run ~/Pictures/wallpaper.jpg
(gdb) bt  # Backtrace on crash
```

### Debug Output

```bash
# Enable debug messages
HYPRLAX_DEBUG=1 hyprlax wallpaper.jpg

# Debug specific modules
HYPRLAX_DEBUG_COMPOSITOR=1 hyprlax wallpaper.jpg
HYPRLAX_DEBUG_RENDERER=1 hyprlax wallpaper.jpg
HYPRLAX_DEBUG_ANIMATION=1 hyprlax wallpaper.jpg
```

### Common Debug Points

```c
// Add debug output
DEBUG_LOG("Animation: offset=%.2f target=%.2f progress=%.2f",
          current_offset, target_offset, progress);

// Check OpenGL errors
GLenum err = glGetError();
if (err != GL_NO_ERROR) {
    ERROR_LOG("OpenGL error: 0x%x", err);
}

// Platform events
DEBUG_LOG("Event: type=%d workspace=%d", event.type, event.workspace);
```

### Memory Debugging

```bash
# Valgrind memory check
valgrind --leak-check=full --show-leak-kinds=all \
         --track-origins=yes ./hyprlax wallpaper.jpg

# AddressSanitizer (compile-time)
make CFLAGS="-fsanitize=address -g" clean all
./hyprlax wallpaper.jpg
```

## Testing

### Unit Tests

```bash
# Build and run all tests
make test

# Run specific test suite
./tests/test_animation
./tests/test_config
./tests/test_compositor
./tests/test_x11_platform

# Run with memory checking
make memcheck

# Coverage report
make coverage
```

### Integration Testing

```bash
# Test compositor detection
./scripts/test-compositors.sh

# Test multi-layer rendering
./scripts/test-layers.sh

# Performance benchmarks
./scripts/benchmark.sh
```

### Manual Testing Checklist

- [ ] Single layer wallpaper
- [ ] Multi-layer with different opacities
- [ ] All easing functions
- [ ] Workspace switching in each compositor
- [ ] X11 window manager integration (i3, bspwm, awesome, etc.)
- [ ] Wayland and X11 platform detection
- [ ] Blur effects (where supported)
- [ ] Config file loading
- [ ] IPC commands via hyprlax-ctl
- [ ] Memory usage over time
- [ ] CPU usage during animations

## Code Style

### Formatting Rules

- **Indentation:** 4 spaces (no tabs)
- **Line length:** Max 100 characters
- **Braces:** K&R style
- **Naming:**
  - Functions: `snake_case`
  - Types: `snake_case_t`
  - Constants: `UPPER_SNAKE_CASE`
  - Struct members: `snake_case`

### Example:

```c
typedef struct {
    int current_workspace;
    float animation_progress;
} compositor_state_t;

static int compositor_update_workspace(compositor_state_t *state, 
                                       int new_workspace) {
    if (!state) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    if (state->current_workspace != new_workspace) {
        DEBUG_LOG("Workspace change: %d -> %d", 
                  state->current_workspace, new_workspace);
        state->current_workspace = new_workspace;
    }
    
    return HYPRLAX_SUCCESS;
}
```

### Comments

```c
/* 
 * Multi-line comment for file headers
 * and function documentation
 */

// Single-line comments for inline explanations

/* TODO: Add feature X */
/* FIXME: Handle edge case Y */
/* NOTE: Important information */
```

## Contributing

### Pull Request Process

1. **Fork** the repository
2. **Create branch**: `git checkout -b feature/your-feature`
3. **Make changes** following code style
4. **Add tests** for new functionality
5. **Run tests**: `make test`
6. **Commit** with clear messages
7. **Push** to your fork
8. **Open PR** with description

### Commit Message Format

Follow conventional commits:

```
type(scope): description

[optional body]

[optional footer]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation
- `perf`: Performance improvement
- `refactor`: Code restructuring
- `test`: Test additions/changes
- `chore`: Build/tool changes

Examples:
```
feat(compositor): add River compositor support
fix(animation): resolve memory leak in layer cleanup
docs(readme): update installation instructions
perf(renderer): optimize texture loading
```

### Testing Requirements

- All new features must have tests
- All bug fixes should include regression tests
- Maintain >80% code coverage
- Pass CI checks

### Documentation Requirements

- Update relevant .md files
- Add inline code comments
- Update man pages if needed
- Include examples

## Release Process

### Version Numbering

Semantic versioning: `MAJOR.MINOR.PATCH`

- **MAJOR**: Breaking changes
- **MINOR**: New features, backward compatible
- **PATCH**: Bug fixes

### Release Steps

1. **Update version** in `src/hyprlax_main.c`:
```c
#define HYPRLAX_VERSION "1.3.0"
```

2. **Update CHANGELOG.md** with release notes

3. **Create tag**:
```bash
git tag -a v1.3.0 -m "Release version 1.3.0"
git push origin v1.3.0
```

4. **GitHub Actions** automatically:
   - Builds binaries for multiple architectures
   - Runs test suite
   - Creates GitHub release
   - Uploads artifacts

## Performance Optimization

### Profiling

```bash
# CPU profiling with perf
perf record ./hyprlax wallpaper.jpg
perf report

# GPU profiling with apitrace
apitrace trace ./hyprlax wallpaper.jpg
qapitrace hyprlax.trace

# Frame timing analysis
HYPRLAX_DEBUG_TIMING=1 hyprlax wallpaper.jpg
```

### Optimization Areas

1. **Texture Loading**
   - Use texture atlases
   - Implement mipmapping
   - Cache decoded images

2. **Rendering**
   - Minimize state changes
   - Batch draw calls
   - Use VBOs effectively

3. **Animation**
   - Precalculate easing curves
   - Skip redundant frames
   - Adaptive frame rate

## Resources

### Documentation
- [Wayland Protocol](https://wayland.freedesktop.org/docs/html/)
- [X11/EWMH Specification](https://specifications.freedesktop.org/wm-spec/latest/)
- [OpenGL ES 2.0 Reference](https://www.khronos.org/opengles/sdk/docs/man/)
- [Layer Shell Protocol](https://github.com/swaywm/wlr-protocols)

### Tools
- [Wayland Debug](https://github.com/wmww/wayland-debug) - Protocol debugging
- [RenderDoc](https://renderdoc.org/) - Graphics debugging
- [WAYLAND_DEBUG=1](https://wayland.freedesktop.org/docs/html/ch04.html) - Built-in debugging

### Similar Projects
- [swww](https://github.com/Horus645/swww) - Wayland wallpaper daemon
- [hyprpaper](https://github.com/hyprwm/hyprpaper) - Hyprland wallpaper utility
- [swaybg](https://github.com/swaywm/swaybg) - Sway background tool
- [wpaperd](https://github.com/danyspin97/wpaperd) - Wallpaper daemon

## Troubleshooting Development Issues

### Build Errors

```bash
# Missing dependencies
pkg-config --list-all | grep -E "wayland|egl|gles"

# Protocol generation issues
make clean-protocols
make protocols

# Linker errors
ldd hyprlax | grep "not found"
```

### Runtime Issues

```bash
# Compositor detection failing
HYPRLAX_DEBUG=1 hyprlax --version

# Renderer initialization
HYPRLAX_DEBUG_RENDERER=1 hyprlax test.jpg

# Check capabilities
hyprlax --capabilities
```

## Contact

- GitHub Issues: https://github.com/sandwichfarm/hyprlax/issues
- Pull Requests: https://github.com/sandwichfarm/hyprlax/pulls
- Discussions: https://github.com/sandwichfarm/hyprlax/discussions