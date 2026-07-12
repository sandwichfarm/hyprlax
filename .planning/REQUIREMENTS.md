# Requirements: Hyprlax Dynamic Pixel City

**Defined:** 2026-07-12
**Milestone:** v2.3 Dynamic Pixel City
**Core Value:** Pixel City visibly and automatically tracks the real local sky while remaining useful offline after the last successful daily refresh.

## v2.3 Requirements

### Example Foundation

- [x] **BASE-01**: User can launch a new copied Pixel City example from a valid TOML config without changing `examples/pixel-city`.
- [x] **BASE-02**: The new example remains dependency-free beyond Hyprlax and Python 3.9+ standard library.

### Daily Location and Astronomy

- [x] **GEO-01**: Controller resolves approximate latitude, longitude, IANA timezone, and locality from ip-api.com at most once per resolved local calendar date, including failed attempts and concurrent starts.
- [x] **GEO-02**: User can override latitude, longitude, and IANA timezone so IP geolocation is optional and deterministic.
- [x] **ASTRO-01**: Controller resolves sunrise, sunset, twilight, solar-noon, and solar-position anchors from a free API at most once per resolved local calendar date.
- [x] **ASTRO-02**: Controller resolves moonrise, moonset, named phase, and illumination from a free API at most once per resolved local calendar date.
- [x] **CACHE-01**: Provider attempts and last-good responses are schema-versioned, locked across processes, and atomically persisted under the XDG cache directory.
- [x] **CACHE-02**: Controller continues with stale last-good data or deterministic neutral fallback when providers, schemas, DNS, or the network fail.

### Scene Model

- [x] **LIGHT-01**: Scene model continuously interpolates sunrise, morning, high noon, late afternoon, sunset, night, and intermediate lighting states from current local time and astronomical anchors.
- [x] **LIGHT-02**: Scene model produces per-layer tint/opacity/blur values that visibly change warmth, brightness, contrast, stars/windows, and a saturation impression without claiming unsupported saturation control.
- [x] **LIGHT-03**: Night lighting and moon visibility scale continuously with moon phase, illumination, and lunar rise/set availability.
- [x] **SKY-01**: Scene model produces bounded sun visibility, opacity, and x/y trajectory across the real daylight interval.
- [x] **SKY-02**: Scene model produces bounded moon visibility, phase appearance, opacity, and x/y trajectory across the lunar visibility interval.

### Generated Visuals

- [ ] **ASSET-01**: Example generates valid transparent 576x324 pixel-art sun and phase-correct moon PNG overlays using only Python standard library.
- [ ] **SHADOW-01**: Example synthesizes a directional pixel shadow overlay whose direction, length, and opacity respond to solar position and disappear safely when the sun is unavailable.
- [ ] **ASSET-02**: Dynamic image updates are atomic and double-buffered so Hyprlax never reads a partial PNG and path changes reliably reload textures.

### IPC and Operation

- [ ] **IPC-01**: Controller discovers only its managed layers by canonical distinctive paths from `hyprlax ctl list --json`, never hard-codes IDs, and never modifies unrelated daemon layers.
- [ ] **IPC-02**: Controller applies sun/moon motion, lighting, and shadow deltas through `hyprlax ctl modify`/the runtime socket without restarting the daemon or using `ctl clear`.
- [ ] **CLI-01**: User can run loop, one-shot, dry-run/status, fixed-time, and fixed-location modes with actionable errors and no network/compositor requirement for deterministic preview.
- [ ] **OPS-01**: Controller bounds update cadence, HTTP response size/timeouts, provider retry behavior, and IPC subprocess failures without blocking Hyprlax's render loop.

### Documentation, Tests, and Delivery

- [ ] **DOC-01**: Example README documents copy/setup/run/systemd operation, privacy, ip-api HTTP/non-commercial limits, exact daily request behavior, offline fallback, and visible linked Sunrise-Sunset attribution.
- [ ] **TEST-01**: Automated tests cover same-day success/failure/concurrency request ceilings, schema/range validation, stale fallback, DST/polar/null events, every named lighting state, new/quarter/full moon, shadow/PNG generation, managed-only IPC commands, and error propagation.
- [ ] **DELIV-01**: Branch passes build, C tests, script tests, TOML/Python/static checks, dry-run fixture validation, and a real IPC smoke test where the environment permits; commits are pushed and a verified PR is opened on origin.

## Out of Scope

| Feature | Reason |
|---------|--------|
| Pixel-perfect scientific atmosphere | Astronomical events are accurate anchors; the art direction is intentionally stylized |
| GPS/continuous device tracking | Daily approximate IP location is the requested boundary |
| Weather/cloud simulation | Adds unrelated requests and dependencies |
| New renderer saturation control | The request asks to deploy existing abilities; current Hyprlax exposes tint/opacity but no saturation property |
| PR #59 source/assets | Historical evidence only; the implementation starts from current origin/master |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| BASE-01 | Phase 1 | Complete |
| BASE-02 | Phase 1 | Complete |
| GEO-01 | Phase 1 | Complete |
| GEO-02 | Phase 1 | Complete |
| ASTRO-01 | Phase 1 | Complete |
| ASTRO-02 | Phase 1 | Complete |
| CACHE-01 | Phase 1 | Complete |
| CACHE-02 | Phase 1 | Complete |
| LIGHT-01 | Phase 2 | Complete |
| LIGHT-02 | Phase 2 | Complete |
| LIGHT-03 | Phase 2 | Complete |
| SKY-01 | Phase 2 | Complete |
| SKY-02 | Phase 2 | Complete |
| ASSET-01 | Phase 3 | Pending |
| SHADOW-01 | Phase 3 | Pending |
| ASSET-02 | Phase 3 | Pending |
| IPC-01 | Phase 4 | Pending |
| IPC-02 | Phase 4 | Pending |
| CLI-01 | Phase 4 | Pending |
| OPS-01 | Phase 4 | Pending |
| DOC-01 | Phase 5 | Pending |
| TEST-01 | Phase 5 | Pending |
| DELIV-01 | Phase 5 | Pending |

**Coverage:**
- v2.3 requirements: 23 total
- Mapped to phases: 23
- Unmapped: 0 ✓

---
*Requirements defined: 2026-07-12*
*Last updated: 2026-07-12 after provider, IPC, and PR #59 research*

