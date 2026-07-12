---
phase: 02-astronomical-scene-model
status: passed
score: 5/5
verified: 2026-07-12
requirements: [LIGHT-01, LIGHT-02, LIGHT-03, SKY-01, SKY-02]
---

# Phase 2 Verification: Astronomical Scene Model

## Verdict

**PASSED — 5/5 mapped requirements have direct deterministic evidence.**

## Requirement Evidence

| Requirement | Status | Direct evidence |
|-------------|--------|-----------------|
| LIGHT-01 | PASS | Exact anchor tests assert all six requested labels; one-second before/after tests prove ambient and layer-strength continuity; readback shows the full neutral day sequence. |
| LIGHT-02 | PASS | Six layer looks expose only tint RGB/strength, opacity, and blur; all values are bounded; base blur intent is preserved; production contains no plain `saturation` property. |
| LIGHT-03 | PASS | Identical visible-night fixtures prove `lunar_fill(new) == 0 < quarter < full` and strictly increasing ambient lift; phase is preserved. |
| SKY-01 | PASS | Hourly solar table proves progress/opacity/elevation 0..1, x -0.34..0.34, y -0.24..0.18; sunrise/sunset use opposite x endpoints; polar branches pass. |
| SKY-02 | PASS | Rise-after-set fixtures prove visibility at 01:00 and 23:30 but not noon; null one-sided events hide moon; all lunar position/progress/opacity values are bounded. |

## Fresh Commands

- `python3 -m unittest -v tests.test_pixel_city_dynamic.SceneModelTests` — 9/9 OK.
- `tests/test_pixel_city_dynamic.sh` — full 29-test suite and copied TOML check OK.
- Production/test `py_compile` — exit 0.
- `! rg -n '\bsaturation\b' .../dynamic_scene.py` — exit 0.
- Six-anchor state readback — night/sunrise/morning/high_noon/late_afternoon/sunset values changed as planned.
- `git diff --check` — exit 0.

## Boundaries Confirmed

- Pure model only: no PNG writes, subprocesses, socket calls, loop, or CLI.
- Aware time conversion covers IANA DST and never compares naive timestamps.
- Missing normal solar fields fall back to neutral anchors; polar statuses avoid division by null
  intervals; missing lunar events are not invented.
- UV sign/direction remains stylized and requires live renderer validation in Phase 4, while bounds
  and continuity are proven here.

## VERIFICATION PASSED
