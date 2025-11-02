#!/bin/bash
# Test 4: Slow IPC Client Stress Test

echo "=== Slow Client Timeout Test ==="

SOCKET_PATH="/tmp/hyprlax-$(whoami).sock"

for i in {1..5}; do
    (
        exec 3<>"$SOCKET_PATH" 2>/dev/null || { echo "Client $i: Failed to connect"; exit 1; }
        echo "Client $i: Connected"
        sleep 15  # Hold connection for 15 seconds (should timeout at 10s)
        echo "status" >&3
        cat <&3 2>/dev/null
        exec 3<&-
        echo "Client $i: Disconnected"
    ) &
done

echo "Started 5 slow clients"
echo "Waiting for timeouts (should occur around 10 seconds)..."
sleep 12

echo ""
echo "=== Check hyprlax responsiveness ==="
./hyprlax ctl status

wait
echo "Test complete"
