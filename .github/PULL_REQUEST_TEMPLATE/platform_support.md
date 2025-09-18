---
name: Platform/Compositor/Renderer Support
about: Add support for a new platform, compositor, or renderer
title: '[SUPPORT] Add support for '
labels: platform
---

## Support Type
<!-- Check what this PR adds -->
- [ ] New Operating System/Distribution
- [ ] New Wayland Compositor  
- [ ] New Rendering Backend
- [ ] New Architecture (ARM, etc.)

## Target Platform/Component
<!-- Specify exactly what's being added -->
- **Name**: <!-- e.g., FreeBSD, Sway, Vulkan -->
- **Version**: <!-- Minimum supported version -->
- **Related Issue**: #<!-- issue number -->

## Implementation Summary
<!-- Overview of changes made -->

### Core Changes
- Build system modifications:
- Platform-specific code added:
- Conditional compilation used:
- New dependencies required:

### Compatibility Layer
<!-- If adding compositor support -->
- Protocol differences handled:
- IPC implementation:
- Workspace detection method:

### Renderer Details  
<!-- If adding renderer support -->
- Shader modifications:
- Pipeline changes:
- Performance characteristics:

## Build Instructions
<!-- How to build for this platform -->

### Dependencies Installation
```bash
# Platform-specific dependency installation
```

### Build Commands
```bash
# Build commands for this platform
make PLATFORM=xxx
```

### Installation
```bash  
# Installation process
```

## Testing

### Test Environment
- **Hardware**: <!-- CPU, GPU, RAM -->
- **OS/Distro**: <!-- Exact version -->
- **Compositor**: <!-- If applicable -->
- **Graphics Stack**: <!-- Driver versions -->

### Test Coverage
- [ ] Basic functionality (single layer)
- [ ] Multi-layer parallax
- [ ] All animation easings
- [ ] Blur effects
- [ ] IPC/dynamic control
- [ ] Performance benchmarked
- [ ] Memory leak testing
- [ ] Long-running stability test (24+ hours)

### Test Results
```bash
# make test output
```

### Known Limitations
<!-- Any features that don't work on this platform -->
- 
- 

## Performance Metrics
<!-- Compare with reference platform (Hyprland on Arch Linux) -->

| Metric | Reference Platform | This Platform |
|--------|-------------------|---------------|
| Startup time | X ms | Y ms |
| Memory usage | X MB | Y MB |
| CPU usage (idle) | X% | Y% |
| CPU usage (animating) | X% | Y% |
| GPU usage | X% | Y% |
| Frame timing | X ms | Y ms |

## Compatibility Matrix
<!-- Check all that work -->
- [ ] Static images (JPEG, PNG)
- [ ] Transparency (PNG alpha)
- [ ] Multi-layer rendering
- [ ] Blur effects
- [ ] All easing functions
- [ ] IPC commands
- [ ] Configuration files
- [ ] Verbose/debug output

## Code Quality

### Platform Integration
- [ ] Uses platform idioms and conventions
- [ ] Follows platform library best practices
- [ ] Respects platform file locations (config, cache)
- [ ] Handles platform-specific errors gracefully

### Maintenance
- [ ] Platform detection is automatic
- [ ] Build system changes are minimal
- [ ] Platform-specific code is isolated
- [ ] Easy to maintain long-term

## Documentation
- [ ] Updated README.md with platform support
- [ ] Updated installation.md with platform instructions
- [ ] Added platform-specific troubleshooting
- [ ] Updated build documentation
- [ ] Added platform to CI matrix (if applicable)

## Checklist

### Code Quality
- [ ] PR focuses ONLY on platform/compositor/renderer support
- [ ] No unrelated changes included
- [ ] Code follows project conventions
- [ ] Conditional compilation used appropriately
- [ ] No platform-specific code in core logic

### Testing
- [ ] `make test` passes on target platform
- [ ] `make memcheck` clean (if valgrind available)
- [ ] `make lint` passes
- [ ] Tested all major features
- [ ] No regressions on other platforms

### Review Ready
- [ ] CHANGELOG.md updated
- [ ] Documentation complete
- [ ] Test results included
- [ ] Performance metrics provided
- [ ] Ready for review

## Maintenance Commitment
- [ ] I can help maintain this platform support
- [ ] I can test future releases on this platform
- [ ] I can help with platform-specific issues

## Additional Notes
<!-- Platform-specific notes for reviewers and users -->