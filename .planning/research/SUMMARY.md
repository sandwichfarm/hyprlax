# Research Summary: Dynamic Pixel City

**Researched:** 2026-07-12

## Decision

Build a dependency-free Python controller next to a copied Pixel City config. Query ip-api at
most once per local date for approximate location and Sunrise-Sunset.org v2 at most once per
resolved local date for both solar and lunar facts. Cache attempts and last-good data atomically,
then continuously compute a stylized scene locally and apply only IPC deltas.

## Why This Fits Hyprlax

The current renderer and IPC already expose the right primitives: transparent PNGs, per-layer UV
offset, opacity, tint strength, blur, visibility, path reload, and depth. Keeping APIs and asset
synthesis in a helper avoids blocking the 144 FPS event loop and works across every compositor
adapter without C changes.

## Highest Risks

1. ip-api free is HTTP-only and non-commercial; manual overrides and explicit documentation are mandatory.
2. Once-daily means failed attempts and concurrent processes must also be suppressed.
3. Moon fields are new, nullable, and schema-sensitive.
4. Celestial/shadow motion needs real IPC and compositor validation in addition to pure tests.
5. Provider attribution must be visible in the example README.
6. Current Hyprlax cannot change saturation directly; the docs and implementation must accurately
   describe tint/opacity-based color muting instead.
7. PR #59 demonstrated useful sidecar/path-reload ideas but also stale monitor JSON, broken moon
   masks, unsafe all-layer tinting, midnight staleness, and ignored IPC errors. None of its code or
   generated assets should be transplanted.

## Sources

- <https://ip-api.com/docs/api:json>
- <https://ip-api.com/docs/legal>
- <https://sunrise-sunset.org/api>
- <https://sunrise-sunset.org/terms>
- Current repository architecture and IPC/config sources on `origin/master` at `a7ea786`
