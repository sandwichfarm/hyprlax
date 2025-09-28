hyprlax_main.c Decomposition Plan

Goals
- Reduce `src/hyprlax_main.c` from ~3k LOC to <1k by extracting cohesive units.
- Remove backend‑name checks and extern hooks into platform from core.

New Files and Responsibilities
- src/core/app.c
  - hyprlax_create/destroy/init/run/shutdown
  - Calls into init_platform/init_compositor/init_renderer and event loop.
- src/core/config_cli.c
  - CLI + ENV parsing into `config_t`.
  - No I/O beyond argv/env; returns a struct of parsed layer args for later resolution.
- src/core/event_loop.c
  - epoll/timerfd creation, frame pacing, debounce timer.
  - Dispatches to callbacks: platform event, compositor event, ipc request, frame tick.
- src/core/cursor.c
  - `process_cursor_event()` and smoothing/easing state.
  - Prefers `compositor->ops->get_cursor_position`, else platform fallback, else disabled.
- src/core/render_core.c
  - `hyprlax_update_layers`, `hyprlax_render_frame`, and per‑monitor render path.
  - All GL/renderer calls via renderer ops; no platform/compositor assumptions.
- src/core/config_legacy.c (converter only)
  - Legacy reader + TOML writer for migration; not used in runtime load.

Core Changes in Place
- Remove:
  - extern Wayland helpers from core; add platform ops: `get_window_size`, `commit_monitor_surface`.
  - All `if (ctx->compositor->type == COMPOSITOR_*)` and `if (ctx->platform->type == PLATFORM_*)` guards.
- Replace with capability queries + ops presence checks.

Function Extraction Map (from hyprlax_main.c)
- Argument parsing: `parse_arguments` → core/config_cli.c
- Legacy file parser: `parse_config_file` → core/config_legacy.c (converter only)
- Event/timer helpers, epoll wiring: `create_timerfd_monotonic`, `hyprlax_setup_epoll`, `hyprlax_arm_*`, `hyprlax_clear_timerfd` → core/event_loop.c
- Cursor processing: `cursor_apply_sample`, `process_cursor_event` → core/cursor.c
- Per‑monitor render: `hyprlax_render_monitor` → core/render_core.c
- Render frame orchestration: `hyprlax_render_frame` → core/render_core.c
- Layer texture lazy loads: `hyprlax_load_layer_textures` → core/render_core.c (or a small renderer util)
- Platform/compositor/renderer init: `hyprlax_init_*` → core/app.c

API Additions Required
- platform_ops:
  - int get_window_size(int* w, int* h)
  - void commit_monitor_surface(monitor_instance_t* m)
- compositor_ops:
  - int get_cursor_position(double* x, double* y)  // optional
  - uint64_t get_capabilities(void)                 // caps bitset

Risks & Sequencing
- Introduce ops/caps first (no behavior change), then migrate core call sites, then delete extern hooks.
- Keep names/signatures stable for external tests while moving code; update includes.

