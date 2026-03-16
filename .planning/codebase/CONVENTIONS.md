# Coding Conventions

**Analysis Date:** 2026-03-16

## Naming Patterns

**Files:**
- Lowercase with underscores: `animation.c`, `layer.c`, `config_toml.c`
- Headers in `src/include/` with matching names: `core.h`, `log.h`, `compositor.h`
- Test files: `test_*.c` in `tests/` directory (e.g., `test_animation.c`, `test_ipc.c`)
- Module-specific headers in subdirectories: `src/core/input/input_manager.h`, `src/renderer/texture_atlas.h`

**Functions:**
- Snake_case: `animation_start()`, `layer_create()`, `config_load_file()`
- Module-prefixed for public functions: `animation_*`, `layer_*`, `config_*`, `log_*`, `compositor_*`
- Private static functions use same convention: `static void get_test_socket_path()`
- Paired create/destroy: `layer_create()` → `layer_destroy()`, `ipc_init()` → `ipc_cleanup()`

**Variables:**
- Snake_case for all variables: `current_time`, `layer_count`, `socket_path`
- Global state with `g_` prefix: `g_log_file`, `g_log_to_file`, `g_min_level`
- Static module-level: `static uint32_t next_layer_id = 1`
- Loop counters: `i`, `j`, `k`
- Boolean conditions: `active`, `completed`, `is_gif`, `hidden`

**Types:**
- Typedef structs with `_t` suffix: `animation_state_t`, `parallax_layer_t`, `config_t`, `compositor_type_t`
- Enum types with `_t` suffix: `easing_type_t`, `layer_fit_mode_t`, `log_level_t`, `hyprlax_error_t`
- No typedef for struct pointer types (use `struct name *` not `name_ptr_t`)

**Constants:**
- UPPERCASE_WITH_UNDERSCORES: `HYPRLAX_DEFAULT_FPS`, `HYPRLAX_VERSION`, `ARRAY_SIZE`
- Grouped by module in headers: `defaults.h` contains timing, scaling, animation constants
- Magic numbers in `src/include/defaults.h` as `#define`, not inline literals

## Code Style

**Formatting:**
- No automated formatter (clang-format not configured)
- 4-space indentation (observed in all source files)
- Braces on same line for functions: `void func() {`
- Braces on same line for control flow: `if (condition) {`
- Spaces around operators: `a + b`, `x = y`, `if (x == 5)`
- One statement per line

**Linting:**
- GCC warnings enabled: `-Wall -Wextra` (Makefile line 13)
- Compilation with optimizations in production: `-O3 -march=native -flto`
- CI uses generic build: `-O2` (no native arch for compatibility)
- No .clang-format or .astylerc present; style is conventional C

**Line length:**
- Observed maximum ~100 characters (no enforced limit in config)

## Import Organization

**Order (observed pattern in source files):**
1. System headers: `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<stdbool.h>`, `<stdint.h>`
2. POSIX/platform headers: `<unistd.h>`, `<sys/socket.h>`, `<sys/un.h>`, `<time.h>`
3. External/vendor headers: (none in main code; `src/vendor/` contains inline copies)
4. Local headers: `"../include/core.h"`, `"../include/log.h"`
5. Module headers: `"include/defaults.h"`, `"../src/ipc.h"`

**Path Aliases:**
- Relative includes from source location: `#include "../include/core.h"`
- `-Isrc -Isrc/include` flags in Makefile (line 13) allow `#include "core.h"`
- Protocol headers generated in `protocols/` namespace: `#include "xdg-shell-client-protocol.h"`

## Error Handling

**Patterns:**
- Null pointer checks on entry: `if (!image_path) return NULL;` (layer.c:17)
- Early return on validation failure: `if (!layer) return;` (multiple in layer.c)
- NULL returns for allocation failure: `if (!layer) return NULL;` (layer.c:20)
- Error codes via `hyprlax_error_t` enum (hyprlax_internal.h:37-48):
  - `HYPRLAX_SUCCESS = 0`
  - `HYPRLAX_ERROR_INVALID_ARGS = -1`
  - `HYPRLAX_ERROR_NO_MEMORY = -2`
  - `HYPRLAX_ERROR_GL_INIT = -5`
- No exceptions (C language); errors propagated through return codes or NULL
- Assertions in tests with Check framework: `ck_assert_ptr_nonnull()`, `ck_assert_int_eq()`

## Logging

**Framework:** Custom via `src/core/log.c`

**API:**
- Log levels enum: `LOG_ERROR`, `LOG_WARN`, `LOG_INFO`, `LOG_DEBUG`, `LOG_TRACE` (log.h:12-18)
- Macros for convenience: `LOG_ERROR(...)`, `LOG_WARN(...)`, `LOG_DEBUG(...)`
- Initialization: `log_init(bool debug, const char *log_file)` (log.c:20)
- Level control: `log_set_level(log_level_t level)` (log.c:50)

**Patterns:**
- Default level: `LOG_WARN` (warnings and errors only)
- Debug flag sets level to `LOG_DEBUG`
- Dual output: logs can go to file and stderr
- Format: `[LEVEL] message` with timestamp `HH:MM:SS.mmm`
- File output includes PID and start/end markers
- When logging to file, stderr gets only `WARN` and `ERROR` to avoid spam (log.c:96-102)

**Usage in code:**
```c
LOG_DEBUG("Starting animation from %.2f to %.2f", from_value, to_value);
LOG_WARN("Layer %u not found", layer_id);
LOG_ERROR("Failed to load texture: %s", error_msg);
```

## Comments

**When to Comment:**
- Complex algorithms: easing functions have one-line summaries (easing.c)
- Workarounds or non-obvious code: `/* Note: OpenGL texture cleanup should be done by the renderer */`
- Bug references or future work: `/* Will be set on first evaluate */` (animation.c:22)
- File headers: Purpose statement at top of each C/H file

**Header Comments:**
- File: `/* name.c - Brief description */`
- Struct: `/* Purpose and usage */` above typedef
- Function: Comments above declaration explaining parameters and return

**JSDoc/TSDoc:**
- Not used; C project without automated doc generation
- Inline comments for complex logic suffice

## Function Design

**Size:**
- Typical: 30-100 lines (layer_update_offset ~50 lines, animation_evaluate ~40 lines)
- Keep functions focused on single responsibility
- Longer functions (e.g., config parsing) broken into helper functions

**Parameters:**
- Pointers to structs for out-parameters: `void config_parse_args(config_t *cfg, int argc, char **argv)`
- Const for read-only: `const animation_state_t *anim`
- Null-check entry parameters immediately

**Return Values:**
- Pointers return NULL on failure: `parallax_layer_t* layer_create(...)`
- Int returns 0 for success, negative for errors (matching errno convention)
- Bool returns `true`/`false` for simple predicates: `bool animation_is_active(const animation_state_t *anim)`
- Void for operations that can't fail or where failure is logged

## Module Design

**Exports:**
- Public API in `src/include/*.h` headers
- Single responsibility per module: `core.h` for animation/easing, `log.h` for logging, `compositor.h` for adapter system
- No circular dependencies; clear layer structure (core → compositor → main)

**Barrel Files:**
- `src/include/core.h` aggregates core module interfaces: easing, animation, layer, config
- No glob exports; functions explicitly declared

**Static Functions:**
- Module-private functions marked `static`
- Used for helper functions not part of public API
- Example: `static uint32_t next_layer_id = 1` in layer.c

**Structure:**
- Struct initialization via dedicated create functions: `layer_create()`
- Cleanup via destroy functions: `layer_destroy()`
- Config via dedicated loader: `config_load_file()`

---

*Convention analysis: 2026-03-16*
