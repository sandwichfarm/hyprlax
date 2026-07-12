---
phase: 05-operational-proof-and-delivery
status: complete
date: 2026-07-12
---

# Phase 5 Research

## Repository Surfaces

- `make`, `make test`, `make test-scripts`, `make lint`, `make docs-linkcheck`, and `make memcheck`
  are the authoritative build/quality targets.
- `make install-user` installs to `~/.local/bin/hyprlax`, giving committed systemd examples a
  stable unprivileged executable path.
- Current IPC source confirms `list --json` returns a direct array with `id` and `path`, while
  `modify` accepts every property emitted by the controller: path, x, y, opacity, tint, and blur.
- `HYPRLAX_SOCKET_SUFFIX` is supported by both daemon and control client and documented as the
  supported isolation mechanism.

## Provider and Operator Constraints

- ip-api's free JSON endpoint is HTTP-only, non-commercial, keyless, and rate-limited. The sidecar
  is far below the service rate limit but treats one attempt per resolved local day as absolute.
- Sunrise-Sunset.org v2 supplies solar and lunar values in one HTTPS request and requires visible
  attribution. No API key is needed.
- Manual latitude/longitude/IANA-timezone input bypasses ip-api but still uses the single daily
  astronomy category unless deterministic dry-run is chosen.
- Dry-run is the only zero-external-dependency preview; status may populate/read the daily cache.

## Live-State Findings

- `WAYLAND_DISPLAY=wayland-1` and a Hyprland instance are active.
- PID 491045 runs `/usr/local/bin/hyprlax` with the user's original six-layer copied config.
- Read-only status/list succeeded on its canonical socket. The delivery smoke must not restart or
  mutate that process; it will use a unique suffix and always clean up the temporary daemon.

## GSD Execution Note

The autonomous GSD driver progressed through research, planning, plan checking, and execution
routing, but its configured external Claude workers returned HTTP 401. Under the documented GSD
fallback contract, planner, checker, executor, and verifier roles are therefore executed inline
with persisted artifacts and fresh command evidence rather than retrying an authentication loop.
