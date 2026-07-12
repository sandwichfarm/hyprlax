---
phase: 02-astronomical-scene-model
plan: "01"
subsystem: scene-model
tags: [lighting, astronomy, interpolation, moon]
requires: [01-daily-data-foundation]
provides:
  - continuous six-state lighting and supported per-layer looks
  - bounded sun and cross-midnight moon trajectories
  - moon-illumination-scaled night fill
affects: [03-pixel-sky-and-shadows, 04-safe-ipc-controller]
tech-stack:
  added: []
  patterns: [smoothstep keyframes, adjacent-day lunar interval candidates, frozen scene values]
key-files:
  created: []
  modified: [examples/pixel-city-dynamic/dynamic_scene.py]
key-decisions:
  - Keep semantic phase labels interval-based while numeric values interpolate continuously.
  - Hide moon unless both rise and set define an interval; never invent the opposite event.
requirements-completed: [LIGHT-01, LIGHT-02, LIGHT-03, SKY-01, SKY-02]
completed: 2026-07-12
---

# Phase 2 Plan 1: Astronomical Scene Model Summary

Pure timezone-aware SceneState calculation now turns daily astronomy into six supported per-layer
looks, continuous named lighting, bounded sun/moon motion, and illumination-scaled lunar night fill.

## Delivered

- Frozen `LayerLook`/`SceneState` values with explicit ambient, stars, city-lights, colorfulness,
  celestial visibility/progress/UV/opacity, phase, illumination, and lunar fill.
- Smoothstep numeric keyframes across night, sunrise, morning, high noon, late afternoon, sunset,
  and night, using actual daily anchors plus neutral missing-field fallbacks.
- Normal, polar-night, and midnight-sun solar branches.
- Adjacent-day moon interval selection for rise-after-set data and null-event hiding.
- Full-moon night lift that remains within current tint/opacity/blur renderer primitives.

## Deviations

- Semantic labels stay with the interval's start state until the exact next keyframe rather than
  switching at the numeric midpoint. This prevents `sunrise` from being reported hours before
  civil dawn while preserving smooth values.
- Midnight-sun x motion is cyclic rather than a left-to-right reset, avoiding a discontinuity at
  local midnight.

## Verification Evidence

- Production module compiles.
- Existing 20 Phase 1 tests remain passing.
- Nine new scene-model tests pass while still unstaged in the independent Wave 2 test file.
- Manual state readback showed phase/ambient/trajectory changes through the day.
- `git diff --check` passes.

## Next

Plan 02-02 commits the independent deterministic model coverage and runs the full suite.
