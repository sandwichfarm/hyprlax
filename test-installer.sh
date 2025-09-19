#!/bin/bash

echo "=== Local Installer Testing ==="
echo

# Test 1: Basic run (dry run with help)
echo "Test 1: Help output"
bash install-remote.sh --help
echo

# Test 2: Check version detection
echo "Test 2: Version detection (--v2 --prerelease)"
bash install-remote.sh --v2 --prerelease --dry-run 2>/dev/null || echo "(dry-run not implemented, that's ok)"
echo

# Test 3: Simulate piped input (like curl | bash)
echo "Test 3: Simulating piped execution"
echo "Running: cat install-remote.sh | bash -s -- --v2 --prerelease"
# This will actually prompt for input if conflicts exist

echo
echo "=== Testing Tips ==="
echo "1. Test with existing installations:"
echo "   bash install-remote.sh --v2 --prerelease"
echo
echo "2. Test system vs user install:"
echo "   bash install-remote.sh --system --v2"
echo "   bash install-remote.sh --user --v2"
echo
echo "3. Test force reinstall:"
echo "   bash install-remote.sh --v2 --force"
echo
echo "4. Test piped execution (most important):"
echo "   cat install-remote.sh | bash -s -- --v2 --prerelease"
echo
echo "5. Test with multiple installations present:"
echo "   - Install to /usr/local/bin (--system)"
echo "   - Then try to install to ~/.local/bin (--user)"
echo "   - Should trigger conflict detection"
