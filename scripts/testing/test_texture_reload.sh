#!/bin/bash
# Test 2: Texture Reload Failure Test

echo "=== Test 2.1: Invalid Path Reload ==="
./hyprlax ctl layer reload 0 /path/that/does/not/exist.jpg
echo ""

sleep 1

echo "=== Test 2.2: Invalid Image Format ==="
echo "not an image" > /tmp/bad_image.jpg
./hyprlax ctl layer reload 0 /tmp/bad_image.jpg
rm /tmp/bad_image.jpg
echo ""

sleep 1

echo "=== Verify hyprlax still working ==="
./hyprlax ctl status
./hyprlax ctl list
echo ""

echo "Test complete - hyprlax should remain stable"
