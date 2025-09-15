# Hyprlax Modularization - Implementation Plan

## Overview

This document provides a detailed implementation plan for modularizing hyprlax to support multiple compositors and display servers. The plan is divided into 5 major milestones with specific deliverables, timelines, and success criteria.

## Timeline Summary

**Total Duration**: 16-20 weeks
**Team Size**: 1-3 developers
**Backwards Compatibility**: Maintained throughout

## Milestone 1: Foundation and Core Extraction
**Duration**: 3-4 weeks
**Goal**: Extract core functionality into a separate library and establish abstraction interfaces

### Week 1: Project Setup and Analysis
- [ ] Create new project structure with subdirectories:
  ```
  hyprlax/
  ├── core/           # Core library
  ├── adapters/       # Compositor adapters
  ├── src/            # Main executable
  ├── include/        # Public headers
  └── tests/          # Test suites
  ```
- [ ] Set up CMake build system for modular compilation
- [ ] Create development branch: `feature/modularization`
- [ ] Document current function dependencies and coupling points
- [ ] Set up CI/CD for multi-compositor testing

### Week 2: Core Library Creation
- [ ] Extract core components to `libhyprlax-core`:
  - Animation engine (easing functions, timing, interpolation)
  - Layer management (add, remove, update, sort)
  - OpenGL rendering pipeline (shaders, textures, drawing)
  - Configuration parser (CLI args, config files)
  - Image loading (stb_image integration)
- [ ] Create core public API header: `hyprlax-core.h`
- [ ] Implement core initialization and lifecycle functions
- [ ] Create unit tests for core functions

### Week 3: Abstract Interface Design
- [ ] Design and implement abstract interfaces:
  ```c
  // include/hyprlax-compositor.h
  typedef struct hyprlax_compositor_adapter compositor_adapter_t;
  
  // include/hyprlax-display.h
  typedef struct hyprlax_display_adapter display_adapter_t;
  
  // include/hyprlax-graphics.h
  typedef struct hyprlax_graphics_context graphics_context_t;
  ```
- [ ] Create interface documentation with usage examples
- [ ] Implement interface validation and compliance tests
- [ ] Create adapter loading mechanism (dlopen/dlsym)

### Week 4: Hyprland Adapter Refactoring
- [ ] Refactor existing Hyprland code into adapter pattern:
  - Move IPC connection to `adapters/hyprland/ipc.c`
  - Move workspace detection to `adapters/hyprland/workspace.c`
  - Move environment detection to `adapters/hyprland/detect.c`
- [ ] Implement Hyprland adapter using new interfaces
- [ ] Ensure backwards compatibility with existing CLI
- [ ] Full regression testing with current feature set

### Success Criteria
- ✓ Core library compiles independently
- ✓ All unit tests pass
- ✓ Hyprland functionality unchanged
- ✓ No performance regression (FPS, memory usage)

### Deliverables
- `libhyprlax-core.so` - Core functionality library
- `hyprlax-hyprland.so` - Hyprland adapter
- API documentation
- Updated test suite

---

## Milestone 2: Display Server Abstraction
**Duration**: 3-4 weeks
**Goal**: Abstract Wayland-specific code and prepare for X11 support

### Week 5: Wayland Display Adapter
- [ ] Create Wayland display adapter:
  ```c
  adapters/wayland/
  ├── display.c       # Display connection
  ├── surface.c       # Surface management
  ├── layer-shell.c   # Layer shell protocol
  ├── xdg-shell.c     # XDG shell fallback
  └── egl.c          # EGL context creation
  ```
- [ ] Abstract protocol handling (layer-shell vs xdg-shell)
- [ ] Implement protocol capability detection
- [ ] Create Wayland-specific tests

### Week 6: Graphics Context Abstraction
- [ ] Abstract OpenGL context creation:
  - EGL for Wayland
  - GLX for X11 (preparation)
- [ ] Create graphics capability detection
- [ ] Implement context switching logic
- [ ] Performance benchmarking

### Week 7: X11 Display Adapter (Foundation)
- [ ] Create X11 display adapter structure:
  ```c
  adapters/x11/
  ├── display.c       # X connection
  ├── window.c        # Window management
  ├── properties.c    # EWMH properties
  ├── events.c        # Event handling
  └── glx.c          # GLX context
  ```
- [ ] Implement basic X11 connection and window creation
- [ ] Add EWMH property monitoring for workspace changes
- [ ] Create X11 test environment

### Week 8: Integration and Testing
- [ ] Integrate display adapters with core
- [ ] Create adapter selection logic
- [ ] Test Wayland adapter with multiple compositors
- [ ] Document display adapter API
- [ ] Performance comparison between adapters

### Success Criteria
- ✓ Wayland adapter works with Hyprland
- ✓ X11 adapter creates window and receives events
- ✓ Clean separation between display code and core
- ✓ Adapter switching without recompilation

### Deliverables
- `hyprlax-wayland.so` - Wayland display adapter
- `hyprlax-x11.so` - X11 display adapter (basic)
- Display adapter documentation
- Cross-platform test suite

---

## Milestone 3: Multi-Compositor Support
**Duration**: 4-5 weeks
**Goal**: Implement adapters for additional compositors

### Week 9-10: Sway Adapter
- [ ] Implement Sway compositor adapter:
  ```c
  adapters/sway/
  ├── detect.c        # Sway detection
  ├── ipc.c          # i3/Sway IPC protocol
  ├── workspace.c     # Workspace management
  └── config.c       # Sway-specific config
  ```
- [ ] Implement i3 IPC protocol client
- [ ] Parse Sway workspace events
- [ ] Handle Sway-specific features (output management)
- [ ] Test with various Sway configurations
- [ ] Document Sway-specific setup

### Week 11-12: Niri Adapter
- [ ] Research Niri IPC protocol and capabilities
- [ ] Implement Niri compositor adapter:
  ```c
  adapters/niri/
  ├── detect.c        # Niri detection
  ├── ipc.c          # Niri IPC
  ├── workspace.c     # Workspace/tag management
  └── config.c       # Niri-specific config
  ```
- [ ] Handle Niri's workspace/tag model
- [ ] Test with Niri compositor
- [ ] Document Niri-specific features and limitations

### Week 13: X11 Window Managers
- [ ] Complete X11 adapter for window managers:
  - i3 (X11 mode)
  - Awesome WM
  - bspwm
  - Generic EWMH-compliant WMs
- [ ] Implement `_NET_CURRENT_DESKTOP` monitoring
- [ ] Add root window background setting
- [ ] Test with various X11 window managers

### Success Criteria
- ✓ Sway adapter fully functional
- ✓ Niri adapter working (if Niri IPC available)
- ✓ X11 adapter works with major window managers
- ✓ Automatic compositor detection works
- ✓ Manual adapter selection works

### Deliverables
- `hyprlax-sway.so` - Sway adapter
- `hyprlax-niri.so` - Niri adapter
- Enhanced X11 adapter
- Compositor compatibility matrix
- Installation guides per compositor

---

## Milestone 4: Advanced Features and Optimization
**Duration**: 3-4 weeks
**Goal**: Implement plugin system, runtime switching, and performance optimizations

### Week 14: Plugin System
- [ ] Implement dynamic adapter loading system:
  ```c
  // Plugin discovery
  /usr/lib/hyprlax/adapters/
  ~/.local/lib/hyprlax/adapters/
  ```
- [ ] Create adapter registry and discovery
- [ ] Implement adapter priority and selection logic
- [ ] Add plugin configuration system
- [ ] Create example third-party adapter template

### Week 15: Runtime Features
- [ ] Implement runtime adapter switching
- [ ] Add hot-reload for configuration changes
- [ ] Create adapter capability negotiation
- [ ] Implement graceful fallback mechanisms
- [ ] Add comprehensive error recovery

### Week 16: Performance Optimization
- [ ] Profile and optimize core library
- [ ] Implement adapter-specific optimizations
- [ ] Add adaptive quality settings
- [ ] Optimize memory usage and allocation patterns
- [ ] Create performance benchmarking suite

### Week 17: Polish and Documentation
- [ ] Complete API documentation
- [ ] Write adapter development guide
- [ ] Create migration guide from monolithic version
- [ ] Update user documentation
- [ ] Prepare release notes

### Success Criteria
- ✓ External adapters can be loaded
- ✓ Runtime switching works smoothly
- ✓ Performance meets or exceeds original
- ✓ Documentation is comprehensive
- ✓ All tests pass

### Deliverables
- Plugin system implementation
- Adapter development SDK
- Performance benchmarking tools
- Complete documentation set
- Release candidate build

---

## Milestone 5: Release and Stabilization
**Duration**: 2-3 weeks
**Goal**: Final testing, bug fixes, and release preparation

### Week 18: Beta Testing
- [ ] Release beta version to testing community
- [ ] Set up bug tracking for beta feedback
- [ ] Test on various distributions:
  - Arch Linux
  - Ubuntu/Debian
  - Fedora
  - NixOS
  - Gentoo
- [ ] Gather performance metrics from real users
- [ ] Document known issues and workarounds

### Week 19: Bug Fixes and Stabilization
- [ ] Address critical bugs from beta testing
- [ ] Fix adapter-specific issues
- [ ] Optimize based on real-world usage
- [ ] Update documentation based on feedback
- [ ] Prepare distribution packages

### Week 20: Release
- [ ] Final testing cycle
- [ ] Create release packages:
  - Source tarball
  - Distribution packages (AUR, deb, rpm)
  - Flatpak/AppImage (optional)
- [ ] Update website and documentation
- [ ] Announce release
- [ ] Monitor initial user feedback

### Success Criteria
- ✓ No critical bugs
- ✓ All supported compositors working
- ✓ Performance acceptable on various hardware
- ✓ Positive user feedback
- ✓ Packages available for major distributions

### Deliverables
- hyprlax 2.0.0 release
- Distribution packages
- Updated website
- Release announcement
- Support documentation

---

## Risk Management

### Technical Risks

1. **Compositor API Changes**
   - *Risk*: Compositor IPC protocols may change
   - *Mitigation*: Version detection and compatibility layers
   - *Contingency*: Maintain adapter versions for different compositor versions

2. **Performance Regression**
   - *Risk*: Abstraction layers may impact performance
   - *Mitigation*: Continuous benchmarking and profiling
   - *Contingency*: Provide "direct mode" for critical paths

3. **Protocol Availability**
   - *Risk*: Some compositors may lack required protocols
   - *Mitigation*: Multiple fallback options (layer-shell → xdg-shell → X11)
   - *Contingency*: Document limitations per compositor

### Schedule Risks

1. **Compositor Documentation**
   - *Risk*: Poor documentation for some compositor APIs
   - *Mitigation*: Direct communication with compositor developers
   - *Buffer*: 1 week added to compositor research phases

2. **Testing Coverage**
   - *Risk*: Difficulty testing all compositor combinations
   - *Mitigation*: Community beta testing program
   - *Buffer*: 2-week beta period before release

---

## Resource Requirements

### Development Resources
- **Primary Developer**: Full-time for 20 weeks
- **Testing Support**: Part-time tester for weeks 8-20
- **Documentation Writer**: Part-time for weeks 16-20

### Infrastructure
- **CI/CD**: Multi-distribution testing environment
- **Test Hardware**: Various GPU vendors (Intel, AMD, NVIDIA)
- **Virtual Machines**: For compositor testing

### Community
- **Beta Testers**: 20-30 volunteers
- **Compositor Maintainers**: Communication channel established
- **Distribution Packagers**: Coordination for release

---

## Success Metrics

### Technical Metrics
- **Code Coverage**: >80% for core library
- **Performance**: <5% overhead vs monolithic version
- **Memory Usage**: <10% increase
- **Startup Time**: <100ms increase

### Adoption Metrics
- **Compositor Support**: 5+ compositors at launch
- **Distribution Packages**: 5+ distributions
- **User Migration**: 70% of existing users upgrade
- **New Users**: 50% increase in user base

### Quality Metrics
- **Bug Count**: <10 critical bugs in first month
- **Response Time**: <48 hours for critical issues
- **Documentation**: 100% API coverage
- **User Satisfaction**: >4.0/5.0 rating

---

## Communication Plan

### Internal Communication
- **Weekly Status Reports**: Every Friday
- **Milestone Reviews**: At completion of each milestone
- **Daily Standups**: During critical phases

### External Communication
- **Blog Posts**: At each milestone completion
- **Community Updates**: Bi-weekly on Discord/Matrix
- **Beta Announcements**: Via GitHub, Reddit, forums
- **Release Marketing**: Coordinated across platforms

---

## Post-Release Plan

### Immediate (Weeks 21-24)
- Monitor user feedback and bug reports
- Release patch versions for critical issues
- Gather feature requests for next version
- Update documentation based on user questions

### Short-term (Months 2-3)
- Implement high-priority feature requests
- Add support for additional compositors
- Optimize based on real-world usage patterns
- Establish regular release cycle

### Long-term (Months 4-6)
- Major feature additions (GUI configuration tool)
- Extended plugin ecosystem
- Integration with desktop environments
- Performance improvements for low-end hardware

---

## Conclusion

This implementation plan provides a structured approach to modularizing hyprlax while maintaining stability and user experience. The phased approach allows for continuous testing and validation, reducing the risk of major issues at release. With proper execution, hyprlax will evolve from a Hyprland-specific tool to a universal parallax wallpaper solution for the Linux desktop.