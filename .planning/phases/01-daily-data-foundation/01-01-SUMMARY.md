---
phase: 01-daily-data-foundation
plan: "01"
subsystem: daily-data
tags: [python, cache, ip-api, sunrise-sunset, toml]
requires: []
provides:
  - valid copied six-layer Pixel City scene
  - process-shared daily provider reservation and atomic cache
  - validated manual/ip-api location and combined solar/lunar astronomy normalization
affects: [02-astronomical-scene-model, 04-safe-ipc-controller]
tech-stack:
  added: []
  patterns: [locked attempt ledger, atomic JSON replace, stale-last-good fallback]
key-files:
  created:
    - examples/pixel-city-dynamic/parallax.toml
    - examples/pixel-city-dynamic/dynamic_scene.py
    - examples/pixel-city-dynamic/1.png
    - examples/pixel-city-dynamic/2.png
    - examples/pixel-city-dynamic/3.png
    - examples/pixel-city-dynamic/4.png
    - examples/pixel-city-dynamic/5.png
    - examples/pixel-city-dynamic/6.png
  modified: []
key-decisions:
  - Provider/date reservation is absolute; location identity only controls safe cache reuse.
  - Preserve both canonical and input attempt dates to close the pre-geolocation timezone race.
  - Skip astronomy entirely when no valid location exists and use neutral fallback.
requirements-completed: [BASE-01, BASE-02, GEO-01, GEO-02, ASTRO-01, ASTRO-02, CACHE-01, CACHE-02]
completed: 2026-07-12
---

# Phase 1 Plan 1: Daily Data Production Boundary Summary

Valid copied Pixel City base plus a Python standard-library provider/cache boundary that reserves
attempts under `flock` before I/O, atomically preserves last-good facts, and normalizes combined
solar/lunar daily data.

## Delivered

- Copied the six used 576x324 Pixel City layers into `examples/pixel-city-dynamic` and rebuilt the
  TOML with the invalid source `0.0VV` corrected to `0.0`.
- Added a mode-0600, schema-v1 JSON cache using same-directory temp files, fsync, atomic replace,
  and a process-shared lock held through the single provider attempt.
- Added bounded standard-library JSON HTTP handling (10 seconds, 256 KiB, identified User-Agent).
- Added strict location/IANA-zone and astronomy/date/event/solar/moon validation.
- Added manual location bypass, stale last-good behavior, wrong-location isolation, and exact neutral
  fallback anchors.

## Deviations

- The GSD external autonomous runner and native executor were invoked first but could not execute:
  the external profile returned HTTP 401 and the native executor did not begin within the bounded
  worker window. The documented inline executor fallback completed the same plan.
- Added `last_attempt_input_date` beyond the initial schema sketch. It is necessary when a second
  process computes the system-local date before the first geolocation response normalizes the date
  into the resolved remote timezone.
- Skipped astronomy calls when location is fallback-only. Querying 0,0 would waste the daily
  astronomy attempt and present invented place data as provider-backed.

## Verification Evidence

- `python3 -m py_compile examples/pixel-city-dynamic/dynamic_scene.py` — exit 0.
- TOML/layer assertion — 6 parsed layers and all six paths exist.
- `git diff --exit-code -- examples/pixel-city` — original example untouched.
- Inline cache smoke — same-day success and same-day failure each invoked the fetch callback once.
- Neutral record readback — exact 05:30/06:00/12:00/18:00/18:30 anchors, New Moon, 0%.
- `git diff --check` — exit 0.

## Remaining Phase Work

Plan 01-02 must produce deterministic unit and real-process concurrency coverage before any Phase 1
requirement is considered phase-verified.
