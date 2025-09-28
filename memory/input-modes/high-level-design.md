# Input Modes Refactor - High-Level Design

## Goals
- Replace single `parallax_mode` (workspace | cursor | hybrid) with a modular, composable input system.
- Enable multiple input sources simultaneously; "hybrid" becomes a consequence of enabled sources, not a distinct mode.
- Add a new `window` input source alongside existing `workspace` and `cursor`.
- Keep backward compatibility for CLI, env vars, config, and IPC (with deprecation path).
- Provide a clear pattern to add future input sources without touching render logic.

## Non-Goals
- Implement compositor-specific window geometry fetch for every compositor in this change. The interface must allow it, but initial support can land for Hyprland first and gracefully no-op on unsupported compositors.
- Change visual composition math beyond making it source-based and weighted. Defaults should retain current look where possible.

## Concepts

### Input Source
An input source produces a 2D offset sample in logical pixels (or normalized space) that contributes to the final parallax offset per layer. Examples:
- `workspace`: offset driven by workspace changes and policies.
- `cursor`: offset driven by smoothed cursor motion.
- `window`: offset derived from active window position/changes (new).

Each source is an independent provider with its own state, configuration, and optional smoothing/easing.

### Input Manager
Central coordinator that:
- Registers input providers (static registry at startup).
- Enables/disables providers per configuration and IPC.
- Pulls/ticks providers and aggregates their samples using a composition strategy.
- Exposes monitor-scoped blended offsets to the renderer in pixel space (one composite per active monitor instance).

### Composition Strategy
Default: weighted sum with clamping to `parallax_max_offset_{x,y}`. Future strategies (optional): max, priority, last-writer. The manager applies global per-source weights and per-layer multipliers, then clamps.

## Data Model

### Identifiers
```
input_id_t {
  INPUT_WORKSPACE = 0,
  INPUT_CURSOR    = 1,
  INPUT_WINDOW    = 2,
  INPUT_MAX
}
```

Also a name string for registry lookups: "workspace", "cursor", "window".

### Sample
```
typedef struct {
  float x;      // pixels (preferred) or normalized
  float y;
  bool  valid;  // false => treated as {0,0}
} input_sample_t;
```

### Provider Ops (C API)
```
typedef struct input_provider_ops {
  const char *name;
  int  (*init)(hyprlax_context_t *ctx, void **state);
  void (*destroy)(void *state);
  int  (*on_config)(void *state, const config_t *cfg);
  int  (*start)(void *state);                // subscribe to events / start timers
  int  (*stop)(void *state);
  bool (*tick)(void *state, double now, input_sample_t *out);
  // Optional: normalize output to pixels here; if normalized, provider must mark it in docs.
} input_provider_ops_t;
```

### Input Manager State
```
typedef struct input_manager {
  uint32_t enabled_mask;                 // bit per INPUT_*
  float    weights[INPUT_MAX];           // 0..1 per source
  void    *states[INPUT_MAX];            // provider private states
  const input_provider_ops_t *ops[INPUT_MAX];
  int      compose_strategy;             // default: weighted sum
  input_sample_t monitor_cache[MAX_MONITORS]; // cached composites per monitor
} input_manager_t;
```

`MAX_MONITORS` reuses the core monitor limit (currently 8). Each cache entry stores the blended pixel offset for the monitor ID matching its index. The manager refreshes these entries during `tick()` so render code can request the value with the active `monitor_instance_t`.

### Configuration Extensions
- Replace single `parallax_mode` switch with a set of enabled sources + weights.
- Keep legacy fields for compatibility, mapping them in load/parse steps.
- Extend per-source inversion controls so window (and future providers) can mirror current workspace/cursor inversion semantics at both global and layer levels.
  - Global config keys: `invert_window_x`, `invert_window_y` (default false).
  - Layer struct fields: `invert_window_x`, `invert_window_y` (default false), exposed via TOML/CLI similar to existing cursor/workspace flags.

New conceptual keys (names shown for CLI/TOML/IPС mapping; see Implementation Plan for exact wiring):
- `parallax.input = ["workspace", "cursor", "window"]` (or comma-separated string; repeats supported)
- `parallax.sources.workspace.weight = 0.7`
- `parallax.sources.cursor.weight = 0.3`
- `parallax.sources.window.weight = 0.0` (default off)

Existing per-source settings remain (e.g., cursor sensitivity/easing/invert flags). Window source inherits new inversion toggles plus sensitivity and smoothing similar to cursor.

### Backward Compatibility
- CLI/env/config `parallax.mode=workspace|cursor|hybrid` are accepted and mapped to enabled sources:
  - workspace  => enable {workspace:1.0}
  - cursor     => enable {cursor:1.0}
  - hybrid     => enable {workspace:0.7, cursor:0.3}
- The current bug where "hybrid" behaves as cursor+window disappears because the alias maps to workspace+cursor explicitly.
- Emit a deprecation warning for `parallax.mode` suggesting `--input`/`parallax.input`.

## Event & Data Flow
1. Startup: register built-in providers (workspace, cursor, window). Input Manager created with config mapping.
2. Providers `init` and `start`: subscribe to compositor events or arm timers (cursor provider keeps existing timer; workspace hooks remain; window subscribes to active window and geometry changes if supported).
3. Main loop: Input Manager `tick(now, monitor)` pulls samples from enabled providers and updates the cache entry for each active monitor.
4. Composition: sum(weights[i] * sample[i]) per axis; clamp to `parallax_max_offset_*` per monitor before storing in the cache.
5. Renderer: consumes the composite offset per frame by reading the cached value for the monitor being drawn; per-layer multipliers (`shift_multiplier[_x|_y]`) still apply downstream.

## Providers (First Set)

### Workspace Provider
- Uses existing workspace change handling and policy. Produces pixel offsets equivalent to current `workspace_x/y` calculation.
- Respect global/layer inversion flags for workspace in the render stage (unchanged), or optionally pre-invert within provider if needed (keep current render-stage inversion for now).

### Cursor Provider
- Reuse existing cursor smoothing/easing pipeline; produce normalized -1..1 converted to pixels using `parallax_max_offset_*` and per-layer multipliers in the render stage.
- Initialization stays tied to timerfd updates when enabled or when legacy compatibility requires it.

### Window Provider (New)
- Concept: offset based on active window position relative to monitor center. Example mapping:
  - Compute window center `(wx, wy)` in compositor coordinates for the focused window on the active monitor hosting the wallpaper surface.
  - Normalize relative to monitor center to -1..1 (with deadzone and sensitivity), then convert to pixels similar to cursor.
- Hyprland: implement via IPC queries for active window geometry. Other compositors: no-op if unsupported (provider `tick` returns valid=false).
- Config knobs (initial set): `input.window.sensitivity_x/y`, optional EMA smoothing, deadzone in pixels.

## Renderer Integration
- Replace `switch(parallax_mode)` in `render_core.c` with a composite offset fetched from the Input Manager for the monitor being rendered.
- Maintain per-layer scaling and inversion semantics as-is, expanding inversion toggles to cover window provider output so layering logic remains source-agnostic.

## IPC & Runtime Control
- New/extended properties:
  - `parallax.input` (string list) - get/set enabled sources.
  - `parallax.sources.<name>.weight` - existing cursor/workspace kept; add `window`.
  - Optional: `input.<name>.*` for provider-specific tuning (e.g., `input.cursor.sensitivity_x`).
- `hyprlax ctl` mirrors these via existing GET/SET pathways.

## Failure Modes & Safety
- Providers must validate compositor capabilities and return `valid=false` if unavailable.
- Input Manager treats invalid samples as zero.
- Weights are clamped to [0..1].
- No blocking operations in tick paths; IPC calls for window geometry must be non-blocking and cached with timeouts.

## Performance Considerations
- Providers avoid allocations in tick path; use static/stack storage.
- Cursor timer frequency unchanged; no extra timers per provider unless necessary.
- Composition is O(N) over a small N (≤ 5), trivially under budget for 144 FPS.

## Extensibility Pattern
- New providers are added by implementing `input_provider_ops_t`, registering in `input_register_builtin()`, and wiring optional config/IPC keys.
- Render code remains unchanged for new sources; only Input Manager updates.
