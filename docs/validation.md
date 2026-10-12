# Validation - World and navigation development build

Verified on 13 October 2026. This records development-model checks, not production
hardware performance or completion of the remaining careers/presentation roadmap.

## Current builds and host checks

- `make test` passes with C99, `-Wall -Wextra -Werror`.
- `make sanitize` passes with AddressSanitizer and UndefinedBehaviorSanitizer.
- `make navigation-test` passes the longer, actual AU-scale interplanetary route.
- `./build-amiga.sh all tests` produces `Space.000`, `Space.020` and
  `Space-tests.020`, with soft float, libnix and warnings treated as errors.
  `-Wl,-u,___stkinit` links the stack-swap constructor so the declared 128 KiB
  stack is honoured for the expanded world and commander data.
- The AmigaGuide checker reports zero errors and zero warnings.
- Host surface/orbit previews were rendered and inspected.

The core suite covers deterministic astronomical generation and cross-CPU golden
values, moving parented orbits, sparse settlement/occupancy, disc/arm density,
atmosphere density, clearance queues, fast station wall/aperture crossings, lost
fuel and insufficient gravity-compensation authority, inertial velocity preservation
on leaving a body frame, exploration saves, corruption rejection and planar output.
It completes 58 generated port arrivals and waypoint arrivals for every generated
body type (rock, ocean, desert, ice, gas giant and moon), plus the full surface launch,
moving orbital docking and safe surface return. All six contracts still complete and
pay once. Requested compression preserves the fixed-tick trajectory.

The extended host route departs the initial body, crosses the system frame and
arrives at a moving second body after 8,492,040 physics ticks, with 67 percent fuel
and a 4 m waypoint error. This is one scenario result, not a universal navigation
guarantee. The existing head-on avoidance fixture retains an 871 m closest approach.

Orbital samples are cached per simulated second and interpolated through the 32
physics ticks. Terrain/globe samples share procedural material classification;
coarse/fine edges share grid vertices and the outer patch has a skirt. Forests,
clouds and city buildings are decorative; station walls have collision geometry.
Circular inclined orbits, bounded journals and scheduled clearance traffic remain
explicit model limits, as described in roadmap.md. Generator version 3 saves reject
version 2 commanders.

## Current Amiga runtime checks

The rebuilt `Space-tests.020 --quick` suite passes in AmiKit/Amiberry on the
configured accelerated 68020. It verifies the cross-CPU generation/orbit golden
values, frames, exploration/save integrity, station collisions, rendering, five
generated port arrivals, waypoints for all six body types and compression safety.
The native output is saved in `build/evidence/world-tests-amiga.txt`.
The rebuilt RTG game opens, accepts Q/Y to display the generated body list and
returns to Workbench with Escape. Current screenshots are saved as
`build/evidence/world-final-rtg.png` and `world-final-bodies.png`.
The full launch/return, 58-port and extended AU-scale routes above are host checks;
they are not completed full-suite Amiga runtime checks or hardware timings.

## Current host renderer workload

The fixed orbital scene benchmark reports 0.136 ms/frame over 200 frames, with
indexed frame hash `a66d2d3f`. This scene now uses astronomical planet radii and
moving-world data and is not the same workload as the previous small-planet scene.
Host timing does not establish frame rates on Amiga hardware.

## Earlier prototype emulator evidence

The following evidence predates the world/navigation changes. It establishes the
platform backend's prior behavior, not runtime validation of the new world model.

The tests use the installed AmiKit configuration with an accelerated emulated
68020, AGA and Picasso96-compatible RTG. Both native display layouts were
inspected. The 68000 executable was run on the emulated 68020; a real 68000 is
not yet performance- or runtime-validated.

- RTG screen opens, displays the contract board, receives keyboard input and
  returns to Workbench.
- AGA native display shows the cockpit, generated scene and keyboard launch.
- AGA 64-bit fetches show correct contiguous rows and palette output.
- ECS display shows the five-plane palette and banded atmosphere.
- Escape restores Workbench and the original DMA enable flags.
- Native F5 restores the OS, writes the commander and resumes takeover.

Native save overwrite and load are also verified: after saving, a market
purchase reduced the balance from 1500 to 1488 credits and added one tonne of
water; F9 restored the saved 1500-credit state. AGA-to-ECS switching retained
correct row layout and output. Screenshots and native test output are saved
under `build/evidence/`. Real hardware, NTSC native,
other RTG drivers and long-duration lifecycle stress testing remain unverified.

## Optimization evidence

The host's fixed orbital scene benchmark, over 200 frames, changed from
0.612 ms/frame to 0.218 ms/frame after caching immutable globe meshes and material
colours. The indexed frame hash remained `a1799660` across that optimization.
These are host CPU timings and do not establish Amiga hardware frame rates.

A GCC 16.1.1b 68000 optimization slowdown was isolated to an unnecessarily
64-bit lighting dot product. Using bounded 32-bit operands lets the renderer
compile in about 0.35 seconds at `-O2`, with no need to lower optimization.


## City occlusion and polar contrast (2026-10-13)

The painter renderer sorted triangles by average camera depth. Large ground
triangles could cover parts of nearer buildings when looking down from above.
The shared rasterizer now interpolates reciprocal depth per pixel, including
clipped scanline spans. Its buffer is allocated before native OS takeover and
freed after restoration. Polar ice uses darker blue-grey shades; the ECS
atmosphere uses separate palette entries from ice and water.

- `make test` and `make sanitize` pass, including crossing-surface occlusion,
  opposite submission orders, clipped depth interpolation and ECS polar contrast.
- `./build-amiga.sh all tests` builds both 68000/68020 games and the native suite.
- Host surface, orbit and 1232-metre overhead city frames were inspected.
- AmiKit RTG and ECS screenshots show the full overhead city and darker ground
  after loading an isolated deterministic commander. Evidence:
  `build/evidence/city-depth-rtg.png` and `city-depth-ecs.png`.
- Host orbital benchmark: 0.198 ms/frame over 200 frames, hash `c8302a55`.
  This does not establish native hardware performance or city-scene frame rates.

The depth buffer costs 250 KiB for ECS/AGA and 1060 KiB for RTG. Simple cuboid
buildings, a flattened landing zone and polar ice are still prototype content;
this change fixes visibility and contrast rather than redesigning the city.
