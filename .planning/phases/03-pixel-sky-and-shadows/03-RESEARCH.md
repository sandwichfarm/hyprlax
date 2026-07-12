# Phase 3 Research: Pixel Sky and Shadows

## PNG Contract

Use PNG signature plus IHDR/IDAT/IEND chunks, 8-bit RGBA color type 6, no interlace, filter byte 0
for generated rows, zlib compression, and CRC32 over chunk type+payload. Decoder supports filters
0-4 so it can read the existing source PNG, and rejects other formats/dimensions before allocation.

## Moon Geometry

For normalized visible-disk point `(x,y,z=sqrt(1-x²-y²))`, illumination fraction `k` gives phase
angle `a=acos(2k-1)`. Light vector is `(side*sin(a),0,cos(a))`; a pixel is lit when its dot product
with the surface normal is positive. `side=+1` for waxing/first quarter and `-1` for waning/last
quarter. This avoids PR #59's broken half/full masks.

## Shadow Projection

For each sufficiently opaque foreground pixel, compute height above bottom `h`. Project to
`x + direction*h*(0.15+0.75*(1-elevation))` and
`bottom - h*(0.06+0.14*elevation)`. Direction is opposite sun x. Alpha is bounded by
`110*sun_opacity*(1-0.75*elevation)` so the denser short noon projection remains visibly fainter
than the longer low-sun projection. This is stylized ground projection, not ray tracing.
