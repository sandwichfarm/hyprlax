# Testing Guide

Comprehensive guide for testing hyprlax functionality.

## Running Tests

### Quick Test
```bash
# Run all tests
make test

# Run tests with output
make test VERBOSE=1
```

### Specific Tests
```bash
# Run single test suite
./tests/test_animation
./tests/test_config
./tests/test_compositor

# Run with debugging
gdb ./tests/test_animation
```

### Memory Testing
```bash
# Run tests with valgrind
make memcheck

# Run specific test with valgrind
valgrind --leak-check=full ./tests/test_config
```

### Coverage Report
Coverage targets are not currently provided by the build; use external tools if needed.

## Test Structure

### Test Framework
```c
// tests/test_framework.h
#define TEST_ASSERT(condition) \
    if (!(condition)) { \
        printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    }

#define TEST_ASSERT_EQUAL(expected, actual) \
    TEST_ASSERT((expected) == (actual))

#define TEST_RUN(test) \
    printf("Running %s...", #test); \
    if (test() == 0) printf(" PASS\n"); \
    else { printf(" FAIL\n"); failures++; }
```

### Test File Template
```c
// tests/test_example.c
#include "test_framework.h"
#include "../src/module.h"

static int test_basic_functionality(void) {
    // Arrange
    struct data input = {.value = 10};
    
    // Act
    int result = process_data(&input);
    
    // Assert
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(20, input.value);
    
    return 0;
}

static int test_error_handling(void) {
    // Test NULL input
    int result = process_data(NULL);
    TEST_ASSERT_EQUAL(-1, result);
    
    return 0;
}

int main(void) {
    int failures = 0;
    
    printf("=== Example Tests ===\n");
    TEST_RUN(test_basic_functionality);
    TEST_RUN(test_error_handling);
    
    printf("\nResults: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
```

## Test Categories

### Unit Tests

Test individual functions in isolation:

```c
// tests/test_easing.c
static int test_linear_easing(void) {
    float result = ease_linear(0.5);
    TEST_ASSERT_FLOAT_EQUAL(0.5, result, 0.001);
    return 0;
}

static int test_expo_easing(void) {
    // Test boundaries
    TEST_ASSERT_FLOAT_EQUAL(0.0, ease_out_expo(0.0), 0.001);
    TEST_ASSERT_FLOAT_EQUAL(1.0, ease_out_expo(1.0), 0.001);
    
    // Test midpoint
    float mid = ease_out_expo(0.5);
    TEST_ASSERT(mid > 0.4 && mid < 0.6);
    
    return 0;
}
```

### Integration Tests

Test component interactions:

```c
// tests/test_layer_rendering.c
static int test_multi_layer_render(void) {
    // Create test layers
    struct layer layers[3];
    create_test_layers(layers, 3);
    
    // Initialize renderer
    struct renderer *r = renderer_create();
    TEST_ASSERT(r != NULL);
    
    // Render frame
    int result = renderer_draw_frame(r, layers, 3);
    TEST_ASSERT_EQUAL(0, result);
    
    // Cleanup
    renderer_destroy(r);
    return 0;
}
```

### Platform Tests

Test platform-specific functionality:

```c
// tests/test_platform_wayland.c
static int test_wayland_connection(void) {
    struct platform *p = platform_create("wayland");
    TEST_ASSERT(p != NULL);
    
    // Test display connection
    TEST_ASSERT(p->display != NULL);
    
    // Test window creation
    struct window *w = platform_create_window(p, 1920, 1080);
    TEST_ASSERT(w != NULL);
    
    platform_destroy(p);
    return 0;
}
```

### Configuration Tests

Test config parsing and validation:

```c
// tests/test_config.c
static int test_toml_parsing(void) {
    const char *config = "[global]\nfps = 60\n";
    
    struct config cfg;
    int result = config_parse_string(&cfg, config);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(60, cfg.fps);
    
    return 0;
}

static int test_invalid_config(void) {
    const char *config = "[global]\nfps = -1\n";
    
    struct config cfg;
    int result = config_parse_string(&cfg, config);
    TEST_ASSERT_EQUAL(-1, result); // Should fail
    
    return 0;
}
```

## Test Data

### Test Images
```bash
# Generate test images
tests/generate_test_images.sh

# Test images location
tests/data/
├── test_1920x1080.png
├── test_3840x2160.png
├── test_transparent.png
└── test_corrupted.jpg
```

### Test Configurations
```toml
# tests/data/test_basic.toml
[global]
fps = 60
debug = true

[[global.layers]]
path = "tests/data/test_1920x1080.png"
```

## Performance Tests

### Benchmark Framework
```c
// tests/bench_rendering.c
#include <time.h>

static void bench_frame_render(void) {
    struct renderer *r = create_test_renderer();
    struct layer layers[5];
    create_test_layers(layers, 5);
    
    clock_t start = clock();
    
    for (int i = 0; i < 1000; i++) {
        renderer_draw_frame(r, layers, 5);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("1000 frames in %.2f seconds (%.1f FPS)\n", 
           time_spent, 1000.0 / time_spent);
}
```

### Memory Benchmarks
```bash
# Profile memory usage
valgrind --tool=massif ./hyprlax test.jpg
ms_print massif.out.*

# Check for leaks
valgrind --leak-check=full --show-leak-kinds=all ./hyprlax test.jpg
```

## Mock Objects

### Mock Compositor
```c
// tests/mocks/mock_compositor.c
struct compositor *mock_compositor_create(void) {
    struct compositor *c = calloc(1, sizeof(*c));
    c->ops = &mock_compositor_ops;
    c->mock_workspace = 1;
    return c;
}

static int mock_get_workspace(struct compositor *c) {
    return c->mock_workspace;
}

static struct compositor_ops mock_compositor_ops = {
    .get_active_workspace = mock_get_workspace,
    .init = mock_init,
    .cleanup = mock_cleanup,
};
```

## Test Utilities

### Helper Functions
```c
// tests/test_utils.c
void create_test_image(const char *path, int width, int height) {
    // Create dummy image file for testing
}

struct config *create_test_config(void) {
    struct config *cfg = calloc(1, sizeof(*cfg));
    cfg->fps = 60;
    cfg->duration = 1.0;
    cfg->easing = EASING_EXPO;
    return cfg;
}

void simulate_workspace_change(int from, int to) {
    // Simulate compositor workspace event
}
```

## Continuous Integration

### GitHub Actions
```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y libwayland-dev wayland-protocols
    
    - name: Build
      run: make debug
    
    - name: Run tests
      run: make test
    
    - name: Memory check
      run: make memcheck
    
    - name: Coverage
      run: make coverage
    
    - name: Upload coverage
      uses: codecov/codecov-action@v2
```

## Test Checklist

### Before Commit
- [ ] All tests pass: `make test`
- [ ] No memory leaks: `make memcheck`
- [ ] Coverage maintained: `make coverage`
- [ ] New features have tests
- [ ] Edge cases tested

### Test Types to Write
- [ ] Happy path tests
- [ ] Error condition tests
- [ ] Boundary tests
- [ ] Configuration tests
- [ ] Platform compatibility tests
- [ ] Performance regression tests

## Debugging Failed Tests

### Using GDB
```bash
# Debug test failure
gdb ./tests/test_animation
(gdb) run
(gdb) bt  # Backtrace on failure
(gdb) print variable_name
```

### Add Debug Output
```c
// Temporary debug output in tests
#ifdef DEBUG_TEST
    printf("DEBUG: value=%d expected=%d\n", value, expected);
#endif
```

### Common Test Issues

#### Test Hangs
- Check for infinite loops
- Verify timeouts are set
- Check for deadlocks

#### Flaky Tests
- Remove timing dependencies
- Use mocks for external dependencies
- Ensure proper cleanup

#### Platform-Specific Failures
- Use conditional compilation
- Mock platform-specific calls
- Document platform requirements

## Writing Good Tests

### Principles
- **Fast**: Tests should run quickly
- **Independent**: Tests shouldn't depend on each other
- **Repeatable**: Same result every time
- **Self-Validating**: Clear pass/fail
- **Timely**: Write tests with code

### Best Practices
```c
// Good test
static int test_specific_behavior(void) {
    // Single behavior per test
    // Clear arrange, act, assert
    // Descriptive name
    // Minimal setup
}

// Bad test
static int test_everything(void) {
    // Tests too many things
    // Hard to understand failure
    // Complex setup
    // Order-dependent
}
```
