---
phase: 03-pixel-sky-and-shadows
plan: "02"
subsystem: generated-visual-tests
tags: [png-crc, pixel-metrics, shadow-centroid, buffers]
requires: [03-01]
provides:
  - independent codec and generated-pixel verification
  - directional/elevation shadow regression coverage
  - atomic A/B identity regression coverage
affects: [04-safe-ipc-controller, final-verification]
tech-stack:
  added: []
  patterns: [decoded alpha energy, bright-pixel centroid, corrupt chunk fixtures]
key-files:
  created: []
  modified: [tests/test_pixel_city_dynamic.py, tests/test_pixel_city_dynamic.sh]
key-decisions:
  - Preserve six-base-layer assertion while allowing the three intentional dynamic overlays.
requirements-completed: [ASSET-01, SHADOW-01, ASSET-02]
completed: 2026-07-12
---

# Phase 3 Plan 2: Generated Visual Verification Summary

Six independent generated-asset tests extend the full suite to 35 cases and directly measure PNG
roundtrip/CRC behavior, celestial alpha geometry, shadow direction/energy/absence, atomic A/B
identities, committed initial assets, and exact config order.

## Evidence

- New moon has zero bright pixels; first quarter has fewer than full; waxing centroid is right of
  center and waning centroid is left.
- Morning shadow alpha centroid lies right of afternoon; low-sun alpha energy exceeds noon; hidden
  sun produces zero alpha.
- Source and three committed assets decode as 576x324 RGBA.
- A→B→A writes decode after every atomic replacement.
- Full Python suite: 35/35 OK; shell wrapper confirms six base layers and 9 total paths.

## Deviation

The shell wrapper and original copied-config test were broadened from exactly six total layers to
six required base layers plus all-valid configured paths. The first Phase 3 run correctly failed
the stale six-total assertion, proving the regression gate was active; it now recognizes only the
three intentional overlays and the separate exact-order test fixes total count at nine.

## Phase Readiness

Generated visuals are ready for requirement verification and then managed-only IPC consumption.
