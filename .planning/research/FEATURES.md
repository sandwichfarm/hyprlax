# Feature Research: Dynamic Pixel City

**Researched:** 2026-07-12

## Table Stakes

| Feature | Expected behavior | Complexity | Dependencies |
|---------|-------------------|------------|--------------|
| Daily coarse location | One attempt per local date, schema validation, manual override | Medium | cache ledger |
| Daily astronomy | Solar and lunar events for the resolved local date | Medium | valid location/timezone |
| Last-good offline mode | Scene starts and updates without network after a successful fetch | Medium | atomic cache |
| Continuous celestial motion | Sun follows daylight arc; moon follows lunar visibility interval | Medium | IPC layer discovery |
| Continuous lighting | Named phases blend rather than jump | Medium | astronomy timeline |
| Lunar lighting | Moon phase and illumination scale night brightness and moon appearance | Medium | moon data |
| Directional shadows | Shadow direction/length/opacity follows solar position | High | generated overlay |
| Deterministic controls | Fixed date/time/location, dry-run, one-shot, status | Medium | dependency injection |
| Operational docs | Copy/run/service/privacy/attribution/troubleshooting | Low | stable CLI |

## Differentiators

- Use one combined astronomy response per day for both solar and lunar state.
- Derive a stylized saturation impression from existing per-layer tint strengths rather than
  introducing a new renderer feature.
- Render the sun/moon as transparent pixel-art overlays and animate their UV offsets through IPC.
- Synthesize a low-resolution, deliberately pixelated skyline shadow mask from current scene
  silhouettes, then directionally project it based on solar altitude and horizontal position.
- Make the exact planned IPC commands inspectable in dry-run JSON for debugging and automated tests.

## Anti-Features

- No always-on network polling.
- No precise address/location collection.
- No weather, cloud, GPS, or telemetry integration.
- No opaque background daemon with no status output.
- No dependence on old PR #59 files or generated assets.

## Named Lighting States

The controller should compute continuous values around these anchors:

| State | Astronomical anchor | Visual intent |
|-------|---------------------|---------------|
| night | before civil dawn / after civil dusk | deep blue tint, stars/windows visible, lunar fill scaled by illumination |
| sunrise | civil dawn through morning golden hour | warm horizon, low contrast, long cool shadows |
| morning | golden-hour end toward solar noon | neutralizing tint, increasing brightness, shorter shadows |
| high noon | centered on solar noon | least tint, crisp skyline, shortest/faintest shadows |
| late afternoon | solar noon toward evening golden hour | warmer tint, longer shadows |
| sunset | evening golden hour through civil dusk | strongest warm tint, long cool shadows, rising city lights |
