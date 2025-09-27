# Hyprlax Phased Plan

This document tracks planned work across phases, what’s done, and what’s next.

## Phase 0 — UX/Docs Alignment (Complete)
- Clarify ctl modify x/y as normalized UV pan (with examples and ranges).
- Update `hyprlax ctl --help`, README, and add docs/IPC.md.
- Make `ctl list` show UV pan and key per-layer info.

## Phase 1 — IPC Bridging & Parity (Complete)
- Bridge IPC add/remove/modify/list to live renderer layers.
- Property aliases: `fps`, `shift`, `duration`, `easing` map to live config.
- `status` returns real runtime values; add `--json` output.
- `reload` via TOML config path.
- Per-layer controls: `blur`, `fit`, `content_scale`, `align_x/y`, `overflow`, `tile.x/y`, `margin.x/y`, `hidden`.
- Z-order: `front`, `back`, `up`, `down` with normalized z.
- List formats: compact (default), `--long`, `--json`; filters (`id=`, `hidden=`).

## Phase 2 — Runtime/UX Enhancements (In Progress)
1) Frame pacing improvements
   - Re-arm timerfd when `target_fps` changes (in addition to per-loop gate recompute).

2) Rich status JSON
   - Add `monitors[]` with name, size, position, scale, refresh.
   - Include per-monitor compositor capability flags.

3) Reload fallback
   - If TOML path isn’t set, attempt legacy config reload; report success.

4) List filters & ergonomics
   - Already shipped `id=` and `hidden=`; consider `path~=` and z-range.
   - Optional `--cols=` for compact view (defer unless needed).

5) Minor polish
   - Keep help/docs aligned with added properties and flags.

Notes: Build often; avoid over-investment in unit tests while IPC API evolves. Ensure main binary builds cleanly and features are exercised via ctl.

## Phase 3 — Hygiene/Robustness (Planned)
- Respect `ipc_enabled` to allow disabling IPC.
- Socket path hardening: prefer `$XDG_RUNTIME_DIR`, add session suffix.
- Control client unification and deprecation path for old CLI, if any.
- Refactor nested functions and duplicated helpers in runtime property code.
- Add targeted tests for new IPC flags when the API stabilizes.

