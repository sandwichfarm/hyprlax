# Phase 4: Safe IPC Controller — Context

**Status:** Ready for planning

## Locked Runtime Contract

- `dynamic_scene.py` becomes the executable sidecar; still importable with no side effects.
- Live ownership comes only from `hyprlax ctl list --json` canonical paths under this exact example
  directory. Require all six base paths and exactly one active sun/moon/shadow A/B path; ignore all
  unrelated layers and reject missing/duplicate managed paths. Never use `ctl clear`.
- Paths containing whitespace are rejected because the current IPC daemon tokenizes path values.
- Apply one supported property per `hyprlax ctl modify`: base tint/opacity/blur; sun/moon x/y/opacity;
  moon/shadow inactive-buffer path; shadow opacity. No saturation property and no daemon restart.
- Hold an in-process last-applied signature map so unchanged properties produce no subprocess.
- Each loop tick re-lists managed paths; successful A/B switches therefore define the next inactive side.
- Default update interval is 60 seconds, accepted range 15..3600. Provider work remains in the
  daily cache path and never enters Hyprlax's render loop.
- Modes: `--once` (default), `--loop`, `--status`, `--dry-run`, aware `--at`, manual
  `--latitude/--longitude/--timezone`, `--interval`, `--hyprlax-bin`, `--cache`.
- `--dry-run --at ... [manual location]` uses neutral astronomy and assumed standalone IDs, writes
  no assets, contacts no provider/socket, and prints JSON commands plus state/source disclosure.
- Live mode writes inactive moon/shadow assets atomically before sending path changes. Every IPC
  failure is actionable and nonzero; loop logs it and retries only on the next bounded tick.
