# Codebase Concerns

**Analysis Date:** 2026-03-16

## Tech Debt

### Plugin Detection Not Implemented

**Area:** Hyprland and Wayfire workspace model detection
**Files:**
- `src/compositor/workspace_models.c:20` (split-monitor-workspaces plugin)
- `src/compositor/workspace_models.c:64` (split-monitor-workspaces plugin)
- `src/compositor/workspace_models.c:81` (wsets plugin)

**Issue:** Plugin detection is stubbed with TODO comments. Currently assumes default workspace models without checking for:
- `split-monitor-workspaces` plugin in Hyprland (affects per-monitor workspace isolation)
- `wsets` plugin in Wayfire (affects workspace grouping behavior)

**Impact:** Workspace parallax calculations may be incorrect for users with these plugins installed, causing unexpected animation behavior or workspace navigation issues.

**Fix approach:** Implement plugin detection via `hyprctl plugin list` (Hyprland) and comparable queries for Wayfire. Store detected plugins in compositor state and adjust workspace model selection accordingly.

---

### Policy Configuration Not Exposed

**Area:** River tag animation policies
**Files:** `src/compositor/workspace_models.c:211`

**Issue:** River tag animation policies are hardcoded to NULL instead of being configurable. The comment notes `/* TODO: Get policy from config */`.

**Impact:** Users cannot customize River tag animation behavior. Multi-tag scenarios always apply default policy without user override capability.

**Fix approach:** Extend config to include River policy settings (`--river-tag-policy` or TOML equivalent), load from `config_toml.c`, and pass through to workspace model functions.

---

### Monitor Selection Not Implemented

**Area:** Monitor filtering
**Files:** `src/hyprlax_main.c:594`, `src/hyprlax_main.c:663`

**Issue:** `--monitor` and `--exclude-monitor` flags are parsed but not used. Comments indicate these features are stubbed.

**Impact:** Users cannot selectively enable/disable hyprlax on specific monitors in multi-monitor setups. All monitors always render or none do.

**Fix approach:** Store monitor names in a list during config parse, check against this list in `monitor_instance_create()` and `monitor_list_add()` before adding monitors to active list.

---

### Z-Index Not Implemented

**Area:** Layer rendering order
**Files:** `src/hyprlax.c:618`

**Issue:** Comment states `/* TODO: Add proper z-index field to struct layer */`. Layers are rendered in creation order without explicit z-index control.

**Impact:** Users cannot control layer stacking order. Layer rendering order is fixed by addition order, not configurable priority.

**Fix approach:** Add `float z_index` field to `struct layer`, sort layers by z_index in `hyprlax_render_frame()` before rendering loop. Support `z_index = N` in TOML layer config.

---

### Offset Rendering Not Implemented

**Area:** Layer offset transformations
**Files:** `src/hyprlax.c:570`

**Issue:** Comment states `/* TODO: Apply x/y offsets when rendering */`. Per-layer x/y offsets are not applied during rendering.

**Impact:** Users cannot offset individual layers (e.g., to create panoramic effects where layers have different starting positions). Only shift multipliers are supported.

**Fix approach:** Add `float offset_x, offset_y` to `struct layer`, apply in vertex shader or `gles2_draw_layer()` before position calculations. Support `offset.x` and `offset.y` in TOML config.

---

## Known Bugs

### Zero-Width Monitor Calculations

**Area:** Parallax shift calculations for monitors with invalid geometry
**Files:**
- `src/core/monitor.c:730-753` (monitor_effective_shift_px)
- `src/core/render_core.c:73-79` (render geometry validation)

**Symptoms:** Parallax animations may produce zero-pixel shifts when monitor width is not yet set (early initialization), causing frozen parallax effect or crashes.

**Trigger:** Start hyprlax before Wayland compositor fully reports monitor geometry, or on systems with unusual DPI/scaling configurations.

**Mitigation:** Code includes fallback to `HYPRLAX_DEFAULT_MON_WIDTH` (1920) when geometry invalid, but this is implicit. Enhanced validation added in recent commits (fix-broken-core-functionality.md).

**Test:** `tests/test_zero_dimension_edge_cases.c` (if implemented per HIVE_MIND_FIX_SUMMARY.md)

---

### Cursor Animation Time-Unit Mismatch (Recently Fixed)

**Area:** Cursor parallax animation timing
**Files:** `src/core/render_core.c:275`

**Status:** FIXED in recent commits, but documented as critical issue during hotfix/crashes-p3 analysis.

**Original Symptom:** Cursor parallax frozen or jerky animation. Animation duration was specified in seconds but timing logic used milliseconds.

**Root Cause:** Commit 8065eff introduced millisecond timestamps for layer animations but cursor animation code in `render_core.c` still received seconds from `rc_get_time()`.

**Current Fix:** `time_get_monotonic_ms()` now provides millisecond timestamps consistent with animation system.

**Risk:** Any new cursor-related animation code must ensure time-unit consistency with layer animation system.

---

### GPU Fence Sync Timeout Throttling (Recently Fixed)

**Area:** GPU synchronization in renderer
**Files:** `src/renderer/gles2.c:333`

**Status:** FIXED in commit 57230b0, but critical issue was identified in hotfix/crashes-p3 analysis.

**Original Symptom:** Animations rendered at 1 FPS when GPU under load or suspended (commit 3d4b592).

**Root Cause:** `glClientWaitSync()` timeout changed from 16ms to 1000ms. When GPU is slow, 1000ms timeout causes frame drops to 1 FPS.

**Current Fix:** Timeout reverted to 16ms (1 frame @ 60Hz).

**Risk:** Future GPU optimizations must not reintroduce long timeouts. Monitor for commits changing `glClientWaitSync()` timeout values.

---

## Security Considerations

### IPC Socket Path Handling

**Area:** Unix domain socket creation and permissions
**Files:** `src/ipc.c:332-361` (socket path generation), `src/ipc.c:434-439` (bind)

**Risk:** Socket file is created with default umask permissions. If hyprlax runs as privileged user, socket may be world-readable/writable.

**Current mitigation:** Socket path includes process signature (PID + timestamp) and is in `/tmp` or XDG directory (not world-writable). Default socket mode appears safe but not explicitly hardened.

**Recommendations:**
1. Set explicit socket permissions to 0600 after bind: `chmod(socket_path, 0600)`
2. Document socket security model in SECURITY.md
3. Add `--socket-mode` option for environments requiring stricter permissions

**Test:** Create socket, verify permissions with `stat`, confirm only owner can read/write

---

### TOML Config Path Traversal

**Area:** Image path resolution
**Files:** `src/core/config_toml.c:395-410` (path resolution)

**Risk:** User-supplied image paths in TOML config are resolved relative to config file directory. If config directory is writable by other users, symlink attacks possible.

**Current mitigation:** `realpath()` is used to resolve symlinks (assuming `_POSIX_C_SOURCE >= 200809L`). Direct file access uses `access()` check.

**Recommendations:**
1. Verify `realpath()` behavior on all supported platforms
2. Add explicit symlink detection: reject paths where `realpath()` differs from original
3. Document that config files should be in user-owned directories only

**Test:** Place symlink in config dir pointing to sensitive file, verify it's rejected or detected

---

### IPC Command Injection

**Area:** IPC command parsing
**Files:** `src/ipc.c:549-780` (command dispatch)

**Risk:** IPC commands include image paths and other user-supplied strings. If not properly escaped in responses, JSON output could be malformed.

**Current mitigation:** `json_escape()` function is used for response output. However, input validation depends on command-specific parsers.

**Recommendations:**
1. Audit all `ipc_*` command handlers for bounds checking on input strings
2. Use consistent `token_check_len()` pattern across all commands
3. Add fuzz testing for IPC inputs

**Test:** Send IPC command with very long strings, escaped quotes, binary data in image paths

---

## Performance Bottlenecks

### Niri Multi-Monitor FPS Collapse

**Area:** Niri compositor with multiple monitors
**Files:**
- `src/compositor/niri.c` (entire adapter)
- `src/core/event_loop.c` (frame pacing)

**Problem:** FPS drops to 30 or below when Niri fullscreen window on one monitor while parallax active on another.

**Cause:** Uncertain. Possible root causes documented in commit d9bd726 ("fix: resolve Niri multi-monitor fullscreen FPS collapse"):
- Frame callback pacing blocking all monitors
- Niri event stream becoming slow during fullscreen transitions
- Compositor state transitions delaying frame callbacks

**Improvement path:**
1. Monitor frame callback latency per-monitor: `hyprctl clients` and track callback timing
2. Implement per-monitor frame pacing instead of global lock
3. Consider fallback to timer-based pacing if callbacks become unreliable

**Monitoring:** `HYPRLAX_DEBUG=1` logs frame callback timing. Track frame times > 33ms (30 FPS) as anomalies.

---

### Event Loop Polling Overhead

**Area:** Main event loop
**Files:** `src/core/event_loop.c` (event loop), `src/ipc.c` (IPC accept timeout)

**Problem:** Event loop uses multiple independent file descriptors (epoll, compositor events, IPC) which may accumulate latency during polling.

**Current state:**
- IPC connections previously used 5-second blocking poll (fixed in recent commits)
- epoll uses infinite wait with 100ms timeout for idle checks
- Workspace change events may accumulate in queues

**Improvement path:**
1. Profile event processing time per event type
2. Consider hierarchical polling (prioritize frame events over input)
3. Batch process low-priority events (IPC, logging) between frames

---

### Workspace Model Coordinate Calculations

**Area:** 2D workspace offset calculations
**Files:** `src/compositor/workspace_models.c:305-365` (workspace_calculate_offset_2d)

**Problem:** For Niri with 2D workspace grid, offset calculations involve floating-point math on workspace IDs and coordinates. Precision loss possible at large workspace indices.

**Risk:** Very large workspace grids (>100x100) may have rounding errors in pixel offsets.

**Improvement path:**
1. Add unit tests for offset calculations at extreme coordinates
2. Consider using fixed-point arithmetic for integer workspace indices
3. Log calculated offsets in debug mode for verification

---

## Fragile Areas

### Niri Workspace Position Decoding

**Files:** `src/compositor/niri.c` (entire module)

**Why fragile:** Niri exposes workspace position as `workspace.pos_in_scrolling_layout: [column, row]` in JSON events. Decoding logic is brittle:
- Assumes fixed JSON structure from `niri msg`
- No schema validation of niri output
- Manual JSON parsing with string operations

**Safe modification:**
1. Test against multiple Niri versions (validate output format compatibility)
2. Add JSON schema validation or use proper JSON library
3. Version niri adapter to detect version mismatches
4. Log raw JSON output in debug mode for troubleshooting

**Test coverage:** Currently limited. Needs dedicated `test_niri_adapter.c` with JSON payload variations.

---

### Generic Wayland Compositor Fallback

**Files:** `src/compositor/generic_wayland.c`

**Why fragile:** Fallback adapter makes no assumptions and provides minimal features. Works but offers no parallax animations:
- No workspace change detection
- No monitor/output change handling
- Returns minimal data to keep app running

**Safe modification:**
1. Add capability flags to indicate limitations
2. Log warning that animations disabled on generic compositor
3. Implement basic output tracking to at least update geometry
4. Document which compositors are supported

**Test coverage:** No dedicated tests. Manual testing on unsupported compositors only.

---

### Monitor List Mutations During Rendering

**Files:**
- `src/core/monitor.c` (monitor list management)
- `src/hyprlax_main.c` (layer rendering loop)
- `src/core/monitor.c:500-525` (layer update loop with comment about mutations)

**Why fragile:** Monitor list can change during rendering (Wayland hot-plug events). Comment at `src/core/monitor.c:515` indicates detection of list mutation while updating layers:

```c
if (ctx && ctx->config.debug) fprintf(stderr, "[DEBUG]     WARNING: layer->next mutated (%p -> %p); restoring\n", ...)
```

**Safe modification:**
1. Snapshot monitor list before rendering pass
2. Use reference counting or copy-on-write for monitor objects
3. Defer monitor add/remove until after frame completion
4. Add mutex protection around monitor list (if threading used)

**Test coverage:** Hard to test in unit tests. Requires hot-plugging monitors during rendering.

---

### Resource Monitor Leak Detection Gaps

**Files:** `src/core/resource_monitor.c`

**Why fragile:** Resource monitor counts FDs and memory but doesn't detect all leak types:
- Wayland object leaks (wl_* pointers not freed)
- EGL resource leaks (textures, shaders not freed)
- Thread-local storage leaks
- mmap'd file leaks

**Safe modification:**
1. Extend resource monitor with GPU texture count tracking
2. Add Valgrind-style reporting for memory allocations
3. Check for unreleased Wayland proxy objects
4. Profile against known-clean baseline

**Test coverage:** `tests/test_resource_monitor.c` exists but only checks FD/memory counts, not object leaks.

---

## Scaling Limits

### Maximum Workspace Count

**Area:** Workspace tracking
**Files:** `src/hyprlax.c:93` (config.max_workspaces = 10)

**Current capacity:** Hard-coded default of 10 workspaces. Detected from Hyprland at runtime but may be inaccurate.

**Limit:** Systems with >100 workspaces could cause:
- Array overflows if detection fails
- Memory waste from oversized allocations
- Performance degradation in workspace lookup

**Scaling path:**
1. Use dynamic arrays instead of fixed-size allocations
2. Increase default capacity and auto-grow
3. Detect actual workspace count from each compositor
4. Document limitation for users with extreme workspace counts

---

### Maximum Layers Per Monitor

**Area:** Layer rendering
**Files:** `src/hyprlax.c:6` (INITIAL_MAX_LAYERS = 8)

**Current capacity:** Initial allocation is 8 layers. Grows dynamically via realloc.

**Limit:** Very large numbers of layers (>1000) could cause:
- GPU memory exhaustion (each layer = 1+ textures)
- Rendering performance collapse (O(n) draw calls)
- IPC response buffer overflow if listing all layers

**Scaling path:**
1. Implement layer batching/atlasing to reduce draw calls
2. Add max-layer limit with clear error message
3. Profile rendering time vs. layer count
4. Document performance expectations

---

### IPC Message Size

**Area:** IPC command responses
**Files:** `src/ipc.c:529-780` (command handling)

**Current limit:** `IPC_MAX_MESSAGE_SIZE` (typically 8KB for responses)

**Limit:** If user adds >100 layers, JSON list response could exceed buffer:
```
[{"id":1,"...":"..."},...,{"id":100,"...":"..."}]
```

Each layer entry ~150 bytes = 15KB for 100 layers.

**Scaling path:**
1. Implement paginated responses for LIST command
2. Increase IPC_MAX_MESSAGE_SIZE
3. Stream large responses instead of buffering
4. Add `--limit N` option to LIST command

---

## Dependencies at Risk

### Legacy TOML Configuration Format

**Area:** Config system
**Files:** `src/include/config_legacy.h`, `src/core/config_legacy.c`, `src/main.c:163-165`

**Risk:** Old `.conf` format still supported but not maintained. Requires conversion tool:
```bash
hyprlax ctl convert-config old.conf new.toml
```

**Impact:** Users on old configs don't get new features. Conversion tool could be removed in future versions.

**Migration plan:**
1. Deprecate `.conf` format in v2.0, warn users to convert
2. Remove support in v3.0
3. Document migration in CHANGELOG
4. Auto-convert on startup if possible

---

### Vendor Libraries and Maintenance

**Area:** vendored stb_image.h and toml.c
**Files:**
- `src/vendor/stb_image.h:5898-5899` (TODO comments indicating incomplete implementation)
- `src/vendor/toml.c` (vendored TOML library)

**Risk:**
- stb_image: TODO marks incomplete TGA support, other format gaps possible
- toml: No longer maintained upstream (switched to toml-c99 or others)

**Maintenance burden:**
1. Vendors updates must be tested against all image formats users provide
2. TOML bugs may not get upstream fixes
3. Security issues in vendors require immediate patching

**Migration path:**
1. Evaluate `libtoml` or `toml-c99` as replacements
2. Consider system package for TOML instead of vendoring
3. Keep stb_image but document limitations
4. Test image format support regularly

---

## Test Coverage Gaps

### Workspace Offset Calculation Edge Cases

**What's not tested:**
- 2D workspace offsets with coordinate overflow (negative coordinates)
- Per-output numeric workspaces with gaps in numbering
- Wayfire set-based workspaces with non-contiguous set IDs
- Niri column/row transitions across display boundaries

**Files:** `src/compositor/workspace_models.c`

**Risk:** Workspace changes in uncommon configurations may produce incorrect offsets silently.

**Priority:** HIGH - Affects core parallax functionality

**Test strategy:**
1. Create fixtures for each workspace model type
2. Test boundary transitions (0→1, 9→10, -1→0)
3. Test with actual compositor output samples stored as JSON
4. Validate offset calculations against expected pixel values

---

### Compositor Adapter Capability Interactions

**What's not tested:**
- Split-monitor-workspaces plugin with base Hyprland functionality
- River with multiple tag policies
- Niri with both horizontal and vertical scrolling active
- Wayfire with multiple output groups

**Files:** `src/compositor/*.c` (all adapters)

**Risk:** Feature combinations not tested could crash or behave unexpectedly.

**Priority:** MEDIUM - Affects users with plugin ecosystems

**Test strategy:**
1. Document all tested/unsupported plugin combinations
2. Add integration tests that spawn actual Hyprland/etc. with plugins
3. Mock compositor outputs that exercise all code paths
4. Test adapter initialization against multiple versions

---

### IPC Command Security Edge Cases

**What's not tested:**
- Very long image paths (>PATH_MAX)
- Special characters in layer names (quotes, backslashes)
- Rapid ADD/REMOVE command sequences
- IPC client disconnection during command processing
- Concurrent IPC commands from multiple clients

**Files:** `src/ipc.c`

**Risk:** Malformed or adversarial IPC clients could crash daemon or cause undefined behavior.

**Priority:** MEDIUM - Security and stability

**Test strategy:**
1. Fuzz IPC command input with AFL or libFuzzer
2. Add timeout and partial-read handling for slow clients
3. Test rapid command sequences
4. Test with stress-test tool sending concurrent commands

---

## Missing Critical Features

### Per-Monitor Configuration

**Problem:** All monitors use the same parallax settings. No way to:
- Use different shift amounts on different monitors
- Disable parallax on specific monitors
- Use different easing functions per monitor
- Apply monitor-specific layer sets

**Blocks:** Users with asymmetric multi-monitor setups (different resolutions, refresh rates, use cases)

**Implementation difficulty:** MEDIUM

**Approach:**
1. Extend TOML syntax: `[monitor."HDMI-1"]` section with overrides
2. Store per-monitor config in monitor_instance_t
3. Check per-monitor config in rendering path
4. Fall back to global config if per-monitor not set

---

### Dynamic Layer Reloading

**Problem:** Changing layer config requires restart. No hot-reload of:
- Image paths
- Layer opacity/blur/tint
- Animation duration/easing

**Blocks:** Users tweaking parallax settings, content creators iterating on visuals

**Implementation difficulty:** MEDIUM-HIGH

**Approach:**
1. Watch config file for changes (inotify on Linux)
2. Re-parse config and diff against current state
3. Update layer properties without restart
4. Handle image reloading if path changed

---

### Animation Curve Editor

**Problem:** Easing functions are predefined. No way to:
- Define custom easing curves
- Preview animation curves
- Use Bezier or other spline-based easing

**Blocks:** Advanced users wanting specific animation feel

**Implementation difficulty:** HIGH

**Approach:** Future enhancement - requires bezier math library and possibly GUI tool

---

## Summary by Severity

**CRITICAL (blocks functionality):**
- Plugin detection not implemented (Hyprland, Wayfire)
- Monitor selection not implemented
- Cursor animation time-unit issue (recently fixed)
- GPU fence timeout issue (recently fixed)
- Zero-width monitor calculations (mostly mitigated)

**HIGH (affects performance or stability):**
- Niri multi-monitor FPS collapse
- Workspace offset calculation edge cases
- IPC message size limits for many layers
- Monitor list mutations during rendering

**MEDIUM (affects usability or security):**
- Per-monitor configuration not supported
- IPC socket permissions not hardened
- Test coverage gaps in adapter interactions
- Resource monitor doesn't detect all leak types

**LOW (nice-to-have or rare edge cases):**
- Z-index not implemented
- Layer offset rendering not implemented
- Very large workspace grids (>100x100)
- Dynamic layer reloading not supported

---

*Concerns audit: 2026-03-16*
