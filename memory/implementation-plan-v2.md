# Hyprlax Modularization - Implementation Plan v2

## Overview

This pragmatic implementation plan focuses on incremental, low-risk modularization of hyprlax while maintaining its single-binary architecture, GNU Make build system, and minimal dependencies. The plan delivers multi-compositor support in 10 weeks with clear milestones and testable deliverables.

## Core Principles

1. **No Breaking Changes**: Existing users see no change in behavior
2. **Single Binary**: All code statically linked into one executable
3. **GNU Make**: Keep existing build system, enhance incrementally
4. **EGL Everywhere**: Use EGL/GLES2 for both Wayland and X11
5. **Test What Matters**: Focus testing on pure logic, not graphics/IPC
6. **Performance First**: Profile and benchmark at each step

## Timeline

**Duration**: 10 weeks (2.5 months)
**Team Size**: 1-2 developers
**Risk Level**: Low (incremental changes)

---

## Phase 0: Foundation (Week 1)
**Goal**: Establish module structure without changing behavior

### Tasks

**Day 1-2: Project Structure**
```bash
# Create module directories
mkdir -p src/{core,gfx,platform,compositor,include}

# Create internal headers
touch src/include/{core,gfx,platform,compositor}.h
touch src/include/hyprlax_internal.h
```

**Day 3-4: Extract Core Logic**
- [ ] Move easing functions to `src/core/easing.c`
  - Pure math functions, no dependencies
  - Create `ease_linear()`, `ease_quad_out()`, etc.
  - Add inline documentation for each easing type
  
- [ ] Move animation state to `src/core/animation.c`
  - Extract `animation_state_t` struct
  - Implement `animation_start()`, `animation_evaluate()`
  - Ensure no allocations in evaluate path

- [ ] Move layer management to `src/core/layer.c`
  - Extract `layer_t` struct definition
  - Implement `layer_create()`, `layer_destroy()`, `layer_update()`
  - Clear ownership semantics

**Day 5: Update Makefile**
```makefile
# Add module sources
CORE_SRCS = src/core/easing.c src/core/animation.c src/core/layer.c
GFX_SRCS = src/gfx/renderer.c src/gfx/shader.c src/gfx/texture.c
SRCS = src/hyprlax.c $(CORE_SRCS) $(GFX_SRCS) $(PROTOCOL_SRCS)

# Add module include path
CFLAGS += -Isrc/include
```

### Verification
- [ ] Binary size unchanged (±1KB)
- [ ] Behavior identical (test with existing configs)
- [ ] No new dependencies
- [ ] Clean compilation with -Wall -Wextra

### Deliverables
- Module directory structure
- Core logic extracted but behavior unchanged
- Updated Makefile

---

## Phase 1: Graphics Abstraction (Week 2)
**Goal**: Separate rendering from platform code

### Tasks

**Day 1-2: Extract Renderer**
- [ ] Move OpenGL rendering to `src/gfx/renderer.c`
  ```c
  // src/include/gfx.h
  typedef struct {
      GLuint standard_shader;
      GLuint blur_shader;
      // ... uniforms
  } renderer_t;
  
  int renderer_init(renderer_t* r);
  void renderer_draw_layer(renderer_t* r, layer_t* layer);
  ```

- [ ] Move shader compilation to `src/gfx/shader.c`
  - Embed shader source as const char*
  - Implement compile and link functions
  - Add shader validation

- [ ] Move texture management to `src/gfx/texture.c`
  - Extract texture loading (stb_image)
  - Implement texture cache
  - Add texture cleanup

**Day 3-4: Create Renderer Interface**
- [ ] Define clean boundary between renderer and GL context
- [ ] Renderer assumes valid GL context exists
- [ ] No EGL/platform code in renderer

**Day 5: Add Renderer Tests**
```c
// tests/test_renderer.c
void test_shader_compilation() {
    // Test shader source generation
    const char* src = generate_blur_shader(5.0f);
    assert(strstr(src, "uniform float u_blur_amount"));
}
```

### Verification
- [ ] Rendering output unchanged
- [ ] FPS performance unchanged
- [ ] Memory usage unchanged
- [ ] Shader compilation errors handled

### Deliverables
- Separated graphics module
- Clean renderer interface
- Basic renderer tests

---

## Phase 2: Platform Abstraction (Week 3-4)
**Goal**: Abstract Wayland code, prepare for X11

### Week 3: Wayland Platform Adapter

**Day 1-2: Define Platform Interface**
```c
// src/include/platform.h
typedef struct platform_adapter {
    const char* name;
    int (*init)(struct platform_adapter* self);
    int (*create_surface)(struct platform_adapter* self, ...);
    int (*create_gl_context)(struct platform_adapter* self, ...);
    void (*swap_buffers)(struct platform_adapter* self);
    int (*get_fd)(struct platform_adapter* self);
    int (*dispatch_events)(struct platform_adapter* self);
    void* priv;
} platform_adapter_t;
```

**Day 3-4: Implement Wayland Adapter**
- [ ] Create `src/platform/platform_wayland.c`
- [ ] Move Wayland connection code
- [ ] Move EGL context creation
- [ ] Move layer-shell protocol handling
- [ ] Add xdg-shell fallback

**Day 5: Integration**
- [ ] Update main loop to use platform adapter
- [ ] Add platform detection logic
- [ ] Test with Hyprland

### Week 4: X11 Platform Adapter

**Day 1-3: Implement X11/EGL**
- [ ] Create `src/platform/platform_x11.c`
- [ ] Implement X11 display connection
- [ ] Implement EGL on X11 (not GLX!)
- [ ] Create root window management
- [ ] Handle EWMH atoms (`_NET_WM_WINDOW_TYPE_DESKTOP`)

**Day 4-5: Testing and Optimization**
- [ ] Test on X11 window managers (i3, openbox)
- [ ] Compare performance Wayland vs X11
- [ ] Fix any EGL compatibility issues

### Verification
- [ ] Wayland path unchanged
- [ ] X11 creates fullscreen window
- [ ] EGL context works on both
- [ ] No GLX dependency

### Deliverables
- Platform abstraction layer
- Wayland platform adapter
- X11 platform adapter (EGL-based)
- Platform auto-detection

---

## Phase 3: Compositor Adapters (Week 5-7)
**Goal**: Support multiple compositor IPC protocols

### Week 5: Compositor Interface & Hyprland

**Day 1-2: Define Compositor Interface**
```c
// src/include/compositor.h
typedef struct compositor_adapter {
    const char* name;
    bool (*detect)(void);
    int (*init)(struct compositor_adapter* self);
    int (*get_current_workspace)(struct compositor_adapter* self);
    int (*connect_events)(struct compositor_adapter* self);
    int (*process_events)(struct compositor_adapter* self, ...);
    void* priv;
} compositor_adapter_t;
```

**Day 3-5: Refactor Hyprland Code**
- [ ] Create `src/compositor/compositor_hyprland.c`
- [ ] Move Hyprland IPC socket connection
- [ ] Move workspace event parsing
- [ ] Implement detection via `HYPRLAND_INSTANCE_SIGNATURE`

### Week 6: Sway/i3 Adapter

**Day 1-3: Implement i3 IPC Protocol**
- [ ] Create `src/compositor/compositor_sway.c`
- [ ] Implement i3 IPC client
  ```c
  // i3 IPC message format
  struct i3_ipc_header {
      char magic[6];  // "i3-ipc"
      uint32_t len;
      uint32_t type;
  };
  ```
- [ ] Subscribe to workspace events
- [ ] Parse JSON responses (minimal parser)

**Day 4-5: Testing**
- [ ] Test with Sway
- [ ] Test with i3 (if on X11)
- [ ] Verify workspace numbering

### Week 7: X11/EWMH Adapter

**Day 1-3: Implement EWMH Monitor**
- [ ] Create `src/compositor/compositor_x11_ewmh.c`
- [ ] Monitor `_NET_CURRENT_DESKTOP` property
- [ ] Handle PropertyNotify events
- [ ] Query `_NET_NUMBER_OF_DESKTOPS`

**Day 4-5: Integration**
- [ ] Add compositor auto-detection logic
- [ ] Test with various X11 WMs
- [ ] Document supported compositors

### Verification
- [ ] Hyprland works as before
- [ ] Sway workspace changes work
- [ ] X11 desktop changes detected
- [ ] Graceful fallback if no compositor

### Deliverables
- Compositor abstraction layer
- Hyprland adapter (refactored)
- Sway/i3 adapter
- X11/EWMH adapter
- Auto-detection logic

---

## Phase 4: Integration & Testing (Week 8)
**Goal**: Integrate all adapters, ensure stability

### Tasks

**Day 1-2: Main Loop Integration**
```c
// Simplified main loop
int main(int argc, char** argv) {
    // Detect adapters
    compositor = compositor_detect();
    platform = platform_detect();
    
    // Initialize
    platform->init(platform);
    compositor->init(compositor);
    renderer_init(&renderer);
    
    // Event loop
    while (running) {
        poll_events();
        update_animations();
        render_frame();
    }
}
```

**Day 3: Error Handling**
- [ ] Add graceful fallbacks
- [ ] Improve error messages
- [ ] Add debug logging (only with --debug)

**Day 4: Performance Testing**
- [ ] Profile with `perf`
- [ ] Check for memory leaks with valgrind
- [ ] Benchmark FPS with each adapter
- [ ] Ensure no allocations in hot paths

**Day 5: Documentation**
- [ ] Update README with compositor support
- [ ] Document adapter selection
- [ ] Create troubleshooting guide

### Verification
- [ ] All adapters work correctly
- [ ] Performance meets targets
- [ ] No memory leaks
- [ ] Clean error handling

### Deliverables
- Fully integrated multi-compositor support
- Performance benchmark results
- Updated documentation

---

## Phase 5: Optimization & Polish (Week 9)
**Goal**: Optimize performance and user experience

### Tasks

**Day 1-2: Micro-optimizations**
- [ ] Profile hot paths with `perf record`
- [ ] Inline critical functions
- [ ] Optimize matrix operations
- [ ] Reduce function call overhead

**Day 3: Binary Size Optimization**
- [ ] Enable LTO (link-time optimization)
- [ ] Strip unnecessary symbols
- [ ] Optimize compile flags
- [ ] Target: <100KB binary

**Day 4: User Experience**
- [ ] Improve startup time
- [ ] Add helpful error messages
- [ ] Create example configurations
- [ ] Add compositor-specific tips

**Day 5: Release Preparation**
- [ ] Update version number
- [ ] Create changelog
- [ ] Tag release candidate
- [ ] Build test packages

### Verification
- [ ] Binary size <100KB
- [ ] Startup time <100ms
- [ ] 60 FPS with 5 layers
- [ ] Memory usage <50MB

### Deliverables
- Optimized binary
- Performance report
- Release candidate

---

## Phase 6: Release (Week 10)
**Goal**: Test, package, and release

### Tasks

**Day 1-2: Distribution Testing**
- [ ] Test on Arch Linux
- [ ] Test on Ubuntu/Debian
- [ ] Test on Fedora
- [ ] Test on NixOS

**Day 3: Package Creation**
- [ ] Update AUR package
- [ ] Create .deb package
- [ ] Create .rpm package
- [ ] Update install script

**Day 4: Documentation**
- [ ] Update website
- [ ] Write release notes
- [ ] Create migration guide
- [ ] Update screenshots

**Day 5: Release**
- [ ] Tag version 2.0.0
- [ ] Push to GitHub
- [ ] Announce on Reddit/forums
- [ ] Monitor initial feedback

### Verification
- [ ] Packages install correctly
- [ ] No regression for existing users
- [ ] New features work as documented
- [ ] Positive initial feedback

### Deliverables
- hyprlax 2.0.0 release
- Distribution packages
- Release announcement
- Support documentation

---

## Testing Strategy

### Unit Tests (Minimal)
```c
// tests/test_core.c - Pure logic only
void test_easing() {
    assert(ease_linear(0.5f) == 0.5f);
}

void test_animation() {
    animation_state_t anim;
    animation_start(&anim, 0.0f, 100.0f, 1.0f);
    assert(animation_evaluate(&anim, 0.5) == 50.0f);
}
```

### Integration Tests
```bash
#!/bin/bash
# tests/test_compositors.sh

# Test Hyprland detection
HYPRLAND_INSTANCE_SIGNATURE=test ./hyprlax --dry-run
assert_compositor "hyprland"

# Test Sway detection  
SWAYSOCK=/tmp/sway.sock ./hyprlax --dry-run
assert_compositor "sway"

# Test X11 fallback
DISPLAY=:0 ./hyprlax --dry-run
assert_platform "x11"
```

### Manual Test Matrix
| Test Case | Hyprland | Sway | i3 | Expected |
|-----------|----------|------|----|----------|
| Workspace change | ✓ | ✓ | ✓ | Animation triggers |
| Multi-monitor | ✓ | ✓ | ✓ | Correct display |
| Config reload | ✓ | ✓ | ✓ | Updates without restart |
| Blur effect | ✓ | ✓ | ✓ | Renders correctly |

### Performance Benchmarks
```bash
# Automated benchmark
make benchmark

# Expected results:
# Startup time: <100ms
# Frame time: <16ms (60 FPS)
# Memory usage: <50MB
# CPU usage: <5% idle
```

---

## Risk Mitigation

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| EGL on X11 issues | Medium | High | Test early, have GLX fallback plan |
| Sway IPC differences | Low | Medium | Use i3 IPC spec as reference |
| Performance regression | Low | High | Profile at each phase |
| Binary size growth | Medium | Low | Use LTO, strip symbols |

### Process Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Scope creep | Medium | Medium | Strict phase boundaries |
| Testing coverage | Medium | Low | Focus on critical paths |
| User confusion | Low | Medium | Clear documentation |

---

## Success Criteria

### Phase 0-2 (Foundation)
- ✓ Code organized into modules
- ✓ No behavior changes
- ✓ Graphics abstraction complete

### Phase 3 (Compositors)
- ✓ 3+ compositors supported
- ✓ Auto-detection works
- ✓ Graceful fallbacks

### Phase 4-5 (Integration)
- ✓ Performance targets met
- ✓ No memory leaks
- ✓ Binary size <100KB

### Phase 6 (Release)
- ✓ Positive user feedback
- ✓ No critical bugs
- ✓ Documentation complete

---

## Future Considerations (Post-Release)

### Plugin System (v3.0)
After stable release and user feedback:
1. Define stable ABI
2. Implement dlopen loader
3. Create plugin SDK
4. System directory only (`/usr/lib/hyprlax/adapters/`)
5. Optional user plugins with explicit flag

### Additional Compositors
Based on user demand:
- River (Wayland)
- dwl (Wayland)  
- GNOME (requires research)
- KDE (requires research)

### Advanced Features
- GUI configuration tool
- Multi-monitor layouts
- Video wallpapers
- Network control

---

## Communication Plan

### Development Updates
- Weekly progress reports (GitHub discussions)
- Milestone completion announcements
- Beta releases for testing

### Documentation
- Inline code comments
- API documentation (Doxygen)
- User guides per compositor
- Troubleshooting FAQ

### Community
- Discord/Matrix for support
- GitHub issues for bugs
- Reddit for announcements

---

## Conclusion

This pragmatic plan delivers multi-compositor support in 10 weeks through incremental changes that maintain stability and performance. By keeping the single-binary architecture, GNU Make build system, and focusing on proven technologies (EGL), we minimize risk while achieving the modularization goals. The plan provides clear interfaces for future plugin support without implementing unnecessary complexity upfront.