Module Decoupling and Registry

Objectives
- Core does not branch on backend names. It queries capabilities and calls ops.
- Backends self‑describe via a registry: name, detect score, caps, and ops.

Registry Model
- For each domain (renderer, platform, compositor):
  - Static list of built‑ins exposing `module_desc { const char* name; uint64_t caps; int (*detect_score)(void); const ops*; }`.
  - Selection: if user passed `--compositor name`, pick by name; else pick the highest detect score.
  - Provide `module_get_by_name()` and `module_auto_select()` utilities.

Capabilities (initial set)
- Compositor caps (examples):
  - `C_CAP_GLOBAL_CURSOR` — has a global cursor provider.
  - `C_CAP_WORKSPACE_LINEAR` — 1D workspace index stream.
  - `C_CAP_WORKSPACE_2D` — 2D grid with x,y indices.
  - `C_CAP_PER_OUTPUT_WS` — per‑monitor workspaces.
  - `C_CAP_TAG_BASED` — tag‑based (River‑like).
- Platform caps:
  - `P_CAP_LAYER_SHELL`, `P_CAP_MULTI_OUTPUT`, `P_CAP_EVENT_FD`, `P_CAP_WINDOW_SIZE_QUERY`, `P_CAP_SURFACE_COMMIT`.
- Renderer caps:
  - `R_CAP_BLUR`, `R_CAP_VSYNC`, `R_CAP_MSAA` (already exist; extend as needed).

Ops Additions
- Compositor ops: optional `get_cursor_position(double*,double*)` to avoid hardcoding Hyprland JSON IPC in core.
- Platform ops: `get_window_size`, `commit_monitor_surface` to eliminate extern calls.

Core Usage Pattern
- Cursor: if (comp.get_cursor_position) use it; else if (platform has global cursor) use it; else disable cursor parallax.
- Workspace: use normalized event struct (already in place) + caps to decide model; remove `workspace_detect_model(ctx->compositor->type)` dependency.

Header/Type Changes
- Keep existing enums for now to avoid churn in adapters, but make core ignore them.
- Add `uint64_t caps` to `compositor_adapter_t` and `platform_t` and propagate from adapters during init.
- Extend `src/include/compositor.h` and `src/include/platform.h` with new ops and a `get_capabilities()` function or a `caps` field exposed via the instance.

Migration Strategy
- Step 1: Add `caps` to adapters; implement getters; do not change behavior.
- Step 2: Rewrite core call sites to rely on caps and ops presence.
- Step 3: Remove the few remaining name checks.

Testing
- Add an adapter ops smoke test asserting presence of `get_event_fd` and `get_capabilities`.
- Unit test for cursor provider selection order (compositor > platform > none).

