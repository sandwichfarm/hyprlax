---
name: Bug Fix
about: Fix a bug in hyprlax
title: '[FIX] '
labels: bug
---

## Bug Fix Summary
<!-- Brief description of what bug this PR fixes -->
Fixes #<!-- issue number -->

## Root Cause Analysis
<!-- Explain what was causing the bug -->
- What was broken:
- Why it was broken:
- When it was introduced (if known):

## Solution
<!-- Describe your fix -->
- How the fix works:
- Why this approach was chosen:
- Alternative approaches considered:

## Testing Performed
<!-- Describe how you tested this fix -->
- [ ] Tested on affected configuration
- [ ] Tested on clean installation
- [ ] Tested with multiple workspace switches
- [ ] Tested with different layer configurations
- [ ] Regression testing on related features

### Test Commands
```bash
# Commands used to test the fix
hyprlax --verbose [options]
```

## System Information
- **Hyprlax Version**: <!-- Branch/commit -->
- **Test Platform**: <!-- Distribution, GPU, etc. -->
- **Hyprland Version**: 

## Changes Made
<!-- List the key changes -->
- 
- 
- 

## Performance Impact
- [ ] No performance impact
- [ ] Minor performance improvement
- [ ] Minor performance regression (justified because: )
- [ ] Benchmarked before/after (results: )

## Checklist

### Code Quality
- [ ] PR focuses ONLY on fixing the bug (no unrelated changes)
- [ ] Code follows existing style and conventions
- [ ] No commented-out code or debug prints left
- [ ] Error handling is appropriate
- [ ] Memory management is correct (no leaks)

### Testing
- [ ] `make test` passes all tests
- [ ] `make memcheck` shows no memory leaks
- [ ] `make lint` passes without errors
- [ ] Added/modified tests for this bug fix (if applicable)
- [ ] Manually tested the specific bug scenario

### Documentation
- [ ] Updated relevant documentation if behavior changed
- [ ] Updated troubleshooting guide if relevant
- [ ] Added code comments for complex fixes
- [ ] Updated CHANGELOG.md with fix description

### Review Ready
- [ ] Self-reviewed the diff
- [ ] Tested on clean clone of repository
- [ ] Commit messages are clear and descriptive
- [ ] PR description is complete
- [ ] Linked to related issue(s)

## Breaking Changes
- [ ] This fix changes existing behavior
- [ ] This fix requires configuration changes
- [ ] This fix affects the public API/CLI

## Additional Notes
<!-- Any other information reviewers should know -->