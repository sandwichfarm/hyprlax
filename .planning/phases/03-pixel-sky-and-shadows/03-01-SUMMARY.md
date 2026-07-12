---
phase: 03-pixel-sky-and-shadows
plan: "01"
subsystem: generated-visuals
tags: [png, moon-phase, shadows, atomic-assets]
requires: [02-astronomical-scene-model]
provides:
  - standard-library RGBA PNG codec and A/B writer
  - sun and phase-correct moon sprites
  - foreground-derived solar shadow projection
  - initialized ordered dynamic overlay layers
affects: [04-safe-ipc-controller]
tech-stack:
  added: []
  patterns: [visible-sphere moon lighting, alpha projection, double-buffered atomic paths]
key-files:
  created:
    - examples/pixel-city-dynamic/generated/sun-a.png
    - examples/pixel-city-dynamic/generated/moon-a.png
    - examples/pixel-city-dynamic/generated/shadow-a.png
  modified:
    - examples/pixel-city-dynamic/dynamic_scene.py
    - examples/pixel-city-dynamic/parallax.toml
key-decisions:
  - Derive shadows from actual 6.png alpha and squash/project them along the ground.
  - Keep dynamic config layers initially invisible until the IPC controller owns them.
requirements-completed: [ASSET-01, SHADOW-01, ASSET-02]
completed: 2026-07-12
---

# Phase 3 Plan 1: Generated Pixel Sky and Shadows Summary

Dependency-free pixel generation now produces validated sun/moon sprites, phase-correct lit moon
geometry, actual-foreground directional shadows, atomic A/B paths, and three initialized invisible
overlay layers in the copied scene.

## Delivered

- Strict non-interlaced 8-bit RGBA PNG decoder with filters 0-4, CRC and size validation.
- Filter-0 RGBA encoder and fsync/replace binary writer.
- Sun halo/disk plus visible-sphere phase lighting with waxing/waning side selection.
- Shadow projection from decoded `6.png` alpha opposite sun x, scaled by elevation/opacity.
- Generated sun-a, moon-a, and transparent shadow-a at 576x324.
- Nine-layer TOML order: sky, sun, moon, four city depths, shadow, foreground.

## Deviation

Noon alpha attenuation was strengthened from 0.45 to 0.75 times elevation because the compressed
noon projection contains more unique pixels; the stronger reduction makes total noon shadow energy
measurably lower than the long low-sun projection, matching the visual requirement.

## Evidence

- PNG/image file readback: all three generated assets are 576x324 RGBA.
- Nine-layer TOML assertion passes and every path exists.
- Source 6.png decodes with 46,370 nontransparent pixels.
- Six independent asset tests pass while remaining in the Wave 2 test file.
- Full production/test module compiles and `git diff --check` passes.

## Next

Plan 03-02 commits independent pixel geometry, shadow, codec, and buffer regression tests.
