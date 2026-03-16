# Testing Patterns

**Analysis Date:** 2026-03-16

## Test Framework

**Runner:**
- Check framework (libcheck) for unit tests
- Config: Makefile lines 247-250 define `CHECK_CFLAGS` and `CHECK_LIBS` via `pkg-config`
- Required: `libcheck` library installed on system

**Assertion Library:**
- Check framework assertions: `ck_assert_*` macros
- Common assertions:
  - `ck_assert(condition)` - boolean check
  - `ck_assert_int_eq(a, b)` - integer equality
  - `ck_assert_float_eq(a, b)` - float equality (exact)
  - `ck_assert_float_eq_tol(a, b, tolerance)` - float with tolerance
  - `ck_assert_ptr_nonnull(ptr)` - pointer not NULL
  - `ck_assert_str_eq(a, b)` - string equality
  - `ck_assert_int_gt(a, b)` - greater than
  - `ck_assert_int_ge(a, b)` - greater or equal

**Run Commands:**
```bash
make test                # Run all tests (Makefile line 368)
make test-scripts        # Run shell-based test scripts (Makefile line 391)
make memcheck            # Run tests with Valgrind memory checking (Makefile line 418)
make clean-tests         # Remove test binaries and logs (Makefile line 471)
```

**Test Targets:**
- 30 individual test programs under `tests/test_*.c`
- Run via Make: `tests/test_animation`, `tests/test_ipc`, `tests/test_config`, etc.
- Tests can run individually: `./tests/test_animation`
- Environment variable for socket namespace: `HYPRLAX_SOCKET_SUFFIX=tests` (Makefile line 374)

## Test File Organization

**Location:**
- All tests in `tests/` directory at project root
- Co-located with source code (not in subdirectories)
- One test file per major feature: `test_animation.c`, `test_config.c`, `test_ipc.c`, `test_compositor.c`

**Naming:**
- Test files: `test_<feature>.c` (e.g., `test_animation.c`, `test_blur.c`, `test_easing.c`)
- Shell tests: `test_*.sh` (e.g., bash scripts for integration testing)
- Helper stubs: `stubs_*.c` (e.g., `stubs_gfx.c` for graphics mocking - Makefile line 343)

**Structure:**
```
tests/
├── test_animation.c      # Animation state and timing
├── test_easing.c         # Easing function tests
├── test_config.c         # Configuration parsing
├── test_ipc.c            # IPC socket and messaging
├── test_compositor.c     # Compositor detection
├── test_hyprland_events.c # Hyprland-specific parsing
├── test_toml_config.c    # TOML config loading
├── stubs_gfx.c           # Mock graphics functions
└── [28 more test files]
```

## Test Structure

**Suite Organization (Check framework pattern):**
```c
// From test_animation.c
START_TEST(test_animation_timing)
{
    float duration = 1.0f;
    float delay = 0.5f;
    float current_time = 2.0f;
    float animation_start = 1.0f;

    // Calculate and assert
    float elapsed = current_time - animation_start - delay;
    float progress = elapsed / duration;

    ck_assert_float_eq_tol(progress, 0.5f, 0.001f);
}
END_TEST

Suite *animation_suite(void)
{
    Suite *s = suite_create("Animation");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_animation_timing);
    tcase_add_test(tc_core, test_workspace_offset);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(animation_suite());
    srunner_set_fork_status(sr, CK_FORK);  // Fork for isolation
    srunner_run_all(sr, CK_NORMAL);
    return (srunner_ntests_failed(sr) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

**Patterns:**
- One `Suite` per test file (aggregates test cases)
- Multiple `TCase` within suite (group related tests)
- Multiple `START_TEST(name) ... END_TEST` blocks within case
- Fork mode enabled for test isolation: `srunner_set_fork_status(sr, CK_FORK)` (test_animation.c:145)

## Setup & Teardown

**Fixture Pattern (test_ipc.c example):**
```c
static ipc_context_t* test_ctx = NULL;
static char* test_image = NULL;

void setup(void) {
    // Create test fixtures
    test_image = strdup("/tmp/test_image.png");
    FILE* f = fopen(test_image, "w");
    if (f) fclose(f);
}

void teardown(void) {
    // Clean up resources
    if (test_ctx) {
        ipc_cleanup(test_ctx);
        test_ctx = NULL;
    }
    if (test_image) {
        unlink(test_image);
        free(test_image);
        test_image = NULL;
    }
}
```

**Lifecycle:**
- `setup()` runs before each test
- Test code runs
- `teardown()` runs after each test (even on failure)
- Tests are forked for isolation (no shared state)

## Mocking

**Framework:**
- No dedicated mocking library (e.g., no Cmocka)
- Mocking via stub implementations or conditional compilation

**Patterns:**

**Stub approach (test_runtime_properties.c):**
```c
// tests/stubs_gfx.c - Mock graphics functions
// Provides no-op implementations of renderer functions
// Linked in composite test targets
```

**Conditional compilation (test_hyprland_events.c, Makefile line 314):**
```makefile
tests/test_hyprland_events: tests/test_hyprland_events.c src/compositor/hyprland.c ...
	$(CC) $(TEST_CFLAGS) -DUNIT_TEST -Isrc -Isrc/include $^ ...
```

**Environment variable mocking (test_compositor.c):**
```c
// Mock environment-based detection
unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
setenv("SWAYSOCK", "/run/user/1000/sway-ipc.sock", 1);
compositor_type_t detected = detect_compositor();
ck_assert_int_eq(detected, COMPOSITOR_SWAY);
```

**What to Mock:**
- Graphics/rendering (use stubs_gfx.c)
- File I/O (create temp files in /tmp)
- Environment variables (setenv/unsetenv)
- System calls (mock via test fixtures)

**What NOT to Mock:**
- Core business logic (test real implementations)
- Animation/easing functions (pure math, always test)
- String parsing/config parsing (test with real parsing)

## Fixtures and Factories

**Test Data (test_config.c example):**
```c
// Create temporary config file for testing
FILE *f = fopen("/tmp/test_config.conf", "w");
ck_assert_ptr_nonnull(f);
fprintf(f, "# Comment line\n");
fprintf(f, "duration 2.5\n");
fprintf(f, "shift 150\n");
fclose(f);

// Parse config and verify
FILE *config = fopen("/tmp/test_config.conf", "r");
char line[256];
while (fgets(line, sizeof(line), config)) {
    // Parse logic
}
fclose(config);
unlink("/tmp/test_config.conf");  // Clean up
```

**Location:**
- Test-specific fixtures created in test functions: `mkdir("/tmp/test_config_dir", 0755)`
- Cleaned up in teardown: `unlink("/tmp/config.conf")`, `rmdir("/tmp/test_dir")`
- Prefix with `/tmp/` for safety

**Factory Pattern:**
- No dedicated factory functions
- Create test objects inline in tests
- Example: `ipc_context_t* ctx = ipc_init()` directly in test

## Coverage

**Requirements:**
- Not enforced (no coverage percentage target)
- 30 test files covering major features
- Emphasis on high-risk code (IPC, animation, rendering)

**View Coverage:**
- No automated coverage report generation
- Manual inspection via test count and feature coverage

## Test Types

**Unit Tests (majority of tests):**
- Scope: Individual functions or tightly-coupled modules
- Approach: Direct function calls with Check assertions
- Example: `test_animation_timing()` tests animation progress calculation
- Isolated via fork mode and temporary files

**Integration Tests:**
- `test_integration.c` - Links multiple modules (ipc.c, log.c)
- Makefile line 264: `tests/test_integration: tests/test_integration.c src/ipc.c src/core/log.c`
- Tests IPC + logging together

**Compositor Adapter Tests:**
- `test_hyprland_events.c` - Tests Hyprland event parsing (Makefile line 313)
- `test_niri_events.c` - Tests Niri event parsing (Makefile line 316)
- `test_compositor_ops.c` - Validates all compositor adapters have required operations
- `test_compositor_caps.c` - Tests capability querying across adapters

**E2E Tests:**
- Shell scripts in `tests/test_*.sh` (if present)
- Run via `make test-scripts`
- Not present in current codebase (tests/test_*.sh is empty set)

## Common Patterns

**Async Testing:**
- Not applicable (single-threaded animation/config code)
- IPC tests use socket communication but not threads

**Error Testing (test_config.c example):**
```c
START_TEST(test_config_validation)
{
    struct {
        const char *line;
        int should_fail;
    } test_cases[] = {
        {"duration -1.0", 1},      // Negative duration (invalid)
        {"duration 0.0", 1},       // Zero duration (invalid)
        {"duration 10.0", 0},      // Valid
        {"shift -100", 1},         // Negative shift (invalid)
        {"fps 0", 1},              // Zero FPS (invalid)
    };

    // Test each case
    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        if (test_cases[i].should_fail) {
            ck_assert_int_ne(validate(test_cases[i].line), 0);
        } else {
            ck_assert_int_eq(validate(test_cases[i].line), 0);
        }
    }
}
END_TEST
```

**Boundary Testing:**
- Edge cases: `t = 0.0f`, `t = 1.0f` for easing functions
- Limits: `HYPRLAX_MAX_ALLOWED_FPS = 999` (defaults.h:10)
- NULL checks on all pointer parameters

**Memory Testing:**
```bash
# Run with Valgrind for memory leaks
make memcheck

# Validates:
# - No unfreed allocations
# - No use-after-free
# - No uninitialized memory access
# Configuration: VALGRIND_FLAGS in Makefile line 254
# DEBUGINFOD_URLS for symbol resolution on Arch Linux
```

## Test Execution Flow

**Full suite run:**
```bash
$ make test
=== Running Full Test Suite ===

--- Running tests/test_integration ---
✓ tests/test_integration PASSED

--- Running tests/test_ipc ---
✓ tests/test_ipc PASSED

[... 28 more tests ...]

=== Test Summary ===
✓ All tests passed!
```

**Individual test:**
```bash
$ HYPRLAX_SOCKET_SUFFIX=tests ./tests/test_animation
Running suite: Animation
...
All tests passed!
```

**With memory checking:**
```bash
$ make memcheck
=== Running Tests with Valgrind Memory Check ===

--- Memory check: tests/test_animation ---
✓ tests/test_animation MEMORY CLEAN

[... more tests ...]

=== Memory Check Summary ===
Total: 30 tests
Passed: 30 tests
Failed: 0 tests
✓ All tests memory clean!
```

---

*Testing analysis: 2026-03-16*
