#!/usr/bin/env python3
"""Read-only structural check of full-canvas PNG planes, ordered back to front."""

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", help="PNG paths, opaque base first")
    parser.add_argument("--max-texture-size", type=int, help="Measured target GPU dimension limit")
    parser.add_argument("--allow-transparent-base", action="store_true",
                        help="For targets with an intentionally external background")
    args = parser.parse_args()
    if len(args.images) < 2:
        parser.error("Provide at least two planes, ordered back to front")
    if args.max_texture_size is not None and args.max_texture_size <= 0:
        parser.error("--max-texture-size must be positive")
    try:
        from PIL import Image
    except ImportError:
        print("Pillow is required for this optional image inspection helper.", file=sys.stderr)
        return 2

    errors = []
    layers = []
    canvas = None
    total_bytes = 0
    for index, path in enumerate(args.images):
        try:
            with Image.open(path) as im:
                if im.format != "PNG":
                    errors.append(f"{path}: expected PNG export, got {im.format}")
                if getattr(im, "n_frames", 1) != 1:
                    errors.append(f"{path}: animation is outside this static-plane check")
                im.load()
                size = im.size
                if canvas is None:
                    canvas = size
                if size != canvas:
                    errors.append(f"{path}: canvas {size} differs from {canvas}")
                if args.max_texture_size and max(size) > args.max_texture_size:
                    errors.append(f"{path}: exceeds target texture dimension limit")
                alpha = im.convert("RGBA").getchannel("A")
                histogram = alpha.histogram()
                pixels = size[0] * size[1]
                minimum, maximum = alpha.getextrema()
                if maximum == 0:
                    errors.append(f"{path}: plane is entirely transparent")
                if index == 0 and minimum != 255 and not args.allow_transparent_base:
                    errors.append(f"{path}: background plate is not fully opaque")
                if index > 0 and minimum == 255:
                    errors.append(f"{path}: overlay is entirely opaque; no alpha openings")
                total_bytes += pixels * 4
                layers.append({
                    "path": path,
                    "size": list(size),
                    "alpha_range": [minimum, maximum],
                    "transparent_fraction": histogram[0] / pixels,
                    "partial_alpha_fraction": sum(histogram[1:255]) / pixels,
                    "nonzero_alpha_bbox": alpha.getbbox(),
                })
        except (OSError, ValueError, Image.DecompressionBombError) as exc:
            errors.append(f"{path}: {exc}")
    print(json.dumps({
        "structural_check_passed": not errors,
        "layers": layers,
        "base_rgba_bytes": total_bytes,
        "errors": errors,
        "limits": "Does not verify content, registration, hidden reconstruction, "
                  "straight-alpha provenance, seamlessness, or motion coverage.",
    }, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
