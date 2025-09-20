# Optimization Guide

This document summarizes the performance knobs available in hyprlax and suggests practical combinations to balance frame rate, power usage, and visual quality.

## Goals
- Maintain smooth animations (e.g., 60–144 FPS) during workspace changes.
- Keep GPU power near baseline when idle; no post‑animation regressions.
- Minimize driver calls and redundant GL state changes.

## Environment Toggles
All toggles are optional and can be combined. Example usage:
```
HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1 ./hyprlax -c examples/pixel-city/parallax.conf
```

- `HYPRLAX_PERSISTENT_VBO=1`
  - Reuses a single VBO across draws; avoids per‑draw `glGenBuffers/glDeleteBuffers`.
  - Best paired with `HYPRLAX_UNIFORM_OFFSET=1` to minimize buffer updates.

- `HYPRLAX_UNIFORM_OFFSET=1`
  - Passes layer offset via `u_offset` uniform in the vertex shader instead of altering texcoords.
  - Reduces bandwidth and lets geometry remain static.

- `HYPRLAX_NO_GLFINISH=1`
  - Skips `glFinish()` before `eglSwapBuffers` to avoid CPU/GPU stalls.
  - Throughput improvement during animations; safe default is to keep finish unless testing.

- `HYPRLAX_SEPARABLE_BLUR=1`
  - Enables a two‑pass (horizontal/vertical) separable blur with a fixed 9‑tap kernel.
  - Significantly faster than the legacy 2D kernel at higher blur amounts.

- `HYPRLAX_BLUR_DOWNSCALE=N`
  - Downscales the blur FBO (e.g., `2`, `3`, `4`) to reduce cost further.
  - Improves performance at a small quality trade‑off; 2–3 is a good starting point.

- `HYPRLAX_FRAME_CALLBACK=1`
  - Uses Wayland frame callbacks per monitor to pace rendering.
  - Reduces busy loops and idle wakeups; pair with an FPS cap for best power results.

- `HYPRLAX_PROFILE=1`
  - Emits per‑frame `[PROFILE]` logs with draw/present timing to track regressions.

- `HYPRLAX_RENDER_DIAG=1`
  - Emits `[RENDER_DIAG]` logs when rendering during idle to identify spurious triggers.

## Recommended Recipes

- Balanced performance (safe default):
```
HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1
```

- Max throughput during animations:
```
HYPRLAX_PERSISTENT_VBO=1 HYPRLAX_UNIFORM_OFFSET=1 HYPRLAX_NO_GLFINISH=1
```

- Power‑friendly (reduced idle wakeups):
```
HYPRLAX_FRAME_CALLBACK=1 ./hyprlax --fps 60  # or --fps 30
```

- Quality blur with good performance:
```
HYPRLAX_SEPARABLE_BLUR=1 HYPRLAX_BLUR_DOWNSCALE=2
```

## Tips and Caveats
- If visuals look flipped on the vertical axis when using separable blur, keep the default build (the renderer samples with a V‑flip in pass 2 to match typical FBO orientation).
- VSync can be controlled via CLI (`--vsync`) or config; disabling vsync and skipping `glFinish()` maximizes throughput but may increase power on some stacks.
- When investigating idle power, enable `HYPRLAX_RENDER_DIAG=1` and ensure no resize, IPC, or compositor events are repeatedly triggering renders.

## Benchmarking
See `docs/benchmarking.md` for Make targets and example runs that measure power, utilization, and FPS under different configurations.

