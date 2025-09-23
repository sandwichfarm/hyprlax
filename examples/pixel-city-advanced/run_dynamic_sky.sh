#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Load secrets if present
if [[ -f "$DIR/.secrets.env" ]]; then
  # shellcheck disable=SC1090
  source "$DIR/.secrets.env"
fi

cd "$DIR"
exec python3 "$DIR/dynamic_sky.py" "$@"

