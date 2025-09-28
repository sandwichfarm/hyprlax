# Input Modes Refactor - Implementation Plan

This plan is incremental, keeps compatibility, and focuses on minimal, well-scoped changes per step.

## Milestone 0 - Prep & Compatibility
- [ ] Keep existing behavior and public surface intact while scaffolding internals.
- [ ] Map legacy `parallax.mode` to new source set at config load and CLI parse time.
- [ ] Log a deprecation warning when `parallax.mode` is used.

## Milestone 1 - Core Scaffolding
1. Create input manager and provider interface.
   - Files:
     - `src/core/input/input_manager.h`
     - `src/core/input/input_manager.c`
     - `src/core/input/input_provider.h`
   - Key API:
     - `int  input_manager_init(hyprlax_context_t *ctx, input_manager_t *im, const config_t *cfg);`
     - `void input_manager_destroy(input_manager_t *im);`
     - `bool input_manager_tick(input_manager_t *im, monitor_instance_t *monitor, double now, float *out_px_x, float *out_px_y);`
     - `int  input_register(const input_provider_ops_t *ops, input_id_t id);` (static registry)
     - `int  input_manager_set_enabled(input_manager_t *im, input_id_t id, bool enabled, float weight);`
2. Add Input Manager instance to `hyprlax_context_t`.
   - `src/include/hyprlax.h`: embed `input_manager_t input;`
   - Initialize/destroy in `hyprlax_main.c`.
3. Composition logic (weighted sum + clamp) inside Input Manager.
4. Ensure manager stores per-monitor samples (keyed by monitor id) so render path can request the composite offset for the monitor currently being drawn.
5. Extend shared structs so window inversion matches other sources:
   - Add `bool invert_window_x` / `bool invert_window_y` to `config_t` (default false).
   - Add `bool invert_window_x` / `bool invert_window_y` to `parallax_layer_t` (default false) with parsing and serialization deferred to Milestone 5.

## Milestone 2 - Providers (Workspace, Cursor)
1. Workspace Provider
   - Files: `src/core/input/modes/workspace.c`
   - Reuse existing workspace policy math to produce pixel offsets (`workspace_x/y`).
   - `tick()` emits `{x=workspace_x, y=workspace_y, valid=true}`.
2. Cursor Provider
   - Files: `src/core/input/modes/cursor.c`
   - Wrap existing cursor pipeline; `tick()` copies `{cursor_eased_x/y}` converted to pixels using `parallax_max_offset_*`.
   - Preserve current smoothing/easing knobs.
3. Register both providers in a new `input_register_builtin()` called during app init.

## Milestone 3 - Render Integration
1. Modify `src/core/render_core.c` to consume the monitor-aware composite offset instead of `switch(parallax_mode)`.
   - Replace the block computing `offset_x/offset_y` with a call such as `input_manager_tick(&ctx->input, monitor, now_time, &offset_x, &offset_y);`.
   - Keep per-layer multipliers and inversion flags as they are, while adding window inversion hooks so the renderer remains source-agnostic.
2. Ensure `parallax_max_offset_*` remains the final clamp target (manager already clamps; renderer uses normalized per-monitor scaling as today).

## Milestone 4 - Window Provider (New)
1. Files: `src/core/input/modes/window.c`
2. Hyprland first pass:
   - Query active window geometry via IPC (non-blocking, cached for a small TTL; refresh on focus/geometry events if available).
   - Compute window center relative to monitor center => normalized -1..1 => multiply by `parallax_max_offset_*`.
   - Apply deadzone and sensitivity settings.
3. Other compositors: provider compiles but returns `valid=false` when unsupported.

## Milestone 5 - Config, CLI, IPC Wiring
1. Config (`src/core/config.c`, `src/core/config_toml.c`):
   - Add new keys:
     - `parallax.input` list or comma-separated string.
     - `parallax.sources.window.weight`.
     - `parallax.invert.window.x/y` (mirroring existing workspace/cursor inversion toggles).
     - `input.window.sensitivity_x/y`, optional `input.window.ema_alpha`, `input.window.deadzone_px` (mirroring cursor).
   - Legacy mapping:
     - `parallax.mode=workspace|cursor|hybrid` => set enabled sources + default weights (0.7/0.3 for hybrid).
2. CLI (`src/hyprlax_main.c`):
   - New flag: `--input <src>[:weight][,<src>[:weight]]` (repeatable). Examples:
     - `--input workspace`
     - `--input cursor:1.0 --input workspace:0.5`
   - Keep existing `--parallax-mode` (or `--mode`) for compatibility; emit deprecation note and map to sources.
   - Introduce `--invert-window-x` / `--invert-window-y` options mirroring current workspace/cursor inversion flags.
3. Env vars mapping (optional but consistent):
   - `HYPRLAX_PARALLAX_INPUT=workspace,cursor`
   - `HYPRLAX_PARALLAX_SOURCES_WINDOW_WEIGHT=0.25`
4. IPC (`src/hyprlax_main.c`, `src/ipc.c`):
   - GET/SET properties:
     - `parallax.input` (string list)
     - `parallax.sources.window.weight`
     - `input.window.sensitivity_x/y` (and cursor properties unchanged)
   - Preserve existing `parallax.mode` GET/SET as alias with deprecation warning.

## Milestone 6 - Tests & Validation
- Unit tests (where present) for:
  - Config parsing of `parallax.input` and weight keys.
  - Legacy mapping correctness (hybrid => workspace+cursor, not window).
  - Input Manager composition with different weights and enablement.
- Integration tests:
  - Cursor-only, workspace-only, window-only (Hyprland), and combined.
  - IPC enable/disable at runtime; weight adjustments reflected in output.
- Memory checks (`make memcheck`) to ensure no leaks in providers/manager.

## Milestone 7 - Documentation & Deprecation
- Update docs:
  - `docs/animation.md` or a new `docs/input-sources.md` describing sources and examples.
  - CLI usage in `README.md` and examples.
- Deprecation note:
  - Clearly state `parallax.mode` is deprecated and will be removed in a future major release; provide migration examples.

## File Touch Map (anticipated)
- New:
  - `src/core/input/input_provider.h`
  - `src/core/input/input_manager.h`
  - `src/core/input/input_manager.c`
  - `src/core/input/modes/workspace.c`
  - `src/core/input/modes/cursor.c`
  - `src/core/input/modes/window.c`
- Modified:
  - `src/include/hyprlax.h` (add manager in context)
  - `src/include/core.h` (extend inversion fields)
  - `src/core/render_core.c` (use composite offset)
  - `src/hyprlax_main.c` (init/destroy manager, CLI/env/IPC mapping)
  - `src/core/config.c`, `src/core/config_toml.c` (new keys + mapping)
  - `src/ipc.c` (new properties)

## Default Behaviors
- Fresh defaults mirror current visuals:
  - If no explicit input set => `workspace` enabled with weight 1.0.
  - `hybrid` alias => `workspace:0.7`, `cursor:0.3`.
  - `window` disabled by default.

## Risk Mitigation
- Gate window provider behind capability checks; degrade to 0 offset when unsupported.
- Keep per-source code free of allocations in hot paths.
- Maintain existing cursor timer interval and workspace event handling to avoid regressions.
- Add verbose debug logs behind `config.debug` to trace provider outputs and final composite.

## Rollout Strategy
1. Land scaffolding + workspace/cursor providers + renderer integration behind compatibility mapping.
2. Add window provider for Hyprland.
3. Expand compositor support for window provider incrementally.
4. After stabilization, consider surfacing composition strategies if requested.
