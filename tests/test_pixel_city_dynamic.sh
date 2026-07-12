#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

python3 -m unittest -v tests/test_pixel_city_dynamic.py
python3 - <<'PY'
from pathlib import Path
import tomllib

config = Path("examples/pixel-city-dynamic/parallax.toml")
with config.open("rb") as handle:
    layers = tomllib.load(handle)["global"]["layers"]
assert len(layers) == 6
assert all((config.parent / layer["path"]).is_file() for layer in layers)
print("pixel-city-dynamic TOML: 6 layers present")
PY
