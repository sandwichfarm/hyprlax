#!/bin/bash
# Memory Leak Testing Script for Phase 1 Fixes
# Tests the critical memory leak fixes implemented in hyprlax

set -e

HYPRLAX_BIN="${1:-./hyprlax}"
VALGRIND_LOG="valgrind_test_$(date +%s).log"
TEST_IMAGE="${TEST_IMAGE:-/tmp/test_wallpaper.png}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "================================"
echo "Memory Leak Test Suite"
echo "Testing fixes for:"
echo "  1. Output info memory leak"
echo "  2. Frame callback cleanup"
echo "  3. Layer path management"
echo "================================"
echo ""

# Check if test image exists
if [ ! -f "$TEST_IMAGE" ]; then
    echo -e "${YELLOW}WARNING: Test image not found at $TEST_IMAGE${NC}"
    echo "Creating a simple test image..."

    # Try to create a simple PNG using imagemagick if available
    if command -v convert &> /dev/null; then
        convert -size 1920x1080 gradient:blue-red "$TEST_IMAGE"
        echo -e "${GREEN}Created test image: $TEST_IMAGE${NC}"
    else
        echo -e "${RED}ERROR: Cannot create test image. Please set TEST_IMAGE environment variable.${NC}"
        echo "Usage: TEST_IMAGE=/path/to/image.png $0"
        exit 1
    fi
fi

# Check if hyprlax binary exists
if [ ! -f "$HYPRLAX_BIN" ]; then
    echo -e "${RED}ERROR: hyprlax binary not found at $HYPRLAX_BIN${NC}"
    echo "Please build hyprlax first or specify path as first argument."
    exit 1
fi

# Function to check valgrind results
check_valgrind_leaks() {
    local log_file="$1"
    local test_name="$2"

    if [ ! -f "$log_file" ]; then
        echo -e "${RED}✗ FAIL: Log file not found${NC}"
        return 1
    fi

    # Check for "definitely lost" bytes
    local definitely_lost=$(grep "definitely lost:" "$log_file" | awk '{print $4}')
    local indirectly_lost=$(grep "indirectly lost:" "$log_file" | awk '{print $4}')

    echo ""
    echo "Results for: $test_name"
    echo "  Definitely lost: ${definitely_lost:-0} bytes"
    echo "  Indirectly lost: ${indirectly_lost:-0} bytes"

    if [ "${definitely_lost:-0}" = "0" ] && [ "${indirectly_lost:-0}" = "0" ]; then
        echo -e "${GREEN}✓ PASS: No memory leaks detected${NC}"
        return 0
    else
        echo -e "${RED}✗ FAIL: Memory leaks detected${NC}"
        echo "See $log_file for details"
        return 1
    fi
}

echo "Test 1: Basic Valgrind Run"
echo "----------------------------"
echo "Running hyprlax under Valgrind for 5 seconds..."
echo "This tests basic memory management and initialization."
echo ""

timeout 5 valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --log-file="${VALGRIND_LOG}.basic" \
    "$HYPRLAX_BIN" "$TEST_IMAGE" 2>&1 > /dev/null || true

sleep 1
check_valgrind_leaks "${VALGRIND_LOG}.basic" "Basic initialization" || true

echo ""
echo "================================"
echo "Test 2: Extended Runtime Test"
echo "----------------------------"
echo "Running hyprlax for 30 seconds to check for accumulation..."
echo ""

# Get initial memory
echo "Starting memory test..."
timeout 30 "$HYPRLAX_BIN" "$TEST_IMAGE" &
HYPRLAX_PID=$!
sleep 2

if ! kill -0 $HYPRLAX_PID 2>/dev/null; then
    echo -e "${RED}✗ FAIL: hyprlax failed to start${NC}"
else
    INITIAL_RSS=$(ps -p $HYPRLAX_PID -o rss= 2>/dev/null || echo "0")
    echo "  Initial RSS: ${INITIAL_RSS} KB"

    # Wait and check again
    sleep 25

    if kill -0 $HYPRLAX_PID 2>/dev/null; then
        FINAL_RSS=$(ps -p $HYPRLAX_PID -o rss= 2>/dev/null || echo "0")
        echo "  Final RSS: ${FINAL_RSS} KB"

        # Calculate growth
        GROWTH=$((FINAL_RSS - INITIAL_RSS))
        echo "  Growth: ${GROWTH} KB"

        if [ $GROWTH -lt 1000 ]; then
            echo -e "${GREEN}✓ PASS: Memory usage stable (growth < 1MB)${NC}"
        else
            echo -e "${YELLOW}⚠ WARNING: Significant memory growth detected${NC}"
        fi

        # Clean up
        kill $HYPRLAX_PID 2>/dev/null || true
    else
        echo -e "${RED}✗ FAIL: hyprlax crashed during test${NC}"
    fi
fi

wait 2>/dev/null || true

echo ""
echo "================================"
echo "Test 3: IPC Layer Operations"
echo "----------------------------"
echo "Testing layer add/remove for memory leaks..."
echo ""

# This test would require hyprlax-ctl, so we'll just document it
echo "Manual test required:"
echo "  1. Start hyprlax with IPC enabled"
echo "  2. Run: for i in {1..100}; do hyprlax-ctl layer add test.png 1.0 1.0; hyprlax-ctl layer remove 0; done"
echo "  3. Monitor memory with: watch -n 1 'ps aux | grep hyprlax'"
echo "  4. Memory should remain stable"

echo ""
echo "================================"
echo "Summary"
echo "================================"
echo ""
echo "Log files created:"
echo "  - ${VALGRIND_LOG}.basic"
echo ""
echo "Manual testing recommended:"
echo ""
echo "1. Monitor Hotplug Test:"
echo "   Connect/disconnect external display 10+ times"
echo "   Watch memory: ps aux | grep hyprlax"
echo ""
echo "2. Long Runtime Test:"
echo "   Run hyprlax for 24+ hours with workspace switching"
echo "   Memory should remain stable"
echo ""
echo "3. Valgrind Full Test:"
echo "   valgrind --leak-check=full ./hyprlax wallpaper.png"
echo "   Should show 'definitely lost: 0 bytes'"
echo ""
echo -e "${GREEN}Testing complete!${NC}"
echo "Review the Valgrind logs for detailed analysis."
