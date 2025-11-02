#!/usr/bin/env python3
"""
Analyze resource usage logs for memory and FD leaks
Usage: ./analyze_resources.py <resource_log.csv>
"""

import sys
import csv
from collections import defaultdict

def analyze_log(filename):
    """Detect resource leaks from monitoring log"""

    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            data = list(reader)
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found")
        return True
    except Exception as e:
        print(f"Error reading file: {e}")
        return True

    if len(data) < 2:
        print("Not enough data points (need at least 2)")
        return False

    # Parse data
    timestamps = []
    rss_values = []
    fd_values = []
    thread_values = []

    for row in data:
        try:
            timestamps.append(int(row['elapsed_sec']))
            rss_values.append(int(row['rss_kb']))
            fd_values.append(int(row['fd_count']))
            thread_values.append(int(row['threads']))
        except (ValueError, KeyError) as e:
            print(f"Warning: Skipping malformed row: {e}")
            continue

    if len(timestamps) < 2:
        print("Not enough valid data after parsing")
        return False

    # Analysis
    initial = 0
    final = len(timestamps) - 1

    print("=" * 60)
    print("RESOURCE USAGE ANALYSIS")
    print("=" * 60)
    print(f"\nLog file: {filename}")
    print(f"Duration: {timestamps[final]} seconds ({timestamps[final]/3600:.2f} hours)")
    print(f"Samples: {len(timestamps)}")

    # Memory analysis
    print(f"\n{'MEMORY (RSS)':─^60}")
    print(f"  Initial: {rss_values[initial]:,} KB")
    print(f"  Final:   {rss_values[final]:,} KB")
    print(f"  Peak:    {max(rss_values):,} KB")
    print(f"  Change:  {rss_values[final] - rss_values[initial]:+,} KB")

    mem_growth_pct = ((rss_values[final] - rss_values[initial]) / rss_values[initial] * 100) if rss_values[initial] > 0 else 0
    print(f"  Growth:  {mem_growth_pct:+.1f}%")

    mem_leak = False
    if mem_growth_pct > 10:
        print(f"  ⚠️  MEMORY LEAK DETECTED (>10% growth)")
        mem_leak = True
    elif mem_growth_pct > 5:
        print(f"  ⚠️  Possible memory leak (>5% growth)")

    # File descriptor analysis
    print(f"\n{'FILE DESCRIPTORS':─^60}")
    print(f"  Initial: {fd_values[initial]}")
    print(f"  Final:   {fd_values[final]}")
    print(f"  Peak:    {max(fd_values)}")
    print(f"  Change:  {fd_values[final] - fd_values[initial]:+d}")

    fd_leak = False
    fd_change = fd_values[final] - fd_values[initial]
    if fd_change > 10:
        print(f"  ⚠️  FILE DESCRIPTOR LEAK DETECTED (>{10} FD growth)")
        fd_leak = True
    elif fd_change > 5:
        print(f"  ⚠️  Possible FD leak (>5 FD growth)")
    elif fd_change == 0:
        print(f"  ✓  No FD leak detected")

    # Thread analysis
    print(f"\n{'THREADS':─^60}")
    print(f"  Initial: {thread_values[initial]}")
    print(f"  Final:   {thread_values[final]}")
    print(f"  Peak:    {max(thread_values)}")
    print(f"  Change:  {thread_values[final] - thread_values[initial]:+d}")

    thread_leak = False
    thread_change = thread_values[final] - thread_values[initial]
    if thread_change > 5:
        print(f"  ⚠️  THREAD LEAK DETECTED")
        thread_leak = True
    elif thread_change > 0:
        print(f"  ⚠️  Possible thread leak")

    # Calculate leak rates
    elapsed_hours = timestamps[final] / 3600
    if elapsed_hours > 0:
        mem_rate = (rss_values[final] - rss_values[initial]) / elapsed_hours
        fd_rate = (fd_values[final] - fd_values[initial]) / elapsed_hours

        print(f"\n{'LEAK RATES (per hour)':─^60}")
        print(f"  Memory:  {mem_rate:+.1f} KB/hour")
        print(f"  FDs:     {fd_rate:+.1f} FD/hour")
        print(f"  Threads: {thread_change / elapsed_hours:+.1f} threads/hour")

        if abs(mem_rate) > 100:
            print(f"  ⚠️  HIGH MEMORY LEAK RATE (>100 KB/hour)")

        # Extrapolate to 24 hours
        print(f"\n{'PROJECTED 24-HOUR IMPACT':─^60}")
        print(f"  Memory:  {mem_rate * 24:+.0f} KB (~{mem_rate * 24 / 1024:+.1f} MB)")
        print(f"  FDs:     {fd_rate * 24:+.0f}")

    # Stability metrics
    print(f"\n{'STABILITY METRICS':─^60}")

    # Memory volatility (standard deviation)
    if len(rss_values) > 2:
        mean_rss = sum(rss_values) / len(rss_values)
        variance = sum((x - mean_rss) ** 2 for x in rss_values) / len(rss_values)
        std_dev = variance ** 0.5
        cv = (std_dev / mean_rss * 100) if mean_rss > 0 else 0

        print(f"  Memory stability:  {cv:.1f}% variation")
        if cv > 20:
            print(f"    ⚠️  High memory volatility")
        else:
            print(f"    ✓  Stable memory usage")

    # FD stability
    fd_changes = sum(1 for i in range(1, len(fd_values)) if fd_values[i] != fd_values[i-1])
    print(f"  FD changes:        {fd_changes} times")
    if fd_changes > len(fd_values) * 0.1:
        print(f"    ⚠️  Frequent FD changes")

    # Overall verdict
    print(f"\n{'VERDICT':═^60}")

    issues = []
    if mem_leak:
        issues.append("MEMORY LEAK")
    if fd_leak:
        issues.append("FD LEAK")
    if thread_leak:
        issues.append("THREAD LEAK")

    if issues:
        print(f"  ❌ LEAKS DETECTED: {', '.join(issues)}")
        print(f"\n  Recommended actions:")
        if mem_leak:
            print(f"    - Run with valgrind: valgrind --leak-check=full ./hyprlax")
        if fd_leak:
            print(f"    - Check FD usage: lsof -p <pid>")
        if thread_leak:
            print(f"    - Check thread creation: pstack <pid>")
        print("")
        return True
    else:
        print(f"  ✓ NO LEAKS DETECTED")
        print(f"  Resource usage appears stable")
        print("")
        return False

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <resource_log.csv>")
        print(f"")
        print(f"Analyzes resource usage logs created by monitor_resources.sh")
        print(f"")
        print(f"Example:")
        print(f"  {sys.argv[0]} hyprlax_resources_1234567890.log")
        sys.exit(1)

    has_leak = analyze_log(sys.argv[1])
    sys.exit(1 if has_leak else 0)
