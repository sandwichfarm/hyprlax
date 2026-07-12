# Phase 2 Research: Astronomical Scene Model

**Researched:** 2026-07-12
**Confidence:** High

## Recommended Formulas

- `smoothstep(t) = t*t*(3 - 2*t)` for keyframe interpolation.
- Sun progress `p = clamp((now - sunrise) / (sunset - sunrise), 0, 1)`.
- Solar elevation impression `e = sin(pi*p)`; trajectory `x=-0.34+0.68*p`,
  `y=0.18-0.42*e`; horizon opacity `min(1, p/0.08, (1-p)/0.08)`.
- Moon interval: normalize rise/set to the astronomy zone; if set <= rise add one day to set;
  inspect intervals beginning at rise-1d, rise, and rise+1d and select the one containing now.
- Moon opacity factor `sqrt(illumination/100)` with horizon fades; lunar fill additionally requires
  moon visibility and night factor.
- Lighting interpolation operates on numeric RGB channels, strength, opacity, blur, ambient,
  colorfulness, stars, and city-lights. Hex conversion waits for the IPC layer.

## Preset Intent

| State | Ambient | Saturation impression | Dominant tint |
|-------|---------|-----------------------|---------------|
| night | 0.18 | 0.55 | deep cool blue |
| sunrise | 0.48 | 0.82 | strong orange-pink |
| morning | 0.78 | 0.95 | light warm cream |
| high_noon | 1.00 | 1.00 | neutral/clear |
| late_afternoon | 0.74 | 0.92 | amber |
| sunset | 0.42 | 0.78 | strong orange-red |

Tint strength decreases with layer depth toward the foreground. Full moon may reduce night tint
strength by at most 0.16 and shift cool channels upward without exceeding 1.0.

## Test Recommendations

- Extend the existing Python suite; do not create a second runner.
- Use Phase 1 normalized/fallback astronomy fixtures and exact aware datetimes.
- Compare full `SceneState` numeric fields around anchors, not only the phase label.
- Test a DST date with an input instant in UTC and astronomy in `Europe/Budapest`.
- Assert model output property names contain no `saturation` command; only the descriptive
  `saturation_impression` scalar is allowed.
