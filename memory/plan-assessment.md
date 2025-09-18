# Hyprlax Modularization Plan – Assessment

## Summary
- The proposed modularization and HLD documents are well‑structured, ambitious, and correctly identify adapter boundaries (compositor, display, graphics) with a sensible core API. They anticipate capability differences across compositors and outline risk/mitigation and testing.
- However, several items conflict with current project constraints and priorities (per AGENTS.md) and may introduce unnecessary complexity and risk. A narrower, incremental path within the existing toolchain and single‑binary architecture will better preserve stability and performance.

## Strengths
- Clear separation of concerns: core engine vs. adapters; good public API sketch.
- Thoughtful capability detection and graceful degradation strategy.
- Performance awareness (event batching, render skipping, adaptive quality).
- Acknowledges protocol fallbacks (layer‑shell → xdg‑shell) and outlines IPC per compositor.
- Sensible testing strata (unit/integration/system) and migration phases.

## Misalignments with Project Constraints
- Build system: proposes CMake + multi‑repo feel; current standard is GNU Make. Switching build systems adds churn without direct user value.
- Single‑binary requirement: plan moves to shared libraries and a plugin loader, conflicting with “single‑binary daemon” architecture. Packaging and startup reliability may suffer.
- Graphics stack: project standard is OpenGL ES 2.0 with EGL, avoiding divergent code paths and GL feature skew.
- Dependencies and tooling: CI/CD infra, plugin SDK, and test frameworks likely add dependencies (contrary to “don’t add deps without discussion”).

## Technical Risks and Oversights
- Runtime adapter switching: of limited practical value (users don’t switch compositors mid‑session). Adds complex lifecycle/state migration with little payoff. Prefer restart‑on‑change.
- GNOME/KDE background support: xdg‑shell alone is insufficient for a true wallpaper experience. GNOME typically requires gnome‑shell integration/extension; KDE uses Plasma plugins or specific protocols. The plan marks these with an asterisk but underestimates engineering effort and permissions/UX constraints.
- Security of plugin discovery: loading from `~/.local/lib/...` increases attack surface. If plugins remain, prefer opt‑in, signed or checksum‑verified paths, and never load from writable directories by default.
- Performance overhead: vtable indirection is cheap, but adapter boundary placement matters. Ensure render‑loop hot paths contain no allocation, logging, or cross‑module chatter. The plan doesn’t explicitly call this out.
- Software rendering fallback: unspecified implementation and likely to miss performance targets. Better to fail clearly or gate behind a compile‑time option.
- Testing practicality: 80% coverage on C with graphics/IPC is ambitious. Without substantial seams and mocks, coverage will stall. The plan doesn’t pin down test strategy for GL/Wayland dependent code (fakes vs. headless backends).
- Error handling and recovery: adapter re‑init flows (display reconnects, monitor hotplug, IPC reconnection) need explicit contracts and state diagrams to avoid leaks and inconsistent GL state.

## Scope and Sequencing Concerns
- Timeline optimism: multi‑compositor support, plugin system, runtime switching, and “5+ compositors at launch” are aggressive for 1–3 devs in 16–20 weeks while preserving performance and stability.
- Parallelization: many tasks depend on a stable core/adapter boundary. Start with a single adapter (Hyprland) moved behind the abstraction inside one binary, then add Sway, before considering plugins or runtime switching.

## Testing and Toolchain Notes
- Keep GNU Make; split the monolith into logical units under `src/` with headers in `src/include/`. Add a `make debug` target and a minimal test harness only for pure‑C core code (animation/easing, config parsing), avoiding new deps.
- For GL/Wayland, write adapter‑facing interfaces that can be exercised with small integration tests or headless backends (EGL surfaceless where possible), but don’t over‑promise coverage.
- Add lightweight logging behind `config.debug` and ensure no logging in hot paths in production.

## Packaging and Distribution
- If plugins are pursued later, default to statically linked adapters in the single binary, with an optional `--enable-plugins` build that installs `.so` files under a root‑owned path only (no user‑writable discovery by default). Document RPATH/loader behavior and versioned adapter ABI.
- Provide distro‑friendly defaults: one binary, no service files unless requested, minimal runtime files.

## Recommendations (Adjusted Plan)
1. Preserve single‑binary design and GNU Make. Defer plugins/dlopen.
2. Introduce internal modules with clear headers under `src/`:
   - `core/` (animation, layers, easing, config, image loading)
   - `gfx/` (shaders, textures, render) – consumes a provided GL context
   - `platform/` (display + graphics context) with Wayland(EGL) implementation
   - `compositor/` (Hyprland IPC first; later Sway/i3 via i3 IPC)
3. Use EGL for Wayland to maintain GLES2 uniformity.
4. Define adapter interfaces as plain C structs with function pointers, but link implementations statically at first; select at runtime by detection.
5. Narrow initial target set to: Hyprland (Wayland) → Sway (Wayland), in that order. Defer GNOME/KDE “wallpaper” until a concrete integration approach is proven.
6. Replace “runtime adapter switching” with “restart on adapter change”. Implement robust teardown/re‑init paths and leak checks.
7. Establish strict hot‑path rules: no allocations, no I/O, no dlopen, no string formatting in `render_frame()` and animation tick. Add a micro‑benchmark target.
8. Add focused tests only for pure core logic (easing, timeline, config) with a tiny in‑tree test harness. Avoid introducing a unit test framework dependency initially.
9. Document compositor support levels and limitations up front; provide clear fallback behavior and error messages.

## Actionable Next Steps (Pragmatic)
- Step 0: Add `src/include/` headers; move easing, animation state, and config parsing out of `hyprlax.c` into `src/core/` and `src/gfx/` while keeping behavior identical. Keep Makefile.
- Step 1: Introduce `platform_wayland.c` (EGL+layer‑shell/xdg‑shell) and a narrow interface the renderer consumes; migrate current Wayland/EGL code there.
- Step 2: Extract Hyprland IPC into `compositor_hyprland.c` behind a simple compositor interface already used by the main loop.
- Step 3: Add Sway/i3 adapter (IPC) reusing the Wayland platform code; share render path, validate workspace events parity.
- Step 5: Only after the above is stable and perf‑clean, consider optional pluginization as a separate milestone.

## Conclusion
The direction is sound, but the plan is broader than necessary and conflicts with current constraints around single‑binary design, toolchain, and GLES2/EGL. Tightening scope, keeping one binary and Make, standardizing on EGL across backends, and deferring plugins/runtime switching will reduce risk while delivering the intended portability with minimal performance overhead.

