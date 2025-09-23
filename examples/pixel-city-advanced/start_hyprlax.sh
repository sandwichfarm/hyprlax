#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Prefer system hyprlax in PATH
if command -v hyprlax >/dev/null 2>&1; then
  exec hyprlax --config "$DIR/parallax.toml"
fi

# Fallback to repo root binary if available
ROOT="$(cd "$DIR/../.." && pwd)"
if [[ -x "$ROOT/hyprlax" ]]; then
  exec "$ROOT/hyprlax" --config "$DIR/parallax.toml"
fi

echo "hyprlax not found in PATH or repo root. Build or install hyprlax first." >&2
exit 1

