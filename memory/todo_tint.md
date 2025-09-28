Per-Layer Tint TODOs

Core (this pass)
- [x] Extend `struct layer` with tint fields.
- [x] Initialize defaults in allocation + `load_layer()`.
- [x] Update fragment shader (standard) to support tint uniforms (modular basic shader re‑enabled).
- [x] Keep blur shaders tint‑free for stability (toggleable later).
- [x] Cache/set uniforms where used.
- [x] Add `--layer ...:#RRGGBB[:strength]` CLI; shown in `--help`.
- [x] Color parsing helper (hex; clamped).

IPC & Config (follow-up)
- [x] Extend `src/ipc.h: layer_t` with tint fields (fallback mode).
- [x] Support `modify <id> tint <#hex|none>[:strength]` in `src/ipc.c` (bridged mode updates GL layers).
- [x] Expose in ctl help.
- [x] TOML: `tint_color` and `tint_strength` per-layer.
- [ ] Legacy sync path (hyprlax.c) – only if needed.

Tests (follow-up)
- [ ] Unit: color parser valid/invalid, strength clamping.
- [ ] CLI: tint parse order, error on no prior layer.
- [ ] IPC: modify tint; list returns tint in JSON/text.
- [ ] Integration: headless render sanity; workspace/IPC stability.

Docs (follow-up)
- [ ] Update CLI usage and examples with tint.
- [ ] Add a short note in `docs/animation.md` describing tint behavior.
- [ ] Add example commands/configs showing warm/cool tint.

Performance
- [x] No allocations in render loop; minimal uniform updates.
- [ ] Validate FPS unchanged at typical resolutions with 3–5 layers.
