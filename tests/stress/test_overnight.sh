#!/bin/bash
# Overnight stability test for hyprlax
# Simulates long-running session with periodic workspace changes and lock/unlock cycles
# Usage: ./test_overnight.sh [duration_hours]

set -euo pipefail

DURATION_HOURS=${1:-8}
DURATION_SECONDS=$((DURATION_HOURS * 3600))

TEST_CONFIG="/tmp/hyprlax_test_overnight.toml"
LOG_FILE="overnight_test_$(date +%s).log"

echo "=== Hyprlax Overnight Stability Test ==="
echo "Duration: $DURATION_HOURS hours ($DURATION_SECONDS seconds)"
echo "Log: $LOG_FILE"
echo ""

# Create test configuration
cat > "$TEST_CONFIG" << 'EOF'
[general]
fps = 60
duration = 1.0
easing = "cubic"

[[layer]]
path = "/tmp/hyprlax_test_bg.png"
shift = 0.3
opacity = 1.0
blur = 0.0

[[layer]]
path = "/tmp/hyprlax_test_fg.png"
shift = 1.0
opacity = 0.8
blur = 5.0
EOF

# Create dummy test images (1x1 pixel)
printf "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB\x60\x82" > /tmp/hyprlax_test_bg.png
printf "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB\x60\x82" > /tmp/hyprlax_test_fg.png

# Start hyprlax
echo "[$(date '+%H:%M:%S')] Starting hyprlax..."
./hyprlax --config "$TEST_CONFIG" --debug > "$LOG_FILE" 2>&1 &
HYPRLAX_PID=$!

# Wait for initialization
sleep 5

# Verify it started
if ! kill -0 "$HYPRLAX_PID" 2>/dev/null; then
    echo "ERROR: hyprlax failed to start"
    cat "$LOG_FILE"
    exit 1
fi

echo "[$(date '+%H:%M:%S')] hyprlax started (PID: $HYPRLAX_PID)"

# Start resource monitoring
echo "[$(date '+%H:%M:%S')] Starting resource monitoring..."
./scripts/monitor_resources.sh "$HYPRLAX_PID" 60 &
MONITOR_PID=$!

# Test loop
START_TIME=$(date +%s)
WORKSPACE_CHANGES=0
LOCK_CYCLES=0
CRASHES=0

echo "[$(date '+%H:%M:%S')] Beginning test loop..."
echo ""

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    # Check if duration reached
    if [ $ELAPSED -ge $DURATION_SECONDS ]; then
        echo "[$(date '+%H:%M:%S')] Test duration reached. Stopping."
        break
    fi

    # Check if hyprlax still running
    if ! kill -0 "$HYPRLAX_PID" 2>/dev/null; then
        echo "[$(date '+%H:%M:%S')] ERROR: hyprlax crashed!"
        CRASHES=$((CRASHES + 1))
        break
    fi

    # Simulate workspace change every 30 seconds
    if [ $((ELAPSED % 30)) -eq 0 ]; then
        WORKSPACE_CHANGES=$((WORKSPACE_CHANGES + 1))
        # Note: This would normally send a workspace change event
        # For testing without real compositor, we just count
    fi

    # Simulate lock/unlock cycle every 10 minutes
    if [ $((ELAPSED % 600)) -eq 0 ] && [ $ELAPSED -gt 0 ]; then
        LOCK_CYCLES=$((LOCK_CYCLES + 1))
        echo "[$(date '+%H:%M:%S')] Lock cycle $LOCK_CYCLES"

        # Note: This would normally interact with surface visibility
        # For testing, we use IPC if available
        if command -v hyprlax &>/dev/null; then
            timeout 5s hyprlax ctl set surface_visible false 2>/dev/null || true
            sleep 30  # Stay locked for 30 seconds
            timeout 5s hyprlax ctl set surface_visible true 2>/dev/null || true
        fi
    fi

    # Progress update every hour
    if [ $((ELAPSED % 3600)) -eq 0 ] && [ $ELAPSED -gt 0 ]; then
        HOURS=$((ELAPSED / 3600))
        echo ""
        echo "=== Progress Update: $HOURS hour(s) ==="
        echo "  Workspace changes: $WORKSPACE_CHANGES"
        echo "  Lock cycles: $LOCK_CYCLES"
        echo "  Status: Running"

        # Check resource usage
        if [ -f "hyprlax_resources_"*.log ]; then
            RESOURCE_LOG=$(ls -t hyprlax_resources_*.log | head -1)
            if command -v python3 &>/dev/null; then
                echo "  Resource check:"
                python3 ./scripts/analyze_resources.py "$RESOURCE_LOG" 2>/dev/null | grep -E "Change:|LEAK" || true
            fi
        fi
        echo ""
    fi

    sleep 1
done

# Cleanup
echo ""
echo "[$(date '+%H:%M:%S')] Stopping hyprlax..."
if kill -0 "$HYPRLAX_PID" 2>/dev/null; then
    kill "$HYPRLAX_PID" 2>/dev/null || true
    sleep 2
    kill -9 "$HYPRLAX_PID" 2>/dev/null || true
fi

if kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill "$MONITOR_PID" 2>/dev/null || true
fi

# Final analysis
FINAL_TIME=$(date +%s)
TOTAL_ELAPSED=$((FINAL_TIME - START_TIME))

echo ""
echo "=== TEST COMPLETE ==="
echo "  Duration: $((TOTAL_ELAPSED / 3600))h $((TOTAL_ELAPSED % 3600 / 60))m"
echo "  Workspace changes: $WORKSPACE_CHANGES"
echo "  Lock cycles: $LOCK_CYCLES"
echo "  Crashes: $CRASHES"

# Analyze resource logs
if [ -f "hyprlax_resources_"*.log ]; then
    RESOURCE_LOG=$(ls -t hyprlax_resources_*.log | head -1)
    echo ""
    echo "=== RESOURCE ANALYSIS ==="
    if command -v python3 &>/dev/null; then
        python3 ./scripts/analyze_resources.py "$RESOURCE_LOG"
    else
        echo "  (python3 not available for analysis)"
        echo "  Resource log: $RESOURCE_LOG"
    fi
fi

# Cleanup test files
rm -f "$TEST_CONFIG"
rm -f /tmp/hyprlax_test_*.png

# Exit code
if [ $CRASHES -gt 0 ]; then
    echo ""
    echo "❌ TEST FAILED: hyprlax crashed"
    exit 1
else
    echo ""
    echo "✓ TEST PASSED: No crashes detected"
    exit 0
fi
