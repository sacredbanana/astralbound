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
