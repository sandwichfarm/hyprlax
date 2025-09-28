Hyprlax Cleanup and Modularization — High‑Level Design

Goals
- Remove the legacy, monolithic path (`src/hyprlax.c`, legacy config loader/runtime).
- Migrate to a single TOML configuration format with a safe, interactive converter.
- Detect legacy configs at startup, offer conversion (y/n), then guide with the new command.
- Decompose `src/hyprlax_main.c` into focused modules; keep the render loop lean.
- Fully decouple core from backend names. Core manages modules via capabilities and ops, not by hardcoding backend names.

Non‑Goals
- No new rendering effects or compositor features beyond what’s needed for cleanup.
- No behavior changes to IPC or CLI other than config migration prompts and flags.

Current Pain Points
- Monolithic `hyprlax_main.c` (~3k LOC) intertwines: CLI parsing, config I/O, event loop, compositor/platform specifics, and rendering orchestration.
- “Legacy” code paths remain in build and runtime (`USE_LEGACY`, `src/hyprlax.c`, legacy config parser, env var bridges).
- Core checks backend names and calls extern platform functions (e.g., Wayland) instead of using explicit ops/capabilities.
- Duplicate config handling (legacy vs TOML) increases surface area and risk.

Target Architecture
- Core (platform‑agnostic): app runtime, event loop, config, layers, animations, parallax math, render orchestration.
- Modules (plug‑in style): renderer(s), platform(s), compositor(s), each with ops and a capability bitset.
- Registry/selection layer: enumerate available modules, query detect() + capabilities, and select best match or a user‑requested backend by name. Core never references backend names directly for behavior — it queries capabilities.

Module Capabilities (examples)
- Compositor: GLOBAL_CURSOR, WORKSPACE_LINEAR, WORKSPACE_2D, PER_OUTPUT_WORKSPACE, TAG_BASED, IPC_EVENTS, BLUR_TOGGLE.
- Platform: LAYER_SHELL, MULTI_OUTPUT, WINDOW_SIZE_QUERY, SURFACE_COMMIT, EVENT_FD.
- Renderer: BLUR, VSYNC, MULTISAMPLING.

Normalized Ops Additions (key deltas)
- Compositor ops: optional get_cursor_position(double* x, double* y), emits normalized workspace events (unified struct already exists; adapters fill 2D vs linear fields appropriately).
- Platform ops: get_window_size(int* w, int* h), commit_monitor_surface(monitor_instance_t*), plus existing get_event_fd.
- Core code paths switch from if (type == HYPRLAND/WAYLAND) to capability/ops checks.

Configuration Strategy
- Single source of truth: TOML. File path default: `~/.config/hyprlax/hyprlax.toml`.
- Legacy format support is conversion‑only. Remove runtime loading of legacy files.
- Converter maps the simple legacy keys: duration, shift, fps, vsync, easing, idle_poll_rate, and `layer <path> [shift [opacity [blur]]]` to TOML under `[global]` and `[[global.layers]]`.
- Legacy global `scale` is mapped to per‑layer `scale` (applied uniformly to every layer).

Startup Flow with Migration
1. Parse CLI/env for early help/version and non‑interactive flags (e.g., `--yes`, `--convert-config`).
2. If `--config` points to legacy (`.conf`) or a default legacy path exists, and we are interactive (isatty):
   - Offer conversion (y/n) with a clear destination path (default `~/.config/hyprlax/hyprlax.toml`).
   - On success, print the exact new command: `hyprlax --config <new.toml>`.
   - Abort startup after conversion unless `--continue` is specified.
3. If non‑interactive, refuse to auto‑convert and print a one‑liner with the command to run.

hyprlax_main Decomposition (ownership boundaries)
- core/app.c: lifecycle (create/init/run/shutdown), minimal orchestration.
- core/config_cli.c: CLI+ENV parsing to `config_t` (no I/O beyond argv/env).
- core/config_legacy.c: legacy reader (for conversion only) and TOML writer.
- core/event_loop.c: epoll/timerfd wiring, debounce logic; provides callbacks for platform/compositor/ipc events.
- core/cursor.c: cursor sampling, smoothing, easing; sources from compositor or platform via capability‑gated ops.
- core/render_core.c: update tick + render orchestration across monitors; pure use of renderer ops.
- platform/*, compositor/*, renderer/* remain isolated, exporting ops/caps and registering with the registry.

Core Does Not Know Backend Names
- No checks for `COMPOSITOR_HYPRLAND`, `PLATFORM_WAYLAND`, etc. Replace with capability queries, e.g. `if (comp->ops->get_cursor_position || (comp->caps & GLOBAL_CURSOR))`.
- No direct extern calls into Wayland helpers from core; everything via platform ops.

Build/Runtime Changes
- Remove `USE_LEGACY` and the monolithic `src/hyprlax.c` from build.
- Keep legacy parser only inside `core/config_legacy.c` to implement migration; not part of normal startup.
- Ensure tests and examples use TOML only.

User Experience
- Clear, one‑time automated migration with safety: backup `.conf`, idempotent re‑runs, prominent “new command” output.
- `hyprlax ctl reload` continues to reload TOML only.
- Optional `hyprlax ctl convert-config [src] [dst]` for scripted migrations.

Testing and Perf
- Unit tests for legacy→TOML converter including relative paths and defaults.
- Integration tests for CLI detection and prompt logic (TTY mocked) and non‑interactive behavior.
- Render loop unaffected; perf budget remains under ~7 ms per frame.

Risks and Mitigations
- Interactive prompts in autostart: default to non‑interactive refusal with instructions; provide `--yes` & `--convert-config`.
- Path resolution: keep resolution rules consistent with current loaders; prefer relative paths in TOML when dst is under the same directory, otherwise preserve absolute.
- Adapter capability drift: introduce caps with conservative defaults; adapters can opt‑in incrementally without breaking.

