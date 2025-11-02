#!/bin/bash

HYPRLAX_PID=$(pgrep -f "hyprlax.*jpg" | head -1)

if [ -z "$HYPRLAX_PID" ]; then
    echo "ERROR: No hyprlax process found"
    exit 1
fi

echo "Monitoring PID $HYPRLAX_PID for 15 minutes..."

# Monitor for 15 minutes (900 seconds / 60 = 15 checks)
/home/sandwich/Develop/hyprlax/monitor_loop.sh $HYPRLAX_PID 15 &
MONITOR_PID=$!

# Generate some activity every 3 minutes
for i in {1..5}; do
    sleep 180

    echo "Activity burst $i/5..."
    # Trigger various operations
    ./hyprlax ctl status > /dev/null 2>&1
    ./hyprlax ctl resource_status > /dev/null 2>&1
    ./hyprlax ctl list > /dev/null 2>&1

    # Switch workspaces if in Hyprland
    for j in {1..10}; do
        hyprctl dispatch workspace $((j % 4 + 1)) 2>/dev/null
        sleep 0.5
    done
done

# Wait for monitoring to complete
wait $MONITOR_PID

echo "Runtime test complete"
