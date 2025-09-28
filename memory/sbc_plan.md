# Saturation/Brightness/Contrast (SBC) Filter — End‑to‑End Plan

Goal: Add a per-layer SBC filter with zero regressions, minimal renderer changes, full CLI + IPC + docs + example + tests. Defaults remain neutral so existing setups are unaffected.

## Design Principles
- Backward compatible: neutral defaults, feature disabled unless set.
- Minimal renderer impact: add uniforms + fast path when neutral.
- Per-layer scope with optional global defaults.
- Performance-safe: branchless math when enabled; skip entirely when neutral.

## Data Model and Defaults
- struct layer: add `sbc_enabled`, `saturation`, `brightness`, `contrast`.
- Defaults: `sbc_enabled=false`, `saturation=1.0`, `brightness=0.0`, `contrast=1.0`.
- Initialize in layer creation; copy through layer duplication/moves.

## Configuration (TOML)
- Accept in per-layer tables:
  - Composite: `sbc = [sat, bri, con]` → enables SBC.
  - Individual: `saturation`, `brightness`, `contrast` → any present enables SBC.
- Validation:
  - `saturation >= 0.0` (typical 0..3 recommended).
  - `contrast >= 0.0` (typical 0..3 recommended).
  - `brightness` typically in `[-1.0, 1.0]` (not hard-clamped unless needed).
- Global defaults (optional): top-level `default_sbc = [sat, bri, con]` applied to new layers unless overridden.

## CLI
- Add `--sbc sat,bri,con` for most recently defined `--layer`.
- Add `--default-sbc sat,bri,con` for global defaults (neutral if omitted).
- Strict parsing with clear errors; values validated as above.
- Update `--help` with examples.

## IPC
- Properties exposed via `hyprlax ctl`:
  - Set composite: `hyprlax ctl set layer <id> sbc <sat> <bri> <con>` (enables SBC).
  - Set individual: `saturation|brightness|contrast` (also enables SBC).
  - Get: `hyprlax ctl get layer <id> sbc` returns values + enabled flag.
  - Optional clear: `hyprlax ctl clear layer <id> sbc` → reset to neutral and disable.
- Include SBC fields in `status`/`list` when enabled (or show neutral explicitly in verbose/debug modes).

## Renderer API Plumbing
- renderer.h/c: extend per-layer render state with SBC fields.
- Only pass SBC data to backend when `sbc_enabled` or values non-neutral.
- Preserve ordering with existing effects (tint/blur integration noted below).

## GLES2 Backend and Shader
- Uniforms added and cached at link:
  - `bool uSbcEnabled`
  - `float uSaturation`
  - `float uBrightness`
  - `float uContrast`
- Fragment shader SBC block (applied after tint, before output):
  - Contrast: `rgb = (rgb - 0.5) * uContrast + 0.5;`
  - Brightness: `rgb += uBrightness;`
  - Saturation: `l = dot(rgb, vec3(0.2126, 0.7152, 0.0722)); rgb = mix(vec3(l), rgb, uSaturation);`
  - Clamp: `rgb = clamp(rgb, 0.0, 1.0);`
- Fast path: if `uSbcEnabled == false`, skip all SBC math.

## Effect Ordering
- Existing order maintained with minimal disruption:
  1) Sample base texture
  2) Apply existing tint (if any)
  3) Apply SBC (new)
  4) Compose/alpha as usual
- Blur stays separate as it operates on spatial domain; SBC is color domain.

## Debug Logging (behind config.debug)
- Parsing: log parsed SBC per layer and any clamping decisions.
- Renderer link: log uniform locations found once.
- IPC: log set/get of SBC values.

## Examples (TOML in dedicated subdirectory)
- Create `examples/sbc_demo/`:
  - `config.toml`:
    - Layer A (background): `sbc = [0.2, 0.0, 1.0]` (desaturated background)
    - Layer B (mid): `saturation=1.0, brightness=0.05, contrast=1.1` (individual keys)
    - Layer C (foreground): `sbc = [1.2, 0.08, 1.25]` (boosted contrast/brightness)
  - `README.md`:
    - What SBC does, recommended ranges, visual expectations.
    - How to run the demo with the program’s TOML config loader.

## Documentation
- `docs/filters.md` (new) or augment existing rendering docs:
  - Explain SBC math, ranges, defaults, and order relative to tint/blur.
- `docs/cli.md`: document `--sbc` and `--default-sbc` with examples.
- `docs/ipc.md`: document set/get/clear and status/list output changes.
- Changelog/README: note feature, compatibility, and performance notes.

## Tests
- Config/CLI parsing tests:
  - Valid/invalid `--sbc` and TOML `sbc=[..]`/individual keys.
  - Neutral defaults unchanged when not specified.
- IPC tests:
  - Composite and individual set/get; enabling behavior; clear to neutral.
- Renderer tests:
  - CPU reference SBC vs shader output on tiny textures within epsilon.
  - Ensure neutral fast path equals baseline rendering.
- Integration:
  - Run with a layer using SBC; verify renderer receives enabled + values.
- Memory/perf:
  - Valgrind clean; neutral path performance within baseline noise; enabled cost within frame budget.

## Regression and Performance Strategy
- Neutral defaults + disabled flag ensure no behavior change unless used.
- Single conditional in shader; otherwise math is simple and safe for 144 FPS.
- Avoid additional allocations in render loop; cache uniform locations.

## Implementation Steps (High Level)
1. Add layer fields, defaults, copy semantics.
2. Extend config parsing (TOML) with composite/individual keys.
3. Add CLI flags and validation, update help text.
4. Add IPC set/get/clear and serialization in status/list.
5. Extend renderer API; plumb SBC to GLES2 backend.
6. Add uniforms and SBC math to fragment shader; fast path when disabled.
7. Add `examples/sbc_demo/` with config.toml and README.
8. Update docs (filters, CLI, IPC, changelog/README snippet).
9. Add tests (parsing, IPC, renderer, integration); run memcheck.
10. Verify performance and visual checks; finalize.

## Progress
- Completed: Data model + defaults
- Completed: CLI `--sbc` and `--default-sbc` with validation
- Completed: TOML parsing for per-layer `sbc=[..]` and individual keys; global `default_sbc`
- Completed: IPC set/get for `layer.<id>.sbc` and individual fields; LIST includes SBC when enabled
- Completed: Renderer API, GLES2 plumbing, and shader SBC math with neutral fast path
- Completed: Example `examples/sbc_demo/` (TOML + README)
- Completed: Docs (CLI, IPC, new Filters guide; README link)
- Completed: Tests (TOML layer SBC); full suite passing
- Completed: Build and perf sanity (neutral path unchanged)
