# Phase 4 Research: Safe IPC Controller

## Current IPC Facts

- `hyprlax ctl list --json` emits id/path/opacity/uv/blur/tint for every layer.
- `modify <id> <property> <value>` supports path, x/y, opacity, tint, blur, visibility, and more,
  but only one property per call. Paths are whitespace-tokenized.
- Config order gives standalone assumed IDs: 1.png=1, sun=2, moon=3, 2.png=4, 3.png=5,
  4.png=6, 5.png=7, shadow=8, 6.png=9. Dry-run must label them assumed, never claim live IDs.
- JSON output may be a list directly; reject wrappers/strings and invalid id/path types.

## Command Order

1. Base layer tint/opacity/blur deltas.
2. Moon inactive path, sun/moon x/y, then opacities.
3. Shadow inactive path, then opacity.

Asset writes happen before command construction. If an IPC command fails, the active path remains
discoverable on the next tick and the inactive buffer can be safely overwritten/retried.

