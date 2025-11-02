# Resource Monitoring

Hyprlax includes a built-in resource monitoring subsystem for proactive leak detection in production environments.

## Overview

The resource monitor tracks:
- **File Descriptors**: Total count and growth over time
- **Memory Usage**: RSS (Resident Set Size) and VMS (Virtual Memory Size)
- **GPU Resources**: OpenGL textures, buffers, and framebuffers (best-effort)

Measurements are taken periodically (default: every 5 minutes) and warnings are logged when significant growth is detected.

## Features

### Baseline Tracking
At startup, the monitor captures baseline measurements:
- Initial FD count
- Initial memory usage (RSS/VMS)

All subsequent measurements are compared against these baselines to detect growth.

### Growth Detection
The monitor tracks:
- **Current values**: Latest measurements
- **Maximum values**: Peak resource usage since startup
- **Growth thresholds**: Configurable warning levels

### Automatic Warnings
Warnings are logged when growth exceeds thresholds:
- Default FD growth threshold: **10 file descriptors**
- Default memory growth threshold: **50 MB**

## Environment Variables

Configure the resource monitor via environment variables:

### `HYPRLAX_RESOURCE_MONITOR_DISABLE`
Disable resource monitoring entirely.
```bash
export HYPRLAX_RESOURCE_MONITOR_DISABLE=1
```

### `HYPRLAX_RESOURCE_MONITOR_INTERVAL`
Set check interval in seconds (default: 300 = 5 minutes).
```bash
export HYPRLAX_RESOURCE_MONITOR_INTERVAL=60  # Check every minute
```

### `HYPRLAX_RESOURCE_MONITOR_DEBUG`
Enable verbose debug output for every check.
```bash
export HYPRLAX_RESOURCE_MONITOR_DEBUG=1
```

## IPC Commands

Query resource monitor status via IPC:

### `resource_status` / `resources`
Get current resource usage statistics:
```bash
hyprlax ctl resource_status
```

Output format:
```
=== Resource Monitor Status ===
Checks performed: 42
Check interval: 300.0 seconds

File Descriptors:
  Baseline: 15
  Current:  18 (+3)
  Maximum:  22 (+7)

Memory (RSS):
  Baseline: 12345 KB
  Current:  13456 KB (+1111 KB)
  Maximum:  14567 KB (+2222 KB)
```

## Implementation Details

### Monitoring Cycle
1. Periodic check triggered by event loop
2. Current metrics gathered from `/proc/self/`
3. Compared against baseline and thresholds
4. Warnings logged if growth exceeds thresholds
5. Statistics updated (check count, maximums)

### File Descriptor Counting
FDs are counted by reading `/proc/self/fd` directory:
```c
int resource_monitor_get_fd_count(void);
```

### Memory Measurement
Memory usage read from `/proc/self/status`:
```c
size_t resource_monitor_get_memory_rss_kb(void);  /* RSS in KB */
size_t resource_monitor_get_memory_vms_kb(void);  /* VMS in KB */
```

### Integration Points

#### Initialization
Resource monitor created in `hyprlax_create()`:
```c
ctx->resource_monitor = resource_monitor_create(300.0);  /* 5 minute interval */
```

#### Event Loop
Periodic checks added to main event loop:
```c
if (ctx->resource_monitor &&
    resource_monitor_should_check(ctx->resource_monitor, current_time)) {
    resource_monitor_check(ctx->resource_monitor);
}
```

#### Cleanup
Resource monitor destroyed in `hyprlax_destroy()`:
```c
if (ctx->resource_monitor) {
    resource_monitor_destroy(ctx->resource_monitor);
}
```

## Production Usage

### Recommended Settings
For production deployments:
```bash
# Check every 5 minutes (default)
export HYPRLAX_RESOURCE_MONITOR_INTERVAL=300

# Disable debug output (default)
unset HYPRLAX_RESOURCE_MONITOR_DEBUG
```

### Monitoring Best Practices

1. **Regular Checks**: Monitor resource status periodically via IPC
2. **Log Analysis**: Review logs for growth warnings
3. **Trend Analysis**: Track maximum values over time
4. **Alert Integration**: Parse IPC output for automated alerts

### Example Monitoring Script
```bash
#!/bin/bash
# Monitor hyprlax resource usage

while true; do
    output=$(hyprlax ctl resource_status 2>&1)

    # Extract current FD count
    fd_current=$(echo "$output" | grep "Current:" | head -1 | awk '{print $2}')

    # Extract current memory
    mem_current=$(echo "$output" | grep "Current:" | tail -1 | awk '{print $2}')

    # Log to monitoring system
    echo "$(date): FDs=$fd_current Memory=${mem_current}KB"

    sleep 300  # Check every 5 minutes
done
```

## Testing

Run resource monitor tests:
```bash
make tests/test_resource_monitor
./tests/test_resource_monitor
```

Test coverage includes:
- Creation and destruction
- FD counting accuracy
- Memory measurement
- Periodic checking
- Growth tracking
- Environment variable configuration
- Disabled monitor behavior

## Troubleshooting

### High FD Count
If FD warnings appear:
1. Check for unclosed file descriptors in compositor adapters
2. Review IPC connection handling
3. Check Wayland protocol socket cleanup

### High Memory Usage
If memory warnings appear:
1. Check for texture leaks (GL resources)
2. Review layer lifecycle (creation/destruction)
3. Verify animation state cleanup

### Monitor Not Working
If no checks are being performed:
1. Verify monitor is enabled: `resource_status` command
2. Check environment: `HYPRLAX_RESOURCE_MONITOR_DISABLE`
3. Verify event loop is running

## Performance Impact

The resource monitor has minimal performance impact:
- **Check Duration**: < 1ms per check
- **Memory Overhead**: ~200 bytes
- **CPU Usage**: Negligible (checks are infrequent)

Default 5-minute interval ensures monitoring overhead is less than 0.001% of CPU time.

## Future Enhancements

Potential improvements for future releases:
- JSON output format for IPC command
- Configurable thresholds via IPC
- Historical data tracking (ring buffer)
- GPU resource tracking improvements
- Export metrics to Prometheus/StatsD

## Related Documentation

- [IPC System](ipc.md) - Runtime control interface
- [Event Loop](event_loop.md) - Main event processing
- [Memory Management](memory.md) - Resource lifecycle
