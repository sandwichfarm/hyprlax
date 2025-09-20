#!/bin/bash

# Comprehensive optimization test script
# Always run from repo root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR" || exit 1

# Config path: allow env override, else default to example in repo
CONFIG_DEFAULT="examples/pixel-city/parallax.conf"
CONFIG="${HYPRLAX_BENCH_CONFIG:-$CONFIG_DEFAULT}"

echo "=== Hyprlax Optimization Tests ==="
echo "Using config: $CONFIG"
echo "Override config via: HYPRLAX_BENCH_CONFIG=/path/to/parallax.conf make bench"
echo ""

# Clean old test logs to avoid stale summaries
rm -f hyprlax-test-*.log 2>/dev/null || true

# Function to run a test with specific settings
run_test() {
    local TEST_NAME="$1"
    local TEST_ARGS="$2"
    
    echo "=== Test: $TEST_NAME ==="
    echo "Arguments: $TEST_ARGS"
    
    pkill hyprlax 2>/dev/null
    sleep 2
    
    # Get baseline
    BASELINE_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    echo "Baseline power: ${BASELINE_POWER}W"
    
    # Start hyprlax
    ./hyprlax -c "$CONFIG" $TEST_ARGS --debug 2>&1 | tee "hyprlax-test-${TEST_NAME// /-}.log" &
    HYPRLAX_PID=$!
    sleep 5
    
    if ! kill -0 $HYPRLAX_PID 2>/dev/null; then
        echo "ERROR: hyprlax failed to start!"
        return
    fi
    
    # Test idle
    IDLE_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    IDLE_UTIL=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1)
    echo "Idle: Power=${IDLE_POWER}W, Util=${IDLE_UTIL}%"
    
    # Test animation
    ORIGINAL_WS=$(hyprctl monitors -j | jq -r '.[0].activeWorkspace.id' 2>/dev/null || echo 1)
    
    # Do 3 quick workspace changes
    hyprctl dispatch workspace 2 2>/dev/null
    sleep 1.5
    ANIM1_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    
    hyprctl dispatch workspace 3 2>/dev/null
    sleep 1.5
    ANIM2_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    
    hyprctl dispatch workspace 4 2>/dev/null
    sleep 1.5
    ANIM3_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    
    # Calculate average animation power
    ANIM_AVG=$(awk "BEGIN {print ($ANIM1_POWER + $ANIM2_POWER + $ANIM3_POWER) / 3}")
    echo "Animation avg: ${ANIM_AVG}W"
    
    # Return to original
    hyprctl dispatch workspace $ORIGINAL_WS 2>/dev/null
    sleep 5
    
    # Post-animation idle
    POST_POWER=$(nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits | head -1)
    echo "Post-animation: ${POST_POWER}W"
    
    kill $HYPRLAX_PID 2>/dev/null
    
    # Calculate deltas
    IDLE_DELTA=$(awk "BEGIN {print $IDLE_POWER - $BASELINE_POWER}")
    POST_DELTA=$(awk "BEGIN {print $POST_POWER - $BASELINE_POWER}")
    
    echo "Power delta: Idle=+${IDLE_DELTA}W, Post=+${POST_DELTA}W"
    echo ""
    
    sleep 2
}

# Test 1: Baseline (60 FPS, high quality)
run_test "Baseline-60fps" ""

# Test 2: 30 FPS only
run_test "30fps" "--fps 30"

# Quality cases removed (CLI does not support --quality). Keeping only FPS variants.

# Summary
echo "=== OPTIMIZATION SUMMARY ==="
echo ""
echo "Test Results (Power Usage):"
echo "----------------------------"
for LOG in hyprlax-test-*.log; do
    if [ -f "$LOG" ]; then
        TEST=$(basename "$LOG" | sed 's/hyprlax-test-//;s/.log//')
        echo "$TEST:"
        grep -E "(Idle:|Animation avg:|Post-animation:)" "$LOG" | tail -3 | sed 's/^/  /'
    fi
done

echo ""
echo "Recommendations:"
echo "- For battery/low-power: Use --fps 30"
echo "- For quality: Use default settings (60 FPS)"
echo ""
echo "Test complete."
