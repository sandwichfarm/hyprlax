Dead Code Audit – hyprlax (refactor/feature branch)

Summary
- Default build path is the new modular architecture. Several legacy/duplicate files remain in-tree but are not compiled by default. A few unused types/macros were also identified. Nothing critical found inside the active modular code path; static/global functions all have at least one call/reference.

Method
- Reviewed Makefile SRCS to determine compiled sources vs. legacy path (`USE_LEGACY`).
- Grepped for function definitions and references to flag likely-unused code paths and types.
- Searched for duplicate/obsolete files and macros not referenced in the codebase.

Findings
- Legacy monolith (not compiled by default)
  - File: `src/hyprlax.c`
    - Status: Excluded from default `SRCS`; only compiled when `USE_LEGACY` is set.
    - Notes: Contains full monolithic implementation (Wayland/EGL/GLES2, IPC integration, etc.). Duplicates functionality now present across `src/core/`, `src/renderer/`, `src/platform/`, and `src/compositor/`.
    - Version drift: defines `#define HYPRLAX_VERSION "1.3.1"`, while the modular path defines `HYPRLAX_VERSION "2.0.0-dev"` in `src/include/hyprlax_internal.h`.
    - External references: `install.sh` and `install-remote.sh` parse version from `src/hyprlax.c` if present. AGENTS.md still mentions “Update version in `src/hyprlax.c`”.

- Standalone control client (replaced by integrated subcommand)
  - File: `src/hyprlax-ctl.c`
    - Status: Not in Makefile `SRCS`; not built by default. Functionality superseded by `hyprlax ctl …` implemented in `src/hyprlax_ctl.c`.
    - References: Mentioned in docs/scripts (e.g., `docs/development.md`, `docs/compositors.md`) and used by `scripts/lint.sh` (cppcheck input), but not compiled.
    - Recommendation: Deprecate and remove or move to `tools/` if kept for reference; update docs/scripts accordingly.

- Integrated control interface (active) – minor unused macro
  - File: `src/hyprlax_ctl.c`
    - Unused macro: `#define SOCKET_TIMEOUT_MS 5000` is defined but never used.
    - Impact: None (minor cleanup opportunity).

- IPC layer – unused type
  - File: `src/ipc.h`
    - Unused typedef: `ipc_message_t` (struct) is declared but never referenced anywhere in `src/` or `tests/`.
    - Active types: `ipc_context_t`, `layer_t` and related functions are used by `src/ipc.c` and tests.
    - Recommendation: Remove `ipc_message_t` or implement its usage if a message framing layer is still planned.

- Empty/placeholder directory
  - Dir: `src/gfx/`
    - Status: Currently empty; not referenced by Makefile. Appears to be a placeholder for earlier planning.

- Headers and ops tables
  - `src/include/shader.h` is used via `#include "../include/shader.h"` in `src/renderer/shader.c` (basename-only include searches can miss this).
  - Renderer backends: Only `renderer_gles2_ops` is implemented; mentions of future GL3/Vulkan are intentional placeholders, not dead code.

Non-findings (checked, no dead code found)
- Static and non-static function definitions in active modules (`src/core/*`, `src/renderer/*`, `src/platform/*`, `src/compositor/*`, `src/hyprlax_main.c`) all have ≥1 additional reference within their translation unit or via vtable assignments. No single-occurrence function definitions were detected.

Suggested Cleanup Plan
- Short term
  - Remove unused macro `SOCKET_TIMEOUT_MS` from `src/hyprlax_ctl.c`.
  - Remove unused `ipc_message_t` from `src/ipc.h` (or wire it up if message framing will be used).

- Medium term
  - Confirm deprecation of `src/hyprlax-ctl.c`; update docs (`docs/development.md`, `docs/compositors.md`, CHANGELOG notes) and `scripts/lint.sh` to stop referencing it; then delete or move to `tools/`.
  - Migrate version source of truth to `src/include/hyprlax_internal.h` and update `install.sh` / `install-remote.sh` to parse that header instead of `src/hyprlax.c`. Once no scripts depend on it, archive or remove `src/hyprlax.c`.

Context/References
- Makefile source selection:
  - Default SRCS: `src/main.c src/hyprlax_main.c src/hyprlax_ctl.c src/ipc.c …`
  - Legacy path (only with `USE_LEGACY`): `src/hyprlax.c` + modules
- Script references:
  - `install.sh`/`install-remote.sh` grep `HYPRLAX_VERSION` from `src/hyprlax.c` if present.
  - `scripts/lint.sh` runs cppcheck on `src/hyprlax-ctl.c`.

Notes & Caveats
- This audit is textual/static: it does not execute builds with all optional flags nor run runtime paths (e.g., different compositors/platforms). Indirect uses via plugin-like selection (ops tables) were considered by scanning for symbol references and assignments.
- If any optional build modes (`USE_LEGACY`) are still in active use for compatibility, reclassify those files as “legacy path” rather than dead code.

