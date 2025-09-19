#!/bin/bash

# 30 FPS performance check (moved from test-30fps.sh)

# Always run from repo root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR" || exit 1

# Config path: allow env override, else default to example in repo
CONFIG_DEFAULT="examples/pixel-city/parallax.conf"
CONFIG="${HYPRLAX_BENCH_CONFIG:-$CONFIG_DEFAULT}"

echo "=== 30 FPS Performance Test ==="
echo "Config: $CONFIG"
echo "Override config via: HYPRLAX_BENCH_CONFIG=/path/to/parallax.conf make bench-30fps"
pkill hyprlax 2>/dev/null
sleep 2

# Get baseline
echo "Baseline GPU power:"
nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1

# Start with 30 FPS
echo ""
echo "Starting hyprlax with 30 FPS limit..."
./hyprlax -c "$CONFIG" --fps 30 --debug 2>&1 | tee hyprlax-30fps.log &
HYPRLAX_PID=$!
sleep 5

if ! kill -0 $HYPRLAX_PID 2>/dev/null; then
    echo "ERROR: hyprlax failed to start!"
    exit 1
fi

echo "Idle power (30 FPS):"
nvidia-smi --query-gpu=power.draw,utilization.gpu --format=csv,noheader,nounits | head -1

# Trigger animations
echo ""
echo "Triggering workspace changes..."
ORIGINAL_WS=$(hyprctl monitors -j | jq -r '.[0].activeWorkspace.id' 2>/dev/null || echo 1)
for WS in 2 3 4 2 1; do
    hyprctl dispatch workspace $WS 2>/dev/null
    sleep 2
    POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    UTIL=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1)
    echo "  Workspace $WS: Power=${POWER}W, Util=${UTIL}%"
done

# Return to original
hyprctl dispatch workspace $ORIGINAL_WS 2>/dev/null
sleep 5

echo ""
echo "Post-animation idle (30 FPS):"
nvidia-smi --query-gpu=power.draw,utilization.gpu --format=csv,noheader,nounits | head -1

# Check FPS from log
echo ""
echo "FPS samples:"
grep "FPS:" hyprlax-30fps.log | tail -5

kill $HYPRLAX_PID 2>/dev/null
echo ""
echo "Test complete."
