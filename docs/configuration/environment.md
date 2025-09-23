# Environment Variables

Hyprlax supports environment variables for configuration and diagnostics. This page documents:
- Canonical mapping from configuration keys to environment variables
- Currently supported environment toggles
- Precedence relative to CLI and config files

## Canonical Mapping Rule

For canonical configuration keys (dotted, snake_case), the environment variable name is:
- Prefix with `HYPRLAX_`
- Uppercase
- Replace dots `.` with underscores `_`

Examples:
- `render.fps`           → `HYPRLAX_RENDER_FPS`
- `parallax.shift_pixels`→ `HYPRLAX_PARALLAX_SHIFT_PIXELS`
- `animation.duration`   → `HYPRLAX_ANIMATION_DURATION`
- `animation.easing`     → `HYPRLAX_ANIMATION_EASING`

This rule keeps ENV, CLI, Config (TOML), and IPC keys aligned and unambiguous.

## Supported Environment Variables

The following variables are recognized by hyprlax today (non-exhaustive; implementation-backed):

- Core/diagnostics
  - `HYPRLAX_DEBUG=1`                Enable debug logging
  - `HYPRLAX_PROFILE=1`              Per-frame profiling logs
  - `HYPRLAX_RENDER_DIAG=1`          Extra render diagnostics
  - `HYPRLAX_FORCE_LEGACY=1`         Force legacy draw path (no draw_layer_ex)

- Rendering performance toggles
  - `HYPRLAX_PERSISTENT_VBO=1`       Use a single persistent VBO
  - `HYPRLAX_UNIFORM_OFFSET=1`       Use a uniform vec2 offset for parallax
  - `HYPRLAX_NO_GLFINISH=1`          Skip glFinish() before present
  - `HYPRLAX_SEPARABLE_BLUR=1`       Enable two-pass FBO blur
  - `HYPRLAX_BLUR_DOWNSCALE=N`       Downscale factor for blur FBO (2/3/4)

- Frame pacing
- `HYPRLAX_FRAME_CALLBACK=1`       Use Wayland frame callbacks

- IPC and sockets
  - `HYPRLAX_SOCKET_SUFFIX=…`        Append a suffix to the socket filename for isolation
  - `HYPRLAX_IPC_ERROR_CODES=1`      Include error codes in IPC error lines

- Compositor detection
- `HYPRLAX_COMPOSITOR=hyprland|sway|…` Override compositor auto-detect

- Canonical configuration overrides
  - `HYPRLAX_RENDER_FPS=144`                 Target FPS
  - `HYPRLAX_ANIMATION_DURATION=1.25`       Workspace animation duration (seconds)
  - `HYPRLAX_ANIMATION_EASING=expo`         Workspace animation easing
  - `HYPRLAX_PARALLAX_SHIFT_PIXELS=200`     Base parallax shift per workspace (pixels)
  - `HYPRLAX_RENDER_VSYNC=true|false`       VSync toggle
  - `HYPRLAX_RENDER_TILE_X=true|false`      Force tiling on X
  - `HYPRLAX_RENDER_TILE_Y=true|false`      Force tiling on Y
  - `HYPRLAX_RENDER_MARGIN_PX_X=24`         Extra horizontal safe margin (px)
  - `HYPRLAX_RENDER_MARGIN_PX_Y=24`         Extra vertical safe margin (px)
  - `HYPRLAX_RENDER_OVERFLOW=repeat_x`      Overflow behavior (repeat_edge|repeat|repeat_x|repeat_y|none)
  - `HYPRLAX_PARALLAX_MODE=cursor`          Parallax mode (workspace|cursor|hybrid)
  - `HYPRLAX_PARALLAX_SOURCES_CURSOR_WEIGHT=0.5`    Cursor source weight (0..1)
  - `HYPRLAX_PARALLAX_SOURCES_WORKSPACE_WEIGHT=0.5` Workspace source weight (0..1)

Notes:
- Other environment variables from the desktop (like `HYPRLAND_INSTANCE_SIGNATURE`, `XDG_RUNTIME_DIR`) are used to form default IPC socket paths.

## Canonical Key Examples (ENV)

Using the mapping rule:

```bash
# Set FPS and base parallax shift (pixels)
export HYPRLAX_RENDER_FPS=144
export HYPRLAX_PARALLAX_SHIFT_PIXELS=200

# Tune animation
export HYPRLAX_ANIMATION_DURATION=1.25
export HYPRLAX_ANIMATION_EASING=expo
```

## Precedence

At startup, hyprlax merges configuration from multiple sources. The intended precedence is:

1. CLI flags (highest)
2. Environment variables
3. Config file (TOML or legacy)
4. Built-in defaults (lowest)

This order keeps ad-hoc overrides simple (CLI), supports system/session defaults (ENV), and preserves file-based configuration for persistent settings.

## Tips

- Prefer canonical dotted keys for scriptable control at runtime via `hyprlax ctl set/get`.
- For per-layer settings, use the TOML file or the `hyprlax ctl add/modify` commands — per-layer ENV is not supported.
- Keep `HYPRLAX_SOCKET_SUFFIX` handy when running multiple test instances in parallel.
