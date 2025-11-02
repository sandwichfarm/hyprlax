# Testing Scripts

This directory contains test scripts created during Phase 1 and Phase 2 implementation and validation.

## Core Testing Scripts

### Resource Monitoring

**`get_baseline.sh`**
- **Purpose**: Capture baseline resource measurements (FD count, memory usage)
- **Usage**: `./scripts/testing/get_baseline.sh`
- **Output**: Prints baseline metrics to stdout
- **When to use**: Before starting long-running tests to establish baseline

**`monitor_loop.sh`**
- **Purpose**: Continuously monitor process resources over time
- **Usage**: `./scripts/testing/monitor_loop.sh <PID> <DURATION_MINUTES>`
- **Output**: Creates `resource_tracking.csv` with timestamped measurements
- **When to use**: During extended runtime tests to track resource growth

**`analyze_resources.py`**
- **Purpose**: Analyze resource tracking data and calculate leak rates
- **Usage**: `python3 ./scripts/testing/analyze_resources.py`
- **Input**: Reads `resource_tracking.csv`
- **Output**: Growth analysis, leak rates, pass/fail verdict
- **When to use**: After extended runtime test to analyze results

### Memory Leak Testing

**`test_memory_leaks.sh`**
- **Purpose**: Comprehensive memory leak detection with Valgrind
- **Usage**: `./scripts/testing/test_memory_leaks.sh`
- **Tests**: Basic run, extended runtime, memory growth, IPC operations
- **Output**: Valgrind logs and summary
- **Duration**: ~5 minutes
- **Requirements**: Valgrind installed, Wayland display available

**`valgrind_test.sh`**
- **Purpose**: Run hyprlax under Valgrind with IPC activity
- **Usage**: `./scripts/testing/valgrind_test.sh`
- **Output**: `valgrind_phase2.log` with leak detection results
- **When to use**: To verify no memory leaks in Phase 2 features

### IPC Testing

**`test_ipc_stress.sh`**
- **Purpose**: Stress test IPC system with 100 rapid concurrent commands
- **Usage**: `./scripts/testing/test_ipc_stress.sh`
- **Tests**: Multiple concurrent status/list/resource_status commands
- **Duration**: ~30 seconds
- **Expected**: All commands succeed, no crashes, stable memory

**`ipc_leak_test.sh`**
- **Purpose**: Test for FD/memory leaks in IPC operations
- **Usage**: `./scripts/testing/ipc_leak_test.sh`
- **Tests**: 1,000 IPC operations with resource tracking
- **Expected**: FD count stable (±2), memory growth <10MB

**`test_slow_clients.sh`**
- **Purpose**: Test IPC timeout handling with slow clients
- **Usage**: `./scripts/testing/test_slow_clients.sh`
- **Tests**: 5 concurrent slow clients that don't send data
- **Expected**: Clients timeout after ~10 seconds, daemon remains responsive

### Feature-Specific Testing

**`test_hyprlock_compatibility.sh`**
- **Purpose**: Test hyprlock integration (lock detection, rendering suspension)
- **Usage**: `./scripts/testing/test_hyprlock_compatibility.sh`
- **Tests**: Lock detection, GPU monitoring, stress testing, multi-monitor
- **Requirements**: Hyprland session, hyprlock installed
- **Duration**: ~10 minutes (varies with manual interaction)

**`test_texture_reload.sh`**
- **Purpose**: Test texture reload error handling
- **Usage**: `./scripts/testing/test_texture_reload.sh`
- **Tests**: Invalid path, invalid image format
- **Expected**: Errors logged, old texture kept, no crash

### Performance Testing

**`performance_check.sh`**
- **Purpose**: Measure IPC latency, CPU usage, memory footprint
- **Usage**: `./scripts/testing/performance_check.sh`
- **Output**: `performance_results.txt` with metrics
- **Expected**: IPC <100ms, CPU <10%, reasonable memory

**`runtime_test.sh`**
- **Purpose**: Extended runtime stability test
- **Usage**: `./scripts/testing/runtime_test.sh`
- **Duration**: Configurable (default setup for long runs)
- **When to use**: Overnight or multi-day stability testing

## Test Workflow

### Quick Validation (5 minutes)
```bash
# 1. Build
make clean && make

# 2. Run basic tests
make test

# 3. Quick IPC test
./scripts/testing/test_ipc_stress.sh

# 4. Check for obvious leaks
./scripts/testing/get_baseline.sh
# ... run hyprlax for a bit ...
./scripts/testing/ipc_leak_test.sh
```

### Comprehensive Validation (1 hour)
```bash
# 1. Memory leak testing
./scripts/testing/test_memory_leaks.sh

# 2. IPC stress testing
./scripts/testing/test_ipc_stress.sh
./scripts/testing/test_slow_clients.sh

# 3. Feature testing (if in Hyprland)
./scripts/testing/test_hyprlock_compatibility.sh

# 4. Extended runtime
./scripts/testing/runtime_test.sh
```

### Extended Validation (8+ hours)
```bash
# Start hyprlax with monitoring
export HYPRLAX_RESOURCE_MONITOR_DEBUG=1
./hyprlax ~/Pictures/wallpaper.jpg &
PID=$!

# Monitor resources
./scripts/testing/monitor_loop.sh $PID 480  # 8 hours

# In separate terminal, generate activity periodically
# ... workspace switches, IPC commands, etc ...

# Analyze results
python3 ./scripts/testing/analyze_resources.py
```

## Environment Variables

Most scripts respect these environment variables:
- `HYPRLAX_DEBUG=1` - Enable debug output
- `HYPRLAX_RESOURCE_MONITOR_DEBUG=1` - Enable resource monitor debug output
- `HYPRLAX_IPC_TIMEOUT=10` - Override IPC timeout

## Requirements

- **Wayland/Hyprland session** - For hyprlock and full integration tests
- **Valgrind** - For memory leak detection
- **Python 3** - For analyze_resources.py
- **hyprlock** - For lock integration tests

## Notes

- Some tests require manual interaction (hyprlock testing)
- Extended tests can run for hours - use `nohup` or `screen`
- All scripts assume hyprlax binary is in project root
- Resource tracking creates CSV files in current directory
- Valgrind tests require Wayland display (can't run headless)

## Troubleshooting

**"Failed to connect to socket"**
- Ensure hyprlax is running before running IPC tests

**"No data collected"**
- Check that process is still running during monitor_loop.sh
- Verify PID is correct

**"Permission denied"**
- Make scripts executable: `chmod +x scripts/testing/*.sh`

**Valgrind errors**
- Ensure X11/Wayland display is available
- Some Wayland/GL initialization warnings are expected

## Cleanup

After testing, you may want to clean up generated files:
```bash
rm -f resource_tracking.csv
rm -f valgrind*.log
rm -f hyprlax_*.log
```
