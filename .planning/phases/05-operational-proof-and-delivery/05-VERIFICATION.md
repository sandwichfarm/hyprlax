---
phase: 05-operational-proof-and-delivery
status: passed
score: 23/23
verified: 2026-07-12
requirements: [DOC-01, TEST-01, DELIV-01]
pr: https://github.com/sandwichfarm/hyprlax/pull/94
---

# Phase 5 Verification: Operational Proof and Delivery

## Verdict

**PASSED — all 23 milestone requirements and all three Phase 5 requirements have direct proof.**

| Phase 5 requirement | Status | Direct evidence |
|---------------------|--------|-----------------|
| DOC-01 | PASS | Complete copied-directory README, paired user services, provider/privacy/request/fallback contract, visible attribution, troubleshooting, two indexes, and four documentation regression tests. |
| TEST-01 | PASS | 48 deterministic tests cover provider ceilings and schemas, concurrency, fallback, every named state, lunar geometry/fill, generated PNG/shadows, IPC ownership/deltas/failures, dry-run side effects, and documentation. |
| DELIV-01 | PASS | Clean build, 27 C suites, all script suites, lint/static checks, 27 Valgrind targets, deterministic preview, isolated real IPC readback, clean diff/history, pushed branch, and open mergeable PR 94. |

## Repository Evidence

- `make clean && make -j2`: exit 0.
- `make test`: 27/27 test binaries pass.
- `make test-scripts`: all five shell scripts pass; Dynamic Pixel City reports 48/48.
- `make lint`: exit 0, including compile/security checks. Reported warnings predate and lie outside
  this feature's source.
- `make memcheck`: 27/27 memory-clean.
- Python production/test files compile and contain no lines over 100 columns.
- Copied TOML preserves six base paths, fixes only the copy's malformed final blur token, and adds
  exactly sun/moon/shadow at the asserted order; original `examples/pixel-city` has no diff.
- Dry-run outputs 27 allowlisted commands and tests prove no network, cache, file, or IPC effect.
- Whole branch `git diff --check origin/master`: exit 0; secret/shell-eval scan: no findings.

## Live IPC Evidence

- Fresh local binary started a temporary copied scene on a short isolated suffix socket.
- Initial real `ctl list --json`: exactly nine canonical copied/generated paths.
- Controller `--once` at an aware time with manual Budapest location and a validated cached daily
  fixture: 27 commands applied, no errors.
- Final real list: sun opacity/UV changed, base tint strengths changed, moon path was `moon-b.png`,
  shadow path was `shadow-b.png`, and shadow opacity was visible.
- Temporary process and socket were removed.
- Existing default daemon before/after: PID 491045, exact default socket, and exact six-layer JSON
  unchanged.

## Remote Evidence

- GitHub PR 94: open, non-draft, base `master`, head `feature/dynamic-pixel-city`, mergeable.
- Remote branch is zero commits behind current `origin/master` at delivery audit.
- GitHub Feature Branch Build and Docs Link Check are the authoritative remote checks.

## Known Environment Boundary

Local `make docs-linkcheck` could not start because `mkdocs` and `mkdocs-material` are not
installed. No project or machine dependency was added for this task. Four documentation contract
tests and direct local Markdown target checks pass; GitHub runs the repository's actual link check.

## Corrective visual verification

The first user-visible local screenshot invalidated the earlier inference that nonzero UV/opacity
readback proved visible overlays. It showed the lighting tint but no sun, moon, or shadow.

- Renderer evidence: `overflow=none` derives automatic safe UV margins from
  `parallax.max_offset_px`; its 100000-pixel default collapsed the dynamic layers' sample range.
- Renderer evidence: `base_uv_x/base_uv_y` translate sampling coordinates, so requested screen
  displacement requires the opposite UV sign.
- Red regression evidence: the copied TOML lacked zero max offsets and command X/Y equaled rather
  than negated SceneState values.
- Green evidence: both focused tests and the full 48-case wrapper pass after correction.
- Restart evidence: live config reports max offsets `0.0/0.0`; controller reports cached fresh-day
  facts, 27 applied commands, and no errors; sun/moon UV signs are corrected.
- Visual evidence: post-restart wallpaper-only workspace thumbnails show a clear high-noon sun.
  Structured visual verdict improved from 35 to 82 to 93 (pass).

## VERIFICATION PASSED
