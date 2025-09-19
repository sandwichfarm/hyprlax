**Summary**
- The proposal is directionally solid: per-output layer-surfaces, shared textures, and a clear precedence model across global, per-monitor overrides, and per-monitor full configs. The phased rollout (clone → overrides → full configs → groups) fits stability and compatibility goals.
- Main risks are dependency creep (TOML), render scheduling across multiple `wl_surface`/`EGLSurface`, fractional scaling, hot‑plug, and deterministic precedence when patterns overlap. Addressing these explicitly will keep 144 FPS and stability targets.

**Strengths**
- Clean precedence hierarchy including CLI → per-monitor → overrides → groups → global → defaults.
- Shared texture cache strategy to control memory growth across monitors.
- Single process, per-monitor surfaces matches Wayland layering model and compositor expectations.
- Backwards compatibility plan and CLI ergonomics are preserved.

**Gaps/Risks**
- Dependencies: TOML introduces a new dependency, conflicting with “no new deps without discussion.” If adopted, prefer single-header or optional build flag; otherwise extend existing parser.
- Surface scheduling: Naively iterating monitors and `eglSwapBuffers` each frame risks frame-jitter and inconsistent pacing across mixed refresh-rate displays. Must use per-surface frame callbacks (`wl_callback`) and schedule rendering per-output.
- GL context sharing: Shared textures require a shared EGL context (or context sharing). Binding a single context to multiple `EGLSurface`s is ok, but be careful with `eglMakeCurrent` thrash. Without sharing, textures will duplicate per context → large memory hit.
- Fractional scale/transform: Different `wl_output` scales and transforms (rotation) complicate sampling and coordinate math. Need per-monitor projection and careful sampling to avoid blur on integer-scaling displays.
- Hot‑plug: The design mentions hot‑plug questions but not the lifecycle details. Need robust add/remove with full cleanup (surfaces, EGL surfaces, textures refcount, list unlink) and re-resolution of per-monitor config.
- Pattern precedence: Wildcards (e.g., "DP-*") and specific names need deterministic order. Overlapping group + override + explicit monitor must be clearly ordered and test-covered.
- Continuous/panoramic mode: Layer-shell surfaces cannot span outputs. “Continuous” must be implemented as coordinated per-output renders with global coordinates and per-monitor clip/offset. Mixed scales/refresh complicate visual alignment.
- IPC integration: Per-monitor config changes at runtime imply IPC schema extensions and safe reconfigure paths (no blocking texture loads in render loop).

**Technical Recommendations**
- Rendering/scheduling
  - Drive each monitor with its own Wayland frame callback; render only when that output requests a new frame. Maintain a global animation clock, but compute per-output sample time to match the callback cadence.
  - Use a single EGL context shared across all `EGLSurface`s. Sequence per-output: `eglMakeCurrent(surface) → render → eglSwapBuffers`. Keep shader programs and textures resident; avoid re-linking/compiling per output.
  - Add a per-monitor projection/uniform block: viewport size, scale factor, monitor origin, and per-monitor parallax scale/shift. Do not recompile shaders for these differences.
  - Avoid per-frame allocations; cache per-layer VAOs/VBOs and uniforms.

- Texture sharing/cache
  - Implement a global texture cache keyed by absolute path, guarded by a mutex; entries: GL id, size, format, refcount. On hot‑plug removal or config switch, decrement refcounts and delete when 0.
  - If an alternative context is required (not recommended), add explicit path to share via `eglShareContext` at creation; otherwise expect texture duplication.

- Configuration/precedence
  - Deterministic order: CLI > per-monitor full config > per-monitor overrides > group > global > built‑in defaults. For overrides, prefer “most specific wins”: exact name over wildcard; longer wildcard pattern over shorter; stable tie‑break (lexicographic) to avoid nondeterminism.
  - Consider extending the existing `.conf` into a hierarchical INI-like format to satisfy Mode 2 without new deps. If TOML is adopted, gate behind `ENABLE_TOML=1` and vendor a small, single-header parser.
  - Persist a stable monitor identifier: name + EDID hash where available. Expose both in config matching; allow users to pin by EDID for docking scenarios.

- Wayland/platform specifics
  - Bind each `zwlr_layer_surface_v1` to a specific `wl_output`. Set exclusive zone to full screen, correct anchors, and listen for `configure` to handle size/scale changes.
  - Handle transform and scale from `wl_output`; update GL viewport and projection accordingly.
  - Use `wp_presentation` timestamps when available to better align animation time to compositor timing.

- Hot‑plug lifecycle
  - On output add: build `monitor_instance`, resolve config, create surface + EGLSurface, register frame callback, and join render schedule.
  - On output remove: cancel frame callback, destroy layer surface, `eglDestroySurface`, unlink, decrement all texture refs associated via that monitor’s config, and free.
  - Debounce topology changes; during mass changes, pause updates briefly to avoid thrash.

- IPC/runtime control
  - Extend IPC with per-monitor verbs: `set <monitor> <prop> <value>`, `reload <monitor>`, `list-monitors`, `enable/disable <monitor>`. Ensure non-blocking reconfiguration by preloading textures on a worker thread.

**Phasing Adjustments (to de-risk)**
- Phase 1 (clone config):
  - Implement per-output surfaces, shared EGL context, per-output frame callbacks, and texture cache. No new config format yet.
  - Use existing `.conf` globally; forbid per-monitor overrides for now.
- Phase 2 (overrides without TOML):
  - Add minimal sectioned `.conf` (INI-like) to support `[monitor:DP-1]` property overrides. Deterministic wildcard order. Keep parser simple.
- Phase 3 (full per-monitor configs):
  - Option A: Enhanced `.conf` that allows `clear_layers` and inline layers.
  - Option B: Optional TOML (header-only, `ENABLE_TOML`). Provide converter tool when enabled.
- Phase 4 (groups/continuous):
  - Implement panoramic coordination as per-surface offsets; ensure fractional scale alignment. Add tests for visual seam alignment math.

**Testing/Validation Checklist**
- Unit tests
  - Config resolution precedence and wildcard specificity ordering.
  - Texture cache refcounting and deletion on monitor removal/switch.
  - Parser acceptance for legacy `.conf`, sectioned overrides, and (optionally) TOML.
- Integration tests
  - Multi-output lifecycle: add/remove monitors; scale/transform changes; different refresh rates.
  - Per-monitor FPS caps and global FPS cap interactions.
  - IPC: per-monitor set/reload flows without frame hitches.
- Performance
  - 144 FPS target per output with 3–5 layers across 2–3 monitors; measure `render_frame()` budget per surface (<6.9 ms at 144 Hz). Validate no per-frame allocations.
  - GPU memory profile: shared textures vs per-monitor duplication; verify single set of textures in VRAM across outputs.
- Memory safety
  - Valgrind clean on monitor add/remove cycles and config reloads.

**Decision Requests**
- Approve optional TOML path gated via `ENABLE_TOML` and a vendored single-header parser, or prefer extending current `.conf` only.
- Confirm precedence ordering for: CLI > per-monitor full > per-monitor overrides > group > global > defaults.
- Confirm initial default behavior for new outputs: clone global config and enable by default.
- Confirm panoramic “continuous” is implemented via coordinated per-surface rendering (not single stretched surface).

**Conclusion**
- The design aligns well with project goals provided we: (1) prioritize per-output frame scheduling and shared resources; (2) avoid unnecessary dependencies or gate them; and (3) make precedence deterministic and test-covered. Executed in the proposed phases, this should deliver stable, high‑performance multi-monitor support while preserving simplicity for default use cases.

