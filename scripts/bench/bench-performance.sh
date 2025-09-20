#!/bin/bash

# Performance testing script for hyprlax
# Tests GPU usage, FPS, and idle behavior

# Always run from repo root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR" || exit 1

# Config path: env override takes precedence, else first arg, else default example
CONFIG_DEFAULT="examples/pixel-city/parallax.conf"
CONFIG=${HYPRLAX_BENCH_CONFIG:-${1:-$CONFIG_DEFAULT}}
DURATION=${2:-30}  # Test duration in seconds

echo "=== Hyprlax Performance Test ==="
echo "Config: $CONFIG"
echo "Duration: ${DURATION}s"
echo "Override config via: HYPRLAX_BENCH_CONFIG=/path/to/parallax.conf or pass as first argument"
echo ""

# Kill any existing hyprlax
pkill hyprlax 2>/dev/null
sleep 2

# Function to get GPU power usage
get_gpu_power() {
    nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits 2>/dev/null | head -1
}

# Function to get GPU utilization
get_gpu_util() {
    nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | head -1
}

# Function to get GPU performance state
get_gpu_state() {
    nvidia-smi --query-gpu=pstate --format=csv,noheader,nounits 2>/dev/null | head -1
}

# Test 0: Baseline (before hyprlax)
echo "=== Test 0: Baseline (Before Hyprlax) ==="
BASELINE_SAMPLES=3
BASELINE_POWER_SUM=0
BASELINE_UTIL_SUM=0

for i in $(seq 1 $BASELINE_SAMPLES); do
    POWER=$(get_gpu_power)
    UTIL=$(get_gpu_util)
    STATE=$(get_gpu_state)
    echo "Sample $i: Power=${POWER}W, Util=${UTIL}%, State=${STATE}"
    BASELINE_POWER_SUM=$(awk "BEGIN {print $BASELINE_POWER_SUM + $POWER}")
    BASELINE_UTIL_SUM=$(awk "BEGIN {print $BASELINE_UTIL_SUM + $UTIL}")
    sleep 1
done

BASELINE_POWER_AVG=$(echo "scale=1; $BASELINE_POWER_SUM / $BASELINE_SAMPLES" | bc)
BASELINE_UTIL_AVG=$(echo "scale=1; $BASELINE_UTIL_SUM / $BASELINE_SAMPLES" | bc)
echo "Baseline Average: Power=${BASELINE_POWER_AVG}W, Util=${BASELINE_UTIL_AVG}%"
echo ""

# Start hyprlax in background with debug
echo "Starting hyprlax..."
./hyprlax -c "$CONFIG" --debug 2>&1 | tee hyprlax-debug.log &
HYPRLAX_PID=$!
sleep 3

if ! kill -0 $HYPRLAX_PID 2>/dev/null; then
    echo "ERROR: hyprlax failed to start!"
    exit 1
fi

echo "hyprlax running with PID: $HYPRLAX_PID"
echo "Waiting for GPU to settle after startup..."
sleep 5  # Extra time for GPU to settle
echo ""

# Test 1: Initial idle state
echo "=== Test 1: Initial Idle State (3s) ==="
IDLE_SAMPLES=3
IDLE_POWER_SUM=0
IDLE_UTIL_SUM=0

for i in $(seq 1 $IDLE_SAMPLES); do
    POWER=$(get_gpu_power)
    UTIL=$(get_gpu_util)
    echo "Sample $i: Power=${POWER}W, Util=${UTIL}%"
    IDLE_POWER_SUM=$(echo "$IDLE_POWER_SUM + $POWER" | bc)
    IDLE_UTIL_SUM=$(echo "$IDLE_UTIL_SUM + $UTIL" | bc)
    sleep 1
done

IDLE_POWER_AVG=$(echo "scale=1; $IDLE_POWER_SUM / $IDLE_SAMPLES" | bc)
IDLE_UTIL_AVG=$(echo "scale=1; $IDLE_UTIL_SUM / $IDLE_SAMPLES" | bc)
echo "Idle Average: Power=${IDLE_POWER_AVG}W, Util=${IDLE_UTIL_AVG}%"
echo ""

# Test 2: Simulate workspace changes
echo "=== Test 2: Workspace Changes (10s) ==="

# Save original workspace
ORIGINAL_WS=$(hyprctl monitors -j | jq -r '.[0].activeWorkspace.id' 2>/dev/null || echo 1)
echo "Original workspace: $ORIGINAL_WS"
echo "Simulating workspace changes..."

# Test workspace changes
for NEXT_WS in 2 3 4 2; do
    echo "  Switching to workspace $NEXT_WS"
    hyprctl dispatch workspace $NEXT_WS 2>/dev/null
    
    sleep 0.5
    POWER=$(get_gpu_power)
    UTIL=$(get_gpu_util)
    STATE=$(get_gpu_state)
    echo "  During animation: Power=${POWER}W, Util=${UTIL}%, State=${STATE}"
    sleep 1.5
done

# Return to original workspace
echo "  Returning to original workspace $ORIGINAL_WS"
hyprctl dispatch workspace $ORIGINAL_WS 2>/dev/null
sleep 2

echo ""

# Test 3: Post-animation idle
echo "=== Test 3: Post-Animation Idle (5s) ==="
echo "Waiting for animations to complete..."
sleep 3

POST_SAMPLES=3
POST_POWER_SUM=0
POST_UTIL_SUM=0

for i in $(seq 1 $POST_SAMPLES); do
    POWER=$(get_gpu_power)
    UTIL=$(get_gpu_util)
    echo "Sample $i: Power=${POWER}W, Util=${UTIL}%"
    POST_POWER_SUM=$(echo "$POST_POWER_SUM + $POWER" | bc)
    POST_UTIL_SUM=$(echo "$POST_UTIL_SUM + $UTIL" | bc)
    sleep 1
done

POST_POWER_AVG=$(echo "scale=1; $POST_POWER_SUM / $POST_SAMPLES" | bc)
POST_UTIL_AVG=$(echo "scale=1; $POST_UTIL_SUM / $POST_SAMPLES" | bc)
echo "Post-Animation Average: Power=${POST_POWER_AVG}W, Util=${POST_UTIL_AVG}%"
echo ""

# Check FPS from debug log
echo "=== FPS Analysis ==="
grep "FPS:" hyprlax-debug.log | tail -5
echo ""

# Check for idle detection
echo "=== Idle Detection ==="
grep "IDLE:" hyprlax-debug.log | tail -3
echo ""

# Summary
echo "=== SUMMARY ==="
echo "Baseline (no hyprlax): Power=${BASELINE_POWER_AVG}W, Util=${BASELINE_UTIL_AVG}%"
echo "Initial Idle: Power=${IDLE_POWER_AVG}W, Util=${IDLE_UTIL_AVG}%"
echo "Post-Animation: Power=${POST_POWER_AVG}W, Util=${POST_UTIL_AVG}%"
echo ""

# Calculate differences
IDLE_DELTA=$(echo "$IDLE_POWER_AVG - $BASELINE_POWER_AVG" | bc)
POST_DELTA=$(echo "$POST_POWER_AVG - $BASELINE_POWER_AVG" | bc)

echo "Power increase from baseline:"
echo "  Initial idle: +${IDLE_DELTA}W"
echo "  Post-animation: +${POST_DELTA}W"
echo ""

if (( $(echo "$POST_POWER_AVG > $IDLE_POWER_AVG + 20" | bc -l) )); then
    echo "❌ FAIL: Post-animation power is significantly higher than initial idle!"
    echo "   This indicates GPU is not returning to idle properly."
elif (( $(echo "$IDLE_DELTA > 10" | bc -l) )); then
    echo "❌ FAIL: Idle power increase is too high (>${IDLE_DELTA}W above baseline)!"
    echo "   Hyprlax should have minimal impact when idle."
elif (( $(echo "$POST_DELTA > 10" | bc -l) )); then
    echo "❌ FAIL: Post-animation power remains elevated (+${POST_DELTA}W)!"
    echo "   GPU is not returning to idle state after animations."
else
    echo "✓ PASS: GPU power usage is within acceptable limits."
fi

# Kill hyprlax
echo ""
echo "Stopping hyprlax..."
kill $HYPRLAX_PID 2>/dev/null
wait $HYPRLAX_PID 2>/dev/null

# Make sure we're back on original workspace
if [ -n "$ORIGINAL_WS" ]; then
    echo "Ensuring return to original workspace $ORIGINAL_WS..."
    hyprctl dispatch workspace $ORIGINAL_WS 2>/dev/null
fi

echo "Test complete."
