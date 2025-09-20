# Hyprland exec-once Startup Analysis for hyprlax

This note captures root-cause hypotheses and code references for failures when `hyprlax` is launched via Hyprland's `exec-once` but works when run directly or via a wrapper script.

## Summary

- Symptom: `hyprlax` runs reliably when launched manually or from a wrapper script, but “crashes out” when started by Hyprland `exec-once`.
- Likely cause: early startup race and environment/FD quirks under `exec-once` combined with strict initialization behavior in `hyprlax` — particularly Hyprland IPC connection timing and env var availability.

## Exec-Once Constraints (why environment differs)

- Early lifecycle: `exec-once` runs during compositor startup; IPC sockets and env vars may not be ready immediately.
- Environment: Non-interactive environment; PATH and `XDG_*` may differ from a login shell.
- FDs: stdout/stderr may be redirected (pipes or `/dev/null`). Writing to closed pipes can raise `SIGPIPE` if not ignored; `/dev/null` hides output.
- CWD and shell: The command string is executed by a shell (supports `;`, `~`), but quoting/expansion pitfalls still apply.

## Code Paths Sensitive To Exec-Once

1) Hyprland IPC initialization is strict and short-lived

- File: `src/compositor/hyprland.c`
  - `get_hyprland_socket_paths()` requires `XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE`. Missing either → failure.
  - `hyprland_connect_ipc()` uses `compositor_connect_socket_with_retry()` with only 30 × 100 ms = ~3 s total for the event socket (`.socket2.sock`). If the socket isn’t up yet, connect fails.
- File: `src/compositor/compositor.c`
  - `compositor_connect_socket_with_retry()` implements the retry loop.
- File: `src/hyprlax_main.c`
  - In `hyprlax_init()`, failure of compositor initialization is treated as fatal and the program exits.

2) Wayland platform init is tolerant (asymmetry vs IPC)

- File: `src/platform/wayland.c`
  - `wayland_connect()` retries `wl_display_connect()` up to ~15 s (30 × 500 ms) and logs “Waiting for Wayland display to be ready...”. This is significantly longer than the IPC retry above and more likely to survive early boot races.

3) Logging and FD handling

- File: `src/main.c`
  - Ignores `SIGPIPE` and redirects `stderr` to `/tmp/hyprlax-stderr.log` if it points to `/dev/null`, and logs startup info to `/tmp/hyprlax-exec.log`. This mitigates FD pitfalls under `exec-once`.
- File: `src/core/log.c`
  - Unified logger writes to file when `--debug-log` is set, and conditionally to `stderr`.

4) Env-dependent paths / argument parsing

- File: `src/hyprlax_main.c`
  - `parse_arguments()` checks `access(argv[i], F_OK)` for bare image args. If `~` or env vars are not expanded (due to quoting or different shell handling), the image path can fail to resolve under `exec-once` while succeeding in a wrapper.

## Most Likely Root Causes

- Hyprland IPC readiness race is fatal:
  - IPC retry window (~3 s) is shorter than Wayland’s (~15 s).
  - If `HYPRLAND_INSTANCE_SIGNATURE` and/or IPC sockets are not ready at the moment `exec-once` fires, compositor init fails and `hyprlax` exits.
  - Wrapper scripts implicitly delay or re-exec later, making IPC ready by the time `hyprlax` starts.

- Environment not fully populated at `exec-once` time:
  - Missing `XDG_RUNTIME_DIR` or `HYPRLAND_INSTANCE_SIGNATURE` causes `get_hyprland_socket_paths()` to fail early.

## Secondary/Contextual Factors

- PATH differences: Historically relevant when forking `hyprctl`; current modular path doesn’t rely on `hyprctl` for core flow, but user-side scripts might.
- FD behavior: With `SIGPIPE` ignored and stderr redirection in `main`, FD issues are less likely to crash now, but unguarded `fprintf(stderr, ...)` remains noisy without `--debug-log`.
- Relative/unexpanded paths: Misquoted `~` or reliance on CWD can fail under `exec-once`.

## Quick Validation

- Inspect `/tmp/hyprlax-exec.log` for failing runs:
  - Confirms `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `HYPRLAND_INSTANCE_SIGNATURE` values and whether `stderr` was `/dev/null`.
- Add a small delay in Hyprland config to test race:
  - `exec-once = sleep 1; hyprlax ...`
- Use a debug log file explicitly:
  - `hyprlax --debug-log=/tmp/hyprlax.log ...` to avoid reliance on stderr.
- Ensure absolute/expanded image paths (avoid CWD dependence and unexpanded `~`).

## Mitigation Options (if we choose to change code)

- Increase Hyprland IPC retry window to match Wayland (e.g., 10–15 s) in `hyprland_connect_ipc()`.
- Treat initial IPC connection failure as non-fatal:
  - Start renderer and attempt IPC connection later (periodic retry in main loop) so workspace events arrive when IPC becomes available.
- Improve diagnostics when env vars are missing:
  - Explicit logs when `XDG_RUNTIME_DIR`/`HYPRLAND_INSTANCE_SIGNATURE` are unset at startup; keep trying instead of aborting.
- Encourage users to pass `--debug-log` in `exec-once` lines to collect logs independent of stderr.

## Bottom Line

The strongest explanation in current sources is a mismatch in readiness handling: Wayland display connect waits up to ~15 s, but Hyprland IPC connect waits ~3 s and failing IPC is treated as fatal during init. Under `exec-once`, IPC env/sockets may not be ready yet, causing early exit; wrapper scripts change timing/environment enough to hide the race.

