# Contributing to hyprlax

Thank you for your interest in contributing to hyprlax! This guide will help you get started.

## Ways to Contribute

- 🐛 Report bugs
- 💡 Suggest features
- 📝 Improve documentation
- 🧪 Write tests
- 💻 Submit code changes
- 🎨 Create wallpaper examples
- 🌍 Add compositor support

## Getting Started

### 1. Fork and Clone
```bash
# Fork on GitHub, then:
git clone https://github.com/YOUR_USERNAME/hyprlax.git
cd hyprlax
git remote add upstream https://github.com/sandwichfarm/hyprlax.git
```

### 2. Create Branch
```bash
# Update main branch
git checkout main
git pull upstream main

# Create feature branch
git checkout -b feature/your-feature-name
```

### 3. Set Up Development Environment
```bash
# Install dependencies (see building.md)
make debug
make test
```

## Code Style Guidelines

### C Code Style
```c
// 4-space indentation
void function_name(int param) {
    if (condition) {
        // K&R brace style
        do_something();
    } else {
        do_other();
    }
    
    // Descriptive variable names
    int workspace_index = 0;
    float animation_progress = 0.0f;
    
    // Comment complex logic
    // Calculate easing curve for smooth animation
    float eased = ease_out_expo(progress);
}
```

### Naming Conventions
- Functions: `snake_case`
- Types: `snake_case_t`
- Macros: `UPPER_SNAKE_CASE`
- Enums: `UPPER_SNAKE_CASE`

### Code Organization
- Keep functions focused and small
- Group related functions together
- Use header files for interfaces
- Document public APIs

## Commit Guidelines

### Commit Message Format
```
component: brief description (50 chars or less)

Longer explanation of what changed and why. Wrap at 72 characters.
Focus on the why, not the how (code shows how).

- Bullet points for multiple changes
- Keep related changes in same commit
- Separate unrelated changes

Fixes #123
Closes #456
```

### Good Commit Examples
```
compositor: add River workspace tracking support

River exposes workspace info through different protocol than
Hyprland. This adds a new adapter that polls River's IPC.

- Implement river_get_active_workspace()
- Add River to auto-detection logic
- Update compatibility matrix

Fixes #44
```

```
renderer: optimize blur performance with separable filter

Replace single-pass blur with two-pass separable implementation.
Reduces complexity from O(n²) to O(2n), improving FPS by ~40%.
```

### Commit Best Practices
- Make atomic commits (one logical change)
- Test before committing
- Don't commit broken code
- Sign commits if possible: `git commit -S`

## Pull Request Process

### Before Submitting

1. **Test Your Changes**
```bash
# Run test suite
make test

# Test manually
./hyprlax --debug test.jpg

# Check for memory leaks
valgrind --leak-check=full ./hyprlax test.jpg
```

2. **Format Code**
```bash
make format
```

3. **Update Documentation**
- Update relevant docs in `docs/`
- Add/update code comments
- Update CHANGELOG.md if needed

### PR Description Template
```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
- [ ] Tests pass locally
- [ ] Added new tests
- [ ] Tested on [compositor name]

## Checklist
- [ ] Code follows style guidelines
- [ ] Self-reviewed code
- [ ] Updated documentation
- [ ] No new warnings

## Related Issues
Fixes #123
```

### After Submitting
- Respond to review feedback
- Keep PR updated with main branch
- Be patient - reviews take time

## Testing Guidelines

### Writing Tests
```c
// tests/test_my_feature.c
#include "test_framework.h"
#include "../src/my_feature.h"

void test_feature_basic(void) {
    // Arrange
    struct my_data data = {0};
    
    // Act
    int result = my_function(&data);
    
    // Assert
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(42, data.value);
}

int main(void) {
    TEST_RUN(test_feature_basic);
    TEST_RUN(test_feature_edge_case);
    return TEST_RESULT();
}
```

### Test Coverage
- Test happy path
- Test error conditions
- Test edge cases
- Test different configurations

## Adding Features

### New Compositor Support
1. Create adapter in `src/compositor/`
2. Implement `compositor_ops_t` interface
3. Add to auto-detection in `compositor.c`
4. Update documentation
5. Add tests

### New Configuration Options
1. Add to config structure
2. Update parser in `config.c`
3. Add to TOML schema
4. Update documentation
5. Add validation tests

### New IPC Commands
1. Add command handler in `ipc.c`
2. Update protocol documentation
3. Add to `hyprlax_ctl.c`
4. Write tests
5. Update IPC reference docs

## Bug Reports

### Good Bug Report
```markdown
**Description**
Clear description of the bug

**Steps to Reproduce**
1. Run `hyprlax --config test.toml`
2. Switch to workspace 2
3. Observe crash

**Expected Behavior**
Should switch smoothly

**Actual Behavior**
Segmentation fault

**System Information**
- OS: Arch Linux
- Compositor: Hyprland v0.35.0
- hyprlax version: 2.0.0-beta.6
- GPU: NVIDIA RTX 3080

**Debug Output**
```
[Attach hyprlax --debug output]
```

**Additional Context**
Only happens with multi-layer configs
```

## Feature Requests

### Good Feature Request
```markdown
**Problem**
Describe the problem you're trying to solve

**Proposed Solution**
How you think it should work

**Alternatives Considered**
Other solutions you've thought about

**Additional Context**
Examples, mockups, or reference implementations
```

## Development Tips

### Debugging
```bash
# Enable all debug output
HYPRLAX_DEBUG=1 ./hyprlax test.jpg

# Use GDB for crashes
gdb ./hyprlax
(gdb) run --debug test.jpg
(gdb) bt  # Get backtrace after crash

# Check specific modules
HYPRLAX_DEBUG_RENDERER=1 ./hyprlax test.jpg
```

### Performance Testing
```bash
# Run benchmarks
make bench

# Profile with perf
perf record ./hyprlax test.jpg
perf report

# Monitor GPU usage
nvidia-smi dmon -s u  # NVIDIA
radeontop             # AMD
```

### Common Issues

#### "undefined reference" errors
- Check all necessary files are in Makefile
- Verify function declarations match definitions

#### Compositor not detected
- Check environment variables
- Verify compositor-specific detection logic

#### OpenGL errors
```c
// Add after GL calls
GLenum err = glGetError();
if (err != GL_NO_ERROR) {
    ERROR_LOG("GL error: 0x%x at %s:%d", err, __FILE__, __LINE__);
}
```

## Community Guidelines

### Be Respectful
- Treat everyone with respect
- Welcome newcomers
- Be patient with questions

### Be Collaborative
- Work together to solve problems
- Share knowledge and expertise
- Credit others' contributions

### Be Professional
- Keep discussions focused
- Avoid personal attacks
- Respect differing opinions

## Getting Help

- **Questions**: Open a [Discussion](https://github.com/sandwichfarm/hyprlax/discussions)
- **Bugs**: Open an [Issue](https://github.com/sandwichfarm/hyprlax/issues)
- **Security**: Email security@hyprlax.dev

## License

By contributing, you agree that your contributions will be licensed under the same license as hyprlax (MIT).