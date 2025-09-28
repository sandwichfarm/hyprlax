Cleanup Tasks — Checklist by File

Build and Repo Hygiene
- Makefile
  - Remove `USE_LEGACY` branch and `src/hyprlax.c` from `SRCS`.
  - Ensure `src/core/config_legacy.c` and `src/vendor/toml.c` remain.
  - Add new core files after decomposition (app.c, event_loop.c, cursor.c, render_core.c, config_cli.c).

Legacy Removal and Migration
- src/hyprlax.c
  - Delete from repository or mark deprecated and stop compiling.
- src/hyprlax_main.c
  - Remove runtime legacy `parse_config_file` usage.
  - When `--config` ends with `.conf`, print conversion guidance and exit non‑zero.
  - Remove env bridges intended for legacy (e.g., setting HYPRLAX_DEBUG for adapters) once adapters use logger levels.
- src/core/config_legacy.c (new)
  - Implement legacy reader + TOML writer used by `hyprlax ctl` and startup prompt.
- src/main.c
  - Early detection of legacy config + interactive prompt; handle `--convert-config`, `--yes`, `--continue`.

Decoupling via Caps/ops
- src/include/compositor.h
  - Add caps bitset and optional `get_cursor_position(double*,double*)` to ops; add `uint64_t caps` to adapter instance.
- src/include/platform.h
  - Add ops: `get_window_size(int*,int*)`, `commit_monitor_surface(monitor_instance_t*)`; add `uint64_t caps`.
- src/compositor/*.c
  - Populate caps; implement `get_cursor_position` for Hyprland; set workspace model caps for others.
- src/platform/wayland.c
  - Implement new ops; remove need for externs from core.
- src/hyprlax_main.c
  - Replace backend‑name checks with caps/ops presence; remove externs.

Main Decomposition
- Create and move logic into:
  - src/core/app.c — lifecycle + init_{platform,compositor,renderer} + run/shutdown.
  - src/core/config_cli.c — CLI/env handling.
  - src/core/event_loop.c — epoll/timerfd/debounce.
  - src/core/cursor.c — cursor processing.
  - src/core/render_core.c — update + render per monitor.
  - src/core/config_legacy.c — converter only.

Tests and Docs
- tests/
  - Add `test_legacy_to_toml.c`, `test_cursor_provider.c`, update existing tests to drop legacy runtime path.
- docs/
  - Add migration guide references; update quick start to show TOML.
- examples/
  - Ensure all examples are TOML; delete `.conf` examples.

Release Notes
- Mark legacy runtime deprecated and removed; include exact migration commands.

