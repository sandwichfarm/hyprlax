---
name: Platform Support Request
about: Request support for a new operating system or distribution
title: '[PLATFORM] Add support for '
labels: platform, enhancement
assignees: ''
---

## Platform Information
- **Operating System**: <!-- e.g., FreeBSD, OpenBSD, NixOS -->
- **Version**: <!-- OS version -->
- **Architecture**: <!-- e.g., x86_64, aarch64, armv7 -->

## Dependencies Availability
<!-- Check if required dependencies are available on your platform -->
- [ ] Wayland libraries (libwayland-client, libwayland-egl)
- [ ] wayland-protocols
- [ ] Mesa with EGL/GLES support
- [ ] pkg-config
- [ ] C compiler (gcc/clang)
- [ ] Make or alternative build system

## Package Management
- **Package Manager**: <!-- e.g., pkg, apt, dnf, nix -->
- **Dependency Installation Command**: 
```bash
# Command to install dependencies on this platform
```

## Build Environment
<!-- Describe any platform-specific build requirements -->
- Compiler version:
- Special compiler flags needed:
- Library paths:
- Include paths:

## Testing Results
<!-- If you've attempted to build hyprlax -->
```bash
# Paste build output or errors here
```

## Platform-Specific Considerations
<!-- Any special considerations for this platform -->
- Graphics driver situation:
- Wayland compositor availability:
- Known limitations:

## Contribution
- [ ] I can help test builds on this platform
- [ ] I can contribute platform-specific code/patches
- [ ] I can maintain platform-specific packaging

## Distribution Package
<!-- If requesting distribution-specific support -->
- Would you like official package in distribution repos? Yes/No
- Can you help maintain the package? Yes/No
- Package format: <!-- e.g., .deb, .rpm, PKGBUILD, .nix -->

## Additional Context
<!-- Any other platform-specific information -->

## Checklist
- [ ] I have verified Wayland is available on this platform
- [ ] I have checked dependency availability
- [ ] I have attempted to build from source
- [ ] I have searched for existing platform support requests