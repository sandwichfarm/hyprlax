#!/bin/bash
# Test 3: IPC Stress Test

echo "=== IPC Stress Test: 100 rapid commands ==="

for i in {1..100}; do
    echo -n "Iteration $i..."
    
    # Multiple concurrent commands
    ./hyprlax ctl status >/dev/null 2>&1 &
    ./hyprlax ctl list >/dev/null 2>&1 &
    ./hyprlax ctl resource_status >/dev/null 2>&1 &
    
    if [ $((i % 10)) -eq 0 ]; then
        wait
        echo " (checkpoint)"
        sleep 0.1
    else
        echo ""
    fi
done

wait
echo "IPC stress test complete"

echo ""
echo "=== Final Status Check ==="
./hyprlax ctl status

echo ""
echo "=== Memory Check ==="
ps -o pid,rss,vsz,cmd -p $(pgrep -f "hyprlax /")
