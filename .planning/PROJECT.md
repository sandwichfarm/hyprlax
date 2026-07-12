# Hyprlax Dynamic Pixel City

## What This Is

Hyprlax is an existing Wayland parallax wallpaper engine. This project adds a self-contained
`pixel-city-dynamic` example, copied from the current Pixel City scene, whose sun, moon, sky,
city lighting, and synthesized shadows follow the user's approximate location and the current
astronomical day through Hyprlax's runtime IPC socket.

## Core Value

Pixel City should visibly and automatically track the real local sky while remaining useful
offline after the last successful daily data refresh.

## Requirements

### Validated

- ✓ Hyprlax loads ordered PNG layers from TOML and animates them during workspace changes — existing
- ✓ The runtime Unix socket can list layers and modify path, x/y, opacity, tint, blur, visibility, and z order — existing
- ✓ The renderer supports per-layer opacity, RGB tint strength, blur, transparent PNGs, and parallax depth — existing
- ✓ The repository already ships a ten-layer `examples/pixel-city` scene suitable as the visual base — existing
- ✓ A valid copied six-layer dynamic example and Python standard-library sidecar foundation exist — Phase 1
- ✓ ip-api location/manual override and combined solar/lunar daily inputs obey an absolute provider/date attempt ceiling — Phase 1
- ✓ Locked atomic schema-v1 cache preserves last-good data and supplies deterministic neutral offline fallback — Phase 1

### Active

- [ ] Use `hyprlax ctl`/the runtime socket to animate mutually appropriate sun and moon layers through the daily arc.
- [ ] Model sunrise, morning, high noon, late afternoon, sunset, night, and lunar night lighting with continuous transitions.
- [ ] Drive existing tint, opacity, blur, visibility, and parallax controls to change the sky, stars, city saturation impression, windows, and celestial bodies.
- [ ] Synthesize simple directional shadows from scene silhouettes and current solar azimuth/elevation, without adding dependencies.
- [ ] Provide deterministic time/location overrides, a one-shot mode, and a dry-run/status surface so behavior is testable without a compositor or network.
- [ ] Document setup, privacy/network behavior, systemd user operation, troubleshooting, and the exact daily request policy.
- [ ] Add automated tests for astronomy mapping, cache policy, lighting transitions, shadow generation, IPC command generation, and offline behavior.

### Out of Scope

- Pixel-perfect scientific sky rendering — the APIs anchor astronomical events; the scene remains stylized pixel art.
- Continuous GPS or device-location tracking — approximate IP geolocation is refreshed daily by explicit requirement.
- Weather/cloud simulation — weather is not requested and would add another recurring network dependency.
- New third-party Python or C dependencies — the example must run with Python's standard library and the existing Hyprlax binary.
- Reusing PR #59 implementation or its generated moon assets — it is historical evidence only; the new design starts from current `origin/master`.

## Context

- The current release line is v2.2.x and `origin/master` already contains the modern event loop, TOML configuration, renderer, and IPC controls.
- PR #59 (`feature/pixel-city-advanced`) is still open from 2025 and mixed the old experiment with unrelated repository changes. Its useful concepts may be studied, but its code is not the implementation base.
- The source scene is `examples/pixel-city`, including ten PNG layers and both TOML and legacy configs.
- The controller belongs in the example directory so users can copy the directory into `~/.config/hyprlax/` and run it independently.
- Network-derived location is privacy-sensitive and must be documented. Successful responses are cached under the user's XDG cache directory; no API key is required.

## Constraints

- **Runtime integration**: Dynamic changes must travel through `hyprlax ctl` and its Unix socket — no daemon restart per lighting update.
- **Network budget**: Each external data category is fetched at most once per local calendar day; retries cannot become an unbounded polling loop.
- **Offline behavior**: Startup and animation must continue with cached or deterministic fallback values when any API is unavailable.
- **Dependencies**: No new repository/runtime dependency; Python 3 standard library only for the controller and asset synthesis.
- **Compatibility**: Preserve the original Pixel City example and all existing CLI/config behavior.
- **Performance**: The controller runs outside the render loop, batches changes, and defaults to a modest update interval.
- **Security**: Validate API schemas, bound downloaded response sizes/timeouts, write cache/assets atomically, and never evaluate remote content.
- **Delivery**: Build, test, lint/static checks, functional dry-run evidence, Lore commits, and a verified PR on `origin` are required.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Implement as a copied example plus standard-library Python controller | Exercises current Hyprlax abilities and avoids coupling network I/O to the 144 FPS C render loop | ✓ Good |
| Discover dynamic layers by path from `ctl list --json` | Layer IDs are runtime-assigned and should not be hard-coded | — Pending |
| Generate celestial and shadow PNG overlays locally | Keeps artwork deterministic, dependency-free, and adjustable to astronomical state | — Pending |
| Use continuous interpolation around named lighting keyframes | Avoids abrupt scene jumps while retaining the requested recognizable time-of-day states | — Pending |
| Treat PR #59 as evidence, not a code source | The request explicitly calls for a restart and the branch predates current IPC/config architecture | ✓ Good |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition:**
1. Move verified requirements to Validated with the phase reference.
2. Record invalidated or deferred requirements under Out of Scope with reasons.
3. Add implementation decisions and newly discovered constraints.
4. Keep the description and core value aligned with the shipped behavior.

**After each milestone:**
1. Audit every requirement against current source, tests, runtime evidence, and remote PR state.
2. Recheck the core value and offline/privacy guarantees.
3. Record remaining risks without converting them into implicit completion.

---
*Last updated: 2026-07-12 after Phase 1 daily-data verification*
