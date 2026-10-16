# Autopilot time-compression performance - 13 October 2026

- Clear system-frame cruise uses bounded constant-acceleration intervals, up to
  128 simulated seconds at 4096x, with interval-aware guidance and impulse fuel.
  Eligibility checks cover every body's influence/braking region, stellar
  clearance, orbital motion, target approach, NPCs, controls and mission events.
  Local flight keeps the original fixed-tick trajectory and swept collisions.
- At most 128 simulation integrations run per displayed frame. Host and native
  loops submit elapsed ticks together so catch-up cannot multiply that budget.
  The HUD effective rate reflects elapsed simulation time actually delivered.
- The remote-cruise host regression measured approximately 0.033 ms for 4096x
  versus 30.4 ms for 4096 full ticks at the same initial state: about 900x less
  simulation CPU work in that sample. After 128 simulated seconds, position
  differed by 683 m and velocity by less than 100 m/s; cruise integration is an
  approximation rather than bit-for-bit fixed-tick equivalence.
- End-to-end AU-scale flight reached the moving body waypoint after 8,558,417
  simulated ticks with 69 percent fuel remaining and 4 m arrival error. Tests
  cover timer updates, delivery expiration, pause, NPC/input/patrol fixed-tick
  fallback and a shared frame budget during eight elapsed normal ticks.
- Native frame rates and physical hardware performance have not been measured
  for this change. The timing above is a host simulation benchmark, excluding
  rendering and audio.

# Presentation and hardware completion work - 13 October 2026

The remaining implementation items in this section are now present. Physical
hardware and peripheral validation remain open, as recorded in the roadmap.

- Original integer PCM synthesis provides an eight-second musical motif plus
  engine, laser, damage, explosion, docking/launch and jump effects. Audio follows
  wall time. Native modes allocate Paula channels before takeover; RTG queues
  separate left/right audio.device writes; the host uses an SDL callback. Saves
  release/reallocate native audio ownership, pause mutes it, allocation failure
  runs silently, and exit stops DMA before freeing samples.
- Native proportional input uses owned POT resources with startup/manual centre
  calibration and a dead zone. Fractional turn rates accumulate over fixed ticks.
  CD32 uses lowlevel.library V40+ with seven mapped buttons and edge-triggered
  actions; SDL controllers use the same normalized mappings. These paths have
  native builds and deterministic calibration, rate, button and menu logic
  tests. Physical joystick/CD32 and Poseidon devices have not been exercised.
- The depth rasterizer has a bounded 32-bit pixel loop and a wide fallback;
  portable C2P writes pairs of planar bytes (word stores on m68k). Native blitter
  work clears both display buffers at startup/resume. It is not a blitter C2P
  implementation. Existing depth, clipping and guarded planar roundtrip tests pass.
- The 68000/020/030/040/060 software-integer games build with warnings as errors.
  Native C2P/orbit/surface benchmark programs and a 120-frame game profile mode
  are available. Native profiling uses CIA TOD display ticks; EClock requires
  timer interrupts, so it is reserved for RTG/OS-managed timing.
- RTG16 opens a high-colour screen and uses WriteLUTPixelArray through the CGX/P96
  driver. Screen buffers and driver conversion determine available acceleration;
  no card registers are accessed. Scene shading retains the indexed palette.
- The release has original two-plane Workbench icons (icon.library wrote/reloaded
  them), CPU tooltypes, launch scripts, an Installer script and ZIP/LHA packages.
  Installer 44.10 compiled the script; the same copy statements were run with an
  unattended test destination. All 23 selected files matched their sources, the
  drawer icon was created and an existing commander sentinel survived. Interactive
  destination selection/confirmation still needs a manual run; host UI automation
  could not click the main emulator window (`noWindowsAvailable` / focus rejection).
  Evidence: `build/evidence/hardware-installer-check.json`.
- The OFS boot disk includes only Space.000, its guide/icons/license and an original
  resident-DOS boot entry/startup sequence. The filesystem and boot checksum pass;
  every packaged file was read back. The final disk is shipped without a commander.

## Current runtime and regression evidence

Host core, renderer and new presentation tests pass, including ASan/UBSan. The
full AU-scale navigation scenario still reaches its moving destination in
8,492,040 ticks with 67 percent fuel and 4 m error. Music tests cover event
priority, repeated observations, pause, effect termination, loop repetition and
identical PCM across different callback chunk sizes. Controller tests cover
noncentral calibration, full travel, dead zone, partial-rate accumulation,
digital precedence, button edges, braking and menu isolation.

AmiKit ran the new native presentation tests and the native benchmark.
The new test executable declares its stack requirement. In a bare boot CLI,
a direct stack-swapped 020 test run passed assertions but faulted at exit.
`Space-RunTests` avoids that path by starting its children with 256 KiB stacks;
Workbench icons now request the same size, and CLI documentation sets it
explicitly. The launcher passed the final presentation and core quick suites in
the separate NTSC emulator (JIT off); its logs were extracted from the test ADF. The direct bare-CLI stack-swap exit remains toolchain QA. PAL AGA
320 x 200, saving/resuming and return to Workbench were inspected. Native audio
state shows channels 0/1/3 active and a populated emulator sound buffer; Escape
restores DMA 03F0 and turns all audio channels off. RTG16 displays the starting
scene and returns to Workbench. Both RTG stereo channels are queued separately.
These checks establish emulated DMA/output behavior, not a listening test on
physical Paula hardware. Screenshots are `build/evidence/hardware-aga-*.png` and
`hardware-rtg16.png`; native test output is `hardware-presentation-tests.txt`.

A separate NTSC 68020/AGA emulator, with 2 MiB Chip and 8 MiB Fast, boots the
68000 binary from the ECS ADF. Launch, F5 save, F9 restore and Escape to the boot
CLI were inspected. Native save now sends ACTION_FLUSH before takeover so DOS
can commit cached disk writes. The resulting commander was extracted from the
OFS disk and its portable checksum checked. Escape disables all audio DMA.
Screenshots are `hardware-adf-ntsc-{launch,save,load,exit}.png`. This validates ECS
output under AGA emulation; the attempted 68000/Generic ROM configuration did
not reach the game, so an emulated stock 68000 boot is not claimed.

The accelerated emulator reports C2P to Chip RAM at 1406/521 microseconds for
320 x 200 depth 5/8 and 709/809 microseconds for 320 x 256 depth 5/8. Orbit render
is 3427 microseconds (hash 3e03e20f); surface render is 39352 microseconds (hash
ae1ab045). These are one-run figures with warm-up/JIT effects, not a comparative
CPU ranking. Evidence: `hardware-benchmark-020.txt`.
The docked game profile measured AGA 320 x 200 at 3666 microseconds render and
20500 microseconds output+sync (CIA tick resolution); RTG16 640 x 480 measured
30756 and 5417 microseconds with EClock. They are different workloads/backends.
The current host orbit frame matches the pre-optimization renderer hash
3e03e20f; a single host run measured 0.184 ms versus 0.197 ms for the baseline,
which is not sufficient evidence for a universal speedup.

No physical PAL/NTSC machine, real CD32/analogue controller, other graphics card,
stock A500 frame rate or long-duration audio/display stress result is claimed.

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

## Planet rotation and weather (2026-10-13)

Continues from `7559bc89027f93d72a583f1ccb9e5f5e42c70f2b`. The audit found
analytic inclined planetary and parented moon orbits, a central/binary star model,
translated local frames, landable terrain, single moons and simple rings already
implemented. Surface features did not rotate. Globe lighting was cached for an
hour, clouds followed the camera, and low-precision forest placement could bury
vegetation beneath the collision terrain.

The new model rotates body-fixed terrain/materials, flora, landing pads and
surface ports using analytic Q30 rotation at simulation time. Ports include their
rotational velocity. Landed ships use a transient body-fixed anchor to avoid
cumulative rounding drift; launch inherits ground velocity. Atmospheric assist,
drag/heating, open-ground landing and survey speeds account for rotating ground.
The existing 1/32-second physics, translated orbital frames and safe warp remain.
Extra gas-giant moons are appended without changing existing body/port indices.
Gas giants have separated equatorial ring bands and body-fixed band/longitude
colour variation. Globe geometry/materials are cached; illumination is live.

Visible stellar geometry supplies hemisphere lighting and planet/moon eclipses.
The indexed atmosphere approximates Rayleigh single scattering with the angular
`1 + cos^2` phase, horizon path enhancement and a blue palette. Stars below the
local horizon or occulted by another body do not contribute. A shared deterministic
cloud field supplies a curved deck and cockpit fog when the ship enters it.
Temperate/frozen precipitation uses climate, body-fixed latitude and altitude;
clouds have slow simulation-time drift. Flora uses the same immutable body-fixed
terrain and full-precision radial placement. Work is bounded: an 8 x 8 local
cloud patch, a 9 x 9 flora lattice, 24 ECS/48 enhanced precipitation particles,
and two-pixel fog blocks with clock servicing. These are original generated visuals.

Save format 5 retains the market/exploration payload and reads versions 3 and 4.
Older open-ground landings migrate to rotating terrain; docked commanders follow
their regenerated port. Existing generated body indices, settlement and market
indices are retained. New saves cannot be opened by the older executable.

Validation:

- `make test` and `make sanitize`: core, presentation and renderer suites pass.
  Added checks cover long-period moon half/full turns without angle overflow,
  body-fixed height/material sampling and inverse rotation,
  moving-port derivatives, anchored ground/save migration, moon landing/launch,
  day/night and eclipses, scattering, actual fixed-tick flight through clouds,
  rain/snow/vacuum boundaries, flora geometry and visible fog entry/exit.
  Existing 58 port arrivals, all six body types, mission completion, swept
  collisions, warp/braking, docking clearance and save-corruption checks pass.
- `make navigation-test`: AU-scale arrival remains 8,492,040 ticks, 67% fuel
  and 4 m error. This is simulation evidence, not physical hardware evidence.
- `./build-amiga.sh -j4 all variants tests`: SDL/OpenGL-free, soft-float
  68000/020/030/040/060 games and native suites build successfully.
- `make all preview` and `make environment-preview`: host surface/orbit output,
  320 x 256 weather/rotation fixtures and 640 x 480 RTG-size fixtures render.
  Rain, flora, inside/above cloud, clear polar snowfall, night and quarter-turn
  frames were inspected. `./build/environment-preview --rtg` generates the
  larger fixtures. Frames and portable saves are `build/evidence/environment-*`.
- Host orbit benchmark: about 0.453 ms/frame over 200 frames, hash `e3347ecc`.
  Live geometry/lighting costs more than the previous roughly 0.2 ms/frame;
  caching material classifications reduced an initial 0.752 ms/frame result.
  No Amiga frame rate is inferred from these host timings.
- Isolated PAL 68020/AGA-configured Amiberry, with a distinct runtime/socket,
  runs `Space-RunTests` with 256 KiB parent/child stacks. Native presentation
  and core quick suites pass. ECS low-resolution screenshots show rain, cloud
  fog, the deck from above and clear polar snowfall. Native F5 resumes with
  SAVED; the written version 5 commander has a verified payload/checksum.
  Escape restores the CLI and DMA `03F0`.
  Evidence is `environment-native-ecs-*.png` and native suite logs under
  `build/evidence/`. The user's existing AmiKit process was preserved.
- `./package.sh` builds and verifies ZIP/LHA contents and the bootable ADF;
  `Space.guide` passes the AmigaGuide checker without warnings/errors.

Limits: this uses common system-frame Z spin axes, type-assigned climates and
circular orbits, rather than per-body obliquity, stellar thermodynamics or an
N-body solver. Satellite spin periods match their orbital periods but do not
constitute a full tidal/orientation model. Weather is a sparse procedural visual
model, without fluid dynamics, accumulation, wind forces or hydrology. Flora and
buildings remain decorative geometry. Equal-strength direct stellar contributions
are a lighting approximation, not a radiometric atmosphere model. The bare isolated
boot did not reach the AGA display after its initial hardware-detection refusal;
new AGA/RTG weather runtime checks are not claimed. They retain build/host coverage
and the earlier presentation evidence above. Physical hardware, stock 68000 weather
frame rates and long-duration runtime stress remain unverified. The previously
recorded direct bare-CLI stack-swap exit fault remains toolchain QA.


## Procedural surface reflectance (13 October 2026)

- `make test` and `make sanitize` pass, including new geology diversity,
  body-space scale invariance, shared cache boundaries, face-edge continuity and
  radius/type invalidation checks. Existing coastline, camera-motion, collision,
  navigation, golden generation and save regressions remain intact.
- `./build-amiga.sh` builds SDL/OpenGL-free soft-float 68000 and 68020 executables.
- `make surface-preview` produces matched ECS/AGA rock, dune, ice and crater
  frames under `build/evidence/surface-*`. Daylight AGA dune/crater/ice frames
  were visually inspected. These body-type fixtures intentionally disable weather.
- `make terrain-detail-preview` measures warmed host frames: ECS 4.81–5.90 ms,
  AGA 5.33–6.44 ms and 640 x 480 RTG 14.53–17.27 ms, at 30/120/500/1500 m.
  Measurements are host CPU timings, not target frame rates.
- Local reflectance is generated lazily into 64 m bilinear samples only when
  visible/resolved. The cache grows by 48 KiB, with another 6732 bytes for
  orbital deposits. Features modify reflectance, not collision geometry.

New native emulator/hardware runtime and performance have not been tested for
this surface change. Earlier native evidence above does not validate this revision.

## System Drive (2026-10-13)

All hulls now have a field drive with 4,000,000 m/s² acceleration/braking and
200,000,000 m/s maximum commanded cruise speed. A engages manual cruise at full
set speed; W/S adjusts it, and A again commands a stop. Autopilot selects it for
clear legs beyond 20,000 km, retaining ordinary engines for terminal approaches,
atmospheric departure, terrain/star detours and docking. Safety shells around all
bodies, stars, ports and active ships reduce cruise speed continuously. Warp remains
separate; drive cruise uses at most one-second intervals with bounded travel,
orbit-motion and braking checks. Local physics and collision sweeps remain fixed
at 1/32 second. The HUD identifies manual/automatic drive operation and uses AU,
km and km/s where large cruise values would overflow a native 32-bit display value.

- `make test navigation-test`: full core/presentation/renderer suites pass, including
  58 generated port arrivals and all body types. New tests cover rapid manual
  acceleration, disengagement braking, docked/atmospheric/traffic locks, high-speed
  AI avoidance arithmetic and cruise-velocity save/load without changing format 5.
- The same AU-scale route now arrives in 166,604 ticks (86.77 simulated minutes),
  with 100% displayed fuel, full hull and 4 m waypoint error, versus the previously
  recorded 8,492,040 ticks: about 51 times fewer simulation ticks. Atmospheric and
  terminal legs still use ordinary engines. This is not a native wall-time speedup.
- At 4096x, remote drive cruise differs from fixed ticks by 2,343,648 m in position
  and about 1 m/s in velocity after 128 simulated seconds. The position bound is
  derived from the end-velocity versus mean-velocity integration difference at the
  drive's acceleration; conservative cruise safety bounds cover that difference.
- `make sanitize`: full core, presentation, renderer and astronomical navigation
  suites pass with AddressSanitizer/UndefinedBehaviorSanitizer.
- `./build-amiga.sh -j4 all variants tests`: 68000/020/030/040/060 soft-float games
  and native tests build. The guide checker reports zero errors or warnings.
- Isolated PAL 68020 Amiberry: native presentation and core quick suites pass,
  including System Drive tests. Live ECS low-resolution flight shows A activating
  DRIVE and acceleration above 8,000 km/s; A again reduces velocity through drive
  braking. Escape restores the CLI and DMA `03F0`. The user's AmiKit instance was
  preserved. Logs/screenshots are `build/evidence/system-drive-*`.

Physical hardware, live AGA/RTG drive flight and a complete interplanetary journey
under native emulation remain untested; those routes have host simulation evidence.

## Remaining career/economy roadmap (2026-10-13)

The software career items now have bounded playable implementations: passenger
cabins and rescue pickup/delivery, hourly crew recruitment/wages and hull crew
requirements, escort/reconnaissance/assassination/installation/multi-leg objectives,
negotiated terms and reputation gates, three original factions with military
service/ranks/medals and separate fines, black markets/customs/police/piracy,
finite asteroids, persistent surface rigs, salvage pickup and hydrogen scooping,
six hulls with distinct geometry/specifications and cargo-dependent acceleration,
separate turret upgrades, missiles, decoys and escape-pod rescue.

- `make test navigation-test` passes the full core, presentation, renderer,
  careers and interplanetary suites. The existing 58 generated port arrivals and
  all six original contract paths remain covered. Career regressions cover
  capacity/refusal paths, reserved seats, hourly wages, gates/negotiation,
  objective completion/failure and payment once, police/pirate encounters,
  swept missile hits, salvage, saved finite deposits, rigs, scooping, pod rescue,
  full exploration/market/deposit ledgers and semantic save rejection.
- `make sanitize` passes with AddressSanitizer/UndefinedBehaviorSanitizer,
  including the career suite and existing astronomical navigation regressions.
- `./build-amiga.sh -j4 all variants tests` builds software-integer
  68000/020/030/040/060 games and native tests without SDL/OpenGL. The test runner
  includes `Space-career-tests.020` with a 256 KiB child stack.
- Save format 6 appends explicit big-endian career fields to the existing
  commander prefix. Formats 3/4/5 migrate with default faction data, hull crew,
  no passenger reservations and preserved legacy turret upgrade levels.
  Malformed checksummed extension data leaves the live commander unchanged.
- New equipment and crew/faction pages are included in compact-layout regression
  comparisons. ECS 320 x 200 previews were visually inspected, along with live
  native menus. The guide checker reports zero errors or warnings.
- Isolated PAL 68020 Amiberry passes presentation, core quick and career suites.
  Live ECS low-resolution interaction confirms crew hire, cabin purchase, both
  board pages and F5/F9 restoration of crew/cabins/credits from a version 6 file.
  Escape returns to the CLI and restores DMA `03F0`. The user's running AmiKit
  instance was left untouched. Evidence is under `build/evidence/careers-*`.
- `./package.sh` produces ZIP/LHA packages with extracted-byte verification and
  the self-booting ECS ADF. No ROM or OS files are included.

These tests establish host and selected ECS emulator behavior, not physical
PAL/NTSC performance, analogue/CD32 devices, other RTG cards, live AGA/RTG career
play or long-duration audio/display lifecycle stability. That hardware QA item
remains unchecked. Careers are bounded templates and local encounters, not a
persistent civilian economy or strategic war simulation.

## Mouse cockpit and GUI (2026-10-13)

- Shared integer layout and pointer capture/hit testing in `src/ui.c`; both SDL
  preview and native ECS/AGA/RTG backends use it. The original console has a
  central elliptical 10 km scanner/weapon panel, raised contextual controls,
  hull/shield/fuel/temperature gauges, drag throttle and safe time selection.
- Clickable markets, contract/equipment/hull lists, crew actions, galaxy tiles
  and true-scale orbital markers; docking, flight, save/load and exit are mouse
  operated. Release outside the pressed widget cancels activation. UI gestures
  cannot start weapon fire. Pointer state is transient and commander formats
  remain unchanged. The pointer stays in place on loading a commander.
- `tests/test_ui.c` exercises purchases/sales, target selection, launch,
  autopilot, warp, pause, weapons-panel switching, throttle, cancelled/disabled
  controls, death/restart, platform requests and transient save state at
  320x200, 320x256 and 640x480. It is part of host/native builds and the native
  `Space-RunTests` wrapper.
- Host core/career/presentation/renderer suites and ASan/UBSan checks pass,
  including astronomical navigation. 68000/020/030/040/060 soft-float binaries
  build with warnings treated as errors. AmigaGuide checker: zero defects.
- Isolated PAL 68020 Amiberry ran the native UI workflow suite and ECS 320x200
  GUI. Actual mouse input opened PORT/Market, purchased and sold cargo, saved
  and restored the commander, and exited to the CLI. Screenshots show cargo
  and credit changes and SAVED/COMMANDER RESTORED feedback. Exit restores
  DMA `03F0` from flight's `03C0`; the user's original AmiKit process remains
  untouched. Evidence: `build/gui-native-*.png`, `build/gui-guest/ui-results.txt`,
  `build/gui-tests.log`, `build/gui-sanitize.log` and `build/gui-amiga-build.log`.

Live RTG mouse/focus/pointer hiding, live AGA GUI, physical controllers/hardware
and real-machine performance remain unverified. Host RTG rendering and shared
mouse tests do not establish those runtime properties.


# Situation-based MOD playlists - 16 October 2026

The synthesized eight-second placeholder score has been removed. User-supplied
MOD files preload from music/battle, planetary-flight, space-flight, docked and
game-over before native takeover. Scene changes and song completion choose random
tracks without immediately repeating the previous selection when alternatives
exist. Empty folders leave music silent; procedural engine/effects remain.

Validation for this change:

- `make test` and `make sanitize` pass. The new MOD fixtures cover scene priority,
  hostile range, menus/landing, note-to-PCM output, case-insensitive MOD names,
  single-track looping, no immediate repeats, chunk/tick/song continuity, pause,
  malformed/truncated files, invalid sample loops, 128-order headers, missing
  folders, and effects with no music. Fixtures are generated only in scratch RAM
  or build directories and are excluded from release music folders.
- `./build-amiga.sh all variants tests` builds the 68000/020/030/040/060 games
  and native tests with warnings treated as errors. No SDL or floating-point
  decoder dependency is added to the Amiga executables.
- Isolated Amiberry 68020/ECS low-resolution playback runs synthetic test MODs
  from PROGDIR:music. Native music and presentation tests pass in the guest.
  In-game Paula channels 0/1/3 have DMA active (DMACON 03CB); pause is visible;
  Escape returns to the CLI with DMACON 03F0 and all four audio DMA channels off.
  Evidence: build/music-guest/music-results.txt, presentation-results.txt and
  build/music-runtime/{playing,paused,exited}.png. This verifies playback state
  and lifecycle, not listening quality, real-hardware timing or a broad MOD corpus.
- The AmigaGuide checker reports no defects. ZIP/LHA archives are extracted and
  byte-verified, including empty situation folders; the boot ADF includes the same
  folder layout, setup notes and Micromod redistribution notice.

Native music uses 256 ms DMA blocks, with no DOS/device/allocation calls during
selection and replay. Frames exceeding the buffer budget can repeat a block.
Real-hardware performance, AGA/RTG interactive playback, and long-duration audio
quality/stability remain unverified. Modules are limited to 128 total, 4 MiB each
and 16 MiB total, subject to memory; display/audio buffers are reserved first.

## Unrestricted time compression (2026-10-16)

Time compression is available in manual flight, combat, local approaches and
while docked or landed. Missiles and autopilot/System Drive toggles preserve the
selected rate. Arrival, docking and insufficient autopilot thrust still reset
the selection to 1x. Pause and death stop simulation. The HUD displays only the
actual multiplier; the existing 128-work-step frame cap remains.

- Host core, navigation, career, UI, presentation, music and renderer suites pass.
  ASan/UBSan checks pass, including a clean full core rerun. New regressions compare
  eight local warp situations against ordinary physics ticks and check the single
  HUD multiplier in PAL/NTSC layouts, including pause.
- All 68000/020/030/040/060 game builds and native test builds pass.
- Isolated Amiberry 68020/ECS compact-display smoke check shows TIME 64X while
  docked, then accelerated manual atmospheric flight after launch. Its docked
  commander save records tick 3856 with autopilot off. Lowering warp to 1x and
  exiting returns to the CLI with DMACON 03F0 and audio DMA off. Screenshots:
  build/evidence/warp-native-{docked64,manual64,return1,cli}.png.
- AmigaGuide validation and git diff whitespace checks pass. Interactive AGA/RTG
  warp and real-hardware performance remain unverified.


## Independent System Drive and autopilot (2026-10-16)

A/DRIVE and P/AUTO preserve each other's switch state. Autopilot uses ordinary
engines unless the player arms System Drive. Guidance keeps detour/approach
speed limits, and body arrival zeros commanded speed while leaving the drive
armed. Disengaged cruise momentum still retains powerful braking.

- Host core, UI, career, music, presentation, renderer and extended navigation
  suites pass, including physics checks for all four switch combinations and
  drive-armed body arrival. Hetha D arrives after 206426 ticks and the second
  interplanetary route after 168048 ticks, both within 4 m of the waypoint.
  Detours and terminal approaches retain ordinary thrust authority to avoid
  high-acceleration capture oscillation while the drive switch stays armed.
- All 68000/020/030/040/060 game builds and native test builds pass.
- Isolated Amiberry 68020/ECS core quick and UI tests pass. Paused cockpit
  screenshots confirm AUTO alone, AUTO plus DRIVE, DRIVE after disabling AUTO,
  and AUTO after disabling DRIVE. The game exits to the CLI with DMACON 03F0
  and audio DMA off. Evidence is in build/independent-drive-runtime and native
  results in build/independent-drive-guest/{core,ui}-results.txt.
- AmigaGuide validation passes. AGA/RTG interactive and real-hardware control
  checks remain unverified.

# Physical planetary landforms - 16 October 2026

The common shallow angular height field has been replaced by deterministic
body-fixed geology in metres. Rocky worlds have folded ranges, hills, valleys
and seeded volcanic cones/calderas. Deserts have elevated plateaus cut by winding
canyons; ice worlds have glacial uplands, pressure ridges and rifts. Moons have
excavated crater bowls with raised rims and optional volcanic provinces. Ocean
worlds preserve sea-level water and coasts with inland relief. Gas giants remain
without solid relief. Volcanoes are static geometry, without lava or eruptions.

Elevations are bounded to 8 km. Rendering and collision still share the fixed
256 m cube-face triangles. Pad transitions use accurate metre distances and
smooth blending across 1-3 km, avoiding the old angularly quantized steps.
The detailed mesh now covers viewpoints up to 20 km above sea level, with a
larger horizon span and facet lighting beyond the texture range. Near texture
lighting blends into facet lighting. No additional per-pixel terrain noise is
introduced. There is still a discrete globe/detail transition at 20 km.

Altitude queries and collision broad phases now include tall peaks. Autopilot
climbs relative to rotating ground and uses a clear overhead waypoint before
surface-port descent; a loaded-freighter regression exposed the old tangent-only
approach circling above its destination. Saves retain format 6: buried airborne
commanders are raised to 50 m clearance, and landed commanders reattach at 12 m.
Unburied airborne saves retain position and velocity.

Evidence and reproduction:

- `make landform-preview` produces five real generated world fixtures, each from
  two angles at ECS/AGA 320 x 256 and RTG 640 x 480, plus portable commanders in
  `build/evidence/landform-*`. Fixture selection searches natural slopes rather
  than changing the world's type or seed after generation. Host timings include
  rendering and are workload/machine dependent, not native frame-rate claims.
- The dedicated regression samples rock/ocean/desert/ice/gas/moon terrain over
  three seeds and a 131 km region. Observed maxima were 7,120 / 3,628 / 6,645 /
  3,658 / 0 / 4,708 m. It covers smooth neighbouring samples, flattened pads,
  high-peak altitude/collision and buried/landed save recovery.
- Renderer coverage retains 32 camera angles, moving material boundaries,
  clipmap seams/recentering, cache revisits and 120 m render/collision agreement.
  Material assertions allow physical slope illumination within the same ramp.
- Native 68000/020/030/040/060 games and test executables build with warnings as
  errors through `./build-amiga.sh -j4 all variants tests`.
- AmiKit/Amiberry loaded the host-generated mountain commander in native ECS,
  AGA and RTG. `build/evidence/landform-native-{ecs,aga,rtg}.png` show the same
  ridgelines at approximately 2,200 m ground clearance; RTG is 640 x 480.
  Audio was disabled for these visual checks. A pre-test emulator snapshot
  preserves the original live session.

Physical hardware, volcano eruptions, and real-machine performance remain
unverified. Host/emulator frames establish visible geometry, not a hardware
performance budget.

Final checks passed: `make -j4 test navigation-test`,
`UBSAN_OPTIONS=halt_on_error=1 make sanitize`, native 020 quick core tests with an
explicit 262144-byte CLI stack, the AmigaGuide checker (zero diagnostics), and
`./package.sh` ZIP/LHA/boot-ADF readback. The native terrain golden ranges matched
the host. The new test fixture initially lacked `parent=-1`, causing a divide by
zero in rotation setup; this was corrected before the final passing runs.

AmiKit exited during an attempted IPC restore of the pre-test snapshot. It was
relaunched successfully; exact live-session restoration was not established.
The snapshot/config remain under `build/evidence/terrain-before-native.*`.
The final five CPU executables were installed in `/Volumes/repos/AmigaGPT/out`,
with previous installed binaries backed up under
`build/evidence/out-programs-before-terrain`. No commander slots were replaced.

### Roll steering (2026-10-16)

Horizontal arrows, right-drag mouse movement, digital joystick/CD32 directions
and calibrated analogue X now control roll. Vertical input pitches about the
banked hull axis; roll alone preserves the nose direction. Front/rear cameras,
terrain ray inversion and the external hull mesh include bank. Turret steering
continues to aim independently. Autopilot levels the hull in local and compressed
cruise steps. Commander format 7 appends roll; formats 3 through 6 load level.

- `make -j4 test navigation-test all` passed, including banked/inverted pitch,
  pole crossing, analogue fractional roll, pause/turret isolation, saved bank,
  v3/v4/v5/v6 migration and rolled target-marker/terrain-ray rendering.
- `make sanitize` passed all suites with no AddressSanitizer or undefined
  behaviour diagnostics.
- `./build-amiga.sh -j4 all variants tests` passed for 68000/020/030/040/060.
- Isolated PAL ECS Amiberry with a 68020 and 262144-byte CLI stack passed the
  native presentation tests and core `--quick` suite. Right-arrow steering
  visibly banked the launched ship; Escape returned to the guest shell.
  Evidence: `build/roll-native-{launched,banked,exit}.png` and
  `build/roll-guest/{presentation,core}-results.txt`.
- AmigaGuide validation passed with zero diagnostics. ZIP/LHA/boot ADF packages
  were rebuilt and read back successfully. The five installed `OUT:Space.*`
  executables were refreshed, with previous binaries backed up under
  `build/evidence/out-programs-before-roll`.

Physical controllers, real hardware, and live AGA/RTG roll input remain untested.

### Inverted vertical steering (2026-10-16)

Down now pitches up and up pitches down for keyboard arrows, right-drag mouse,
digital joystick/CD32 directions, SDL joystick axes/hats and calibrated analogue
controllers. Turret aiming uses the same inversion; roll mapping is unchanged.

Host presentation tests and the core `--quick` suite passed. Native
68000/020/030/040/060 builds and the guide checker passed, and ZIP/LHA/boot ADF
packages were rebuilt. Isolated ECS/68020 emulation passed native presentation
and core smoke tests. Live Down/Up arrows produced saved pitch values +39/-45,
confirming nose-up/nose-down respectively; see `build/invert-y-native-pitch.txt`
and `build/invert-y-native-down.png`. Installed `OUT:Space.*` binaries were
updated with previous copies in `build/evidence/out-programs-before-invert-y`.
Physical controller and live AGA/RTG input checks remain untested.

### FPU builds (2026-10-16)

`./build-amiga.sh all variants benchmarks tests release-tools` now produces
`Space.000`, `Space.020` and `Space.030` (soft float) plus `Space.020fpu`
(68020/030 + 68881/2), `Space.040` and `Space.060` (`-m68881`; this GCC defaults
to soft float even for `-m68040`/`-m68060`). Previously `Space.030` and
`Space.040` linked the 68000 libgcc multilib; 030/040 now link the 020 libraries,
and 060 links the 060 libgcc so no 64-bit MULU/DIVU forms (which the 68060 traps)
are present. No FPU object references libgcc soft-float helpers; the 040 build
contains no FINTRZ and none of the FPU builds use transcendental instructions.

FPU builds use an FSQRT-plus-correction `isqrt64` (bit-identical to the
integer loop, checked by `test_isqrt` over edge cases and 200000 random values),
reciprocal projection in `emit`, FPU facet lighting and a cached ray matrix for
terrain colour lookup. All builds now interpolate textured spans and scanline
edges in 32 bits when exact, instead of a 64-bit division per pixel.

`make test` and `make fpu-test` (host suites with `-DSPACE_FPU`) pass, including
the terrain colour stability tests. The host orbit benchmark hash is unchanged
(`981fbe0b`) for the integer and FPU paths.

`Space-benchmark.<cpu>` was run from the AmiKit shell (emulated 68040 + FPU at
maximum speed, so only the ratios matter). Surface render, ms per frame:

| Build | Before | After | Surface hash |
| --- | --- | --- | --- |
| 000 | 150.4 | 131.9 | `9026613f` (unchanged) |
| 020 | 91.5 | 90.8 | `9026613f` (unchanged) |
| 030 | — | 89.5 | `9026613f` |
| 020fpu | — | 64.2 | `4218d80c` |
| 040 | 94.8 (soft float) | 69.0 | `4218d80c` |
| 060 | — | 99.9 | `4218d80c` |

The 060 figure is not representative: the emulated 68040 traps the 060's FINTRZ to
software. Orbit and C2P hashes are unchanged on every build. Cycle-exact 6888x
timing and real 6888x/040/060 hardware remain unverified.

### Distant terrain material LOD (2026-10-16)

Flight between about 5 and 20 km was several times slower than low flight. A
frame touched up to 31,000 distinct 256 m material tiles (about one per terrain
pixel), each a full five-field geology evaluation, overflowing the 4096-slot
cache every frame. Beyond 2 km, where the renderer already skips texturing, the
material tile now doubles in size (up to 64 km) until it spans at least a third
of a pixel. Pixel scale is estimated from distance, altitude-based grazing and
planet curvature, as per-frame reciprocal-depth thresholds. Nearby and textured
ground still uses 256 m tiles.

Host: 5–20 km frames fell from 10–29 ms to 4.5–6.5 ms, with at most 2,830 material
evaluations per frame; at most 31 of 64,000 viewport pixels changed in any tested
view. `make test` and `make fpu-test` pass. `Space-benchmark` gained a 12 km
"high terrain" scene. In AmiKit (ms/frame, before → after, two runs for 000/020):

| Build | 12 km terrain | 220 m surface |
| --- | --- | --- |
| 000 | 522–649 → 149–163 | 125 → 125–130 |
| 020 | 403–438 → 115–122 | 91–96 → 95–97 |
| 020fpu | 355–360 → 86–93 | 70–73 → 67–75 |
| 040 | 381 → 96 | 77 → 77 |

### Autopilot drive and atmospheric pad approach (2026-10-16)

Autopilot treated the point 10 km above a surface pad as a detour, so an armed
drive never engaged on a surface-port route and the trip ran at the 750 km/s
ordinary cap with no cruise compression. Route waypoints now report whether they
are detours; the station corridor and the point above a pad count as the
destination for the drive, cruise intervals and destination motion matching. The
drive hands over to ordinary engines 1,000 km out instead of 20,000 km.

Deep in the air with the pad below the horizon, the climb waypoint restored the
line of sight to the overhead point within seconds; the two then alternated and
swung the nose by up to ~45° every few seconds. Within sight-line reach of the
climb ceiling, the autopilot now glides along the tangent to the sight-line shell
(1 km above the highest terrain), whose bearing meets the overhead bearing as it
clears. Farther pads keep the climb-out route. The nose also holds within 100 m
of a gate or pad instead of chasing a noisy bearing.

Host: Hetha B launch to Hetha D Reach with the drive armed at 4096x fell from
113,305 frames (14.5M ticks) to 509 frames (127,861 ticks), hull and fuel 100. A
start 26 km up and ten degrees from the pad fell from 8,226 large steering steps
to 0. Hetha D arrives after 96,685 ticks (was 206,012) and the interplanetary route
after 57,825 (was 168,048). `make test`, `make sanitize`, `make fpu-test` and
`make navigation-test` pass. Native emulator/hardware runtime is untested.
