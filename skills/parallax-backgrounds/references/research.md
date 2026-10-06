# Research and provenance

Reviewed 2026-10-06. Primary sources inform the workflow alongside the user's requirement that alpha and hidden content depend on position in the stack.

| Source | Finding used |
|---|---|
| [Shih et al., 3D Photography using Context-aware Layered Depth Inpainting, CVPR 2020](https://shihmengli.github.io/3D-Photo-Inpainting/) | Motion reveals occluded regions; the authors reconstruct hidden color/depth in a layered representation. Supports hidden-content reconstruction, not just segmentation. Their research pipeline is not required for flat PNG planes. |
| [Wallpaper Engine: Parallax Effect](https://docs.wallpaperengine.io/en/scene/parallax/introduction.html) | Independent layer strengths create motion depth. Larger background artwork or displayed scale prevents exposed borders. |
| [Godot: 2D Parallax](https://docs.godotengine.org/en/stable/tutorials/2d/2d_parallax.html) | Repetition requires suitable image sizing/position and seamless content. Scaling and repeat dimensions must agree. Godot property names are not hyprlax settings. |
| [PNG specification: Alpha representation](https://www.w3.org/TR/png/#6AlphaRepresentation) | PNG stores unassociated alpha. Export straight alpha and inspect the channel rather than trusting a checkerboard-looking image. |
| [Khronos OpenGL ES 2.0 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf) | Section 3.8 describes NPOT filtering/wrapping restrictions; repeating textures have stricter requirements than static clamped ones. |
| [Wallpaper Engine: Depth Parallax](https://docs.wallpaperengine.io/en/scene/effects/effect/depthparallax.html) | Depth-map parallax is a distinct rendering technique. A depth texture is not a replacement deliverable for separately movable alpha planes. |

## Deductions and local evidence

Complete-object alpha, recursive reconstruction behind every occluder, shared-canvas registration, reference-first generation, and pairwise reveal checks are workflow deductions. No cited source guarantees exact registration or correct alpha from generative prompts alone.

Reserve equations follow from swept rectangles and relative translations under their stated assumptions; they do not generalize automatically to arbitrary shaders or 3D transforms.

hyprlax guidance comes from direct local inspection of Makefile (active modular sources), `src/core/render_core.c`, `src/renderer/gles2.c`, `src/renderer/shader.c`, `src/core/config_toml.c`, and `docs/configuration/toml-reference.md`, rechecked against `6aaf881` in [sandwichfarm/hyprlax](https://github.com/sandwichfarm/hyprlax).
