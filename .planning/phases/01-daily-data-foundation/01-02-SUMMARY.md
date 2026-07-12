---
phase: 01-daily-data-foundation
plan: "02"
subsystem: daily-data-tests
tags: [unittest, multiprocessing, fixtures, script-tests]
requires: [01-01]
provides:
  - deterministic provider and cache contract coverage
  - real-process same-day success and failure race proof
  - repository script-test integration
affects: [all-later-phases, final-verification]
tech-stack:
  added: []
  patterns: [injected provider fixtures, explicit process synchronization, network guard]
key-files:
  created:
    - tests/test_pixel_city_dynamic.py
    - tests/test_pixel_city_dynamic.sh
  modified: []
key-decisions:
  - Use forked Linux processes with an Event and bounded joins to prove flock behavior.
  - Keep provider tests entirely local and make unplanned network access fail immediately.
requirements-completed: [BASE-01, BASE-02, GEO-01, GEO-02, ASTRO-01, ASTRO-02, CACHE-01, CACHE-02]
completed: 2026-07-12
---

# Phase 1 Plan 2: Daily Data Verification Summary

Twenty deterministic tests now prove the copied config, provider validation, manual bypass, strict
same-day failure/restart behavior, wrong-location isolation, stale fallback, polar/null handling,
bounded HTTP reads, and two-process request ceilings without network or Wayland.

## Delivered

- Added `unittest` fixtures for valid/invalid ip-api and Sunrise-Sunset v2 response shapes.
- Added direct cache tests for same-date success/failure, next-day stale fallback, canonical/input
  timezone aliases, private schema-v1 files, and location-identity isolation.
- Added real multiprocessing success and failure races; both prove exactly one fetch and use bounded
  joins with forced termination on hangs.
- Added a fail-closed opener guard and oversized response fixture.
- Added an executable `test_*.sh` wrapper discovered by the repository's `make test-scripts` target.

## Deviations

- `.gitignore` broadly ignores `tests/test_*`, so the Python test must be force-added just like a
  source fixture; the executable `.sh` wrapper is not ignored. The ignore file remains unchanged.
- The Python test module registers the importlib-loaded dataclass module in `sys.modules`; the
  original plan's one-line loader omitted that step and fails on current Python 3.14.

## Verification Evidence

- `python3 -m unittest -v tests/test_pixel_city_dynamic.py` — 20 tests, all OK.
- `tests/test_pixel_city_dynamic.sh` — 20 tests plus six-layer TOML assertion, exit 0.
- `make test-scripts` — all 5 executable shell suites passed, including the new suite.
- `python3 -m py_compile ...dynamic_scene.py tests/test_pixel_city_dynamic.py` — exit 0.
- `bash -n tests/test_pixel_city_dynamic.sh` — exit 0.
- `git diff --check` — exit 0.
- The build performed by `make test-scripts` completed with pre-existing compiler warnings in
  unchanged C files; no new C source was modified.

## Phase Readiness

Phase 1 implementation and direct test coverage are complete. A separate GSD verifier must audit
the eight requirement IDs and current artifacts before the roadmap advances to Phase 2.
