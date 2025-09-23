
  Naming Rules

  - Use dotted, snake_case keys for CLI/IPC/Config.
  - ENV uses the same keys uppercased with dots replaced by underscores and prefixed with HYPRLAX_ (e.g., render.fps → HYPRLAX_RENDER_FPS).
  - Per‑layer keys live under layer.<id>.* in IPC/GET; in config they live per layer table.

  Global Keys

  - render.fps
  - animation.duration
  - animation.easing
  - parallax.mode
  - parallax.shift_pixels
  - parallax.sources.cursor.weight
  - parallax.sources.workspace.weight
  - parallax.invert.cursor.x
  - parallax.invert.cursor.y
  - parallax.invert.workspace.x
  - parallax.invert.workspace.y
  - render.overflow (repeat_edge|repeat|repeat_x|repeat_y|none)
  - render.tile.x
  - render.tile.y
  - render.margin_px.x
  - render.margin_px.y
  - input.cursor.sensitivity_x
  - input.cursor.sensitivity_y
  - input.cursor.ema_alpha
  - input.cursor.deadzone_px
  - debug.enabled

  ENV equivalents (examples):

  - HYPRLAX_RENDER_FPS, HYPRLAX_ANIMATION_DURATION, HYPRLAX_PARALLAX_SHIFT_PIXELS, HYPRLAX_RENDER_OVERFLOW, etc.

  Config (TOML) placement (examples):

  - [global] (basic daemon opts if needed)
  - [animation] duration, easing
  - [parallax] mode, shift_pixels; nested tables for sources., invert.
  - [render] overflow, tile.x/y, margin_px.x/y
  - [input.cursor] sensitivity_x/y, ema_alpha, deadzone_px

  Per‑Layer Keys

  - layer.<id>.path
  - layer.<id>.shift_multiplier
  - layer.<id>.opacity
  - layer.<id>.uv_offset.x
  - layer.<id>.uv_offset.y
  - layer.<id>.z
  - layer.<id>.fit (stretch|cover|contain|fit_width|fit_height)
  - layer.<id>.content_scale
  - layer.<id>.align.x
  - layer.<id>.align.y
  - layer.<id>.overflow
  - layer.<id>.tile.x
  - layer.<id>.tile.y
  - layer.<id>.margin_px.x
  - layer.<id>.margin_px.y
  - layer.<id>.blur
  - layer.<id>.visible

  Config (TOML) layer block:

  - [[global.layers]]
      - path = "..."
      - shift_multiplier = 1.0
      - opacity = 1.0
      - uv_offset = { x = 0.0, y = 0.0 }
      - z = 0
      - fit = "cover"
      - content_scale = 1.0
      - align = { x = 0.5, y = 0.5 }
      - overflow = "repeat_edge"
      - tile = { x = false, y = false }
      - margin_px = { x = 0.0, y = 0.0 }
      - blur = 0.0
      - visible = true

  CLI Alignment

  - hyprlax ctl set/get use the same global keys (e.g., ctl set render.fps 144, ctl get parallax.shift_pixels).
  - hyprlax ctl modify/add use the same per‑layer keys (e.g., modify 3 shift_multiplier 0.8, add img.png uv_offset.x 0.02 uv_offset.y -0.01).
  - Keep both key=value and key value forms for add/modify.

  IPC Alignment

  - hyprlax_runtime_set/get_property and ctl set/get should use exactly these keys.
  - layer.* tree matches the list above.

  Output Alignment

  - list --long fields use names matching keys:
      - “Shift Multiplier” instead of “Shift” for layer.move multiplier
      - “Content Scale”, “UV Offset”, “Align”, “Overflow”, “Tile”, “Margin Px”
  - list --json fields:
      - Use shift_multiplier, content_scale, uv_offset as [x,y], align as [x,y], margin_px as [x,y], tile as [x,y].
  - status --json: include parallax.shift_pixels and render.fps.

  Exact Changes Required

  1. CLI/IPC property names
      - Replace scale (parallax multiplier) with shift_multiplier in:
          - ctl add/modify parser and help
          - IPC property handler (apply_layer_property and parsing)
          - list outputs (long, compact, JSON): rename “Shift” label/field to shift_multiplier
      - Replace x/y (UV pan) with uv_offset.x / uv_offset.y in:
          - ctl add/modify parser and help
          - apply_layer_property handler
      - Replace align_x/align_y with align.x / align.y in:
          - ctl add/modify parser and help
          - apply_layer_property handler
      - Replace margin.x/margin.y with margin_px.x / margin_px.y in:
          - ctl add/modify parser and help
          - apply_layer_property handler
      - Prefer layer.<id>.visible (single source of truth). Keep hidden internal, but canonical key is visible.
  2. Global set/get keys
      - Replace set/get fps → render.fps; duration → animation.duration; easing → animation.easing; shift → parallax.shift_pixels.
      - Update IPC_CMD_SET/GET handlers to recognize only canonical keys; update runtime bridge if necessary.
  3. Config (TOML) keys
      - Global:
          - shift → parallax.shift_pixels
          - add [animation], [parallax], [render], [input.cursor] sections with canonical names above
      - Per-layer:
          - shift_multiplier (existing OK)
          - uv_offset table (already present)
          - content_scale (rename from scale)
          - align table keys x/y (already present)
          - margin_px table (already present)
          - visible (new boolean)
      - Update config_toml.c mapping accordingly.
  4. list/status output
      - list --long labels: use canonical names (Shift Multiplier, Content Scale, UV Offset, Margin Px).
      - list --json fields: shift_multiplier, uv_offset, align, content_scale, margin_px, tile.
      - status --json: include render.fps, parallax.shift_pixels.
  5. Help/Docs
      - hyprlax ctl --help, per‑command help: update examples to use canonical names.
      - README/docs/IPC: document the key map; clarify global vs per‑layer; define ENV mapping rule (dots → underscores).
      - Note that Content Scale sizes the image; Shift Multiplier controls motion; Shift Pixels is global motion magnitude.
  6. Tests
      - Update tests/test_ctl.c and any docs-driven tests to match new keys and list labels/JSON fields.
      - Add tests for uv_offset.x/y, shift_multiplier on add/modify, and get layer.<id>.visible.
  7. JSON compatibility (optional migration easing)
      - In list/status JSON, add the canonical keys; you may retain legacy fields internally during transition, but canonical fields must be present and
  documented.

  Rationale for key choices

  - shift_multiplier is explicit and already used in config structures; avoids collision with content_scale.
  - content_scale is unambiguous for sizing.
  - uv_offset.x/y matches existing config and renderer semantics.
  - render., animation., parallax., input.cursor. group settings logically and match current internal structure.
  - Using the same keys everywhere makes docs, tests, and IPC round‑trips predictable.

  If you want, I can implement these changes next, starting with CLI/IPC property names and outputs, then config parser, then help/docs/tests.