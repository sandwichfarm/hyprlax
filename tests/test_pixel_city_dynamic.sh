#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

python3 -m unittest -v tests/test_pixel_city_dynamic.py
python3 -m unittest -v tests/test_pixel_city_dynamic_weather.py
tests/gif_probe \
    examples/pixel-city-dynamic/generated/weather-cloud-a.gif \
    examples/pixel-city-dynamic/generated/weather-fog-back-a.gif \
    examples/pixel-city-dynamic/generated/weather-precip-back-a.gif \
    examples/pixel-city-dynamic/generated/weather-fog-front-a.gif \
    examples/pixel-city-dynamic/generated/weather-precip-front-a.gif
weather_gif_tmp="$(mktemp -d)"
trap 'rm -rf "$weather_gif_tmp"' EXIT
WEATHER_GIF_TMP="$weather_gif_tmp" python3 - <<'PY'
from datetime import datetime, timezone
import importlib.util
import os
from pathlib import Path
import sys

root = Path.cwd()
example = root / "examples" / "pixel-city-dynamic"

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

weather = load("pixel_city_weather_probe", example / "weather.py")
scene = load("pixel_city_scene_probe", example / "dynamic_scene.py")
output = Path(os.environ["WEATHER_GIF_TMP"])
now = datetime(2026, 7, 12, 12, tzinfo=timezone.utc)
cases = (
    ("cloud", "cloudy", "weather-cloud"),
    ("fog-back", "fog", "weather-fog-back"),
    ("fog-front", "fog", "weather-fog-front"),
    ("rain-back", "rain-heavy", "weather-precip-back"),
    ("rain-front", "rain-heavy", "weather-precip-front"),
    ("snow-front", "snow-heavy", "weather-precip-front"),
    ("hail-front", "hail-heavy", "weather-precip-front"),
    ("wind-debris", "wind", "weather-precip-front"),
)
for filename, preset, layer in cases:
    sample = weather.preset_weather_sample(preset, now)
    state = weather.derive_weather_state(sample)
    (output / f"{filename}.gif").write_bytes(
        weather.render_weather_gif(layer, state)
    )
for index in range(1, 4):
    width, height, pixels = scene.decode_png_rgba(example / f"{index}.png")
    (output / f"heat-{index}.gif").write_bytes(
        weather.render_heat_gif(pixels, 1.0, width, height)
    )
PY
tests/gif_probe "$weather_gif_tmp"/*.gif
python3 - <<'PY'
from pathlib import Path
import tomllib

config = Path("examples/pixel-city-dynamic/parallax.toml")
with config.open("rb") as handle:
    layers = tomllib.load(handle)["global"]["layers"]
paths = {layer["path"] for layer in layers}
assert {f"./{index}.png" for index in range(1, 7)} <= paths
assert all((config.parent / layer["path"]).is_file() for layer in layers)
expected = [
    "./1.png",
    "./generated/sun-a.png",
    "./generated/moon-a.png",
    "./generated/weather-cloud-a.gif",
    "./2.png",
    "./3.png",
    "./generated/weather-fog-back-a.gif",
    "./4.png",
    "./generated/weather-precip-back-a.gif",
    "./5.png",
    "./generated/shadow-a.png",
    "./6.png",
    "./generated/weather-fog-front-a.gif",
    "./generated/weather-precip-front-a.gif",
]
assert [layer["path"] for layer in layers] == expected
print("pixel-city-dynamic TOML: six base layers preserved; 14 core layers present")
PY
rm -rf "$weather_gif_tmp"
trap - EXIT
