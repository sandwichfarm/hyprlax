#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

# Create a simple legacy config
cat >"$tmpdir/parallax.conf" <<'CONF'
layer ./bg.png 0.2 1.0 0.0
layer ./fg.png 1.0 0.9 0.0
duration 1.5
shift 200
easing sine
CONF

# Touch the referenced files, not required but realistic
touch "$tmpdir/bg.png" "$tmpdir/fg.png"

dst="$tmpdir/hyprlax.toml"

echo "Converting legacy -> TOML ..."
./hyprlax ctl convert-config "$tmpdir/parallax.conf" "$dst" --yes >/dev/null

test -f "$dst" || { echo "FAIL: TOML not created"; exit 1; }

echo "Asserting TOML contains expected keys ..."
grep -q "\[global\]" "$dst" || { echo "FAIL: missing [global]"; exit 1; }
grep -q "duration = 1.5" "$dst" || { echo "FAIL: missing duration"; exit 1; }
grep -q "shift = 200" "$dst" || { echo "FAIL: missing shift"; exit 1; }
grep -q "easing = \"sine\"" "$dst" || { echo "FAIL: missing easing"; exit 1; }
grep -q "\[\[global.layers\]\]" "$dst" || { echo "FAIL: missing layers"; exit 1; }
grep -E -q "path = \"(\./)?bg.png\"" "$dst" || { echo "FAIL: missing bg layer"; exit 1; }
grep -E -q "path = \"(\./)?fg.png\"" "$dst" || { echo "FAIL: missing fg layer"; exit 1; }

echo "PASS: convert-config basic mapping works"
