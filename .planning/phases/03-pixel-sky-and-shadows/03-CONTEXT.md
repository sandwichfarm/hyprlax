# Phase 3: Pixel Sky and Shadows — Context

**Discussed:** 2026-07-12 (autonomous from verified SceneState)
**Status:** Ready for planning

## Boundary and Decisions

- Add standard-library PNG encode/decode and visual generation to `dynamic_scene.py`; no IPC/CLI.
- All dynamic overlays are 576x324 RGBA to match Pixel City.
- Sun is a hard-edged pixel disk with a small alpha halo, centered in a transparent full-frame
  texture; Phase 4 moves it with verified UV offsets.
- Moon illumination uses a visible-sphere dot product. Full lights the full disk, new lights none,
  first-quarter/waxing light the right, and last-quarter/waning light the left. A dim dark disk may
  remain so a new moon silhouette is possible without contributing lunar fill.
- Decode the existing `6.png` RGBA alpha and project its opaque pixels toward the ground opposite
  `SceneState.sun_x`. Low solar elevation makes longer/flatter/darker projections; noon makes them
  shorter/fainter; no visible sun yields a transparent overlay.
- A/B files are `generated/{sun,moon,shadow}-{a,b}.png`; write the inactive path via same-directory
  temp + fsync + atomic replace. Config initially points at `-a` variants with opacity 0.
- Insert sun/moon immediately after `1.png`, and shadow immediately before `6.png`, preserving the
  intended background/city/foreground visual order.

## Verification

- Independent decoder validates signature, IHDR, CRCs, zlib rows, dimensions, and RGBA roundtrip.
- Bright moon pixel counts satisfy new < quarter < full; waxing/waning bright centroids use opposite sides.
- Morning and afternoon shadow alpha centroids lie on opposite sides; noon alpha sum is lower than low sun; night is zero.
- Buffer writes alternate identities and every output decodes after replacement.
- Updated TOML parses with exactly 9 layers in the locked order.
