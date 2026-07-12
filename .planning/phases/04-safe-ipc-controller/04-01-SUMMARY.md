---
phase: 04-safe-ipc-controller
plan: "01"
subsystem: ipc-controller
tags: [ipc, ownership, delta, subprocess]
requires: [03-pixel-sky-and-shadows]
provides:
  - canonical managed-layer discovery
  - supported one-property IPC command plans
  - inactive dynamic asset switching and delta-only execution
affects: [04-02-cli, final-runtime-proof]
tech-stack:
  added: []
  patterns: [canonical ownership, mark-after-success delta, inactive asset signature]
key-files:
  created: []
  modified: [examples/pixel-city-dynamic/dynamic_scene.py]
key-decisions:
  - Dry-run IDs are explicit standalone assumptions; live IDs always come from list JSON.
  - Asset signatures commit only after all planned IPC changes succeed.
requirements-completed: [IPC-01, IPC-02, CLI-01, OPS-01]
completed: 2026-07-12
---

# Phase 4 Plan 1: Safe IPC Mutation Boundary Summary

Canonical nine-layer ownership, bounded list/modify subprocesses, supported command planning,
inactive moon/shadow generation, and success-marked delta suppression now form the sole runtime
mutation path.

## Delivered

- Validated direct-array `ctl list --json` parsing with positive IDs/nonempty paths.
- Exact canonical matching for six base and three A/B dynamic paths; unrelated paths ignored;
  missing/duplicate/whitespace cases reject.
- Twenty-seven initial one-property commands limited to tint/opacity/blur/path/x/y.
- Asset path writes precede opacity changes; list rediscovery determines active A/B side.
- Per-property delta signatures mark only after successful modify.
- Moon/shadow asset signatures avoid unnecessary rewrites on identical scene state.

## Evidence

- Production module compiles; prior 35 tests remain green.
- Inline list with unrelated ID 99 produced 27 commands and none targeted 99.
- Every property is in the supported allowlist; no clear/saturation/restart/add/remove.
- Marked repeat command plan filtered to zero.
- `git diff --check` passes.

## Next

Plan 04-02 adds operator CLI/loop and mock integration tests, then performs available live smoke.
