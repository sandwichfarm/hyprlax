Title: Per-Layer Tint Implementation Plan

Objective
- Add an optional per-layer tint that multiplies a layer’s RGB by a color with adjustable strength. Default remains visually identical (no tint).

Scope & Defaults
- Scope: multi-layer and single-image render paths (shared shaders). For initial delivery, CLI will support per-layer tint in multi-layer mode. Single-image mode picks up the same shader uniforms but no dedicated CLI flag in this pass.
- Defaults: tint_color = (1,1,1); tint_strength = 0.0 (no-op).

Rendering Design (GLES2)
- Standard fragment shader: add uniforms `u_tint: vec3`, `u_tint_strength: float`.
- Blur fragment shader: add the same uniforms.
- Formula: `effective = mix(vec3(1.0), u_tint, u_tint_strength)` then `rgb *= effective`; keep existing premultiplied alpha handling as-is.
- Overhead: negligible (a couple of ALU ops per fragment).

Data Model
- Add to `struct layer` (src/hyprlax.c): `float tint_color[3]; float tint_strength;`
- Initialize defaults on allocation and in `load_layer()`.

Uniform Plumbing
- Standard program: cache locations `state.u_tint`, `state.u_tint_strength`.
- Blur program: cache `state.blur_u_tint`, `state.blur_u_tint_strength`.
- In render_frame: set these per layer before draw call.

CLI
- Add `--tint <color>[:strength]` option that applies to the last declared layer (order-sensitive, minimal change). Supported color: `#RRGGBB` (strength default 1.0 if provided without value; overall default remains 0.0 unless explicitly set). If no prior layer exists, print an error.
- Help: document `--tint` with examples.

Parsing
- Add a small color parser that accepts hex `#RRGGBB`. Reject invalid tokens with a clear error; clamp strength to [0,1].

IPC (deferred follow-up)
- Extend `src/ipc.h: layer_t` with tint color + strength.
- Allow `modify <id> tint <#hex|none> [strength]` in `ipc.c` and sync into GL layers in `hyprlax.c::sync_ipc_layers()`.
- Update `hyprlax_ctl` to expose the command. (Backlog)

Config (deferred follow-up)
- Optional keys per-layer: `tint_color`, `tint_strength` in config loader if present. (Backlog)

Documentation & Tests
- Docs: update CLI usage and examples; note color format and strength.
- Tests: unit tests for parser, CLI parse happy/error paths, and IPC (once added). Integration can assert shader uniform presence via logs and basic render invocation in headless test harness.

Risks & Compatibility
- No breaking behavior. If unused, shader path stays no-op due to defaults.
- IPC/Config tint support is staged to avoid touching many modules at once; core rendering + CLI provide immediate utility.

Estimate
- Core rendering + CLI + help: ~0.5–1 day including build and quick validation.
- IPC/Config + docs/tests: ~0.5–1 day follow-up.

