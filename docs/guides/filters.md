# Color Filters: Saturation, Brightness, Contrast (SBC)

This guide covers per-layer color adjustments using Saturation, Brightness, and Contrast.

- Scope: per-layer; neutral defaults preserve current visuals.
- Order: Tint → SBC → output (alpha premultiply remains unchanged).
- Performance: neutral fast path skips all SBC math; enabled path adds a few ALU ops per fragment.

## Parameters
- saturation: scales color distance from luma. Neutral 1.0, grayscale 0.0, higher increases intensity. Recommended 0.0–3.0.
- brightness: additive offset. Neutral 0.0. Recommended around -1.0–1.0.
- contrast: scales around mid-gray 0.5. Neutral 1.0, 0.0 flattens. Recommended 0.0–3.0.

GPU clamps final color to [0,1] after SBC.

## CLI
- `--sbc` sat,bri,con applies to the most recent `--layer`
- `--default-sbc` sat,bri,con sets defaults for new layers

## TOML
Composite or individual keys in each `[[global.layers]]` entry:

```
[[global.layers]]
path = "bg.png"
sbc = [0.2, 0.00, 1.0]

[[global.layers]]
path = "fg.png"
saturation = 1.2
brightness = 0.08
contrast = 1.25
```

Global default:
```
[global]
default_sbc = [1.0, 0.0, 1.1]
```

## IPC
- Set composite: `hyprlax ctl set layer.<id>.sbc 1.2,0.05,1.1`
- Set individual: `layer.<id>.saturation|brightness|contrast`
- Get: `layer.<id>.sbc`, `layer.<id>.sbc.enabled`

## Notes
- SBC is color-domain; blur remains spatial and is unaffected.
- Applying SBC after tint preserves tint hue while allowing final creative control.
