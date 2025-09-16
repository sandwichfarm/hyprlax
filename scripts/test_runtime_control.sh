#!/bin/bash

# Test script for hyprlax runtime control via ctl subcommand

echo "=== Hyprlax Runtime Control Test ==="
echo

# Start hyprlax in background with a test image
echo "1. Starting hyprlax daemon with initial config..."
./hyprlax -D --fps 60 --shift 200 examples/city1/city1_layer1.jpg:1.0:1.0:0 &
HYPRLAX_PID=$!
echo "   Started with PID $HYPRLAX_PID"

# Give it time to initialize
sleep 2

echo
echo "2. Checking daemon status..."
./hyprlax ctl status

echo
echo "3. Listing initial layers..."
./hyprlax ctl list

echo
echo "4. Adding a new layer dynamically..."
./hyprlax ctl add examples/city1/city1_layer2.jpg 0.7 0.8 5
sleep 1

echo
echo "5. Listing layers after addition..."
./hyprlax ctl list

echo
echo "6. Modifying layer opacity..."
./hyprlax ctl modify 1 opacity 0.5
sleep 1

echo
echo "7. Changing FPS at runtime..."
./hyprlax ctl set fps 120

echo
echo "8. Changing animation duration..."
./hyprlax ctl set duration 2.0

echo
echo "9. Changing easing function..."
./hyprlax ctl set easing elastic

echo
echo "10. Getting current settings..."
./hyprlax ctl get fps
./hyprlax ctl get duration
./hyprlax ctl get easing

echo
echo "11. Adding another layer..."
./hyprlax ctl add examples/city1/city1_layer3.jpg 0.5 0.6 10

echo
echo "12. Final layer list..."
./hyprlax ctl list

echo
echo "13. Removing a layer..."
./hyprlax ctl remove 2

echo
echo "14. Status check..."
./hyprlax ctl status

echo
echo "15. Clearing all layers..."
./hyprlax ctl clear

echo
echo "16. Final status..."
./hyprlax ctl status

echo
echo "Stopping hyprlax daemon..."
kill $HYPRLAX_PID 2>/dev/null
wait $HYPRLAX_PID 2>/dev/null

echo
echo "=== Test Complete ===""