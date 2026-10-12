# Validation - Mouse/joystick controls and 4096x compression

Verified on 13 October 2026:

- Host core and renderer tests, ASan/UBSan, and the extended interplanetary route
  pass. All 58 generated port arrivals now request 4096x. A compressed frame
  matches the position and velocity produced by the same number of normal ticks.
- Input regressions cover all 16 digital joystick switch combinations, mouse
  dead zone, recentering, combined input limits, warp selection and save roundtrip.
- Both 68000 and 68020 games and the native test executable cross-build with
  warnings treated as errors. The AmigaGuide validates without warnings/errors.
- AmiKit/Amiberry ECS screenshots show right-drag changing dorsal turret aim;
  release recenters the virtual stick. Escape restores Workbench. Evidence is
  in `build/evidence/input-ecs-*.png`.
- RTG uses Intuition delta mouse events and clears input on lost window focus.
  RTG mouse runtime validation remains pending: emulator focus automation rejected
  attempts to focus the shell after the ECS check. Physical joystick and real
  hardware input remain unverified. Digital direction decoding is regression tested.

4096x is a requested simulation rate, still limited by autopilot hazard checks
and CPU throughput. Every physics step remains 1/32 second. Extended Amiga analogue
joystick and CD32 button support remain future work.

# Validation - Persistent port market development build

Verified on 13 October 2026:

- `make test` and `make sanitize` pass, including all 58 docking routes and the
  existing navigation/combat/safe-compression regressions.
- New market regressions cover independent port inventories, return to a system,
  scarcity pricing, hourly recovery from shortages and surpluses, exact transaction
  quotes, failed-trade atomicity, credit limits, and same-port buy/sell losses across
  32 seeds and all eight commodities.
- Save checks cover version 3 migration, ledger roundtrips, malformed checksummed
  ledgers, duplicate records, future timestamps, and a fully occupied 128-market
  ledger together with the full exploration journal in the bounded save buffer.
- `make` builds the host preview. `./build-amiga.sh all tests` builds both 68000 and
  68020 games and the native test executable with warnings treated as errors.
- The final `Space-tests.020 --quick` passes in AmiKit/Amiberry, including the new
  market and save regressions. Output: `build/evidence/market-tests-amiga.txt`.
- The ECS market screen is readable at native resolution and shows the port name,
  buy/sell quotes, stock and cargo (`build/evidence/market-ecs.png`). Escape returns
  to Workbench (`build/evidence/market-native-exit.png`).
- RTG purchase/save/sale/load interaction restores the purchased tonne, reduced
  stock and matching credits (`build/evidence/market-final-rtg.png`).
- The AmigaGuide checker reports zero errors and warnings; `package.sh` creates
  ZIP and LHA bundles and verifies their extracted contents.

The ledger stores at most 128 traded ports. It preserves existing entries rather
than evicting them; trading at a new port is refused when it is full. Replenishment
is a deterministic recovery towards generated stock, one tonne per simulated hour,
not a simulation of civilian industry or freight fleets. Version 3 saves preserve
legacy stock at the docked port; in-flight legacy saves start with generated markets.
World generator version 3 is unchanged. These runtime checks use an accelerated
emulator; real Amiga hardware performance and other RTG drivers remain unverified.

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

## Native video layouts and reduced workload (2026-10-13)

Native ECS/AGA now selects 320 x 256 on PAL or 320 x 200 on NTSC from
graphics.library's display flags. `--lowres` selects a vertically centred
320 x 200 window on PAL. Native CIA TOD timing uses the corresponding 50/60 Hz
rate; physics still advances through fixed ticks. Copper plane pointers,
allocation/free sizes, ECS palette mapping and C2P follow the selected height.
AGA/RTG no longer allocate an unused ECS palette-mapping buffer.

At 200 lines, menus occupy the whole screen with a balance/message footer;
flight keeps the cockpit. This reduces indexed/planar display pixels by 21.875%
and the flight viewport from 200 to 144 lines (28% fewer pixels). The depth
buffer falls from 250 to 180 KiB. These are workload/memory reductions, not
measured hardware speedups.

- `make test` and `make sanitize` pass. New regressions compare all seven compact
  menus with the established text layout, check PAL/NTSC display windows, and
  round-trip every pixel of 200-line five/eight-plane C2P with allocation guards.
- `./build-amiga.sh all tests` builds the 68000 and 68020 games and native suite.
- AmiKit's PAL AGA configuration runs both AGA and ECS `--lowres` displays.
  Screenshots show centred output, cockpit rows, all eight market items and
  trade instructions. Native F5 resumes with a SAVED message, and Escape
  restores Workbench with DMA flags `03F0` (native play uses `0380`). Evidence:
  `build/evidence/video-lowres-{aga,ecs,market,save,exit}.png`.
- Host `--lowres` surface and `--ntsc` orbit previews render at 320 x 200.

NTSC selection/timing and layout have build and host regression coverage only;
an actual NTSC emulator boot and real PAL/NTSC machines remain unverified.
The ECS check used AGA hardware emulation, not a physical ECS chipset. Stock
68000 frame rates and long-duration lifecycle tests remain open QA work.

## Turrets, radar and system presentation (2026-10-13)

The first presentation roadmap item is implemented: front/rear/external plus
independently aimed dorsal/ventral turret views; turret-directed laser fire;
10 km contact radar and selection with hull, range, signed closing speed and a
screen marker; top-down system and local orbital maps; and system/body data.
Navigation targets and combat contacts are separate. World-angle turret aim,
contact selection and map display are transient; existing version 3/4 commander
payloads still load. Camera mode is retained, with neutral aim after loading.

- `make test` and `make sanitize` pass. Regressions cover turret control/fire,
  an aft target missed by the forward laser, contact range/loss, map controls,
  turret-mode saves and legacy commander migration. Renderer checks cover
  compact panel equivalence, AU-origin cancellation, heading-relative radar
  positions and out-of-range removal, and maps/data for all bodies in eight seeds.
- `./build-amiga.sh -j4 all tests` builds both CPU games and `Space-tests.020`.
  AmiKit runs the native `--quick` suite successfully, including the new turret
  and contact checks. Output: `build/evidence/presentation-native-tests.txt`.
- AmiKit PAL AGA `--lowres` displays the system/local maps, body data and dorsal
  turret/radar. ECS `--lowres` also displays the orbital map. Escape restores
  Workbench; the AGA exit has DMA flags `03F0`. Screenshots are in
  `build/evidence/presentation-aga-{map,local,data,turret,exit}.png` and
  `build/evidence/presentation-ecs-map.png`.
- AmiKit RTG displays system/body data and the orbital map, then restores
  Workbench on Escape. Evidence: `build/evidence/presentation-rtg-{data,map,exit}.png`.
- Host deterministic previews include orbital/local maps, compact body data,
  a selected hostile radar contact and turret view; RTG previews use 640 x 480.
- `Space.guide` passes the AmigaGuide checker with no warnings or errors.

Orbits are true-scale projected circular/inclined paths. At system scale, moons
and ports can overlap; local scale resolves their separation. Radar represents
active encounter ships, not a persistent civilian fleet. Turrets share the
existing laser heat, cooldown, upgrade and 5 km range; separate hardpoint and
weapon equipment systems remain roadmap work. These checks do not establish
real hardware frame rates or NTSC runtime behavior.
