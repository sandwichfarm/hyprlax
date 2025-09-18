# AGENTS.md

Instructions for AI coding agents working on hyprlax.

## Project Overview

Hyprlax is a smooth parallax wallpaper animation system for Hyprland (Wayland compositor). It creates depth effects by moving multiple image layers at different speeds when switching workspaces.

### Core Technologies
- **Language**: C (C99 standard)
- **Graphics**: OpenGL ES 2.0 with EGL
- **Windowing**: Wayland (layer-shell protocol)
- **Build System**: GNU Make
- **Image Loading**: stb_image (header-only library)

### Architecture
- Modular architecture with separated concerns:
  - **Core**: Animation engine, configuration, layer management
  - **Platform**: Wayland abstraction layer
  - **Compositor**: Adapter system for different compositors
  - **Renderer**: OpenGL ES 2.0 rendering with custom shaders
- Runtime IPC system for dynamic control
- Multi-compositor support (Hyprland, Sway, Wayfire, Niri, River, etc.)
- Integrated control interface via `hyprlax ctl` subcommand
- Comprehensive test suite with memory leak detection

## Development Environment

### Required Tools
```bash
# Install dependencies (Arch Linux)
sudo pacman -S base-devel wayland wayland-protocols mesa

# Clone and build
git clone https://github.com/sandwichfarm/hyprlax.git
cd hyprlax
make
```

### Build Commands
```bash
make            # Standard optimized build
make debug      # Debug build with symbols
make clean      # Clean build artifacts
make install    # Install to /usr/local/bin
make test       # Run comprehensive test suite
make memcheck   # Memory leak testing with Valgrind
make coverage   # Test coverage analysis
```

### Testing
```bash
# Test single layer
./hyprlax test.jpg

# Test multi-layer with debug output
./hyprlax --debug --layer bg.jpg:0.3:1.0:expo:0:1.0:3.0 \
                  --layer fg.png:1.0:0.8

# Runtime control (integrated control interface)
./hyprlax ctl status                    # Check daemon status
./hyprlax ctl add image.jpg 1.5 0.8     # Add layer dynamically
./hyprlax ctl set fps 144               # Change frame rate
./hyprlax ctl set duration 2.0          # Modify animation duration
./hyprlax ctl get fps                   # Query current settings
./hyprlax ctl list                      # List active layers

# Run test suite
make test

# Check for memory leaks
make memcheck
valgrind --leak-check=full ./hyprlax test.jpg
```

## Code Style Guidelines

### C Code Conventions
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Max 100 characters
- **Braces**: K&R style
- **Naming**:
  - Functions: `snake_case`
  - Variables: `snake_case`
  - Constants: `UPPER_SNAKE_CASE`
  - Structs: `snake_case`
  - Enums: `snake_case_t`

### Code Structure
```c
// Good function example
int load_layer(struct layer *layer, const char *path, 
               float shift_multiplier, float opacity) {
    // Input validation first
    if (!layer || !path) {
        return -1;
    }
    
    // Core logic
    // ...
    
    // Resource cleanup
    // ...
    
    return 0;
}
```

### Error Handling
- Return -1 for errors, 0 for success
- Always check malloc/calloc returns
- Free resources in reverse order of allocation
- Use early returns for error conditions

### Comments
- Use `//` for single-line comments
- Document complex algorithms
- Add TODO comments for future work
- No commented-out code in commits

## Contribution Guidelines

### DO's
- ✅ Maintain backward compatibility
- ✅ Add debug output for new features (behind `config.debug`)
- ✅ Update documentation when adding features
- ✅ Test with both single and multi-layer modes
- ✅ Check for memory leaks with valgrind
- ✅ Follow existing code patterns
- ✅ Add examples for new features
- ✅ Keep performance in mind (144 FPS target)

### DON'Ts
- ❌ Break existing command-line interface
- ❌ Add dependencies without discussion
- ❌ Use C++ features (keep it pure C)
- ❌ Ignore compiler warnings
- ❌ Add blocking operations in render loop
- ❌ Use global variables unnecessarily
- ❌ Mix tabs and spaces
- ❌ Leave debug prints in production code

## File Organization

```
hyprlax/
├── src/
│   ├── core/               # Core functionality
│   │   ├── animation.c     # Animation engine
│   │   ├── config.c        # Configuration management
│   │   ├── easing.c        # Easing functions
│   │   └── layer.c         # Layer management
│   ├── platform/           # Platform abstraction
│   │   ├── platform.c      # Platform detection/initialization
│   │   ├── wayland.c       # Wayland implementation
│   ├── compositor/         # Compositor adapters
│   │   ├── compositor.c    # Compositor detection/management
│   │   ├── hyprland.c      # Hyprland IPC integration
│   │   ├── sway.c          # Sway/i3 IPC integration
│   │   ├── wayfire.c       # Wayfire (2D workspaces)
│   │   ├── niri.c          # Niri (scrollable workspaces)
│   │   ├── river.c         # River (tag-based workspaces)
│   │   ├── generic_wayland.c # Generic Wayland fallback
│   ├── renderer/           # Rendering system
│   │   ├── renderer.c      # Renderer interface
│   │   ├── shader.c        # Shader management
│   │   └── gles2.c         # OpenGL ES 2.0 implementation
│   ├── include/            # Header files
│   │   ├── core.h          # Core module definitions
│   │   ├── platform.h      # Platform abstractions
│   │   ├── compositor.h    # Compositor interfaces
│   │   ├── renderer.h      # Renderer interfaces
│   │   └── shader.h        # Shader definitions
│   ├── ipc.c               # IPC system for runtime control
│   ├── ipc.h               # IPC system headers
│   ├── main.c              # Entry point and argument parsing
│   ├── hyprlax_main.c      # Main daemon logic
│   ├── hyprlax_ctl.c       # Integrated control interface
│   └── stb_image.h         # Image loading library
├── protocols/              # Wayland protocol files
├── docs/                   # User documentation
├── examples/               # Example configurations
├── tests/                  # Comprehensive test suite
│   ├── test_animation.c    # Animation system tests
│   ├── test_compositor.c   # Compositor detection tests
│   ├── test_ipc.c          # IPC system tests
│   ├── test_integration.c  # Integration tests
│   └── test_*.c           # Additional test modules
├── scripts/                # Build and utility scripts
├── Makefile               # Build configuration
└── AGENTS.md              # This file
```

### Adding Features

When adding new features:
1. **Choose appropriate module**: Core, Platform, Compositor, or Renderer
2. **Update relevant structures**: `struct layer`, `struct config`, etc.
3. **Add command-line parsing** in `main.c`
4. **Implement logic** in the appropriate module
5. **Update IPC system** if runtime control is needed (`ipc.c`)
6. **Add compositor support** if workspace-related (`src/compositor/`)
7. **Update shaders** if rendering changes are needed (`src/renderer/shader.c`)
8. **Add tests** to the test suite (`tests/test_*.c`)
9. **Add debug output** behind appropriate flags
10. **Update documentation** and examples

### New Architecture Features

#### Runtime IPC System
- **Location**: `src/ipc.c`, `src/ipc.h`
- **Purpose**: Dynamic control of running daemon
- **Commands**: ADD, REMOVE, MODIFY, LIST, CLEAR, STATUS, SET_PROPERTY, GET_PROPERTY
- **Access**: Via `hyprlax ctl` integrated subcommand

#### Modular Compositor Support
- **Location**: `src/compositor/`
- **Supported Compositors**:
  - **Hyprland**: Full IPC integration with workspace events
  - **Sway**: i3-compatible IPC protocol
  - **Wayfire**: 2D workspace grid support
  - **River**: Tag-based workspace system
  - **Niri**: Scrollable workspace model
  - **Generic Wayland**: Fallback for unsupported compositors

#### Platform Abstraction
- **Location**: `src/platform/`
- **Wayland**: Layer-shell protocol implementation
- **Auto-detection**: Runtime platform selection

## Commit Message Format

Use conventional commits:
```
type: description

[optional body]

[optional footer]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `perf`: Performance improvement
- `docs`: Documentation only
- `refactor`: Code restructuring
- `test`: Adding tests
- `chore`: Maintenance

Examples:
```
feat: add per-layer blur effects for depth perception
fix: resolve memory leak in texture cleanup
docs: update multi-layer guide with blur examples
perf: optimize shader for better frame rates
```

## Testing Checklist

Before submitting changes:
- [ ] Code compiles without warnings
- [ ] Single-layer mode works
- [ ] Multi-layer mode works
- [ ] All tests pass (`make test`)
- [ ] No memory leaks (`make memcheck`)
- [ ] Animation is smooth (144 FPS target)
- [ ] IPC system works (`hyprlax ctl` commands)
- [ ] Compositor detection works correctly
- [ ] Debug mode provides useful output
- [ ] Documentation updated
- [ ] Examples work

## Performance Considerations

### Critical Path
The render loop must maintain 144 FPS:
- `render_frame()` - Keep under 7ms (144 FPS = ~6.9ms per frame)
- Texture operations - Use power-of-2 sizes
- Shader complexity - Minimize calculations
- Layer count - Test with 5+ layers
- IPC processing - Non-blocking for runtime control

### Memory Management
- Free textures when switching wallpapers
- Clean up layers on exit
- Avoid allocations in render loop
- Use static buffers where possible

## Security Considerations

- Validate all file paths before loading
- Check image dimensions against GPU limits
- Sanitize config file inputs
- Never execute shell commands from input
- Handle IPC errors gracefully

## Common Tasks

### Addressing Pull Request Review Comments

When asked to address review comments for a specific PR:

**Prerequisites**: Must be given a specific PR number (e.g., "address review comments on PR #42")

**Procedure**:
1. **Use `gh` CLI tool** for all GitHub interactions
2. **Check review comments in DESC order** (newest first):
   ```bash
   gh pr view [PR_NUMBER] --comments
   ```
3. **Identify UNRESOLVED inline review comments**:
   ```bash
   gh api repos/{owner}/{repo}/pulls/[PR_NUMBER]/comments
   ```
4. **Critical Rule**: If you encounter review comments that appear already addressed:
   - **DO NOT** assume the task is complete
   - **CONTINUE** searching for unresolved inline review comments
   - Many resolved comments may exist alongside new unresolved ones
5. **🚨 VALIDATE BEFORE IMPLEMENTING**: For each unresolved comment:
   - **Analyze the suggested change carefully**
   - **Verify the correctness** of the review comment
   - **Consider edge cases** and potential side effects
   - **Do NOT blindly implement** suggestions without validation
   - **Question assertions**: Are they actually invalid or just flagged?
   - **Understand the context**: Does the change make sense in the codebase?
6. **Address each validated comment** systematically
7. **Verify completion** by checking that all inline review comments are resolved

**Critical Validation Examples**:
- ❌ **Bad**: Removing `ck_assert(state.previous != state.current || i == 0)` without checking test data
- ✅ **Good**: Analyzing test data shows no consecutive identical values, so assertion is valid
- ❌ **Bad**: Accepting bounds check "fix" without understanding the existing validation
- ✅ **Good**: Confirming bounds are already checked before applying memcpy improvement

**Common Pitfalls to Avoid**:
- ❌ Stopping after finding addressed comments
- ❌ Assuming old resolved comments mean task is done
- ❌ Missing inline review comments that are children of review summaries
- ❌ Not using `gh` CLI tool for accurate comment status
- ❌ **Blindly implementing review suggestions without validation**
- ❌ **Removing valid assertions or safety checks**
- ❌ **Not understanding the mathematical/logical correctness**

**Only perform this procedure when**:
- Explicitly requested to address review comments
- Given a specific PR number
- NOT for general code review or improvement tasks

### Adding a new easing function
1. Add enum value to `easing_type_t`
2. Implement in `apply_easing()`
3. Add parsing in command-line handler
4. Document in `docs/animation.md`

### Adding a shader uniform
1. Declare uniform in shader source
2. Get location with `glGetUniformLocation()`
3. Set value in render loop
4. Update state structure if needed

### Supporting new image format
1. Check stb_image support
2. Add format validation
3. Update documentation
4. Add test image

## Debug Helpers

```bash
# Enable debug output
export HYPRLAX_DEBUG=1

# Check OpenGL capabilities
glxinfo | grep -E "OpenGL|version"

# Monitor GPU usage
nvidia-smi -l 1  # NVIDIA
radeontop        # AMD

# Trace Wayland protocol
WAYLAND_DEBUG=1 ./hyprlax test.jpg
```

## Project Priorities

1. **Stability** - No crashes, no memory leaks
2. **Performance** - Smooth 144 FPS animation
3. **Compatibility** - Works on all Hyprland setups
4. **Simplicity** - Easy to use and understand
5. **Beauty** - Stunning visual effects

## Version Management

- Version format: MAJOR.MINOR.PATCH
- Update version in `src/hyprlax.c` (#define HYPRLAX_VERSION)
- Tag releases: `git tag -a v1.2.0 -m "Release v1.2.0"`
- Update changelog in README.md

## Resources

- [Wayland Protocol Docs](https://wayland.freedesktop.org/docs/html/)
- [OpenGL ES 2.0 Reference](https://www.khronos.org/opengles/sdk/docs/man/)
- [Layer Shell Protocol](https://github.com/swaywm/wlr-protocols)
- [Hyprland IPC](https://wiki.hyprland.org/IPC/)

## Questions?

- Check existing issues first
- Read documentation thoroughly
- Test your assumptions
- Ask in GitHub issues if stuck

---

Remember: Keep it simple, keep it fast, keep it beautiful.