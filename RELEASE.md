# Release Process

This document describes the release process for hyprlax.

## Versioning

We follow [Semantic Versioning](https://semver.org/):
- **MAJOR** version for incompatible API changes
- **MINOR** version for backwards-compatible functionality additions  
- **PATCH** version for backwards-compatible bug fixes

Format: `v{MAJOR}.{MINOR}.{PATCH}` (e.g., `v1.0.0`, `v1.2.3`)

### Version Management

The version is **never hardcoded in source code**. Instead:
- **Local builds**: Version is automatically generated from git commit hash
- **CI/CD builds**: Version is extracted from the git tag

The build system handles this automatically:
1. If no `VERSION` file exists, the Makefile creates one with the current git commit hash
2. CI/CD workflows write the tag version to `VERSION` file before building
3. The version is passed to the compiler via `-DHYPRLAX_VERSION` flag
4. The `VERSION` file is in `.gitignore` and never committed

## Release Workflow

### 1. Prepare the Release

1. Ensure all changes are committed and pushed to `main` (or `master`)
2. Ensure CI passes on the main branch
3. No manual version updates needed - version is derived from git tag

### 2. Create and Push Tag

```bash
# Create annotated tag
git tag -a v1.0.0 -m "Release v1.0.0: Initial release"

# Push tag to trigger release workflow
git push origin v1.0.0
```

### 3. Automated Release Process

When a tag is pushed, GitHub Actions will automatically:
1. Extract version from tag and write to VERSION file
2. Build the binary with the correct version embedded
3. Run tests
4. Create a GitHub release
5. Upload built binaries as release assets
6. Generate release notes from commits

### 4. Manual Steps (if needed)

After the automated release:
1. Edit the release notes on GitHub if needed
2. Announce the release (Discord, Reddit, etc.)

## Release Checklist

- [ ] Code is tested locally
- [ ] README is up to date
- [ ] No debug code left in
- [ ] Version tag follows semantic versioning
- [ ] Tag message describes key changes

## Tag Message Format

```
Release v{VERSION}: {Brief description}

Key changes:
- Feature: {description}
- Fix: {description}
- Enhancement: {description}
```

Example:
```
Release v1.1.0: Enhanced animation system

Key changes:
- Feature: Added elastic and bounce easing functions
- Fix: Resolved edge stretching on large workspace numbers
- Enhancement: Improved animation performance by 30%
```

## Rollback Process

If a release has critical issues:

1. Delete the release on GitHub (keep the tag for history)
2. Fix the issue
3. Create a new patch version tag
4. Push the new tag

```bash
# Example: Rolling back v1.0.0 and releasing fix as v1.0.1
git tag -a v1.0.1 -m "Release v1.0.1: Critical fix for workspace detection"
git push origin v1.0.1
```

## Development Releases

For testing releases, use pre-release tags:
- `v1.0.0-alpha.1`
- `v1.0.0-beta.1`
- `v1.0.0-rc.1`

These won't be marked as "latest" on GitHub.

## Pre-Release Process (Feature Branches)

### IMPORTANT: Pre-Release Branch Strategy

⚠️ **Pre-releases are NEVER tagged from master branch**
- Pre-releases are created from feature/development branches
- Only stable releases are tagged from master
- This prevents unstable code from being associated with the main branch

### Pre-Release Workflow

#### 1. Prepare Pre-Release on Feature Branch

```bash
# Ensure you're on feature branch, NOT master
git checkout feature/refactor  # or develop, beta, etc.
git branch --show-current      # Verify: Should NOT show 'master'

# No manual version update needed - version comes from tag
# Update CHANGELOG with pre-release section if desired
```

#### 2. Create Pre-Release Tag

```bash
# CRITICAL: Verify you're NOT on master
if [[ $(git branch --show-current) == "master" ]]; then
    echo "ERROR: Cannot create pre-release from master!"
    exit 1
fi

# Create annotated pre-release tag
git tag -a v1.1.0-beta.1 -m "Pre-release v1.1.0-beta.1

⚠️ PRE-RELEASE from feature/refactor branch
Not recommended for production use

Changes:
- Experimental feature X
- Refactored Y system
- Testing Z functionality

Known issues:
- Issue #123: Description"

# Push feature branch and tag
git push origin feature/refactor
git push origin v1.1.0-beta.1
```

#### 3. GitHub Pre-Release Creation

The workflow will automatically:
1. Detect pre-release tag pattern (v*-*)
2. Extract version from tag and write to VERSION file
3. Build from the feature branch with correct version
4. Create GitHub release marked as pre-release
5. Add warning labels to artifacts

Manual steps:
1. Edit release notes to emphasize pre-release status
2. Add testing focus areas
3. Include branch information

### Pre-Release Versioning Scheme

```
v<MAJOR>.<MINOR>.<PATCH>-<stage>.<number>

Stages (in order):
- alpha: Early testing, very unstable
- beta: Feature complete, finding bugs  
- rc: Release candidate, final testing

Examples:
- v1.1.0-alpha.1
- v1.1.0-alpha.2
- v1.1.0-beta.1
- v1.1.0-rc.1
- v1.1.0 (stable)
```

### Pre-Release to Stable Promotion

After successful pre-release testing:

```bash
# 1. Merge feature branch to master
git checkout master
git merge --no-ff feature/refactor -m "Merge feature/refactor for v1.1.0 release"

# 2. Create stable release tag from master
# No manual VERSION changes needed - tag drives version
git tag -a v1.1.0 -m "Release v1.1.0: [Description]"
git push origin master --tags
```

### Branch Protection Rules

Recommended GitHub settings:
- **master branch**: Require PR reviews, no direct pushes
- **Pre-release tags**: Only from non-master branches
- **Stable tags**: Only from master branch

## Notes

- **Stable releases**: Always create tags from `master` branch
- **Pre-releases**: Always create tags from feature/development branches
- Never force-push to existing tags
- Keep release binaries under 50MB for easy distribution
- Test the release binary on a clean system before announcing
- Pre-releases should include clear warnings about stability