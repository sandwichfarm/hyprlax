#!/bin/bash
# Cron-friendly wrapper for time overlay
# Add to crontab: */1 * * * * /path/to/time_overlay_cron.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Run once mode for cron
python3 time_overlay.py --once --font-size 120