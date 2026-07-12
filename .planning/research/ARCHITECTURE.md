# Architecture Research: Dynamic Pixel City

**Researched:** 2026-07-12

## Component Boundaries

```text
ip-api.com (daily max) ─┐
                        ├─ DailyDataStore ── SceneState ── SceneAssetGenerator
sunrise-sunset v2 ──────┘       │                 │                 │
                                 │                 └─ IPC command plan│
manual/test overrides ──────────┘                                  │
                                                                    ▼
                                hyprlax ctl list/modify ── Unix socket ── renderer
```

### Controller

One executable Python file owns CLI parsing, the loop, clock injection, error reporting, and
composition of the smaller pure functions below. It must not be required by core Hyprlax.

### DailyDataStore

- Cache schema versioned JSON under `${XDG_CACHE_HOME:-~/.cache}/hyprlax/pixel-city-dynamic/`.
- Record `last_attempt_date` per provider before issuing a request so restarts cannot exceed one
  attempt on the same date.
- Keep `last_success` independently so a failed refresh never destroys usable data.
- Lock cache updates with `fcntl.flock`, write a temporary file, `fsync`, and `os.replace`.
- Manual coordinates bypass ip-api and use their own cache identity.

### SceneState

Pure functions accept current zoned time plus normalized daily astronomy and return:

- named phase and blend fraction;
- sun/moon visibility, UV x/y position, opacity, and tint;
- base-layer tint/opacity commands;
- shadow direction, length, opacity, and lunar fill intensity.

This is the primary unit-test boundary.

### SceneAssetGenerator

- Emit valid 576x324 RGBA PNGs with only the standard library.
- Sun is a pixelated warm disk with a small halo.
- Moon phase is produced by subtracting an offset ellipse from a lit disk; waxing/waning controls
  which side remains illuminated.
- Shadow overlay is a simple pixel mask/projected skyline wedge derived from fixed low-resolution
  scene geometry; it is intentionally stylized, not physically exact.
- Use double-buffered output paths when changing image content so `ctl modify ... path ...`
  reliably triggers texture reload.

### IPC Adapter

- Run `hyprlax ctl list --json`, parse and validate the layer list, and discover named dynamic
  layers by distinctive generated filenames.
- Build a minimal delta from the last applied state and call `hyprlax ctl modify` for x, y,
  opacity, tint, visible/path, and other supported properties.
- `--dry-run` prints the same command plan without contacting the socket.
- Command failures are reported and retried only on the next controller tick; they never trigger
  extra provider requests.

## Suggested Build Order

1. Establish copied config, cache/provider contracts, normalized astronomy model, and tests.
2. Add pure lighting/celestial calculations and PNG synthesis with deterministic fixture checks.
3. Add layer discovery, IPC delta application, loop/status CLI, and failure recovery.
4. Add operating documentation, service examples, static/build/test validation, and live PR proof.

