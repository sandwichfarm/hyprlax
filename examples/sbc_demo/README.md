# SBC Demo (Saturation / Brightness / Contrast)

This demo showcases per-layer color adjustments using Saturation, Brightness, and Contrast (SBC).

- Background: desaturated for depth separation.
- Midground: slight brightness lift and contrast via individual keys.
- Foreground: boosted contrast with mild brightness.

## Files
- `config.toml` — Example configuration using TOML with both composite `sbc = [sat, bri, con]` and individual keys.

## How to Run

If your build supports TOML configuration loading, run:

```
./hyprlax -c examples/sbc_demo/config.toml
```

Or copy `config.toml` into your preferred config location and launch per your workflow.

## Recommended Ranges
- saturation: >= 0.0 (typical 0.0 – 3.0)
- brightness: around -1.0 – 1.0
- contrast: >= 0.0 (typical 0.0 – 3.0)

Values outside these ranges may still work but can clip colors; the renderer clamps to [0, 1] after SBC.

## Notes
- SBC is applied after tint and before output, ensuring predictable composition with existing effects.
- When SBC is not specified for a layer, defaults are neutral and the feature remains disabled.
