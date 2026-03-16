# External Integrations

**Analysis Date:** 2026-03-16

## APIs & External Services

**Wayland Protocol Stack:**
- wlr-layer-shell (unstable-v1) - Wallpaper layer registration and rendering surface
  - SDK/Client: wayland-client library
  - Protocol: `src/protocols/wlr-layer-shell-unstable-v1.xml`
  - Generated headers: `protocols/wlr-layer-shell-client-protocol.h`

**Compositor-Specific IPC:**
- Hyprland IPC - Socket-based workspace and event communication
  - Socket path: `${XDG_RUNTIME_DIR}/hyprland/`
  - Protocol: Custom text-based (non-standardized)
  - Implementation: `src/compositor/hyprland.c`

- Sway/i3 IPC - i3 protocol implementation
  - Socket path: `${XDG_RUNTIME_DIR}/sway-ipc.sock` (or similar)
  - Protocol: i3 compatible JSON-RPC style
  - Implementation: `src/compositor/sway.c`

- River Tag System - Tag-based workspace model
  - Socket path: River-specific
  - Protocol: river-status-unstable-v1
  - Protocol file: `src/protocols/river-status-unstable-v1.xml`
  - Implementation: `src/compositor/river.c`

- Niri Protocol - Scrollable workspaces
  - Implementation: `src/compositor/niri.c`
  - Experimental support

- Wayfire - Compositor event system
  - Implementation: `src/compositor/wayfire.c`

- Generic Wayland Fallback
  - Minimal compositor integration
  - Implementation: `src/compositor/generic_wayland.c`

## Data Storage

**Configuration Storage:**
- **Format:** TOML files only (no database)
  - Parser: tomlc99 vendor library (`src/vendor/toml.c`)
  - Location: User config directory `~/.config/hyprlax/`
  - Example structure: `~/.config/hyprlax/pixel-city/parallax.toml`

**Image Files:**
- **Loading:** Embedded stb_image library (`src/stb_image.h`)
- **Formats:** PNG, JPG (through stb_image)
- **GIF Support:** gifdec vendor library (`src/vendor/gifdec.c`)
- **Storage:** User's filesystem (relative to config file or absolute paths)

**Runtime Layer State:**
- **In-Memory Only:** Layer definitions stored in `hyprlax_context_t`
- **IPC Temporary Storage:** Layer metadata in `ipc_context_t`
- **Persistence:** No automatic save; configuration is read-only after startup

**Logs:**
- **Stderr:** Debug output when `HYPRLAX_DEBUG=1`
- **Log File:** `/home/sandwich/.local/share/hyprlax/hyprlax.log` (if stderr redirected)
- **No persistent logging:** Logs are ephemeral, rotated on app restart

**File Storage:**
- **Local filesystem only** - No cloud storage or external storage services
- **Socket storage:** IPC sockets in `/tmp/hyprlax-<uid>-<suffix>`

## Authentication & Identity

**Auth Provider:**
- Custom - None required
- No user authentication, identity, or token system
- All access is local user only (file permissions on sockets)

**IPC Access Control:**
- Socket-based communication
- File permissions inherited from `/tmp` (user-only access by default)
- Socket path: `/tmp/hyprlax-<uid>-<suffix>` where `<uid>` is numeric user ID

## Monitoring & Observability

**Error Tracking:**
- None - Custom error handling only
- Errors logged to stderr via custom `LOG_*` macros

**Logs:**
- **Approach:** Direct stderr output
- **Framework:** Custom logging system (`src/core/log.c`)
- **Levels:** INFO, WARN, ERROR, DEBUG (configurable via `HYPRLAX_VERBOSE`)
- **Output:** stderr (optionally redirected to file via `HYPRLAX_STDERR_LOG_PATH`)

**Debug Features:**
- `HYPRLAX_DEBUG=1` - Enable debug-level logging
- `HYPRLAX_VERBOSE=<0-4>` - Control verbosity (0=errors only, 4=maximum)
- `HYPRLAX_INIT_TRACE=1` - Early initialization tracing
- `HYPRLAX_TRACE=<context>` - Context-specific tracing

**Profiling & Performance:**
- Resource monitoring: `src/core/resource_monitor.c`
- Timing instrumentation: `src/core/time_utils.c`
- No persistent metrics collection

## CI/CD & Deployment

**Hosting:**
- GitHub - Source repository
- Self-hosted binary distribution (releases page)
- No cloud deployment

**CI Pipeline:**
- GitHub Actions (`.github/workflows/` inferred from standard repo structure)
- Build targets: x86_64 (primary), aarch64 (cross-compilation support)
- CI mode: Portable binaries with `-O2 -march=generic`

**Deployment:**
- Binary release artifacts
- Installation methods:
  1. Direct download + execute
  2. Remote install script: `https://hyprlax.com/install.sh`
  3. Manual from source: `git clone` + `./install.sh`
- Installation paths:
  - System-wide: `/usr/local/bin/hyprlax` (requires sudo)
  - User-local: `~/.local/bin/hyprlax` (no sudo needed)

## Environment Configuration

**Required env vars:**
- `WAYLAND_DISPLAY` - Wayland display identifier (e.g., `wayland-0`)
- `XDG_RUNTIME_DIR` - Runtime directory for sockets

**Optional env vars:**
- `HYPRLAX_RENDER_FPS` - Override FPS
- `HYPRLAX_ANIMATION_DURATION` - Override animation duration
- `HYPRLAX_ANIMATION_EASING` - Override easing function
- `HYPRLAX_DEBUG` - Enable debug output
- `HYPRLAX_VERBOSE` - Set verbosity level
- `HYPRLAX_COMPOSITOR` - Force specific compositor

**Secrets location:**
- None - No secrets management needed
- No API keys, tokens, or credentials used

## Webhooks & Callbacks

**Incoming:**
- None - No webhook endpoints

**Outgoing:**
- None - No external service callbacks

**Frame Callbacks:**
- Internal: Wayland frame callback protocol (wl_callback)
- Controlled by `HYPRLAX_FRAME_CALLBACK` environment variable
- Non-blocking event loop integration

## Network Communication

**Local Communication:**
- **IPC Socket:** Unix socket at `/tmp/hyprlax-<uid>-<suffix>`
- **Wayland Socket:** Standard Wayland socket (via `WAYLAND_DISPLAY`)
- **Compositor IPC:** Hyprland, Sway, River use their own socket endpoints

**No Network Access:**
- No internet connectivity required
- No remote API calls
- No configuration server
- Fully local/standalone operation

## Protocol Specifications

**Wayland Protocols Used:**
1. **wlr-layer-shell-unstable-v1** - Layer surface creation
   - Main surface for wallpaper rendering
   - Layer: Bottom (ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND)

2. **xdg-shell (stable)** - XDG window protocol
   - Basic window surface handling

3. **fractional-scale-v1 (staging)** - Fractional scaling
   - Handles non-integer scaling factors on high-DPI displays

4. **viewporter (stable)** - Viewport manipulation
   - Scaling and cropping of rendered surfaces

5. **river-status-unstable-v1** - River workspace events
   - Tag and workspace change notifications (River only)

---

*Integration audit: 2026-03-16*
