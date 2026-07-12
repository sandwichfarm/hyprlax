---
phase: 05-operational-proof-and-delivery
status: planned
date: 2026-07-12
---

# Phase 5 Context: Operational Proof and Delivery

## Locked Decisions

- Document an exact copy-to-`~/.config/hyprlax/pixel-city-dynamic` workflow and user-systemd
  services based on `make install-user` (`%h/.local/bin/hyprlax`).
- Import the active Wayland/Hyprland environment into the user manager before starting services.
- Attribute CraftPix and Sunrise-Sunset.org visibly; identify ip-api's HTTP/non-commercial free
  boundary and explain that manual coordinates avoid geolocation.
- State the absolute provider/date attempt ceiling, including failures and concurrent starts.
- Verify the real socket through a temporary `HYPRLAX_SOCKET_SUFFIX` instance so the existing
  six-layer Pixel City daemon and its socket remain untouched.
- Push only after the full matrix passes; open a new PR against `origin/master` and verify its
  title, head/base, state, URL, and mergeability by GitHub readback.

## Existing Environment

- A live Hyprland session and an unrelated six-layer daemon are available.
- The repository explicitly documents `HYPRLAX_SOCKET_SUFFIX` for isolated test sockets.
- The installed daemon is older than this worktree, so final smoke uses the freshly built local
  binary for both daemon and controller subprocesses.

## Stop Condition

Phase 5 is complete only after documentation/static/build/test proof, isolated real IPC evidence,
Lore-compliant clean history, pushed branch, and verified open PR are all present. Any unavailable
environment proof is reported honestly rather than substituted with a mock claim.
