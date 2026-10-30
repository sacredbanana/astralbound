# Planet rendering

Planets used to be drawn as a 32×16 latitude/longitude mesh of flat
triangles, with one material sample and one light value per facet. The
silhouette was a polygon, lighting jumped at every facet edge, coastlines
followed the facet grid and the poles pinched. The mesh also cost more than
it showed: 1024 triangles of 64-bit work for a planet four pixels across.

Planets are now drawn analytically. Enhanced and Classic share geometry and
procedural data, with separate rasterisation paths.

## Selecting and previewing the renderers

Open **Graphics Options** in the launch menu. Its first two settings are
**RENDERER** (Enhanced or Classic) and **BEZIER RINGS** (On or Off). Editing
an option selects Custom without resetting the other settings.

Medium is the new-install default on all builds. Low and Medium select
Classic; High and Ultra select Enhanced. Low disables Bezier rings; Medium,
High and Ultra enable them. All presets render at full resolution, and old
saved pixel-size overrides are ignored. Surface Shading and Triangle
Budget are unavailable with Classic; their saved values remain intact for
Enhanced. Classic terrain/cloud controls omit unused detail levels, and
curved ring detail is automatic (the rings control becomes On/Off). Custom preserves manual renderer
and curve choices. Named presets reapply their complete defaults on load.

Host runs also accept `--classic`, `--enhanced` and `--no-bezier`; explicit
renderer/curve flags override a named preset and select Custom. Intro previews
accept the same overrides for comparison.

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
| A planet's orbital appearance: coasts, geographic ice, albedo, land mix | `src/planet_field.[ch]` | yes |
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
- **Polar ice** uses a shared body-fixed field: latitude sets the cold zone,
  continental uplands extend glacial tongues equatorward, and regional
  deposits break the edge into bays. Each seed and hemisphere has its own
  outline. There is no longitude seam, and both geographic poles stay frozen.
  `terrain_material` evaluates the same field on the ground. Ice worlds stay
  entirely icy; gas giants keep their atmospheric bands.
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

## Measured Solar System maps: `body_map`

Seventeen Sol bodies replace the planet-scale noise with measured maps (sources in
`data/README.md`). `body_map_sample` reads a cube map of (n + 1)^2 lattice
points per face, bilinearly, in 32-bit integers: two divisions give the
gnomonic face coordinates, and no trigonometry is needed. Edge points are
stored identically on both faces, so values are continuous across seams.

Only the fields a planet shows from orbit change. `planet_field` and terrain
both call the same `body_map_*` functions, so coasts, albedo and ice stay
identical in orbit, on the ground and in collision:

- **Continent** (Earth): real coasts at exactly the generator's sea level.
- **Relief** (Mars, the Moon): added to the local landforms.
- **Region and regional albedo**: the measured map, without the fine noise
  term. Classic's dark contour is its median.
- **Ice** (Earth, Mars): a mapped frozen fraction replaces the latitude rule;
  other mapped bodies have none.
- **Arid** (Earth): land brighter than the desert margin, or above 900 m of
  base height, is rock; the rest is vegetation. `planet_field_rock` is the
  same edge for Classic's highland contour.
- **Veil** (giants, Venus, Titan): the orbital disc is cloud tops. Mapped
  giants show their belts as albedo; Classic draws them as albedo contours
  instead of its seven procedural band circles. Venus and Titan are a plain
  deck, and the 20-200 km terrain patch is skipped beneath them.

Each map also names palette ramps per material (`body_ramp` in `render.c`):
rust Mars, grey Moon, tan deserts and Saturn, cyan Uranus, deep-blue Neptune,
and a dark ramp for ground below the map's reference level: Jupiter's belts,
Europa's lineae, Iapetus's Cassini Regio, Pluto's red-brown tholins around its
pale heart, Charon's red cap. Both renderers switch on the same threshold
(Classic's dark contour). A row with no cells (`n` 0) is a palette alone:
`Planet.palette` gives the moon its colours while `Planet.map` stays NULL and
its ground stays procedural (Phobos, Deimos, Saturn's and Uranus's other moons,
Triton). Every ramp a body names comes from `palette`. All are existing 16- or
32-entry ramps; the tan ramp reaches a pale gold, so sand, Io's sulphur,
Saturn's ammonia and Titan's haze are not khaki. On ECS, Classic dims a mapped
body's dark regions by one shade; generated worlds are untouched.

Map north is body -z, the positive (spin) pole, with the prime meridian at
+x. Astralbound's camera is left-handed (right x up points forward), and so
are its coordinates for the stars and Sol (system x/y/z are ecliptic X/Z/Y).
Bodies spin from +x towards +y, so this is the orientation in which maps are
unmirrored and the ground moves east.

## Spin axes

Sol's bodies turn about their real poles. `set_rotation` composes each
catalog body's fixed tilt frame with its spin into `spin_m`, a Q30 matrix,
once a tick; `body_rotate` then costs nine 64/32 products instead of the six
of the upright spin, which generated worlds keep bit for bit. `body_frame`
is the body-fixed frame terrain and collision sample in, and `body_tilt` the
equator without spin: Saturn's ring plane in both renderers, and the plane
the orbital stations circle. Surface velocity is the spin axis crossed with
the position. Both renderers' camera-to-body matrices come from
`body_matrix`.

Planets take the IAU rotation models (WGCCRE 2015) at J2000, the game's tick
0, from `data/solar-poles.csv`: at tick 0 the sun stands over Greenwich at
-23 degrees. Retrograde rotators spin about their positive pole. Synchronous
moons spin about their orbit pole with the prime meridian on the mean
direction of their planet; the Moon keeps its IAU pole, so it librates by up
to about 7 degrees in longitude and latitude and always shows its near side.
Pluto and Charon face each other. `tests/test_planet.c` checks obliquities,
Greenwich noon at J2000, east-moving ground and ten years of the near side.

`make sol-preview` renders every mapped body in Enhanced and Classic, AGA and
ECS, into `build/evidence/sol-*.ppm`.

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
- **Coastlines and ice.** Base height and signed ice coverage are interpolated,
  and their boundaries are applied per pixel. Ice uses the existing cached
  continent and regional lattices, evaluated at sample points rather than
  once per pixel.
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
terminator and gas bands are circles on the sphere, so they can be crossed
per row the same way as the outline. Coastlines and polar ice are extracted
as closed contours of their shared fields. Classic caches the ice contours
alongside coasts, highlands and albedo, clips them at the limb and seeds fill
parity against those same contours. Polar ice is no longer drawn as a pair
of latitude circles.

## Measuring

```bash
make planet-bench      # host timings of the planetary fixtures
make planet-evidence   # PPM frames of the same fixtures, in build/evidence/planets
```

The image set includes north/south oblique and pole-on views, plus a rotated
north view. `tests/test_planet.c` checks irregular seeded margins, rotated
surface agreement and contour parity. `build/renderer-tests --polar-ice`
checks the visible ice against the field across both renderers, ECS/AGA/RTG,
Low/Medium/High, rotation and pole-on views.

Host timings do not predict 68k cost; see the 68020 notes in the commit
history. Native measurement belongs on a cycle-exact emulator with JIT off.

## Planetary aurorae

**PLANETARY AURORAE** is an independent On/Off graphics option. Ultra enables
it; Low, Medium and High disable it. Custom preserves the selected value in
`graphics_18`, appended so legacy graphics keys keep their meanings. Old Ultra
settings acquire the new preset default; old Custom files default to Off.

`render_aurora.inc` supplies the same body-fixed northern and southern ovals
to both backends. A seeded magnetic tilt, flowing latitude folds, varying
curtain heights and emission strengths animate from the fixed game tick.
The shell scales with body radius, independent of terrain and cloud LOD.
Emission fades with solar elevation; atmosphere-free and vacuum bodies are
skipped. Indexed ordered transparency reuses green/cyan/violet palette entries.
No simulation, commander-save or palette format changes are required.

Enhanced draws the clipped curtains against the completed world depth buffer.
Classic places an aurora callback immediately after its globe in the painter
list, including sky-only views, and masks the far oval with an analytic sphere
depth test. Near ground and cloud geometry then draws over the glow. A small
independent triangle scratch buffer avoids consuming the scenery budget.

`tools/gen_aurora.py` generates exact integer lookup tables from `trig.h` for
waveforms, circle/latitude/tilt directions, daylight fading, brightness and
opacity (about 12.7 KiB of read-only data). `--check` verifies reproducibility.
A 4 KiB pose table caches rotated directions and their lazily evaluated norms;
phase, magnetic tilt and both spin components invalidate it. Adjacent height
bands reuse radial endpoints. These preserve the original C99 truncation and
both radial normalization stages. Reference tests check exact geometry across
magnetic tilts, animation phases, spin states and body scales. View-dependent clipping and depth still use the existing
fixed-point arithmetic.

`make aurora-preview` writes deterministic polar-night surface and orbital
PPMs for Enhanced and Classic in ECS 320x200, AGA 320x256 and RTG 640x480 to
`build/evidence/aurora-*.ppm`. `build/renderer-tests --aurorae` checks both
poles, animation and repeatability, toggle/vacuum behavior, clipping,
foreground depth, far-globe occlusion and HUD bounds.


## Gas-based sky, halo and aurora colours

`render_atmosphere.inc` holds small visible-light colour tables shared by
Enhanced and Classic. `atmosphere_gases()` and `atmosphere_haze()` read the
catalogue's explicit physical metadata; generated worlds use the same gas
families as their composition sheets. Density/weather categories such as
THIN no longer imply a specific gas: Pluto is thin nitrogen/methane, while
Mars is thin CO2. Organic haze uses a dense optical column without changing
the flight-density model. It hides stars through Titan-like smog.

Earth-like aurorae put nitrogen pink near the bottom, oxygen green in the
main curtain and oxygen red above. CO2 atmospheres include fainter visible
oxygen from dissociation. Nitrogen/methane atmospheres use nitrogen pink,
blue and violet without an invented green oxygen band. Hydrogen atmospheres
use dim red below and violet above. Only visible emission is drawn; ultraviolet
and infrared observations are not assigned false visible colours.

Low skies include dust, sulphur cloud or organic haze where those constituents
are present. Above the lower haze, molecular scattering and methane absorption
favour blue. Both backends draw the same sunlit atmospheric shell; Enhanced
checks world depth, while Classic uses flat light samples per side and row in
its painter order. Night-side halos fade rather than glow uniformly. Colour
and transmission choices use lookup tables. The base sky uses solid horizontal
colour bands, including ECS warm skies; ordered mixing is confined to the
local twilight glow and translucent effects. These are indexed-palette
approximations, not a full spectral radiative-transfer or auroral-activity model.

`build/aurora-preview --gases` writes night surface, day surface and orbital
fixtures for Earth, Mars, Venus, Pluto, Titan, Jupiter and Neptune in ECS/AGA/RTG
and both renderers. `build/renderer-tests --atmosphere-colours` checks the
chemistry override independently of climate, visible emission colours, haze,
halos, foreground depth, viewport bounds and solid base-sky rows.

Terrain lighting caches identify the star positions/types, body rotation and
positions/radii of potential eclipse occluders as well as simulation time.
Changing a scene's illumination at the same tick therefore refreshes tile
colours and slope lighting, while retaining material and geological lookup
samples. `build/renderer-tests --terrain-lighting` compares warm-cache frames
against cold-cache frames through night/day changes in ECS/AGA/RTG, for
Material, Textured and Full shading. This catches the square night-coloured
tiles previously visible in daylight comparison previews.

Physical references:

- [NASA: auroral gases and altitude-dependent colours](https://science.nasa.gov/sun/auroras/)
- [NASA/JPL: visible green aurora at Mars](https://www.jpl.nasa.gov/news/nasa-observes-first-visible-light-auroras-at-mars/)
- [NASA: Saturn's visible hydrogen aurora](https://science.nasa.gov/resource/saturns-colorful-aurora-annotated/)
- [NASA: methane absorption and ice-giant colours](https://science.nasa.gov/solar-system/planets/neptune/why-uranus-and-neptune-are-different-colors/)
- [NASA: Titan's nitrogen/methane atmosphere and organic haze](https://science.nasa.gov/saturn/moons/titan/facts/)
- [NASA: Pluto's blue haze](https://www.nasa.gov/image-article/plutos-blue-sky/)
