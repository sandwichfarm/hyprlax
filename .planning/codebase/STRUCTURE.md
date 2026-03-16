# Codebase Structure

**Analysis Date:** 2026-03-16

## Directory Layout

```
hyprlax/
├── src/                       # Main source code
│   ├── main.c                 # Application entry point
│   ├── hyprlax_main.c         # Main loop and initialization
│   ├── hyprlax.c              # Legacy monolithic file (refactoring in progress)
│   ├── hyprlax_ctl.c          # Control/IPC client
│   ├── ipc.c, ipc.h           # IPC server and protocol
│   ├── include/               # Public headers
│   │   ├── hyprlax.h          # Main application interface
│   │   ├── hyprlax_internal.h # Shared internal definitions
│   │   ├── core.h             # Core engine (animation, easing, layers, config)
│   │   ├── renderer.h         # Renderer abstraction
│   │   ├── platform.h         # Platform abstraction
│   │   ├── compositor.h       # Compositor abstraction
│   │   ├── wayland_api.h      # Wayland protocol constants
│   │   ├── config_toml.h      # TOML config loading
│   │   ├── config_legacy.h    # Legacy config format
│   │   ├── log.h              # Logging macros
│   │   ├── resource_monitor.h # Performance monitoring
│   │   ├── time_utils.h       # Timing utilities
│   │   └── defaults.h         # Default constants
│   ├── core/                  # Core engine modules
│   │   ├── animation.c        # Animation evaluation
│   │   ├── easing.c           # Easing functions
│   │   ├── layer.c            # Layer lifecycle
│   │   ├── config.c           # Config parsing (legacy)
│   │   ├── config_legacy.c    # Legacy format support
│   │   ├── config_toml.c      # TOML format support
│   │   ├── monitor.c          # Per-monitor state and animation
│   │   ├── monitor.h          # Monitor definitions
│   │   ├── render_core.c      # Render orchestration
│   │   ├── event_loop.c       # Epoll/timerfd utilities
│   │   ├── cursor.c           # Cursor input tracking
│   │   ├── resource_monitor.c # CPU/GPU metrics
│   │   ├── time_utils.c       # get_time_ms(), clock helpers
│   │   ├── log.c              # Logging implementation
│   │   ├── input/             # Input manager and providers
│   │   │   ├── input_manager.h
│   │   │   ├── input_manager.c
│   │   │   ├── input_provider.h
│   │   │   ├── providers.c    # Provider registry and initialization
│   │   │   └── modes/
│   │   │       ├── workspace.c # Workspace parallax provider
│   │   │       ├── cursor.c    # Cursor parallax provider
│   │   │       └── window.c    # Window-based provider
│   ├── platform/              # Platform abstraction (Wayland)
│   │   ├── platform.c         # Platform abstraction dispatcher
│   │   └── wayland.c          # Wayland implementation (layer-shell, EGL)
│   ├── compositor/            # Compositor-specific adapters
│   │   ├── compositor.c       # Compositor abstraction dispatcher
│   │   ├── hyprland.c         # Hyprland IPC client
│   │   ├── sway.c             # Sway IPC client
│   │   ├── niri.c             # Niri D-Bus client
│   │   ├── river.c            # River tags model
│   │   ├── wayfire.c          # Wayfire workspace sets
│   │   ├── generic_wayland.c  # Generic wlr-layer-shell fallback
│   │   ├── workspace_models.c # Workspace model abstractions
│   │   ├── workspace_models.h # Workspace context definitions
│   ├── renderer/              # Renderer abstraction (GLES2)
│   │   ├── renderer.c         # Renderer abstraction dispatcher
│   │   ├── gles2.c            # OpenGL ES 2.0 implementation
│   │   ├── shader.c           # Shader compilation and management
│   │   ├── shader.c           # Shader loading
│   │   └── texture_atlas.c    # Texture atlas (stub)
│   ├── vendor/                # Third-party libraries
│   │   ├── toml.c, toml.h     # TOML parser
│   │   ├── gifdec.c, gifdec.h # GIF decoder
│   │   └── stb_image.h        # Image loader (single-file)
│   ├── gfx/                   # Graphics utilities (empty, placeholder)
│   └── stb_image.h            # Symlink/copy of image loader
├── src_old/                   # Legacy code (if present)
├── protocols/                 # Wayland protocol XML files (if custom)
├── tests/                     # Test suite
│   ├── unit/                  # Unit tests
│   ├── integration/           # Integration tests
│   └── stress/                # Performance/stress tests
├── examples/                  # Example configurations
│   ├── pixel-city/parallax.toml
│   ├── cherry-blossom/hyprlax.toml
│   ├── mouse-parallax/
│   └── ... (more examples)
├── docs/                      # Documentation source (mkdocs)
│   ├── getting-started/
│   ├── guides/
│   ├── configuration/
│   ├── development/
│   ├── api/
│   └── reference/
├── packaging/                 # Distribution/packaging
│   └── arch/                  # Arch Linux PKGBUILD
├── scripts/                   # Build and test scripts
│   ├── testing/
│   ├── bench/
│   └── test-installer.sh
├── Makefile                   # Build configuration
├── VERSION                    # Version file (git hash or tag)
└── mkdocs.yml                 # Documentation config
```

## Directory Purposes

**`src/`:**
- Purpose: All C source and headers
- Contains: Implementation files (.c), headers (.h), vendor code
- Key files: `main.c` (entry), `hyprlax_main.c` (main loop), `ipc.c` (control)

**`src/include/`:**
- Purpose: Public and internal headers (included as `#include "include/..."`)
- Contains: Interface definitions for modules
- Key files: `hyprlax.h` (application interface), `core.h` (engine interface), abstractions (renderer.h, platform.h, compositor.h)

**`src/core/`:**
- Purpose: Platform-agnostic engine logic
- Contains: Animation, easing, layer lifecycle, configuration, monitor management, input blending, event loop
- Key files: `animation.c`, `layer.c`, `config_toml.c`, `monitor.c`, `event_loop.c`

**`src/core/input/`:**
- Purpose: Multi-source input blending system
- Contains: Input manager, provider registry, per-mode implementations (workspace, cursor, window)
- Key files: `input_manager.c` (blending), `providers.c` (registry), `modes/workspace.c`, `modes/cursor.c`, `modes/window.c`

**`src/platform/`:**
- Purpose: Windowing system abstraction (Wayland only)
- Contains: Display connection, layer-shell setup, EGL window creation, surface management
- Key files: `wayland.c` (80%+ of implementation)

**`src/compositor/`:**
- Purpose: Compositor-specific features
- Contains: IPC clients (Hyprland, Sway, Niri, River), workspace model abstractions, event streaming
- Key files: `hyprland.c` (Hyprland IPC), `sway.c` (Sway IPC), `workspace_models.c` (abstract workspace tracking)

**`src/renderer/`:**
- Purpose: GPU-agnostic drawing interface
- Contains: Texture loading, shaders, layer drawing with parallax parameters
- Key files: `gles2.c` (only implementation), `shader.c` (shader compilation), `renderer.c` (dispatcher)

**`src/vendor/`:**
- Purpose: Third-party code
- Contains: TOML parser, GIF decoder, image loader (stb_image)
- Not modified except for bug fixes

**`tests/`:**
- Purpose: Automated tests
- Contains: Unit, integration, stress test suites
- Run: `make test` (if available)

**`examples/`:**
- Purpose: Configuration examples
- Contains: TOML configuration files demonstrating features
- Key: `examples/pixel-city/parallax.toml` (feature-rich example), `examples/mouse-parallax/` (cursor tracking)

**`docs/`:**
- Purpose: User and developer documentation
- Contains: mkdocs markdown files, architecture guides, API reference, configuration examples
- Key: `docs/development/` (building, contributing)

## Key File Locations

**Entry Points:**
- `src/main.c`: Application entry, CLI parsing, ctl dispatch
- `src/hyprlax_main.c`: Initialization and main loop
- `src/ipc.c`: IPC server entry point
- `src/hyprlax_ctl.c`: Control client entry point

**Configuration:**
- `src/core/config.c`: Legacy format parsing
- `src/core/config_toml.c`: TOML format parsing
- `src/include/core.h`: Config struct definition
- `examples/`: TOML examples

**Core Logic:**
- `src/core/animation.c`: Animation state evaluation
- `src/core/easing.c`: Easing function implementations
- `src/core/layer.c`: Layer lifecycle and offset animation
- `src/core/monitor.c`: Multi-monitor orchestration
- `src/core/render_core.c`: Frame rendering pipeline per monitor

**Testing:**
- `src/core/event_loop.c`: epoll/timerfd utilities
- `src/core/input/input_manager.c`: Input blending
- `src/core/input/providers.c`: Provider registry

**Platform/Compositor:**
- `src/platform/wayland.c`: Wayland integration (monitor detection, layer-shell, EGL)
- `src/compositor/hyprland.c`: Hyprland workspace tracking via IPC socket
- `src/compositor/sway.c`: Sway workspace tracking via IPC socket
- `src/compositor/niri.c`: Niri workspace tracking via D-Bus
- `src/compositor/workspace_models.c`: Workspace abstraction (1D vs 2D vs tags)

**Rendering:**
- `src/renderer/gles2.c`: OpenGL ES 2.0 implementation
- `src/renderer/shader.c`: Shader loading and compilation
- `src/vendor/gifdec.c`: GIF animation support

**IPC/Control:**
- `src/ipc.c`: Socket server, message handler
- `src/hyprlax_ctl.c`: Client command parser

## Naming Conventions

**Files:**
- `*.c` + `*.h`: Implementation and header pairs
- Single responsibility: `layer.c` = layer operations, `animation.c` = animation, `monitor.c` = multi-monitor
- Adapter files match concept: `src/compositor/hyprland.c` for Hyprland adapter
- Underscore for clarity: `event_loop.c`, `config_toml.c`, `render_core.c`

**Functions:**
- Prefix by module: `animation_evaluate()`, `layer_create()`, `monitor_list_add()`
- Prefixed with `hyprlax_` for public API: `hyprlax_init()`, `hyprlax_run()`
- Private with `static`: Unprefixed helpers, local functions
- Typedef'd structs use `_t` suffix: `animation_state_t`, `config_t`, `monitor_instance_t`

**Types/Structs:**
- Enum suffixes: `_t` (e.g., `app_state_t`, `easing_type_t`)
- Struct suffixes: `_t` for typedef'd (e.g., `parallax_layer_t`, `input_manager_t`)
- Flags/Capabilities: `_capability_t`, `_caps_t` (e.g., `renderer_capability_t`)

**Variables:**
- Local scope: `camelCase` avoided; `snake_case` used
- Global config: `config_t cfg` or `ctx->config`
- Layer loop: `parallax_layer_t *layer`, iterate with `for (layer = head; layer; layer = layer->next)`
- Temporary: `int i`, `float t`, `double now`

**Macros:**
- `HYPRLAX_*` for application constants
- `MIN(a,b)`, `MAX(a,b)`, `CLAMP()` for math
- `DEBUG_LOG()` for debug output (no-op if DEBUG not defined)
- `ARRAY_SIZE(x)` for array length

## Where to Add New Code

**New Feature (e.g., new input source):**
- Primary code: `src/core/input/modes/feature_name.c` (implements `input_provider_ops_t`)
- Register: Add to provider registry in `src/core/input/providers.c`
- Config: Add config fields to `config_t` in `src/include/core.h`
- Tests: `tests/unit/input_feature_name.c`

**New Component/Module (e.g., new renderer backend):**
- Implementation: `src/renderer/backend_name.c` (implements `renderer_ops_t`)
- Header: `src/renderer/backend_name.h` if complex, else ops in dispatcher
- Dispatcher: Update `src/renderer/renderer.c` to instantiate
- Build: Update `Makefile` with `ENABLE_BACKEND_NAME`, compile flags

**Utilities (helper functions):**
- Shared helpers (timing, math): `src/core/time_utils.c` or `src/core/easing.c`
- Logging macros: `src/include/log.h`
- Platform-specific: `src/platform/wayland.c` or new `src/platform/feature.c`

**Tests:**
- Unit tests: `tests/unit/component_test.c` (GCC + Unity, or custom harness)
- Integration: `tests/integration/` (may require compositor fixtures)
- Stress: `tests/stress/` (for performance benchmarks)

## Special Directories

**`src/vendor/`:**
- Purpose: Third-party code (included, not linked)
- Generated: No (checked in)
- Committed: Yes
- Changes: Minimal; prefer patches/forks if significant changes needed

**`examples/`:**
- Purpose: User configuration templates
- Generated: No
- Committed: Yes
- Changes: Add new TOML files for new feature examples

**`docs/`:**
- Purpose: User and developer documentation
- Generated: `docs/_site/` and `_site_mkdocs/` (mkdocs output)
- Committed: Source yes, build output no (in `.gitignore`)
- Build: `mkdocs build`

**`build/`:**
- Purpose: Build artifacts
- Generated: Yes (created by `make`)
- Committed: No (in `.gitignore`)
- Clean: `make clean`

---

*Structure analysis: 2026-03-16*
