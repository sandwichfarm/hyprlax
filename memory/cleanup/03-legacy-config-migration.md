Legacy Config Migration → TOML

Scope
- Convert the simple legacy config (space‑separated lines) into a first‑class TOML file, then use TOML only.

Legacy Format Summary
- Global keys (any order):
  - `duration <seconds>`
  - `shift <pixels>`
  - `fps <int>`
  - `vsync <0|1>`
  - `easing <name>`
  - `idle_poll_rate <hz>`
  - `scale <factor>` (legacy global scale)
- Layer lines:
  - `layer <path> [shift_multiplier] [opacity] [blur]`
  - Paths may be relative to the config file directory.

TOML Target Schema
- `[global]` with: `fps`, `duration`, `shift`, `easing`, `debug`, `vsync`, `idle_poll_rate`.
- `[[global.layers]]` entries with: `path`, `shift_multiplier`, `opacity`, `blur`.
- Map global `scale` to per‑layer `scale` uniformly (if present), preserving per‑layer future overrides.

Conversion Algorithm
1. Read legacy file and resolve each layer path relative to the legacy config file directory.
2. Compute destination path:
   - If legacy path is under `~/.config/hyprlax/`, write `~/.config/hyprlax/hyprlax.toml`.
   - Else write `~/.config/hyprlax/hyprlax.toml` creating directories as needed.
   - If destination exists, create a timestamped backup and then overwrite (idempotent behavior).
3. Emit TOML:
   - `[global]` section for global scalars.
   - For each layer: `[[global.layers]]` with resolved `path` (prefer relative to TOML dir when possible).
   - If legacy `scale` was set, add `scale = <factor>` to every layer entry.
4. Print summary with the exact next command: `hyprlax --config <dst.toml>`.

Interactive Detection and UX
- If `--config` is a legacy file or default `~/.config/hyprlax/parallax.conf` exists:
  - TTY: prompt “Convert to TOML at <dst>? [y/N]”. On yes, convert then exit 0, printing the command. With `--continue`, convert and then proceed using the new TOML path.
  - Non‑TTY: print “To convert: hyprlax ctl convert-config <src> <dst> --yes” and exit 3.
- Flags: `--yes`, `--continue`, `--convert-config` also supported from the main binary for convenience.

Edge Cases
- Missing or unreadable legacy file: error and exit non‑zero; do not create TOML.
- No layers specified: emit a valid TOML with only `[global]`.
- Invalid numbers: fall back to defaults and log a warning; mirror existing legacy parser behavior.
- Paths with spaces: preserve quoting via TOML string rules.

Mapping Details
- `duration` → `global.duration`
- `shift` → `global.shift`
- `fps` → `global.fps`
- `vsync` (0/1) → `global.vsync` (false/true)
- `easing` (string) → `global.easing` (same name mapping as existing easing parser)
- `idle_poll_rate` → `global.idle_poll_rate`
- `layer` line → `[[global.layers]]` with `path`, optional `shift_multiplier`, `opacity`, `blur`
- `scale` (legacy global) → copy as `scale` on each layer row

File Locations
- Legacy default: `~/.config/hyprlax/parallax.conf`
- TOML default: `~/.config/hyprlax/hyprlax.toml`

Post‑Migration Actions
- Remove or archive the `.conf` file (backup kept). Update autostart service to pass `--config ~/.config/hyprlax/hyprlax.toml`.
- `hyprlax ctl reload` reloads TOML only.

