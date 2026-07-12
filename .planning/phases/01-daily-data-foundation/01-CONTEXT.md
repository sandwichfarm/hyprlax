# Phase 1: Daily Data Foundation — Context

**Discussed:** 2026-07-12 (autonomous from user-provided objective)
**Status:** Ready for planning

## Phase Boundary

Deliver a valid copied Pixel City example and the dependency-free daily location/astronomy data
foundation. This phase does not animate the scene; it establishes deterministic, validated inputs
and strict request/cache invariants for later phases.

## Locked Decisions

- Copy the current `examples/pixel-city` directory into `examples/pixel-city-dynamic`, preserve
  the original, remove unused copied artifacts when they provide no runtime value, and correct the
  invalid `blur = 0.0VV` TOML value.
- Use one Python 3.9+ standard-library controller module in the example directory. Pure functions
  and dependency injection must make it importable by `unittest` without network or Wayland.
- ip-api endpoint is `http://ip-api.com/json/` with a minimal `fields` query. Manual lat/lon/IANA
  timezone overrides bypass it completely.
- Sunrise-Sunset.org v2 daily endpoint supplies both solar and lunar data in one request. Solar and
  lunar requirements share this one astronomy attempt; do not issue separate moon requests.
- "Once daily" means at most one network attempt per provider category and resolved local date.
  Failures count. Concurrent controller starts must share the same decision through `fcntl.flock`.
- Persist `last_attempt_date` before opening the network connection and keep `last_success`
  independently so failures cannot destroy stale usable data.
- The provider/date gate is absolute: changing manual coordinates or cache location identity on the
  same date does not permit a second astronomy attempt. If last-good astronomy belongs to another
  location, return neutral fallback until the next local date rather than reuse wrong-place data.
- Cache JSON is schema-versioned under `${XDG_CACHE_HOME:-~/.cache}/hyprlax/pixel-city-dynamic/`,
  written with temp file + flush/fsync + atomic `os.replace`.
- Bound requests to 10 seconds and 256 KiB, validate JSON types/finite coordinate ranges/IANA zones,
  and never evaluate provider content.
- Polar/null astronomical fields are valid inputs. With no cache and no provider data, return a
  documented deterministic neutral daily record rather than preventing the scene from starting.
- Do not add dependencies or C changes in this phase.

## Provider/Policy Disclosures

- ip-api free is HTTP-only and non-commercial. This is an explicit README/operator limitation,
  not a hidden implementation detail.
- Sunrise-Sunset.org v2 requires visible linked attribution in the final example README.
- Tests must use injected fixtures and must not contact either live provider.

## Verification Shape

- TOML parses with Python `tomllib` and the copied base layers exist.
- Tests prove same-date success, failure, and multiprocessing contention each call an injected
  provider at most once.
- Tests prove manual override performs zero IP-provider calls.
- Tests prove stale success survives a new failed attempt and neutral fallback covers empty cache.
- Tests validate malformed/oversized provider data, coordinate/timezone bounds, and null/polar fields.

## Deferred to Later Phases

- Lighting keyframes and trajectory math — Phase 2.
- PNG celestial/shadow synthesis — Phase 3.
- Live layer discovery/IPC loop and CLI status/dry-run — Phase 4.
- Full user/systemd documentation and remote delivery — Phase 5.
