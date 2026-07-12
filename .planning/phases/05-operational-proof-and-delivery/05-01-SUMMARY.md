---
phase: 05-operational-proof-and-delivery
plan: "01"
subsystem: operator-documentation
tags: [readme, systemd, privacy, attribution]
requires: [04-safe-ipc-controller]
provides:
  - literal repository and personal-copy operations guide
  - paired daemon/controller systemd user service examples
  - provider request/privacy/fallback contract and attribution
  - automated documentation artifact coverage
affects: [05-02-final-proof]
tech-stack:
  added: []
  patterns: [copy-local services, explicit external-boundary documentation]
key-files:
  created:
    - examples/pixel-city-dynamic/README.md
    - examples/pixel-city-dynamic/hyprlax-pixel-city-dynamic.service
    - examples/pixel-city-dynamic/hyprlax-pixel-city-dynamic-controller.service
  modified: [examples/README.md, docs/guides/examples.md, tests/test_pixel_city_dynamic.py]
key-decisions:
  - Services require the explicit personal-copy layout and user-installed binary.
  - Status is documented as provider/cache capable; only dry-run claims zero external effects.
requirements-completed: [DOC-01, TEST-01]
completed: 2026-07-12
---

# Phase 5 Plan 1: Operator Documentation Summary

A fresh reader can now preview, launch, control, copy, service-manage, inspect, and stop Dynamic
Pixel City using literal commands, with the network and fallback behavior visible before opt-in.

## Delivered

- Repository and `~/.config/hyprlax/pixel-city-dynamic` workflows with whitespace/default-socket
  safety notes.
- Once, loop, status, deterministic fixed-time preview, manual location, and live fixed-time forms.
- Exact one-attempt-per-category/date policy, concurrency/failure behavior, cache path and safety,
  provider privacy/commercial/transport limits, offline fallback, and troubleshooting.
- Visible CraftPix, Sunrise-Sunset.org, and ip-api attribution.
- Separate user units for daemon and controller with graphical-session ordering and recovery.
- Gallery/index registration plus four documentation contract tests.

## Evidence

- Documentation tests: 4/4 OK.
- Full dynamic suite and wrapper: 48/48 OK plus valid TOML check.
- Direct local Markdown target validation: all links in the three modified docs exist.
- Python compilation and `git diff --check`: exit 0.

## Environment Gap

`make docs-linkcheck` cannot run because this environment lacks the repository's optional
`mkdocs`/`mkdocs-material` tooling. No dependency was added. Direct Markdown targets pass and the
final verifier will report this exact gap alongside the full build/runtime evidence.

## Next

Plan 05-02 runs the complete repository matrix and an isolated real IPC controller application,
then audits, pushes, and verifies the new GitHub PR.
