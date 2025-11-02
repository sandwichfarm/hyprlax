#!/bin/bash

# Test for leaks in IPC operations

HYPRLAX_PID=$(pgrep -f "hyprlax.*jpg" | head -1)

if [ -z "$HYPRLAX_PID" ]; then
    echo "ERROR: No hyprlax process found"
    exit 1
fi

# Record initial state
INITIAL_FDS=$(ls /proc/$HYPRLAX_PID/fd 2>/dev/null | wc -l)
INITIAL_RSS=$(grep VmRSS /proc/$HYPRLAX_PID/status | awk '{print $2}')

echo "Initial: $INITIAL_FDS FDs, $INITIAL_RSS KB RSS"

# Perform 1000 IPC operations
for i in {1..1000}; do
    ./hyprlax ctl status > /dev/null 2>&1
    ./hyprlax ctl list > /dev/null 2>&1
    ./hyprlax ctl resource_status > /dev/null 2>&1

    if [ $((i % 100)) -eq 0 ]; then
        CURRENT_FDS=$(ls /proc/$HYPRLAX_PID/fd 2>/dev/null | wc -l)
        CURRENT_RSS=$(grep VmRSS /proc/$HYPRLAX_PID/status 2>/dev/null | awk '{print $2}')
        echo "After $i ops: $CURRENT_FDS FDs (+$((CURRENT_FDS - INITIAL_FDS))), $CURRENT_RSS KB RSS (+$((CURRENT_RSS - INITIAL_RSS)) KB)"
    fi
done

# Final check
sleep 2
FINAL_FDS=$(ls /proc/$HYPRLAX_PID/fd 2>/dev/null | wc -l)
FINAL_RSS=$(grep VmRSS /proc/$HYPRLAX_PID/status | awk '{print $2}')

echo ""
echo "=== Results ==="
echo "Initial: $INITIAL_FDS FDs, $INITIAL_RSS KB RSS"
echo "Final: $FINAL_FDS FDs (+$((FINAL_FDS - INITIAL_FDS))), $FINAL_RSS KB RSS (+$((FINAL_RSS - INITIAL_RSS)) KB)"
echo ""

# Verdict
if [ $((FINAL_FDS - INITIAL_FDS)) -le 2 ]; then
    echo "✓ FD leak test: PASS (leaked $((FINAL_FDS - INITIAL_FDS)) FDs)"
else
    echo "✗ FD leak test: FAIL (leaked $((FINAL_FDS - INITIAL_FDS)) FDs)"
fi

if [ $((FINAL_RSS - INITIAL_RSS)) -lt 10000 ]; then
    echo "✓ Memory leak test: PASS (grew $((FINAL_RSS - INITIAL_RSS)) KB)"
else
    echo "⚠ Memory leak test: WARN (grew $((FINAL_RSS - INITIAL_RSS)) KB)"
fi
