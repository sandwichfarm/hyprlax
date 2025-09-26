Pixel City Advanced — Dynamic Sun/Moon scene driven by hyprlax IPC.

Usage (setup):
- Copy `secrets.sample.env` to `.secrets.env` and fill `OPENWEATHER_API_KEY`, `LAT`, `LON`.
- Ensure hyprlax is running with this example background.

Run controller:
- `python3 dynamic_sky.py --verbose` (continuous) or `--once` (single update).
- Adds Sun/Moon overlay layers, moves them across the sky, and tints layers at dawn/dusk/night.

Notes:
- Overlays are written to `tmp/`; assets and API cache in `assets/`.
- Customize sizes and cadence via `--help`.
- Time simulation:
  - `--at 05:30` or `--at 2025-09-23T05:30:00` to compute for a specific time.
  - `--demo dawn --demo-seconds 30` to fast-forward the dawn window for demo purposes. Use with `--dry-run` or live.
 - Moon control:
   - `--moon-phase 0.5` to force a full moon (0=new, 0.5=full).
   - `--force-moon` to show the moon at night regardless of phase.
   - Adjust gating with `--moon-phase-min` and `--moon-phase-max` (default 0.05–0.95).

Hyprland exec-once example (preferred over systemd):
- In your Hyprland config: `exec-once = python3 /absolute/path/to/examples/pixel-city-advanced/dynamic_sky.py`
- Or call the helper: `exec-once = /absolute/path/to/examples/pixel-city-advanced/run_dynamic_sky.sh`

Tuning tips:
- `--sky-regex` and `--bld-regex` control which layers receive tint (defaults expect 1–4.png sky, 5–10.png buildings).
- `--sun-z` and `--moon-z` control overlay depth relative to city layers.
https://craftpix.net/freebies/
