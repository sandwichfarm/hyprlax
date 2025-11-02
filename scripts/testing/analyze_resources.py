#!/usr/bin/env python3
import csv

with open('/home/sandwich/Develop/hyprlax/resource_tracking.csv', 'r') as f:
    reader = csv.DictReader(f)
    data = list(reader)

if not data:
    print("No data collected")
    exit(1)

# Calculate growth
initial_rss = int(data[0]['rss_kb'])
final_rss = int(data[-1]['rss_kb'])
initial_fd = int(data[0]['fd_count'])
final_fd = int(data[-1]['fd_count'])

rss_growth = final_rss - initial_rss
fd_growth = final_fd - initial_fd

print("=== Resource Growth Analysis ===")
print(f"RSS: {initial_rss} KB -> {final_rss} KB ({rss_growth:+d} KB)")
print(f"FDs: {initial_fd} -> {final_fd} ({fd_growth:+d})")

# Calculate leak rate
duration_minutes = len(data)
rss_per_hour = (rss_growth / duration_minutes) * 60 if duration_minutes > 0 else 0
fd_per_hour = (fd_growth / duration_minutes) * 60 if duration_minutes > 0 else 0

print(f"\nLeak Rates (per hour):")
print(f"Memory: {rss_per_hour:+.1f} KB/hr")
print(f"FDs: {fd_per_hour:+.2f} FD/hr")

# Verdict
if abs(rss_growth) < 5000:  # Less than 5MB growth
    print("\n✓ Memory: PASS (stable)")
else:
    print(f"\n⚠ Memory: WARN (grew {rss_growth} KB)")

if abs(fd_growth) <= 2:  # Within 2 FDs is acceptable
    print("✓ FDs: PASS (stable)")
else:
    print(f"✗ FDs: FAIL (grew {fd_growth})")
