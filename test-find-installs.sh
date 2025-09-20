#!/bin/bash

# Source the functions from install-remote.sh
source <(grep -A 1000 "^# Check for all hyprlax installations" install-remote.sh | grep -B 1000 "^# Get latest version from GitHub" | head -n -2)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "Testing find_all_installations function:"
echo "========================================"

result=$(find_all_installations)
echo "Raw result: '$result'"
echo

if [ -n "$result" ]; then
    echo "Found installations:"
    IFS='|' read -ra INSTALLS <<< "$result"
    for install in "${INSTALLS[@]}"; do
        echo "  - $install"
    done
else
    echo "No installations found (BUG!)"
fi

echo
echo "Checking manually:"
echo "which hyprlax: $(which hyprlax)"
echo "/usr/local/bin/hyprlax exists: $([ -f /usr/local/bin/hyprlax ] && echo "YES" || echo "NO")"
echo "~/.local/bin/hyprlax exists: $([ -f ~/.local/bin/hyprlax ] && echo "YES" || echo "NO")"
