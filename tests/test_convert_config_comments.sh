#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

cat >"$tmpdir/parallax.conf" <<'CONF'
# Leading comment line
layer ./bg.png         # comment after path; defaults for shift/opacity/blur
layer ./fg.png 0.6 0.9 # comment after values

# Global settings with inline comments
duration 1.5  # seconds
shift 200     # pixels
easing sine   # type
fps 120       # rate
vsync 1       # boolean
idle_poll_rate 2.0 # hz
scale 1.1 # global scale
CONF

touch "$tmpdir/bg.png" "$tmpdir/fg.png"

dst="$tmpdir/hyprlax.toml"

echo "Converting legacy with inline comments ..."
"$PWD"/hyprlax ctl convert-config "$tmpdir/parallax.conf" "$dst" --yes >/dev/null

test -f "$dst" || { echo "FAIL: TOML not created"; exit 1; }

echo "Asserting TOML contents ..."
grep -q "\\[global\\]" "$dst" || { echo "FAIL: missing [global]"; exit 1; }
grep -q "duration = 1.500" "$dst" || { echo "FAIL: missing/incorrect duration"; exit 1; }
grep -q "shift = 200.000" "$dst" || { echo "FAIL: missing/incorrect shift"; exit 1; }
grep -q "easing = \"sine\"" "$dst" || { echo "FAIL: missing/incorrect easing"; exit 1; }
grep -q "fps = 120" "$dst" || { echo "FAIL: missing/incorrect fps"; exit 1; }
grep -q "vsync = true" "$dst" || { echo "FAIL: missing/incorrect vsync"; exit 1; }
grep -q "idle_poll_rate = 2.000" "$dst" || { echo "FAIL: missing/incorrect idle_poll_rate"; exit 1; }
grep -q "scale = 1.100" "$dst" || { echo "FAIL: missing/incorrect scale"; exit 1; }

# Expect two layer blocks
layers_count=$(grep -c "^\[\[global.layers\]\]$" "$dst")
test "$layers_count" -eq 2 || { echo "FAIL: expected 2 layers, got $layers_count"; exit 1; }

# bg.png should have defaults applied (1.000 for shift_multiplier and opacity)
grep -q "path = \"bg.png\"" "$dst" || { echo "FAIL: missing bg.png layer"; exit 1; }
grep -q "shift_multiplier = 1.000" "$dst" || { echo "FAIL: bg.png default shift_multiplier not applied"; exit 1; }
grep -q "opacity = 1.000" "$dst" || { echo "FAIL: bg.png default opacity not applied"; exit 1; }

# fg.png should have provided values (0.6, 0.9)
grep -q "path = \"fg.png\"" "$dst" || { echo "FAIL: missing fg.png layer"; exit 1; }
grep -q "shift_multiplier = 0.600" "$dst" || { echo "FAIL: fg.png shift_multiplier incorrect"; exit 1; }
grep -q "opacity = 0.900" "$dst" || { echo "FAIL: fg.png opacity incorrect"; exit 1; }

echo "PASS: convert-config handles inline comments"

