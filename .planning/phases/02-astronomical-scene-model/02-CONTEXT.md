# Phase 2: Astronomical Scene Model — Context

**Discussed:** 2026-07-12 (autonomous from roadmap and verified Phase 1)
**Status:** Ready for planning

## Phase Boundary

Transform a zoned instant and Phase 1 `Astronomy` into a deterministic, continuous `SceneState`.
No PNG generation, subprocess, socket, loop, or CLI work belongs in this phase.

## Locked Decisions

- Add pure model types/functions to `dynamic_scene.py`; provider/cache behavior stays unchanged.
- Recognized labels are exactly `night`, `sunrise`, `morning`, `high_noon`,
  `late_afternoon`, and `sunset`. Intermediate values blend continuously with smoothstep.
- Timeline anchors use civil dawn, sunrise-to-noon midpoint, solar noon, noon-to-sunset midpoint,
  sunset, and civil dusk. Missing normal-day solar fields use the already documented neutral
  anchors; `polar_night` remains night and `midnight_sun` remains daylight.
- Sun progress is bounded 0..1 from real sunrise to sunset; its stylized UV trajectory is bounded
  to x -0.34..0.34 and y -0.24..0.18 with opacity fades at the horizon.
- Moon is visible only when both rise and set events define an interval. The interval resolver must
  test adjacent-day candidates so rise-after-set data works across midnight without inventing a
  missing opposite event.
- Moon opacity and lunar fill scale with illumination; the named phase is preserved for Phase 3
  sprite generation. New moon produces no lunar fill; full moon measurably lifts night lighting.
- Per-layer looks are emitted for `1.png` through `6.png` using only tint RGB/strength, opacity,
  and blur. `saturation_impression` is a descriptive scalar for the intended colorfulness; it is
  not and must never become an IPC `saturation` property.
- SceneState also emits ambient brightness, stars opacity, city-light intensity, solar elevation,
  and solar/lunar progress for later visual/IPC phases.

## Verification Shape

- Exact anchor instants return all requested labels.
- Values immediately before/after every anchor are continuous within a small epsilon.
- Sun and moon x/y/progress/opacity never exceed documented bounds.
- Rise-after-set moon intervals work before and after midnight; one/null event hides moon.
- New/quarter/full moon nights have strictly increasing lunar fill and ambient lift.
- DST-aware input is converted into the astronomy timezone and never compared as naive time.
- Polar day/night and missing normal fields remain deterministic and exception-free.
