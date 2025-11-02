#!/bin/bash

# Run under Valgrind for leak detection
# This test requires Wayland display

echo "Starting Valgrind leak test (will run for 3 minutes)..."

valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-fds=yes \
         --log-file=/home/sandwich/Develop/hyprlax/valgrind_phase2.log \
         ./hyprlax --platform wayland --compositor hyprland /home/sandwich/Pictures/8bb39c413276b2d8509bc34aa926b05f46b968913f2a15236d50e6579314b1ae.jpg > /dev/null 2>&1 &

VALGRIND_PID=$!
echo "Valgrind process: $VALGRIND_PID"

# Let it run for 3 minutes with activity
for i in {1..6}; do
    sleep 30
    echo "Activity burst $i/6..."
    ./hyprlax ctl status > /dev/null 2>&1
    ./hyprlax ctl list > /dev/null 2>&1
done

# Stop
echo "Stopping hyprlax..."
pkill -TERM hyprlax
sleep 5
pkill -9 hyprlax 2>/dev/null

wait $VALGRIND_PID 2>/dev/null

echo "Valgrind test complete"

# Analyze results
echo "=== Valgrind Results ===" > /home/sandwich/Develop/hyprlax/valgrind_summary.txt
grep "definitely lost" /home/sandwich/Develop/hyprlax/valgrind_phase2.log >> /home/sandwich/Develop/hyprlax/valgrind_summary.txt
grep "indirectly lost" /home/sandwich/Develop/hyprlax/valgrind_phase2.log >> /home/sandwich/Develop/hyprlax/valgrind_summary.txt
grep "FILE DESCRIPTORS" /home/sandwich/Develop/hyprlax/valgrind_phase2.log >> /home/sandwich/Develop/hyprlax/valgrind_summary.txt
grep "Open file descriptor" /home/sandwich/Develop/hyprlax/valgrind_phase2.log | head -20 >> /home/sandwich/Develop/hyprlax/valgrind_summary.txt

cat /home/sandwich/Develop/hyprlax/valgrind_summary.txt
