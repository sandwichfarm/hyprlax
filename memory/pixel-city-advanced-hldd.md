# Pixel City Advanced — Dynamic Sun/Moon Scene (HLDD)

## Overview

Create a dynamic “sky” for the pixel-city-advanced example using hyprlax IPC. The scene adds/removes a Sun or Moon layer based on time of day, moves it across an arching path, and applies per-layer tints during dawn/dusk/night. Sunrise/sunset and moon phase come from a weather API (with a secrets file for API key and location).

## Goals

- Dynamically add and control Sun/Moon layers via `hyprlax ctl`.
- Compute day/night windows from API sunrise/sunset and place the Sun or Moon along an arch path.
- Render multiple Moon phases; hide Moon near new moon.
- Apply time-of-day tints (dawn/dusk/night) to sky/buildings using the new tint property.
- Efficient update strategy (gated polling; skip work if no visible change).

## Non‑Goals

- No changes to hyprlax C core; this is implemented as an example script and generated assets.
- No reliance on runtime OpenGL custom shaders beyond existing tint support.
- No heavy asset pipeline; keep generation self-contained (Pillow/CairoSVG optional for rasterization).

## Directory Layout (example)

```
examples/pixel-city-advanced/
  assets/
    sun.svg
    moon_*.svg             # generated moon phase SVGs
    cache/
      sun.png              # rasterized/current sized
      moon.png             # rasterized/current phase
  tmp/                     # generated full-frame overlays (transparent PNGs)
  dynamic_sky.py           # main controller (Python)
  secrets.sample.env       # sample secrets (API key, lat/lon)
  .secrets.env             # user-provided actual secrets (gitignored)
  parallax.toml / conf     # base city layers (BG/buildings/clouds)
```

## Data Sources

- Primary: OpenWeather One Call (3.0) API
  - Provides sunrise/sunset per day and `moon_phase` in [0..1]; requires API key.
  - Secrets file stores `OPENWEATHER_API_KEY`, `LAT`, `LON`.
- Fallback: If API unavailable, continue with cached previous window; if none, approximate civil twilight around fixed times and disable Moon phase gating.

## Runtime Components

1. Asset generator
   - Sun: simple radial gradient/glow SVG; cached raster PNG at needed size.
   - Moon: generated SVG per phase using two circle shapes (lit/unlit) with a phase terminator mask; cached raster PNG for current phase.
2. Controller loop
   - Determines current “phase of day” (night, dawn, day, dusk) from sunrise/sunset and civil twilight offsets.
   - Computes current param `t` within the active window (day: sunrise→sunset; night: sunset→next sunrise).
   - Maps `t` to screen coordinates via arch function; updates Sun/Moon overlay position.
   - Applies tints to sky/building layers based on phase.
   - Gated polling: adjusts check frequency and skips IPC if effective output unchanged.

## Layer Model

- Base pixel-city-advanced layers remain static (background sky, distant city, mid/buildings, foreground, etc.). The script adds:
  - Sun layer: transparent PNG full-frame with sun sprite drawn at computed (x,y), `z` above sky but below foreground clouds.
  - Moon layer: same treatment; visibility off in day or near-new-moon.
  - Optional: night glow overlay (subtle) for city to simulate moonlight by tinting buildings.

Recommended z-order (adjust to the example’s stack):
- bg sky: z≈0–1
- distant parallax city: z≈2–3
- Sun/Moon: z≈4–5
- near buildings/foreground: z≥6

## Positioning Model (Arch Path)

We use full-frame transparent PNG overlays for precise on-canvas positioning. The sprite is drawn at integer pixel coordinates to avoid UV precision issues.

- Screen size: read from `hyprlax ctl status --json` first monitor (or largest) or fallback to 3840×2160.
- Arch path: elliptical arc from left horizon → apex → right horizon.
  - Param `t ∈ [0,1]` increases linearly with time within the active window (day or night).
  - x_px = lerp(x_left_margin, x_right_margin, t)
  - y_px = y_horizon - arc_height * sin(pi * t)      # y downward; horizon baseline near bottom 35–40%
  - arc_height can differ for day vs night (moon slightly lower for variety), configurable.
  - Margins prevent clipping under foreground silhouettes.

## Time Windows and Phases

- Day window: [sunrise, sunset]
- Night window: [sunset, next sunrise]
- Dawn: 45 min centered around sunrise (configurable ±N minutes).
- Dusk: 45 min centered around sunset (configurable).
- Phase classification used for tint ramp functions and stricter update cadences.

## Tint Model

Apply per-layer tint via IPC `modify <id> tint <spec>`:
- Day: tint none (or very slight warm, e.g., `#ffe6a3:0.05` optional).
- Dawn/Dusk: warm golden/orange tint on sky and light buildings: `#ffb566` ramping strength 0→0.35 across window edges.
- Night: cool blue tint on sky and buildings: `#7aa5ff:0.12` (sky), `#6b87c8:0.18` (buildings) to simulate moonlight.
- Ramp: smoothstep or cosine interpolation based on minutes to/from sunrise/sunset to avoid steps.

Moon influence on buildings:
- When Moon layer is visible, bias building tint toward cooler (`+0.05..0.10` strength), scaled by moon altitude (higher moon → stronger cool tint). Hide/zero effect during day.

## Moon Phase Handling

- Use `moon_phase ∈ [0..1]` from API: 0 new, 0.5 full. Hide Moon if phase < ε or > 1-ε (ε≈0.05).
- Select pre-generated SVG mask for nearest phase bucket (e.g., 30–60 buckets), then rasterize to PNG at target diameter.
- Optional: subtle glow halo around fuller phases.

## IPC Interactions

- Add layers once (on first run) if not present; then update via `modify path` after regenerating overlay file. This forces reload without re-adding layers.
- Set `fit=cover`, `opacity=1.0`, `shift_multiplier=0.0`, `z=<as configured>`.
- Apply tints to specific existing city layers by filtering `hyprlax ctl list --json` for names/paths that match the pixel-city-advanced stack (document recommended layer IDs once established by the example config).

## Update & Gating Strategy

- Baseline monitor: every 120 s during day; every 300 s at deep night.
- Twilight windows (dawn/dusk): tighten to 30–60 s.
- Change gating: skip raster/IPC if both conditions hold:
  - Position delta < 10 px in both axes since last draw.
  - Tint strength delta < 0.03 (and tint RGB unchanged) for targeted layers.
- API refresh TTL: 6 h (or at midnight). Persist last API response to `assets/cache/astro.json`.

## Error Handling & Resilience

- If hyprlax daemon not running, generate images and exit (with `--once`) or retry (daemon mode) with backoff.
- If API fails, use cached windows; if none, fallback to fixed sunrise/sunset approximations; disable moon phase gating.
- Validate generated images and atomic-write outputs (temp file + rename) to avoid partial loads.

## Security & Secrets

- `examples/pixel-city-advanced/.secrets.env` (gitignored) with:
  - `OPENWEATHER_API_KEY=...`
  - `LAT=..`, `LON=..`
  - Optional: `TZ=...` (else use system tz)
- `secrets.sample.env` for users to copy.

## Performance Considerations

- Generate full-frame PNG only when gated deltas exceed thresholds; otherwise skip.
- Cache current sun/moon PNGs by size and phase; reuse across ticks with only compositing/drawing at new coordinates.
- Use Pillow; avoid loading fonts. No blocking network in the tight loop: refresh API on its own schedule.

## CLI & Configurability (script)

- `--interval N` base loop seconds (default 120). Twilight overrides apply automatically.
- `--once` single evaluation for cron.
- `--out-dir DIR` for tmp image outputs.
- `--sun-size`, `--moon-size`, `--arc-height-day`, `--arc-height-night`.
- `--twilight-minutes 45` to widen/narrow tint windows.
- `--verbose`, `--dry-run`.

## Acceptance Criteria

- Sun appears after sunrise and travels left→right across the sky until sunset; Moon appears after sunset and travels until sunrise; no Moon on (near) new moon.
- Dawn/Dusk warm tint ramps during windows; Night blue tint applied; Buildings receive cooler tint at night proportional to moon altitude.
- CPU/GPU overhead remains low: updates gated; hyprlax stays at target FPS.

## Risks & Mitigations

- SVG rasterization dependency: Prefer Pillow-rendered shapes; SVGs are generated for portability/demos; rasterize only when necessary (CairoSVG optional).
- Screen resolution variance: Pull from hyprlax status; fallback to 3840×2160 to avoid failure.
- Layer ID volatility: Resolve by path filter at runtime; store discovered IDs per session.

