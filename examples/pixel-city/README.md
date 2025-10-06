# Pixel City (Pixel Art)

[![Engine: hyprlax](https://img.shields.io/badge/engine-hyprlax-3fb950?style=flat)](../../README.md)
[![Compositors](https://img.shields.io/badge/compositors-hyprland%20%7C%20sway%20%7C%20wayfire%20%7C%20niri%20%7C%20river-1f6feb?style=flat)](../../docs/getting-started/compatibility.md)
[![Purpose: pixel_art](https://img.shields.io/badge/purpose-pixel__art-f97316?style=flat)](#)
[![Config: TOML](https://img.shields.io/badge/config-TOML-4c1?style=flat)](../../docs/configuration/toml-reference.md)

Retro pixel-art city with multiple depth layers for a classic parallax look.

![Screenshot placeholder: pixel city with parallax layers](./screenshot.png)
![Layer diagram placeholder: ordered pixel layers 1..6](./layers.png)

## How To Use

- Preferred (TOML):
  ```bash
  ./hyprlax --config examples/pixel-city/parallax.toml
  ```
- Legacy format also included:
  ```bash
  ./hyprlax --config examples/pixel-city/parallax.conf
  ```

## Dependencies

- hyprlax (built or installed)
- Wayland compositor (Hyprland, Sway, Wayfire, Niri, River, …)

## What You’ll Learn

- Converting a legacy `.conf` to TOML
- Subtle blur on pixel art without losing crispness
- Tiling options via TOML (`render.tile`)

## Goal Of The Example

Showcase a layered pixel-art scene with smooth parallax suitable for high FPS.

## Configuration Walkthrough (TOML)

- Global: `fps = 144`, `duration = 4.0`, `shift = 0.01%`, `scale = 1.0`, `easing = "expo"`
- **Pixel-art specific**: No scaling (`scale = 1.0`) preserves pixel crispness
- **Minimal shift**: Only 0.01% per workspace for subtle movement
- `render.tile = { x = true, y = false }` — tile horizontally only
- Six layers with increasing `shift_multiplier` from 0.1 … 1.0
- All layers `opacity = 1.0`; light blur on mid layers for depth

## Attribution

Assets sourced from CraftPix: https://craftpix.net/freebies/

## Image Placeholders

- Add a screenshot as `examples/pixel-city/screenshot.png`
- Add a layer diagram as `examples/pixel-city/layers.png`
