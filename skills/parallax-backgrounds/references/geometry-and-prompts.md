# Geometry and prompt recipes

## Two different reserves

**Viewport reserve** supplies artwork beyond the resting frame. **Occlusion reserve** supplies scenery hidden inside that frame. Both are needed.

Let a plane's actual rendered translation be `t_i(q)` over allowed motion states `q`. In output pixels, define `Dx_i = max |t_ix(q)|`, `Dy_i = max |t_iy(q)|`. For centered finite motion, the rendered artwork rectangle must cover at least:

```text
width >= viewport_width + 2*Dx_i
height >= viewport_height + 2*Dy_i
```

Add an explicit allowance for filtering, blur, and rounding. At `s` output pixels per source pixel, per-side source reserves are `ceil(Dx_i/s)` and `ceil(Dy_i/s)`. For asymmetric travel calculate each side using signed extrema. Empty transparent padding cannot supply coverage where solid scenery is required.

With an initially viewport-sized, same-aspect image and centered uniform zoom, a starting scale is:

```text
S >= max(1 + 2*Dx/viewport_width, 1 + 2*Dy/viewport_height)
```

These formulas describe translation after fitting. UV-based engines, different aspects, alignment, and extra crop margins require checking the actual transform. More source resolution alone does not reserve a border: `cover` normally fits the image to the viewport.

For overlapping front and rear planes, reconstruction depends on **relative** displacement:

```text
relative_x(q) = t_front_x(q) - t_back_x(q)
relative_y(q) = t_front_y(q) - t_back_y(q)
```

For shared input `|q| <= A` and movement coefficients `m_front`, `m_back`, maximum separation from neutral is `A*abs(m_front-m_back)`. With independent inputs, conservatively allow the sum of their bounds. Convert distances into the rear plane's source coordinates before planning repairs.

Complete hidden objects/plates in full when practical. For selective reconstruction, cover every rear-plane pixel that becomes visibly sampled in any allowed state. Remove the occluder's entire footprint, including matte fringe; reconstruct underlying content there and across the swept reveal region. Alpha dilation or feathering cannot create hidden geometry. Plan this recursively for all occluding pairs, not only the nearest and farthest planes.

### Example

A 1920×1080 viewport with common bounds ±96×±54 px needs a 2112×1188 canvas at 1:1 display scale, with a centered viewport crop. Its reserves are 96×54 per side, before filtering allowance. At this matching aspect, a common 1.1 cover scale gives that neutral crop. Preserve the same transform on registered planes.

If trees move ±96 px and mountains ±28.8 px on the same X input, maximum relative separation from neutral is 67.2 px. Restore the mountain behind the trees over the region revealed in either direction. Sufficient screen-edge reserve alone cannot fix this interior reveal.

## Stack table

| Plane | Owns | Alpha | Hidden continuation | Motion |
|---|---|---|---|---|
| Back | Sky/distant scenery | Opaque sampled region | Behind mountains and trees | Static/slow |
| Middle | Mountain mass | Transparent above its skyline | Mountain faces/skyline behind trees | Moderate |
| Front | Trees/near ground | Branch gaps/open sky transparent | Cropped forms beyond frame | Faster |

Never paint sky into the middle plane merely to fill empty space. Never erase mountains where the front trees sit. Ground-connected objects may need a common plane to preserve contact.

## Prompt scaffolds

Replace bracketed fields with the scene contract. Request one plane per output; prompts are not guarantees of pixel registration.

### Master reference

> Create [scene/style] with [depth planes], camera/horizon [specification], lighting [specification], palette [specification]. Reserve [borders] beyond the central [viewport] crop. Keep the main composition within that crop with plausible overlaps and clear silhouettes. Full composition reference, no labels, grids, panels, or layer diagrams.

### Clean rear plate

> Edit the composition into the [rear plane] plate. Remove [all nearer objects] completely, including recognizable outlines and effects that would duplicate during motion. Reconstruct [underlying scenery] continuously through removed regions and into [border reserve]. Preserve camera, horizon, scale, lighting, and unoccluded landmarks. Entire canvas opaque. No foreground copies, cutout holes, smeared fills, or transparency.

### Middle plane

> Extract only [owned objects] onto actual transparency using the reference canvas coordinates. Preserve neutral position, size, perspective, and lighting. Reconstruct hidden portions behind [front objects], including missing silhouettes and solid interiors. Alpha describes complete objects, not just visible reference pixels. [Open space] stays transparent; this plane's material behind foreground occluders stays opaque. Extend [cropped forms] through [reserve]. No other planes, recentering, tight cropping, backdrop, or painted checkerboard.

### Foreground plane

> Extract only [foreground objects], preserving reference coordinates on the full shared canvas. Keep genuine openings between [branches/etc.] transparent and edge coverage natural. Continue [truncated forms] beyond the viewport through [reserve] with plausible geometry and texture. Actual alpha transparency outside objects. No sky, matte rectangle, checkerboard, unrelated shadow, new objects, or changed camera.

### Targeted repair

> Repair only [region/failure] in this plane. At [motion extreme], [underlying content] becomes exposed. Complete it consistently with [reference]. Preserve other content, dimensions, registration, lighting, and alpha topology outside the repair. Do not copy the occluding object into this plane.

## Special cases

- **Seamless scroll:** match geometry and alpha as well as color at wrapping edges. Inspect two or more adjacent repeats, including fractional offsets. Finite overscan cannot cover unbounded travel.
- **Fog, glass, hair:** preserve meaningful fractional alpha. Repeated translucent copies may darken overlaps; inspect against contrasting backdrops.
- **Shadows/reflections:** ownership depends on receiving surface and motion. If an effect cannot stay attached across planes, group connected content or reduce separation. Flat images do not provide physical relighting.
- **Large viewpoint changes:** translating cutouts cannot expose true three-dimensional sides. Use modest travel or a geometry/novel-view workflow if the requested effect requires it.
