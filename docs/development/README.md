# Development Documentation

Resources for building, understanding, and contributing to hyprlax.

## Quick Links

- [Building](building.md) - Compile hyprlax from source
- [Architecture](architecture.md) - System design and structure
- [Contributing](contributing.md) - How to contribute code
- [Testing](testing.md) - Running and writing tests
- [Custom Builds](custom-builds.md) - Advanced build configurations

## Getting Started

### First Time Setup
1. Clone the repository
2. Install dependencies ([see building guide](building.md))
3. Build with `make debug`
4. Run tests with `make test`

### Development Workflow
```bash
# Create feature branch
git checkout -b feature/my-feature

# Make changes and test
make debug
./hyprlax --debug test.jpg

# Run tests
make test

# Format code
make format

# Commit changes
git add -A
git commit -m "Add my feature"
```

## Project Overview

hyprlax is organized into modular components:

- **Platform Layer** - OS/display server abstraction
- **Compositor Adapters** - Compositor-specific implementations
- **Renderer** - OpenGL ES 2.0 rendering
- **Core** - Animation, configuration, layer management
- **IPC** - Runtime control interface

See [Architecture](architecture.md) for detailed design documentation.

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, argument parsing |
| `src/hyprlax_main.c` | Main application logic |
| `src/platform/wayland.c` | Wayland platform implementation |
| `src/compositor/*.c` | Compositor adapters |
| `src/renderer/gles2.c` | OpenGL ES 2.0 renderer |
| `src/core/config.c` | Configuration parsing |
| `src/ipc.c` | IPC server |

## Development Tools

### Debugging
```bash
# Debug build with symbols
make debug

# Run with debug output
./hyprlax --debug image.jpg

# Use GDB
gdb ./hyprlax
(gdb) run --debug image.jpg

# Valgrind memory check
valgrind --leak-check=full ./hyprlax image.jpg
```

### Performance Analysis
```bash
# CPU profiling with perf
perf record ./hyprlax image.jpg
perf report

# GPU profiling (NVIDIA)
nvidia-smi dmon -s u

# Benchmark suite
make bench
```

### Code Quality
```bash
# Format code
make format

# Static analysis
make check

# Run tests
make test

# Coverage report
make coverage
```

## Contributing Guidelines

### Code Style
- 4-space indentation
- K&R brace style
- 80-100 character line limit
- Descriptive variable names
- Comments for complex logic

### Commit Messages
```
component: Brief description

Longer explanation if needed. Explain what changed
and why, not how (the code shows how).

Fixes #123
```

### Pull Request Process
1. Fork the repository
2. Create feature branch
3. Make changes with tests
4. Ensure tests pass
5. Submit PR with description

See [Contributing](contributing.md) for full guidelines.

## Testing

### Run All Tests
```bash
make test
```

### Run Specific Test
```bash
./tests/test_animation
./tests/test_compositor
```

### Add New Test
1. Create test file in `tests/`
2. Add to `TESTS` in Makefile
3. Follow existing test patterns

See [Testing](testing.md) for test writing guide.

## Documentation

### Building Docs
```bash
# Generate from source
make docs
```

### Documentation Style
- Clear, concise language
- Code examples where helpful
- Keep reference docs up-to-date
- Update changelog for features

## Release Process

### Version Bumping
1. Update version in `src/version.h`
2. Update CHANGELOG.md
3. Tag release: `git tag -a v2.0.0 -m "Release v2.0.0"`

### Building Release
```bash
# Clean build
make clean
make CFLAGS="-O3 -march=native"

# Test thoroughly
make test
make bench

# Create tarball
make dist
```

## Getting Help

### Resources
- [Issue Tracker](https://github.com/sandwichfarm/hyprlax/issues)
- [Discussions](https://github.com/sandwichfarm/hyprlax/discussions)
- Architecture docs: [architecture.md](architecture.md)

### Debug Information
When reporting issues, include:
```bash
# System info
uname -a
echo $XDG_SESSION_TYPE

# hyprlax version
./hyprlax --version

# Debug output
./hyprlax --debug test.jpg 2>&1 | head -100
```