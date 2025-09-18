---
name: Compositor Support Request
about: Request support for a new Wayland compositor
title: '[COMPOSITOR] Add support for '
labels: compositor, enhancement
assignees: ''
---

## Compositor Information
- **Compositor Name**: <!-- e.g., Sway, River, dwl -->
- **Version**: <!-- Compositor version -->
- **Website/Repository**: <!-- Link to compositor project -->

## Protocol Support
<!-- Check which protocols the compositor supports -->
- [ ] wlr-layer-shell-unstable-v1
- [ ] xdg-shell
- [ ] zwp-linux-dmabuf-v1
- [ ] wp-viewporter
- [ ] Other relevant protocols: <!-- List any -->

## Motivation
<!-- Why should hyprlax support this compositor? -->
- Number of users/popularity:
- Unique features that would benefit from parallax wallpapers:
- Community interest:

## Technical Requirements
<!-- Any specific technical requirements or differences from Hyprland -->
- Workspace switching mechanism:
- IPC interface available:
- Special considerations:

## Testing Environment
<!-- Can you help test the implementation? -->
- [ ] I have this compositor installed and can test
- [ ] I can provide debug logs and testing
- [ ] I can contribute code for this implementation

## Implementation Priority
<!-- Help us understand the priority -->
- Is this blocking you from using hyprlax? Yes/No
- Are there workarounds available? Yes/No
- Estimated number of users who would benefit:

## Additional Context
<!-- Any other relevant information about the compositor or implementation -->

## Checklist
- [ ] I have verified the compositor uses Wayland (not X11)
- [ ] I have checked that this compositor isn't already supported
- [ ] I have provided all protocol information available
- [ ] I have searched for existing requests for this compositor