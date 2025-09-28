#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

mkdir -p "$tmpdir/.config/hyprlax"
cat >"$tmpdir/.config/hyprlax/parallax.conf" <<'CONF'
layer ./one.png 1.0 1.0 0.0
CONF

# Non-interactive invocation with legacy path should prompt conversion and exit with code 3
set +e
out=$(HOME="$tmpdir" ./hyprlax --non-interactive 2>&1)
rc=$?
set -e

echo "$out" | grep -q "Convert with: hyprlax ctl convert-config" || { echo "FAIL: did not suggest converter"; echo "$out"; exit 1; }
test "$rc" -eq 3 || { echo "FAIL: expected exit code 3, got $rc"; exit 1; }

echo "PASS: startup legacy detection suggests converter in non-interactive mode"

