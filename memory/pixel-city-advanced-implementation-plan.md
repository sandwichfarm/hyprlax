# Pixel City Advanced — Implementation Plan

This plan turns the HLDD into a concrete, incremental implementation in `examples/pixel-city-advanced/`.

## Milestone 0 — Scaffolding

- Add secrets templates:
  - `examples/pixel-city-advanced/secrets.sample.env`
    - OPENWEATHER_API_KEY=
    - LAT=
    - LON=
    - TZ= (optional)
  - `.secrets.env` (gitignored) with same keys.
- Create directories: `assets/`, `assets/cache/`, `tmp/`.
- Add Python entrypoint `dynamic_sky.py` with CLI skeleton and logging.

Deliverables:
- Files/directories exist; `python3 dynamic_sky.py --help` works.

## Milestone 1 — Asset Generation (Sun/Moon)

1. Sun SVG generator
   - Produce `assets/sun.svg`: simple circle + radial gradient.
   - Rasterizer: using Pillow draw to create `assets/cache/sun.png` (to avoid external deps). Size configurable.
2. Moon SVG generator
   - For phase `p∈[0..1]` create a crescent/gibbous by overlaying two offset circles; write `moon_<bucket>.svg` and rasterize to `assets/cache/moon.png` for current phase.
   - Bucket count default 30 (12° increments); choose nearest per current phase.
3. Utility: `ensure_assets(target_sizes)` caches raster for requested diameters (sun/moon) and returns filepaths.

Deliverables:
- `assets/sun.svg`, `assets/moon_*.svg` produced on first run; `assets/cache/sun.png`, `assets/cache/moon.png` generated.

## Milestone 2 — API + Caching

1. Secrets loader: read `.secrets.env` (fallback to env vars).
2. Client: OpenWeather One Call request (lat/lon)
   - Extract `sunrise`, `sunset`, `moon_phase`, `moonrise`, `moonset` if available.
3. Cache: write `assets/cache/astro.json` with timestamps; TTL = 6h; refresh at midnight.
4. Fallback: if fetch fails and no cache, synthesize sunrise/sunset around 6:30/18:30 local and `moon_phase=None`.

Deliverables:
- Robust `get_astro_data()` with cache/TTL and error handling.

## Milestone 3 — Positioning + Tints

1. Monitor size discovery
   - Use `hyprlax ctl status --json`; parse first or largest monitor size; fallback 3840×2160.
2. Arch position
   - Compute active window (day or night), param `t` = normalized time within that window.
   - Compute `(x_px, y_px)` with margins; sizes from CLI (`--sun-size`, `--moon-size`).
3. Tint functions
   - Phase detect: dawn (±twilight_minutes around sunrise), dusk analogous, night otherwise.
   - Compute tint spec strings per HLDD (sky/buildings). Strength ramps using smoothstep.
4. IPC layer discovery
   - On first run, add Sun/Moon layers (full-frame transparent overlays) if not present; store IDs.
   - Discover building layer IDs by matching `hyprlax ctl list --json` entries against known file name patterns from this example (e.g., `2.png`, `3.png` for city layers). Make patterns configurable.

Deliverables:
- Functions: `compute_position(now, astro, geometry)`, `compute_tints(now, astro)`, `get_or_create_layer(kind)`.

## Milestone 4 — Drawing + Gating

1. Compose overlays
   - Create full-frame transparent PNGs in `tmp/` and draw the cached sun/moon sprite at `(x_px, y_px)` centered.
   - Atomic write: write to temp, then `os.replace()` to final path.
2. Apply tints via IPC
   - For each targeted layer ID, `hyprlax ctl modify <id> tint <spec>`.
3. Gating strategy
   - Keep last `(x,y)` and tint strengths; skip draw/IPC if deltas below thresholds.
   - Loop base interval `--interval` with dynamic overrides: 30–60s in twilight, 120s daytime, 300s night.
   - Separate timers for API refresh vs. draw tick.

Deliverables:
- Smooth arch traversal with efficient updates and tint changes.

## Milestone 5 — Operational polish

1. CLI options
   - `--interval`, `--once`, `--out-dir`, `--sun-size`, `--moon-size`, `--arc-height-day`, `--arc-height-night`, `--twilight-minutes`, `--verbose`, `--dry-run`.
2. Logging
   - Human-friendly progress; `--verbose` dumps computed params and chosen gating decisions.
3. Cleanup
   - On Ctrl+C, remove Sun/Moon layers (optional flag `--no-remove` to keep layers).
4. Examples README update
   - Usage walkthrough and secrets setup.

Deliverables:
- Usable script with documentation.

## IPC Contract (concrete calls)

- Create layer (first-run):
  - `hyprlax ctl add tmp/sun_overlay.png z=<Z_SUN> opacity=1.0 shift_multiplier=0.0 fit=cover`
  - `hyprlax ctl add tmp/moon_overlay.png z=<Z_MOON> opacity=1.0 shift_multiplier=0.0 fit=cover`
- Update overlay in place: `hyprlax ctl modify <id> path tmp/<kind>_overlay.png`
- Apply tint: `hyprlax ctl modify <id> tint "#RRGGBB:strength"`
- Hide/show Moon: `hyprlax ctl modify <moon_id> visible true|false`

## Pseudocode Highlights

```python
def compute_t(now, start, end):
    if start <= now <= end:
        return (now - start) / (end - start)
    if now < start: return 0.0
    return 1.0

def arch_xy(t, W, H, margins, arc_h):
    x = lerp(margins.left, W - margins.right, t)
    y_base = H * 0.62                    # horizon baseline
    y = y_base - arc_h * math.sin(math.pi * t)
    return round(x), round(y)

def twilight_mix(mins_from_edge, twilight_minutes):
    x = clamp(1.0 - abs(mins_from_edge) / twilight_minutes, 0, 1)
    return smoothstep(0, 1, x)

def should_update(prev, cur, px_thresh=10, tint_thresh=0.03):
    return (abs(prev.x - cur.x) > px_thresh or
            abs(prev.y - cur.y) > px_thresh or
            any(abs(a-b) > tint_thresh for a,b in zip(prev.tints, cur.tints)))
```

## Testing & Validation

- Dry run: `python3 dynamic_sky.py --dry-run -v` prints computed positions/tints without IPC.
- Simulated time: `--at "2025-09-23T05:30:00"` for manual checks at twilight.
- Visual: run hyprlax with pixel-city-advanced, then start controller; verify overlays move and tints ramp smoothly.
- Performance: confirm CPU low usage; hyprlax FPS unaffected.

## Backout Plan

- Stop the controller; `hyprlax ctl remove <sun_id> <moon_id>` and revert any tint changes with `tint none` on modified city layers.

## Open Questions / Options

- SVG rasterization: Prefer Pillow drawings to avoid new deps; keep SVGs for portability and optional pipelines.
- Building layer detection: Provide a `--buildings-filter` CLI with glob/regex to target correct layer(s) robustly across user setups.
- Multi-monitor: initial pass targets a single monitor; later, compute largest monitor size or per-output overlays.

