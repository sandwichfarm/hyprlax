#!/bin/bash
# Monitor hyprlax resource usage over time
# Usage: ./monitor_resources.sh <pid> [interval_seconds]

set -euo pipefail

PID=$1
INTERVAL=${2:-60}  # Default: 60 seconds
LOGFILE="hyprlax_resources_$(date +%s).log"

if [ -z "$PID" ]; then
    echo "Usage: $0 <pid> [interval_seconds]"
    echo ""
    echo "Example:"
    echo "  $0 12345 30    # Monitor PID 12345 every 30 seconds"
    exit 1
fi

# Verify PID exists
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Error: Process $PID does not exist or is not accessible"
    exit 1
fi

echo "Monitoring PID $PID every $INTERVAL seconds"
echo "Logging to: $LOGFILE"
echo ""
echo "Columns:"
echo "  timestamp     - Unix timestamp"
echo "  elapsed_sec   - Seconds since monitoring started"
echo "  virt_kb       - Virtual memory size (KB)"
echo "  rss_kb        - Resident memory size (KB)"
echo "  fd_count      - Number of open file descriptors"
echo "  threads       - Number of threads"
echo "  cpu_percent   - CPU usage percentage"
echo ""

# CSV header
echo "timestamp,elapsed_sec,virt_kb,rss_kb,fd_count,threads,cpu_percent" > "$LOGFILE"

START_TIME=$(date +%s)
SAMPLE_COUNT=0

# Trap to handle cleanup
trap 'echo ""; echo "Monitoring stopped. Log saved to: $LOGFILE"; exit 0' INT TERM

while kill -0 "$PID" 2>/dev/null; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - START_TIME))
    SAMPLE_COUNT=$((SAMPLE_COUNT + 1))

    # Memory from /proc/PID/status
    VIRT=$(grep -m1 VmSize /proc/$PID/status 2>/dev/null | awk '{print $2}' || echo "0")
    RSS=$(grep -m1 VmRSS /proc/$PID/status 2>/dev/null | awk '{print $2}' || echo "0")

    # File descriptors
    FD_COUNT=$(ls /proc/$PID/fd 2>/dev/null | wc -l || echo "0")

    # Threads
    THREADS=$(grep -m1 Threads /proc/$PID/status 2>/dev/null | awk '{print $2}' || echo "0")

    # CPU usage (approximate)
    CPU=$(ps -p "$PID" -o %cpu= 2>/dev/null | tr -d ' ' || echo "0.0")

    # Log data
    echo "$NOW,$ELAPSED,$VIRT,$RSS,$FD_COUNT,$THREADS,$CPU" >> "$LOGFILE"

    # Display progress
    if [ $((SAMPLE_COUNT % 10)) -eq 0 ]; then
        echo "[$ELAPSED s] RSS: ${RSS} KB, FDs: ${FD_COUNT}, CPU: ${CPU}%"
    fi

    sleep "$INTERVAL"
done

echo ""
echo "Process $PID terminated after $ELAPSED seconds"
echo "Total samples: $SAMPLE_COUNT"
echo "Log saved to: $LOGFILE"

# Quick summary
if command -v python3 &>/dev/null; then
    echo ""
    echo "Quick summary:"
    python3 -c "
import csv
import sys

with open('$LOGFILE', 'r') as f:
    reader = csv.DictReader(f)
    data = list(reader)

if len(data) < 2:
    print('Not enough data')
    sys.exit(0)

initial = data[0]
final = data[-1]

print(f'  Duration: {final[\"elapsed_sec\"]} seconds')
print(f'  RSS: {initial[\"rss_kb\"]} KB → {final[\"rss_kb\"]} KB ({int(final[\"rss_kb\"]) - int(initial[\"rss_kb\"]):+d} KB)')
print(f'  FDs: {initial[\"fd_count\"]} → {final[\"fd_count\"]} ({int(final[\"fd_count\"]) - int(initial[\"fd_count\"]):+d})')
print(f'  Threads: {initial[\"threads\"]} → {final[\"threads\"]}')

# Calculate leak rates
elapsed_hours = int(final['elapsed_sec']) / 3600
if elapsed_hours > 0:
    mem_rate = (int(final['rss_kb']) - int(initial['rss_kb'])) / elapsed_hours
    fd_rate = (int(final['fd_count']) - int(initial['fd_count'])) / elapsed_hours
    print(f'')
    print(f'  Leak rates (per hour):')
    print(f'    Memory: {mem_rate:+.1f} KB/hour')
    print(f'    FDs: {fd_rate:+.1f} FD/hour')
"
fi
