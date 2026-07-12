---
phase: 04-safe-ipc-controller
plan: "02"
subsystem: controller-cli-tests
tags: [argparse, dry-run, retry, mock-ipc]
requires: [04-01]
provides:
  - executable once, loop, status, fixed-time, and fixed-location CLI
  - deterministic side-effect-free command preview
  - controller ownership, delta, subprocess, and retry regression coverage
affects: [05-operational-proof-and-delivery]
tech-stack:
  added: []
  patterns: [injected boundaries, structured JSON output, next-tick retry]
key-files:
  created: []
  modified: [examples/pixel-city-dynamic/dynamic_scene.py, tests/test_pixel_city_dynamic.py]
key-decisions:
  - Dry-run uses neutral astronomy and assumed IDs without constructing network or IPC clients.
  - Loop failures retry on the next bounded tick and preserve the daily provider gate.
requirements-completed: [IPC-01, IPC-02, CLI-01, OPS-01]
completed: 2026-07-12
---

# Phase 4 Plan 2: Controller CLI and Failure Verification Summary

The executable sidecar now exposes one-shot, long-running, status, manual-location, fixed-time,
and deterministic preview modes while keeping all external boundaries injectable and bounded.

## Delivered

- Aware ISO-8601 parsing, complete/manual location validation, and 15..3600 second cadence bounds.
- Structured status and apply output with cache/provider freshness and bounded diagnostics.
- A preview path that does not instantiate HTTP or IPC clients and does not write cache/assets.
- Clean interrupt handling and next-tick recovery after provider, cache, generation, or IPC errors.
- Nine mock integration tests covering canonical ownership, unrelated ID isolation, exact argv,
  timeout/nonzero propagation, supported command order, success-only deltas, and retry behavior.

## Evidence

- Focused controller suite: 9/9 OK.
- Full example suite: 44/44 OK plus executable TOML wrapper.
- Dry-run readback: preview source, assumed IDs, and exactly 27 allowlisted commands.
- Generated file bytes and mtimes plus a nominated cache path remain unchanged in dry-run.
- Injected loop failure succeeds on the following tick while astronomy is fetched once.
- Python compilation and `git diff --check` pass.

## Next

Phase verification closes the four IPC/operation requirements. Phase 5 documents deployment and
provider policy, runs repository-wide proof, performs the available real IPC smoke, and publishes
the new branch as a PR.
