# Benchmarking hyprlax

This guide shows how to run the built‑in benchmarks, read their output, and toggle instrumentation to investigate performance and idle power.

## Prerequisites
- Linux Wayland desktop (tested on Hyprland).
- Optional power metrics: `nvidia-smi` for NVIDIA GPUs.
- Optional helpers used by scripts: `hyprctl`, `jq`, `bc`.

Notes:
- The bench scripts default to the example config at `examples/pixel-city/parallax.conf`.
- `scripts/bench/bench-performance.sh` accepts an optional config path as the first argument.

## Quick Start
- Run optimization checks: `make bench`
- Run broader perf suite: `make bench-perf`
- Fixed 30 FPS power check: `make bench-30fps`
- Clean bench logs: `make bench-clean`

These targets invoke scripts from `scripts/bench/` and write logs like `hyprlax-debug.log` and `hyprlax-test-*.log` in the repo root.

## Useful Environment Variables
- `HYPRLAX_PROFILE=1` — adds `[PROFILE]` lines per frame with draw/present timings.
- `HYPRLAX_RENDER_DIAG=1` — prints `[RENDER_DIAG]` lines when rendering while idle to help find spurious redraws.
- `HYPRLAX_FRAME_CALLBACK=1` — uses Wayland `wl_surface_frame` callbacks to pace rendering (reduces busy rendering when idle).
- `HYPRLAX_SEPARABLE_BLUR=1` — enables the separable blur path (two‑pass FBO) instead of legacy blur.
- `HYPRLAX_BLUR_DOWNSCALE=N` — downscale factor for separable blur FBO (e.g., 2, 3, 4) for speed.
- `HYPRLAX_PERSISTENT_VBO=1` — reuse the VBO; avoids per‑draw allocations.
- `HYPRLAX_UNIFORM_OFFSET=1` — pass layer offset via `u_offset` uniform instead of mutating texcoords.
- `HYPRLAX_NO_GLFINISH=1` — skip `glFinish()` before swap to improve throughput.

You can prefix a command with these, for example:
```
HYPRLAX_PROFILE=1 HYPRLAX_RENDER_DIAG=1 make bench-perf
```

## Interpreting Results
- Power and utilization: The scripts sample `nvidia-smi` during idle, during animations (workspace switches), and after animations settle.
- FPS: Recent FPS samples are printed from `hyprlax-debug.log`.
- `[PROFILE]`: Lines like `draw=.. ms present=.. ms` show CPU time around rendering and swap.
- `[RENDER_DIAG]`: Lines indicate the reason flags (resize/ipc/comp/final), frame timing, and first layer offsets for idle renders.

## Common Scenarios
- Baseline behavior (60 FPS): `make bench`
- Low power mode (30 FPS): `make bench-30fps`
- With separable blur and downscale:
```
HYPRLAX_SEPARABLE_BLUR=1 HYPRLAX_BLUR_DOWNSCALE=2 make bench-perf
```
- Max performance toggles:
```
HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1 HYPRLAX_NO_GLFINISH=1 make bench
```

## Tips
- If the bench scripts cannot find your config, pass the path to `bench-performance.sh` or edit the script’s `CONFIG`.
- To focus on idle power regressions, enable `HYPRLAX_RENDER_DIAG=1`, let animations complete, and watch for repeated `[RENDER_DIAG]` lines with all flags `0`.
- Combine `HYPRLAX_FRAME_CALLBACK=1` with a moderate FPS cap (e.g., `--fps 60` or `--fps 30`) to reduce idle wakeups.

