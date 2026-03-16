# Architecture

**Analysis Date:** 2026-03-16

## Pattern Overview

**Overall:** Layered modular architecture with abstraction layers for platform, compositor, and renderer, combined with a central event-driven main loop.

**Key Characteristics:**
- **Abstraction-based**: Platform-agnostic core with adapter pattern for Wayland, compositors (Hyprland, Sway, Niri, Wayfire, River, Generic), and renderers (GLES2)
- **Event-driven**: Linux epoll-based unified event loop (frame timer, platform events, compositor events, cursor input, IPC)
- **Multi-monitor native**: Per-monitor state, workspace tracking, independent animation per monitor
- **Non-blocking**: All I/O is async; frame pacing via timerfd; IPC uses non-blocking poll (fixed in recent commit)
- **Modular input**: Input manager with pluggable providers (workspace, cursor, window) with weighted blending
- **Layer-shell based**: Runs as Wayland layer-shell surface; no direct X11 support

## Layers

**Application/Main:**
- Purpose: Entry point, argument parsing, signal handling, startup/shutdown orchestration
- Location: `src/main.c`, `src/hyprlax_main.c`
- Contains: CLI parsing, application lifecycle (`APP_STATE_*`), global shutdown flag
- Depends on: Core, Platform, Compositor, Renderer, Config
- Used by: System launcher, Hyprland exec-once

**Core/Engine:**
- Purpose: Platform-agnostic parallax engine logic, animation system, layer management
- Location: `src/core/` (animation, easing, layer, config, event_loop, cursor, window, monitor, input/)
- Contains: Animation state machine, easing functions, layer lifecycle, configuration parsing, event infrastructure
- Depends on: Nothing (lowest level)
- Used by: Main application, module initializers

**Platform Abstraction:**
- Purpose: Windowing system operations (Wayland only currently)
- Location: `src/platform/`, `src/platform/wayland.c`
- Contains: Display connection, layer-shell window creation, EGL window setup, surface management
- Depends on: Core, Wayland protocols
- Used by: Main app, Renderer (for native handles)

**Compositor Adapter:**
- Purpose: Compositor-specific features (workspace models, cursor position, events)
- Location: `src/compositor/` (hyprland.c, sway.c, niri.c, wayfire.c, river.c, generic_wayland.c, workspace_models.c)
- Contains: IPC clients (Hyprland, Niri, River), workspace model abstractions, event streaming
- Depends on: Core, Platform event FD
- Used by: Main loop (workspace changes, cursor updates)

**Renderer Abstraction:**
- Purpose: GPU-agnostic drawing interface
- Location: `src/renderer/` (gles2.c, shader.c, renderer.c)
- Contains: Texture loading, draw calls, shader compilation, layer drawing with parallax offsets
- Depends on: Core (layer definitions), Platform (EGL context), Vendor libs (stb_image, gifdec)
- Used by: Main render loop

**Multi-Monitor Management:**
- Purpose: Per-monitor state, workspace tracking, animation orchestration
- Location: `src/core/monitor.c`, `src/core/monitor.h`
- Contains: Monitor instance creation, per-monitor animation state, workspace context tracking
- Depends on: Core, Workspace models
- Used by: Main loop, Renderer

**Input Management:**
- Purpose: Blending multiple input sources (workspace, cursor, window) with weighted mixing
- Location: `src/core/input/` (input_manager.c, providers.c, modes/)
- Contains: Input providers (workspace, cursor, window), monitor cache, weighted blending
- Depends on: Core, Platform (cursor tracking)
- Used by: Main loop for parallax offset calculation

**IPC/Control:**
- Purpose: Runtime command interface via Unix socket
- Location: `src/ipc.c`, `src/hyprlax_ctl.c`
- Contains: Socket server, message parsing, layer updates, parameter changes
- Depends on: Core
- Used by: External tools (hyprlax-ctl), runtime updates

## Data Flow

**Initialization Flow:**

1. `main()` parses CLI args, checks for `ctl` subcommand
2. `hyprlax_init()` initializes modules in order:
   - Config parsing (TOML or legacy)
   - Platform (Wayland display, layer-shell setup)
   - Compositor detection (auto-detect or explicit)
   - Renderer (GLES2, EGL context)
   - Input manager
   - IPC server
   - Event loop (epoll setup)
3. Monitor list populated during platform initialization (via wl_output events)
4. Each monitor gets EGL surface, layer-shell surface, workspace tracking

**Main Event Loop:**

```
epoll_wait() on {frame_timer, compositor_events, platform_events, cursor_events, ipc_events, debounce_timer}
  │
  ├─ Frame timer fires → render frame for each monitor
  │   └─ Input manager samples all sources → blended parallax offset
  │   └─ Renderer draws layers with calculated offsets
  │
  ├─ Compositor event (workspace change) → debounce timer (coalesces rapid events)
  │   └─ Monitor.start_parallax_animation() with target offset
  │   └─ Animation ticked each frame via monitor.update_animation()
  │
  ├─ Platform event (monitor added/removed/resized)
  │   └─ Monitor list modified, EGL surfaces updated
  │
  ├─ Cursor event (mouse moved) or IPC input change
  │   └─ Cursor cache updated, frame triggered
  │
  └─ IPC command → layer added/removed, parameter changed → deferred render
```

**Render Frame Flow per Monitor:**

1. Input manager ticks all sources (workspace, cursor, window)
2. Blend sources with weights → composite offset (px_x, px_y)
3. Monitor animation evaluates (if active) → add eased offset
4. For each layer (z-sorted):
   - Apply parallax offset: `offset = base_offset * shift_multiplier`
   - Render with fit_mode (stretch/cover/contain/fit_width/fit_height)
   - Apply per-layer tint, blur, opacity
5. Swap buffers

**State Management:**

- **Global state**: `hyprlax_context_t` (application, config, monitors, renderer, compositor, input manager)
- **Per-monitor state**: `monitor_instance_t` (workspace context, animation state, EGL surface, parallax offset)
- **Per-layer state**: `parallax_layer_t` (texture, animation, offset, fit mode, tint)
- **Per-input-source state**: Stored in input provider (workspace position, cursor position, window position)

## Key Abstractions

**Animation State (`animation_state_t`):**
- Purpose: Time-based interpolation with easing
- Location: `src/include/core.h`, `src/core/animation.c`
- Pattern: Pure value object with `animation_evaluate()` taking `timestamp_ms_t` (int64 ms)
- Used for: Layer position animation, cursor easing

**Parallax Layer (`parallax_layer_t`):**
- Purpose: Single renderable layer with parallax properties
- Location: `src/include/core.h`, `src/core/layer.c`
- Pattern: Linked list; each has texture(s), fit_mode, animation state, inversion flags per axis
- Contains: GIF support with frame cycling, tint/opacity/blur, overflow/tiling modes

**Configuration (`config_t`):**
- Purpose: Global and per-layer settings
- Location: `src/include/core.h`, `src/core/config.c`, `src/core/config_toml.c`
- Pattern: Single mutable struct; defaults set at init; TOML loader overlays values
- Sources: CLI args, TOML file, IPC commands

**Input Manager (`input_manager_t`):**
- Purpose: Multi-source parallax input blending
- Location: `src/core/input/input_manager.h`, `src/core/input/input_manager.c`
- Pattern: Registry of input providers; weighted blending per monitor; cache of samples
- Providers: workspace (from compositor), cursor (from system), window (from compositor)

**Compositor Adapter:**
- Purpose: Abstract workspace/cursor tracking across compositor types
- Pattern: Ops struct with function pointers; each compositor (hyprland, sway, niri, etc.) implements
- Examples: `src/compositor/hyprland.c`, `src/compositor/sway.c`

**Renderer Ops (`renderer_ops_t`):**
- Purpose: Abstract GPU drawing
- Pattern: Function pointers for init, clear, draw_layer_ex (with params), present
- Current: GLES2 only (`src/renderer/gles2.c`)

**Platform Ops (`platform_ops_t`):**
- Purpose: Abstract windowing (Wayland only currently)
- Pattern: Function pointers for display connection, window/surface creation, event polling
- Current: Wayland only (`src/platform/wayland.c`)

## Entry Points

**Application Entry:**
- Location: `src/main.c` `main()`
- Triggers: User runs `hyprlax [--config ...]` or Hyprland `exec-once`
- Responsibilities: Argument parsing, stderr redirect for exec-once, ctl subcommand dispatch, context creation, init/run cycle

**Main Loop Entry:**
- Location: `src/hyprlax_main.c` `hyprlax_run()`
- Triggers: After `hyprlax_init()` succeeds
- Responsibilities: epoll_wait loop, event dispatching, frame timing, shutdown detection

**IPC Entry:**
- Location: `src/hyprlax_ctl.c` or IPC socket
- Triggers: External command `hyprlax ctl ...` or socket client
- Responsibilities: Parse IPC message, apply command, return result

**Compositor Event Entry:**
- Location: Various `src/compositor/*.c` event handlers
- Triggers: IPC read from compositor (Hyprland socket, Niri D-Bus, etc.)
- Responsibilities: Parse compositor-specific output, emit `compositor_event_t`, queue workspace change

## Error Handling

**Strategy:** Return codes propagate up; context preserved on partial failures (e.g., single monitor fails, others continue).

**Patterns:**
- `hyprlax_error_t` enum: `HYPRLAX_SUCCESS`, `HYPRLAX_ERROR_*`
- Early returns with logging: `if (!x) { LOG_ERROR(...); return -1; }`
- Per-monitor failure flag: `monitor->failed = true` allows other monitors to continue
- Graceful degradation: Missing optional features (cursor, blur) don't crash

**Error Sources:**
- Missing WAYLAND_DISPLAY → `HYPRLAX_ERROR_NO_DISPLAY`
- Compositor IPC failures → logged but continue (may lose workspace tracking)
- Texture load failures → `LOG_ERROR`, layer stays invisible
- EGL context loss → logged, attempt recovery or mark monitor failed

## Cross-Cutting Concerns

**Logging:**
- Framework: `src/include/log.h` with `LOG_ERROR`, `LOG_WARN`, `LOG_INFO`, `LOG_DEBUG`, `LOG_TRACE`
- Levels: 0 (ERROR) to 4 (TRACE); controlled by `log_level` in config or `HYPRLAX_DEBUG=1`
- To file: Startup log at `~/.cache/hyprlax/startup.log`, general logs to stderr unless redirected

**Validation:**
- CLI args: `config_parse_args()` validates ranges, prints help on unknown flags
- TOML: `config_load_toml()` validates types, defaults missing values
- Textures: `load_texture()` checks stb_image result, returns 0 on failure
- Workspace IDs: Compositor adapters validate workspace ranges per compositor model

**Authentication:**
- None: Application runs with user privileges, accesses compositor via IPC
- IPC socket: Located at `$XDG_RUNTIME_DIR/hyprlax-<HYPRLAND_INSTANCE_SIGNATURE>.sock`

**Rendering Pipeline Synchronization:**
- Frame pacing: timerfd at target FPS
- Buffer swapping: eglSwapBuffers provides frame pacing on most compositors
- Optional frame callback: `HYPRLAX_FRAME_CALLBACK=1` enables wl_callback for occluded surfaces (e.g., Niri)
- Debouncing: Rapid compositor events (>50ms) coalesced via debounce_timer_fd to prevent animation thrashing

---

*Architecture analysis: 2026-03-16*
