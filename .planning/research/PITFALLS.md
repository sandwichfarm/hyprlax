# Pitfalls Research: Dynamic Pixel City

**Researched:** 2026-07-12

## Critical Pitfalls

### Free ip-api is HTTP-only and non-commercial

The response can be intercepted or modified and the free service cannot be represented as
commercially unrestricted. Bound field sizes and coordinate ranges, offer manual overrides, and
document the limitation prominently.

### A failed request still counts

Writing only successful fetch dates allows every process restart to retry, violating the daily
request ceiling. Persist the attempt date before opening the network connection and keep stale
success data separately.

### Two processes can stampede

Systemd restarts or manual invocations can race. Hold a filesystem lock across the attempt-ledger
check/update and fetch decision.

### New moon fields are a fresh API surface

Sunrise-Sunset.org added moon data in July 2026. Validate every field and preserve a deterministic
fallback/neutral lunar state if the provider removes or changes it.

### Polar events may be null

Do not invert or manufacture missing sunrise/sunset/moonrise/moonset timestamps. Use `sun_status`
and a safe polar-night/midnight-sun scene strategy.

### Runtime layer IDs are unstable

Discover by path from `ctl list --json`; report missing/duplicate dynamic layers instead of
modifying hard-coded numeric IDs.

Canonicalize managed paths and match distinctive dynamic filenames. PR #59 accidentally gathered
every daemon layer before filtering and could tint unrelated wallpapers; `clear` must never be used.

### Hyprlax has tint, not saturation

There is no saturation uniform, config key, IPC property, or test on current `origin/master`.
Model a saturation *impression* by applying different tint strengths and opacities to the existing
depth layers. Do not send an unsupported `saturation` property or claim literal HSV control.

### IPC paths are whitespace-tokenized

Generated/managed asset paths must not contain spaces. Validate this before starting instead of
letting a partially applied `add` or `modify path` command corrupt ownership assumptions.

### Updating a PNG in place may not reload the texture

Write the inactive side of an A/B asset pair, then change the layer path over IPC. Atomic writes
avoid partially decoded images.

### UV direction and aspect ratios vary visually

Keep motion math pure and bounded, use non-tiling overflow for celestial layers, document the
stylized arc, and include a deterministic manual-time preview path for real-compositor testing.

PR #59 also read the obsolete `monitors[].width/height` shape while current status JSON exposes a
`size` pair. Avoid monitor-size dependence entirely for the 576x324 pixel-art overlays where
possible; if status geometry is used later, contract-test the current schema.

### The copied source config is invalid on current master

`examples/pixel-city/parallax.toml` ends in literal `blur = 0.0VV`. The new example must correct
that value and validate TOML before launch; copying the directory mechanically is not sufficient.

### Attribution is mandatory

Sunrise-Sunset.org requires visible linked attribution. Put it in the example README near the
data behavior, not only in source comments.

## Verification Implications

- Tests must count fetcher calls across same-day restarts and failures.
- Tests must cover DST-aware dates, null polar events, full/new/intermediate moon, and midnight.
- Generated PNGs must be decoded by an independent test parser or at least validate signature,
  chunk CRCs, dimensions, and non-empty alpha.
- Dry-run output must enumerate actual `hyprlax ctl modify` commands for all named layers.
- Tests must prove unrelated daemon layers are never modified.
