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
- Remove glFinish from present()
  - File: src/renderer/gles2.c, gles2_present()
  - Rationale: glFinish stalls CPU until GPU completes; eglSwapBuffers already synchronizes. Keep glFlush in end_frame() if desired.
  - Expected: Lower frame latency and higher throughput during animations; reduced CPU usage.

- Reuse a persistent VBO (and optionally EBO)
  - File: src/renderer/gles2.c, gles2_draw_layer()
  - Current: Creates and deletes a VBO every draw; uploads full vertex array each time.
  - Change: Create one VBO in gles2_init() (already present: g_gles2_data->vbo). Bind once per frame or per program; update only what’s needed with glBufferSubData, or avoid uploads entirely (see next item).
  - Expected: Fewer driver calls, less CPU-GPU sync; noticeable CPU savings at 5+ layers.

- Pass offsets via uniform instead of re-uploading vertices
  - Files: src/renderer/shader.c/.h and src/renderer/gles2.c
  - Change: Add uniform vec2 u_offset to the basic and blur vertex/fragment path and compute v_texcoord = a_texcoord + u_offset in the vertex shader. Then set u_offset per layer in draw_layer().
  - Benefit: Eliminates glBufferData per draw and reduces bandwidth; attribute setup remains static.

- Cache uniform and attribute locations
  - Files: src/renderer/shader.c/.h
  - Current: glGetUniformLocation and glGetAttribLocation called every draw.
  - Change: Extend shader_program_t to include a small struct of cached locations (position, texcoord, u_texture, u_opacity, u_blur_amount, u_resolution, u_offset). Populate on compile/link; reuse in draw calls.
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
- Scripts provided in repo:
  - ./test-optimizations.sh — runs multiple settings (FPS and quality). Produces hyprlax-test-*.log with idle/animation/post metrics.
  - ./test-performance.sh — end-to-end power/FPS/idle behavior (expects NVIDIA nvidia-smi; adapt to radeontop for AMD).
  - ./test-quality.sh — compares quality tiers (if supported by the binary).

- Recommended workflow
  1) Establish baseline
     - Run: ./test-performance.sh examples/pixel-city/parallax.conf 30
     - Save results (baseline-performance.txt exists with a recent run).
  2) Apply quick wins
     - Remove glFinish, reuse VBO, add u_offset uniform, cache locations.
     - Rebuild: make
     - Run: ./test-optimizations.sh
     - Expect lower animation power and equal or better FPS; faster return to baseline in post-animation idle.
  3) Implement separable blur (optional)
     - Gate by a quality flag; verify blur-on performance.
     - Compare ./test-optimizations.sh results for Low/Medium/High tiers.
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
- ./test-optimizations.sh — improved idle/post power; stable FPS.
- ./test-performance.sh — post-animation power near baseline; IPC and resizing still work.
- Visual parity across quality levels where blur is off.

Notes
- The binary may already accept a --quality flag (scripts reference it). If adding tiered quality in source, keep CLI backward compatible and document in docs/.
- Debug: keep debug-only GL error checks and verbose logs behind config.debug or HYPRLAX_DEBUG.

Summary
- Immediate changes (remove glFinish, reuse VBO, pass u_offset, cache locations) are straightforward and should yield measurable gains in both animation throughput and idle power.
- Medium changes (separable blur, frame callbacks) address the biggest remaining CPU/GPU costs and idle wakeups.
- Use the provided scripts to quantify improvements at each step and ensure behavior remains stable across monitors and compositors.

