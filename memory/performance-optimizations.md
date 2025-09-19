Hyprlax Performance Optimization Plan

Goals
- Maintain 144 FPS target with 5–7 ms frame time budget.
- Reduce GPU power during idle; return to baseline after animations.
- Keep render loop allocations and state changes to a minimum.
- Preserve CLI and behavior; keep changes incremental and safe.

Hotspots Identified
- Renderer (src/renderer/gles2.c)
  - glFinish before every swap in present() stalls the GPU; limits pipeline overlap.
  - Per-draw VBO allocation in draw_layer() (glGenBuffers/glBufferData/glDeleteBuffers) causes CPU/GPU overhead and driver sync.
  - Attribute/uniform locations queried every draw (glGetAttribLocation/glGetUniformLocation).
  - Blur shader is a naïve 2D kernel; cost grows quadratically with blur_amount.
- Main Loop (src/hyprlax_main.c)
  - Renders all layers every frame when needs_render is set, even if only offsets changed slightly.
  - Idle sleep is time-based; not using Wayland frame callbacks to schedule draws.
- Wayland (src/platform/wayland.c)
  - Commits every frame per monitor; no damage region or frame callback scheduling yet.

Quick Wins (Low Risk, High Impact)
- Optional: skip glFinish in present()
  - File: src/renderer/gles2.c, gles2_present()
  - Rationale: glFinish stalls CPU; eglSwapBuffers is enough on most stacks.
  - Implementation: Add env toggle HYPRLAX_NO_GLFINISH=1. Default keeps glFinish.
  - Expected: Lower frame latency and higher throughput during animations.

- Optional: persistent VBO (and optionally EBO)
  - File: src/renderer/gles2.c, gles2_draw_layer()
  - Current: Creates and deletes a VBO every draw; uploads full vertex array each time.
  - Change: With HYPRLAX_PERSISTENT_VBO=1 reuses the pre-created VBO and updates with glBufferSubData; no per-draw glGen/glDelete. When combined with uniform-offset (below), avoids buffer updates entirely.
  - Expected: Fewer driver calls, less CPU-GPU sync; noticeable CPU savings at 5+ layers.

- Optional: pass offsets via uniform instead of re-uploading vertices
  - Files: src/renderer/shader.c/.h and src/renderer/gles2.c
  - Change: With HYPRLAX_UNIFORM_OFFSET=1, compile a vertex shader variant with uniform vec2 u_offset and compute v_texcoord = a_texcoord + u_offset; set u_offset per layer in draw_layer().
  - Benefit: Eliminates glBufferData per draw and reduces bandwidth; attribute setup remains static.

- Cache uniform and attribute locations
  - Files: src/renderer/shader.c/.h
  - Current: glGetUniformLocation and glGetAttribLocation called every draw.
  - Change: Cache locations in shader_program_t after link; access via shader_get_* helpers. No behavior change; enabled by default.
  - Expected: Minor but consistent CPU reduction, fewer GL queries in hot path.

- Avoid redundant state changes
  - File: src/hyprlax_main.c, hyprlax_render_monitor()
  - Current: glEnable(GL_BLEND) per monitor when layer_count > 1.
  - Change: Track a simple boolean “blend_enabled” and only enable/disable when state changes. Alternatively enable once at init if always needed for multi-layer.
  - Expected: Small reduction in driver state churn.

Medium-Scope Improvements
- Separable Gaussian blur (two-pass) with FBO ping-pong
  - Files: src/renderer/shader.c, src/renderer/gles2.c
  - Current: Nested 2D kernel loops; cost grows O(n^2) with blur_amount.
  - Change: Implement horizontal and vertical passes with small fixed kernel (e.g., 5–9 taps) and weights; render to offscreen FBO then composite. Gate by quality level.
  - Expected: Large speedup for blur effects; quality levels can trade taps for speed.

- Event-driven rendering when idle
  - Files: src/platform/wayland.c, src/core/monitor.c
  - Change: Use wl_surface_frame callbacks per monitor to schedule the next render only when needed. During idle (no animations, no IPC changes), avoid periodic wakeups and let Wayland drive frame pacing.
  - Expected: Significant idle power reduction; faster return to baseline after animations.

- Damage-aware present (if supported)
  - Files: src/platform/wayland.c, renderer backend
  - Change: Use wl_surface_damage_buffer and (if available) EGL_EXT_swap_buffers_with_damage to limit buffer swaps to changed regions. For parallax this may still be full-frame, but can help on multi-monitor or partial updates.

- Skip unchanged frames
  - File: src/hyprlax_main.c
  - Current: Static last_rendered_offset_x/y is unused.
  - Change: Track per-layer and per-monitor offsets; if all unchanged since last render (within epsilon), skip draw and present. Integrate with has_active_animations().
  - Expected: Fewer submits when offsets settle; helps at end of animations.

Advanced (Optional/Future)
- Texture atlas for small layers
  - Files: src/renderer/texture_atlas.* (present but not integrated)
  - Use a real atlas builder with pixel packing and UV remap to reduce texture binds. Benefits depend on layer composition patterns.

- Dynamic VSync strategy
  - Enable vsync during active animations for smoother pacing; disable when idle to avoid blocking. Requires careful toggling via eglSwapInterval to prevent stutter.

Benchmarking & Validation
- Scripts/targets to run benches:
  - make bench — runs scripts/bench/bench-optimizations.sh (produces hyprlax-test-*.log)
  - make bench-perf — runs scripts/bench/bench-performance.sh (end-to-end power/FPS/idle)
  - make bench-30fps — convenience 30 FPS test wrapper

- Recommended workflow
  1) Establish baseline
     - Run: make bench-perf (or scripts/bench/bench-performance.sh examples/pixel-city/parallax.conf 30)
     - Save results (baseline-performance.txt exists with a recent run).
  2) Apply quick wins (incrementally)
     - Enable caching (already on by default).
     - Test HYPRLAX_PERSISTENT_VBO=1 (no behavior change, lower CPU overhead).
     - Test HYPRLAX_UNIFORM_OFFSET=1 (reduces buffer traffic further). Combine with persistent VBO for best effect.
     - Test HYPRLAX_NO_GLFINISH=1 (present faster; verify visuals on your compositor).
     - Rebuild: make
     - Run: make bench
     - Expect lower animation power and equal or better FPS; faster return to baseline in post-animation idle.
  3) Implement separable blur (optional)
     - Gate by a quality flag; verify blur-on performance.
     - Compare make bench results across presets when available.
  4) Event-driven idle
     - Add frame callbacks; verify post-animation power approaches baseline within seconds.

- Interpreting results
  - Animation: Focus on FPS stability at target and peak power reduction vs previous runs.
  - Idle: Aim for “Post-animation” power close to “Baseline (no hyprlax)”.
  - Logs: hyprlax debug logs show FPS and animation state; confirm animations become inactive promptly.

Code Pointers (for implementation)
- Remove GPU stall
  - src/renderer/gles2.c: In gles2_present(), drop glFinish(); rely on eglSwapBuffers.

- Persistent geometry setup
  - src/renderer/gles2.c: Use g_gles2_data->vbo created in gles2_init(); bind once per frame; do not recreate per draw.
  - src/renderer/shader.c: Add uniform vec2 u_offset in vertex shader and use it to adjust v_texcoord. Set via shader_set_uniform_vec2 in draw_layer().

- Uniform/attribute cache
  - src/include/shader.h: shader_program_t can include a shader_uniforms_t. Populate in shader_compile() after linking; reuse in gles2_draw_layer().

- Blur
  - src/renderer/shader.c: Add separable blur shader sources (horizontal/vertical). In gles2.c, add minimal FBO helpers for ping-pong passes when blur_amount > 0.

- Idle scheduling
  - src/platform/wayland.c: Add wl_surface_frame callbacks; call monitor_mark_frame_pending(), and in callback, monitor_frame_done(). Render only when callback fires or when animations/IPC require it.

Risks and Mitigations
- State changes may subtly affect blending order: keep tests for single vs multi-layer parity.
- Uniform-based offset requires shader update: guard with capability checks and keep a fallback path.
- Separable blur introduces FBOs: ensure GLES2 compatibility; gate behind quality or capability flag.

Test Checklist (post-change)
- make && make test — no warnings; tests pass.
- make bench — improved idle/post power; stable FPS.
- make bench-perf — post-animation power near baseline; IPC and resizing still work.
- Visual parity across quality levels where blur is off.

Notes
- The binary may already accept a --quality flag (scripts reference it). If adding tiered quality in source, keep CLI backward compatible and document in docs/.
- Debug: keep debug-only GL error checks and verbose logs behind config.debug or HYPRLAX_DEBUG.

Current toggles implemented
- HYPRLAX_PERSISTENT_VBO=1 — reuse a single VBO (no per-draw create/delete).
- HYPRLAX_UNIFORM_OFFSET=1 — compute texcoord offset in the vertex shader via u_offset.
- HYPRLAX_NO_GLFINISH=1 — skip glFinish() before eglSwapBuffers.
- HYPRLAX_SEPARABLE_BLUR=1 — enable two-pass FBO blur with a small fixed kernel.
- HYPRLAX_BLUR_DOWNSCALE=N — downscale factor for separable blur FBO (e.g., 2, 3, 4).
- HYPRLAX_FRAME_CALLBACK=1 — use wl_surface_frame pacing to avoid busy rendering.
- HYPRLAX_PROFILE=1 — per-frame [PROFILE] logs with draw/present timings.
- HYPRLAX_RENDER_DIAG=1 — logs [RENDER_DIAG] when rendering during idle/steady state.

Examples
- Baseline: make bench
- Persistent VBO: HYPRLAX_PERSISTENT_VBO=1 make bench
- Uniform offset: HYPRLAX_UNIFORM_OFFSET=1 make bench
- Max perf: HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1 HYPRLAX_NO_GLFINISH=1 make bench

Makefile helpers
- make bench — run optimization checks via scripts/bench/bench-optimizations.sh
- make bench-perf — broader performance runs via scripts/bench/bench-performance.sh
- make bench-30fps — fixed 30 FPS pacing run for power checks
- make bench-clean — remove hyprlax-test-*.log artifacts

Summary
- Immediate changes (remove glFinish, reuse VBO, pass u_offset, cache locations) are straightforward and should yield measurable gains in both animation throughput and idle power.
- Medium changes (separable blur, frame callbacks) address the biggest remaining CPU/GPU costs and idle wakeups.
- Use the provided scripts to quantify improvements at each step and ensure behavior remains stable across monitors and compositors.
