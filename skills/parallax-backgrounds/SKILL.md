---
name: parallax-backgrounds
description: Generate coherent layered parallax backgrounds or separate an existing scene into motion-ready image layers, with stack-aware transparency, reconstructed hidden content, and overscan. Use for hyprlax wallpapers and layered 2.5D scenes; a flattened wallpaper or depth map alone is not the deliverable.
---

# Parallax backgrounds

Produce a stack that remains convincing while layers move independently. Correctness belongs to the moving composite, not just the resting image.

## Hidden is not transparent

Each layer needs **its own complete shape**, not merely the pixels visible through the layers in front of it.

- The farthest plate is ordinarily opaque throughout the sampled region. Remove nearer objects and reconstruct the scenery they covered.
- A middle layer contains its complete objects, including portions hidden by foreground objects. Alpha reveals farther scenery only where this middle plane has no material. Do not punch foreground-shaped holes into it.
- A foreground layer contains its objects and genuine openings, with transparency elsewhere. Continue cropped branches, trunks, terrain, and other forms beyond the initial viewport wherever movement can expose them.
- Nearer objects belong only on their assigned plane. Do not leave duplicate silhouettes, recognizable outlines, or inappropriate shadow residue baked into rear plates.
- Use fractional alpha for genuinely translucent material and antialiased edges. Keep opaque objects opaque; lowering a whole layer's opacity does not replace a correct cutout mask.

Example: sky → mountains → trees. Sky continues behind both mountains and trees. Mountains continue behind trees, with transparency above their own skyline. Trees reveal mountains/sky through branch gaps. Tree silhouettes must not be cut out of the mountain plane.

## Establish the image and motion contract

Infer from the request/project and state assumptions; ask only for consequential missing choices:

- Scene/style or supplied reference, viewport dimensions/aspect, target renderer, output directory.
- Finite wallpaper motion versus seamless scrolling; horizontal only versus two axes.
- Back-to-front plane roles, shared canvas dimensions, neutral viewport crop, maximum motion per plane/axis, combined inputs, and animation overshoot.
- Shared camera, horizon, perspective, lighting direction, palette, and registration anchors.

Usually 3–5 meaningful depth planes suffice; use more when the scene warrants it. Group connected structures and attached details that must move together. Long roads and continuous ground may need restrained motion or geometry; arbitrary horizontal strips rarely produce convincing depth. A depth map does not itself supply hidden scenery or alpha planes.

Read [geometry-and-prompts.md](references/geometry-and-prompts.md) before sizing the canvas or writing prompts. For hyprlax, read [hyprlax.md](references/hyprlax.md) before configuring the stack. [research.md](references/research.md) records primary sources and distinguishes their findings from workflow deductions.

## Generate the stack

Use the host's image-generation/editing capability for raster generation, extraction, inpainting, outpainting, and repairs. In Codex, use the built-in image tool and the imagegen skill when available; request real transparency with `transparent_background: true` for overlays. Follow the current tool schema rather than inventing mask/size/path parameters. Inspect local input images before editing; label reference images and edit targets explicitly. On other agents use the equivalent available capability. If none is available, explain the limitation and provide the actionable prompts/plan without claiming images were generated or silently switching to a paid API.

1. **Lock a composition.** Use the supplied image or generate one master reference. Treat it as a geometry/style guide, not automatically as the rear plate. Independently prompted landscapes rarely align.
2. **Plan ownership and occlusion.** For each plane record what it owns, which front planes hide it, what must be reconstructed, where alpha belongs, and which borders need continuation.
3. **Extract and complete nearer objects.** Preserve neutral positions and scale. Restore portions hidden by still-nearer objects and extend viewport-clipped forms. Do not recenter or tightly crop cutouts.
4. **Reconstruct farther plates.** For an existing image, remove occluders from near to far, filling their whole footprint with the underlying plane's content. Remove matte fringes and inappropriate baked effects. Restore a middle plane's missing silhouette/alpha boundary as well as its color. For a new scene, a clean-base-first workflow is also valid when later planes share the reference geometry.
5. **Outpaint borders.** Extend actual scenery and cropped object geometry into the motion reserve. Transparent padding, stretched edge texels, and blurred strips cannot replace missing scenery. Keep genuinely empty overlay regions transparent.
6. **Export separate planes.** Default to PNGs with identical canvas dimensions, a shared origin/crop, and straight (unassociated) alpha. Base opaque, overlays transparent where appropriate. A contact sheet, layer diagram, opaque checkerboard, or flattened composite is not a layer pack.

Generated edits can shift objects and change canvas dimensions despite the prompt. Inspect actual output and repair drift. If exact export/resizing is necessary, use permitted editor tooling; never silently stretch layers independently. Preserve the requested scene and style rather than adding unrelated objects to make separation easier.

## Verify through motion

- Decode files; check identical dimensions, opaque base, nonempty overlays, real transparent pixels, and target texture-size limits. File extension alone proves none of these.
- Inspect overlays on both light and dark backdrops for matte halos, opaque rectangles, painted checkerboards, and missing fine structures.
- Hide front planes one by one and examine every exposed region of each rear plane. Opaque alpha can still conceal bad inpainting, foreground duplicates, or placeholder fills.
- Composite at neutral, both axis extremes, and all four corners for two-axis motion. Sweep between them; failures can appear midway. Include pairwise maximum relative separation, workspace range, target aspect variants, and easing overshoot.
- Check silhouettes, viewport edges, contact shadows, and reflections for holes, ghost objects, stretched bands, detached effects, and registration jumps. Seamless scrolling additionally requires matching opposite-edge color and alpha geometry across adjacent repeats.
- Prefer final validation in the target renderer. Otherwise preview with the same crop, stacking, and transforms and state that live runtime validation remains outstanding. A static center composite is insufficient.

The optional read-only structural checker uses Python 3 and Pillow if already available; it adds no dependency to hyprlax. Run with paths in back-to-front order:

```bash
python3 <skill-directory>/scripts/check_layers.py base.png middle.png foreground.png
python3 <skill-directory>/scripts/check_layers.py --max-texture-size 4096 base.png middle.png
```

Replace `<skill-directory>` with the installed skill directory; supply a measured target GPU limit, not an assumed one. The checker expects shared-canvas PNGs and reports alpha occupancy, bounds, and decoded RGBA memory. It cannot prove registration, straight-alpha provenance, hidden-content quality, seamlessness, or motion safety. If Pillow is absent, use an available inspector or arrange that optional dependency explicitly.

Repair the failing plane with the established reference and recheck its overlaps and motion range. If generation repeatedly breaks registration or alpha, report the specific limitation instead of calling the stack finished.

## Deliver

Save assets to the requested project/output directory with stable back-to-front names such as `00-background.png`, `10-midground.png`, and `20-foreground.png`. Include compact notes/manifest recording dimensions, crop, plane ownership/order, movement bounds, alpha roles, prompts/reference provenance, and verification results. Include a neutral composite and motion preview when tooling permits, clearly separate from usable plane files. For hyprlax, include a matching TOML config and state any unverified runtime behavior.

For research-only or skill-writing requests, deliver the requested skill/research without generating an unsolicited image pack.
