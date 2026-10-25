# Planet rendering

Planets used to be drawn as a 32×16 latitude/longitude mesh of flat
triangles, with one material sample and one light value per facet. The
silhouette was a polygon, lighting jumped at every facet edge, coastlines
followed the facet grid and the poles pinched. The mesh also cost more than
it showed: 1024 triangles of 64-bit work for a planet four pixels across.

Planets are now drawn analytically. Enhanced and Classic share geometry and
procedural data, with separate rasterisation paths.

## Selecting and previewing the renderers

The launch menu's third row, **RENDERER**, switches between **ENHANCED** and
**CLASSIC**. Graphics presets, including Ultra, control detail independently
of that choice. Host runs also accept `--classic` and `--enhanced`.

The next row, **BEZIER RINGS**, toggles cubic ring curves on or off in both renderers.
Off draws polygonal ring boundaries. The option defaults to On, is saved in
the settings file, and is independent of presets.
Host and intro previews accept `--no-bezier` for comparison.

Rebuild the host with `make` after updating the source. `./package.sh`
rebuilds the Amiga binaries and refreshes the ZIP, LHA and boot ADF; an old
archive or installed executable will still show the old launch menu.

The intro preview tool supports the same choice, for example:

```
make build/intro-preview
./build/intro-preview --classic --preset ultra --frame 148 classic.ppm
./build/intro-preview --enhanced --preset ultra --frame 148 enhanced.ppm
```

## Classic implementation and current limits

Classic builds a depth-sorted painter's draw list and fills flat scanline
spans, without depth-testing those primitives. It uses integer camera
transforms and reciprocal projection. Shared planetary fields provide
coastline, highland and albedo contours; lighting caps become span masks.
Terrain tiles and object geometry use the same world and procedural seeds
as Enhanced. Both backends are exercised by `tests/test_renderers.c`.

The implemented Bézier primitive is **cubic**, evaluated with fixed-point
forward differences and a step count chosen from projected extent. Its
current scene use is planetary rings in both renderers. Planet silhouettes use
analytic sphere outlines; switching to Ultra does not turn them into Béziers.
Enhanced fills the shared curve crossings with its own depth-tested spans.
The ring plane's inverse depth is affine in screen coordinates, so pixels
need additions and comparisons, with no divisions in the fill loop. The
normal orbital path uses no ring triangles. Rings crossing the near plane
or exceeding the curve guard band use the existing clipped mesh as a safe
close-up fallback. Classic retains its flat painter spans. Ring controls
live in `planet_ring.h`, and both paths use the same integer cubic edge
stepping from `classic_raster.c`.

Quadratic curves, general error-bounded adaptive subdivision, a shared
backend-neutral curve emitter, and Bézier cloud/coastline features remain
unfinished. Classic is implemented, but its target 68030 frame rate still
needs optimisation; passing renderer tests is not proof of playable speed.

## Layers

| Layer | Module | Backend-neutral? |
|---|---|---|
| Procedural noise shared with terrain and collision | `src/noise.h` | yes |
| Wide-range per-frame and per-row arithmetic | `src/xfloat.[ch]` | yes |
| Exact sphere outlines, bounds and level of detail | `src/planet_view.[ch]` | yes |
| A planet's orbital appearance: coasts, caps, albedo, land mix | `src/planet_field.[ch]` | yes |
| Per-pixel shading, depth, dithering, palette | `src/render_planet.inc` | Enhanced renderer only |

The shared modules know nothing of frame buffers, depth buffers or palettes.
They produce exact spans and body-fixed surface properties. The Enhanced
renderer shades those spans per pixel and depth-tests them. A Classic
renderer can fill the same spans flat, in painter's order.

## Outlines: `planet_view`

A sphere seen in perspective is bounded by a conic. Each scanline crosses the
sphere's forward view cone in exactly one interval: the cone is convex. For
the ray `d = (u, v, f)`, row `v` is the quadratic `A u² + 2 B u + K ≥ 0`.
By Lagrange's identity its discriminant is `c · E(v)`, where
`E(v) = R²(v² + f²) − (Cy f − Cz v)²` and `c = |C|² − R²`.

- `E` has no catastrophic cancellation for small, distant spheres.
- The caller passes an exact altitude, so `c = alt (2R + alt)` keeps
  low-flight horizons exact.
- When `A > 0` the outline is a hyperbola: the camera is near the surface,
  and the row runs off one side of the screen. When `A = 0` it is a parabola.
  Both cases are handled explicitly.

The coefficients are computed once per frame in `XFloat`. That is a 30-bit
mantissa with an exponent, in pure integer arithmetic, so every CPU variant
gives bit-identical results. Frontier: Elite II made the same split: integers
everywhere, soft floats only for planets. Fixed point cannot cover both an
AU-distant moon and a horizon 200 m below the camera. A 14-bit prototype was
off by hundreds of pixels at the screen edges.

`tests/test_planet.c` checks the result against a brute-force
double-precision ray–sphere test on every pixel:

- 438 million pixels and 3000 views, from 5 m up to 10⁶ radii.
- 1285 of those views have open, hyperbolic outlines.
- Covered limbs land within 0.003 px of the true limb.
- The only disagreements are samples within 0.002 px of the limb, which is
  Q8 rounding of the edge.

We chose an exact conic over Bézier outlines. Frontier approximates the
outline with two cubics, about 2% radial error, plus a special "half mode".
The exact per-row solve costs about the same and is exact at every distance.

## Appearance: `planet_field`

From orbit, a world shows exactly the coastlines and caps the commander lands
on:

- **Coastlines** are terrain's own sea-level contour of the continent noise.
  That noise lives on a 10³ lattice, cached per body.
- **Ice caps** use `terrain_material`'s latitude threshold.
- **Regional albedo** is `terrain_regional_albedo`. Its coarse lattice is
  cached, and its fine lattice is filled lazily.

Every value is bit-identical to terrain's noise; the test checks 680,000
samples. Sea and land agree with `terrain_material` 99.75% of the time. The
rest are kilometre-scale inlets, which are sub-pixel from orbit.

Inland lowland, forest and rock depend on local ranges and valleys well below
a pixel at orbital range. So the field reports their *expected* shares, as a
function of planet-scale base height. The table was measured against terrain:
the field expects lowland/forest/rock 8.4/21.2/70.4%, and terrain has
8.6/21.2/70.2%. The test re-measures it, so it cannot silently drift if
terrain generation changes.

## Shading: `render_planet.inc`

For a pixel ray with `b = c·d`, `s = |d|²` and `k = cos²ρ`:

- The near hit is `D·w·d`, with `w = k / (b + √(b² − k s))`. This form has
  no cancellation at the limb.
- The normal is `(D/R)(w d − c)`.
- Inverse depth is linear in `b + √…`, so it needs no extra division.

Normals match an exact double-precision intersection to 2·10⁻⁴.

- **Sampling.** Exact samples are taken every 4, 8 or 16 pixels, depending on
  the `PLANET` graphics setting. Values between samples are interpolated by
  addition. Within an eighth of the radius of the limb, every pixel is exact.
- **Coastlines.** Base height is interpolated, and the sea-level threshold is
  applied per pixel, so coasts stay sub-pixel smooth.
- **Field work only where it shows.** Depth is tested first, so blocks hidden
  behind ships, ports or terrain skip the field lookups.
- **Lighting** is the sum over stars, with eclipses by other bodies, as
  `surface_light` does.
- **Colour.** The ramps are the existing material ramps. Shading and the
  sub-pixel land mix are ordered-dithered, which gives smooth gradients even
  in the 3-step ECS ramps.

## Distance regimes

| Altitude | Geometry | Sphere |
|---|---|---|
| ≥ 200 km (`PATCH_ALTITUDE`) | none | analytic disc, exact depth |
| 20–200 km | coarse patch clipmap: horizon relief, true materials | analytic backdrop |
| < 20 km | detail terrain clipmap, per-pixel ground texture | analytic backdrop |

The backdrop's depth is pushed back by 1/128. The terrain mesh's chords sag
below the true sphere, and the mesh must always win where it exists.
Above 200 km the patch's facets (about altitude/8 across) would be coarser
than the sphere's own per-pixel coastlines, so it is not drawn.

## For the Classic renderer

It can reuse `planet_view` (spans, bounds, LOD), `planet_field` (coasts,
caps, albedo, land shares) and `noise.h` unchanged. What it needs of its own
is its fill: flat or banded spans, painter's order, and a few colours. The
terminator, ice caps and gas bands are circles on the sphere, so they can be
crossed per row the same way as the outline. Coastlines can be extracted as
contours of the same continent field.

## Measuring

```bash
make planet-bench      # host timings of the planetary fixtures
make planet-evidence   # PPM frames of the same fixtures, in build/evidence/planets
```

Host timings do not predict 68k cost; see the 68020 notes in the commit
history. Native measurement belongs on a cycle-exact emulator with JIT off.
