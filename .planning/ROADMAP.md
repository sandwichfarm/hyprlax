# Roadmap: Hyprlax Dynamic Pixel City

## Overview

The v2.3 milestone builds the dynamic scene from the outside in: establish a valid copied example
and durable once-daily data boundary, make scene calculations deterministic, generate the new
pixel overlays, connect them safely to current IPC, then prove and document the complete operator
experience before opening the PR.

## Milestone

- [ ] **v2.3 Dynamic Pixel City** — Phases 1-5

## Phases

- [x] **Phase 1: Daily Data Foundation** - Valid copied scene plus bounded, cached location and astronomy inputs (completed 2026-07-12)
- [ ] **Phase 2: Astronomical Scene Model** - Deterministic continuous sun, moon, and lighting calculations
- [ ] **Phase 3: Pixel Sky and Shadows** - Dependency-free phase-correct celestial and directional shadow overlays
- [ ] **Phase 4: Safe IPC Controller** - Managed-only runtime animation, loop/status/override surfaces, and error recovery
- [ ] **Phase 5: Operational Proof and Delivery** - Full tests, documentation, runtime smoke evidence, and verified PR

## Phase Details

### Phase 1: Daily Data Foundation
**Goal**: A valid copied Pixel City example can resolve, validate, cache, and reuse daily location/astronomy facts without exceeding one attempt per provider/date.
**Depends on**: Nothing
**Requirements**: BASE-01, BASE-02, GEO-01, GEO-02, ASTRO-01, ASTRO-02, CACHE-01, CACHE-02
**Success Criteria** (what must be TRUE):
  1. User can parse and launch the corrected copied TOML without changing the original example.
  2. Same-day repeated, failed, and concurrent invocations produce no more than one provider attempt per category.
  3. Manual location bypasses ip-api and validated last-good cache survives provider failure.
  4. Daily astronomy supplies solar and lunar fields or a deterministic neutral fallback for null/polar/offline cases.
**Plans**: 2 plans

Plans:
- [x] 01-01: Copy/correct the example and implement versioned locked daily cache/provider clients
- [x] 01-02: Add deterministic provider fixtures and request-ceiling/offline validation

### Phase 2: Astronomical Scene Model
**Goal**: Pure calculations transform a zoned time plus astronomy into smooth, bounded scene values for all requested lighting states and lunar conditions.
**Depends on**: Phase 1
**Requirements**: LIGHT-01, LIGHT-02, LIGHT-03, SKY-01, SKY-02
**Success Criteria** (what must be TRUE):
  1. Fixed-time previews identify and blend every requested named state without discontinuities at anchors.
  2. Sun and moon trajectories/visibility remain bounded for normal, null, and cross-midnight event sequences.
  3. Full, intermediate, and new-moon nights produce measurably different lunar fill and city-light values.
  4. Layer plans use only current tint/opacity/blur/visibility primitives and accurately describe the saturation impression.
**Plans**: 2 plans

Plans:
- [ ] 02-01: Implement normalized timeline, keyframe interpolation, and solar/lunar trajectories
- [ ] 02-02: Lock named states, DST/polar transitions, and moon illumination with model tests

### Phase 3: Pixel Sky and Shadows
**Goal**: The example produces valid, phase-correct celestial sprites and solar-directional shadows that fit the source 576x324 pixel scene.
**Depends on**: Phase 2
**Requirements**: ASSET-01, SHADOW-01, ASSET-02
**Success Criteria** (what must be TRUE):
  1. Generated PNGs have valid signatures/chunks/CRCs, 576x324 RGBA dimensions, and nonempty bounded alpha.
  2. New, quarter, and full moon fixtures have distinct and correct lit-area/side behavior.
  3. Morning and afternoon shadows point in opposite directions; noon shadows are shorter/fainter; night hides them.
  4. A/B asset replacement is atomic and changes path identity for reliable texture reload.
**Plans**: 2 plans

Plans:
- [ ] 03-01: Implement standard-library PNG writer and celestial sprite generation
- [ ] 03-02: Implement projected pixel shadows, double buffering, and artifact tests

### Phase 4: Safe IPC Controller
**Goal**: A long-running sidecar animates only the copied example's layers through current Hyprlax IPC and remains inspectable/recoverable.
**Depends on**: Phase 3
**Requirements**: IPC-01, IPC-02, CLI-01, OPS-01
**Success Criteria** (what must be TRUE):
  1. Controller discovers dynamic and base managed layers from canonical paths and rejects missing/duplicate/whitespace-unsafe ownership.
  2. Dry-run emits the same bounded delta commands as live mode and never includes an unrelated fixture layer or `ctl clear`.
  3. Loop, once, status, fixed-time, and fixed-location modes work with clear exit codes and provider/IPC diagnostics.
  4. Sun/moon x/y and opacity plus lighting/shadow path/tint deltas travel through `hyprlax ctl modify` without daemon restarts.
**Plans**: 2 plans

Plans:
- [ ] 04-01: Implement managed layer discovery, command planning, and delta-only IPC adapter
- [ ] 04-02: Implement controller CLI/loop/status/failure handling and mock-socket integration tests

### Phase 5: Operational Proof and Delivery
**Goal**: Users can install and operate the scene from documentation, and fresh verification proves every milestone requirement before the branch is published as a PR.
**Depends on**: Phase 4
**Requirements**: DOC-01, TEST-01, DELIV-01
**Success Criteria** (what must be TRUE):
  1. README gives exact copy/run/systemd steps, daily request policy, privacy/commercial warnings, attribution, overrides, offline behavior, and troubleshooting.
  2. Automated coverage exercises the full requirement matrix, including concurrent failed fetches and unrelated-layer isolation.
  3. Build, C tests, script tests, TOML/Python/static checks, deterministic dry-run, and available live IPC smoke checks have fresh evidence.
  4. Lore-compliant commits are pushed and GitHub readback shows an open PR against origin/master with verification results and known environment gaps.
**Plans**: 2 plans

Plans:
- [ ] 05-01: Complete docs, service example, integration fixtures, and requirement-level verification
- [ ] 05-02: Run final audits, create Lore delivery commit(s), push, open PR, and verify remote state

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Daily Data Foundation | 2/2 | Complete    | 2026-07-12 |
| 2. Astronomical Scene Model | 0/2 | Not started | - |
| 3. Pixel Sky and Shadows | 0/2 | Not started | - |
| 4. Safe IPC Controller | 0/2 | Not started | - |
| 5. Operational Proof and Delivery | 0/2 | Not started | - |

