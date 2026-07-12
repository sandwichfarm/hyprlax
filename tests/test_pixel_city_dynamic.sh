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
paths = {layer["path"] for layer in layers}
assert {f"./{index}.png" for index in range(1, 7)} <= paths
assert all((config.parent / layer["path"]).is_file() for layer in layers)
print(f"pixel-city-dynamic TOML: six base layers preserved; {len(layers)} total layers present")
PY
