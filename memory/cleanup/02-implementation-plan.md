Hyprlax Cleanup and Modularization — Implementation Plan

Phase 0 — Baseline and Guardrails
- Freeze legacy behavior for reference; add a temporary `HYPRLAX_INIT_TRACE=1` path to validate startup sequencing (already present).
- Confirm TOML examples and docs reflect intended defaults.

Phase 1 — Legacy Config Converter (standalone and library)
- Add `src/core/config_legacy.c` with:
  - int legacy_config_read(const char* path, legacy_cfg_t* out) — reuse existing token rules.
  - int legacy_config_to_toml(const legacy_cfg_t* in, const char* dst_path, const char* base_dir) — write `[global]` and `[[global.layers]]`.
  - Path policy: if legacy path is under `~/.config/hyprlax/`, write `hyprlax.toml` adjacent; else default to `~/.config/hyprlax/hyprlax.toml`.
  - Map legacy `scale` to per‑layer `scale` uniformly.
- Add a small CLI in `hyprlax ctl`:
  - `hyprlax ctl convert-config [SRC] [DST] [--yes]` using the above functions.
  - Exit codes: 0 success, 1 invalid input, 2 I/O error.

Phase 2 — Startup Detection and Prompt
- In early `main` startup (before creating the app context):
  - Detect: `--config` points to `.conf` OR default legacy `~/.config/hyprlax/parallax.conf` exists AND no TOML specified.
  - If interactive (isatty(stdin)) and not `--no-interactive`:
    - Prompt: “Found legacy config at X. Convert to TOML at Y? [y/N]”.
    - On yes: run converter, then print “Run: hyprlax --config Y”. Exit 0 unless `--continue` given.
  - If non‑interactive: print one‑liner with the exact conversion command and exit with status 3.
- Flags:
  - `--convert-config [SRC] [DST]` — do conversion then exit.
  - `--yes` — answer yes to prompts.
  - `--continue` — continue startup after successful conversion using the new TOML path.

Phase 3 — Remove Legacy Runtime Path
- Makefile: remove `USE_LEGACY` branch; stop compiling `src/hyprlax.c`.
- `hyprlax_main.c`:
  - Remove runtime legacy parsing and the reload fallback for `.conf`.
  - Keep a small shim that, when given a `.conf`, fails with a clear message suggesting `hyprlax ctl convert-config`.
- Tests/examples/docs: purge `.conf` examples; ensure TOML only.

Phase 4 — Capability‑Based Decoupling
- Compositor
  - Add `caps` bitset and optional `get_cursor_position(double*,double*)` to `compositor_ops`.
  - Migrate Hyprland adapter to implement `get_cursor_position` (via IPC it already does) and set `GLOBAL_CURSOR` and `WORKSPACE_LINEAR`/`PER_OUTPUT_WORKSPACE` caps.
  - For Wayfire/Niri/River/Sway adapters, set appropriate workspace model caps.
- Platform
  - Add ops: `get_window_size(int*,int*)`, `commit_monitor_surface(monitor_instance_t*)`.
  - Implement in Wayland platform using existing helpers currently called via externs.
- Core
  - Replace all checks of `compositor->type == ...` or `platform->type == ...` with capability/ops checks.
  - Use compositor `get_cursor_position` if available; else platform fallback; else disable cursor parallax.

Phase 5 — hyprlax_main Decomposition
- Create files and move code:
  - `src/core/app.c` — `hyprlax_create/init/run/shutdown` orchestration.
  - `src/core/config_cli.c` — finalize CLI/env handling.
  - `src/core/event_loop.c` — epoll, timerfd, debounce, dispatch.
  - `src/core/cursor.c` — sampling, smoothing, easing state machine.
  - `src/core/render_core.c` — `hyprlax_update_layers`, `hyprlax_render_frame`, per‑monitor draw.
  - `src/core/config_legacy.c` — converter only (no runtime loading).
- Keep function signatures stable; include new headers from `src/include/`.

Phase 6 — Tests and Validation
- Unit tests:
  - `tests/test_legacy_to_toml.c` — migration mapping, idempotency, and path handling.
  - `tests/test_cursor_provider.c` — prefer compositor provider, fallback to platform.
  - Update existing tests to remove dependencies on legacy parser.
- Integration tests:
  - CLI prompt logic (mock TTY), non‑interactive refusal, `--yes`, `--continue`.
- Run `make test`, `make memcheck`, and integration runs on Hyprland and generic Wayland.

Phase 7 — Docs and Communication
- Update docs: getting started, TOML reference, migration guide with exact commands and examples.
- Changelog: mark legacy path removed; add converter; detail new CLI flags.

Milestones and Rollout
- M1: Converter + CLI available; warn on legacy use.
- M2: Core decoupling (caps + ops) with adapters updated.
- M3: Main decomposition complete; `hyprlax_main.c` < 1k LOC.
- M4: Remove legacy runtime; require TOML.

Risk Management
- Keep converter as a supported utility indefinitely.
- Maintain adapter caps conservative defaults to avoid behavior regressions.
- Gate interactive prompt behind TTY detection; provide `--yes` for automation.

