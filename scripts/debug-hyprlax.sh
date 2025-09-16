#!/bin/bash
# Debug wrapper for hyprlax to capture startup logs when run via exec-once

LOG_FILE="/tmp/hyprlax-startup-$(date +%Y%m%d-%H%M%S).log"

echo "[WRAPPER] Starting hyprlax at $(date)" >> "$LOG_FILE"
echo "[WRAPPER] Environment:" >> "$LOG_FILE"
env | grep -E "(WAYLAND|DISPLAY|XDG|HYPR)" >> "$LOG_FILE" 2>&1
echo "[WRAPPER] Running hyprlax..." >> "$LOG_FILE"

# Run hyprlax with full output capture
/home/sandwich/.local/bin/hyprlax --config /home/sandwich/Develop/hyprlax/examples/pixel-city/parallax.conf --debug >> "$LOG_FILE" 2>&1

EXIT_CODE=$?
echo "[WRAPPER] Hyprlax exited with code $EXIT_CODE at $(date)" >> "$LOG_FILE"

# Also output to stderr so we can see it in journal
echo "Hyprlax log saved to: $LOG_FILE" >&2