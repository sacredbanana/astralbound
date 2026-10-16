# Space

An original space trading and combat game for the Commodore Amiga, inspired by
the scope and solid-polygon presentation of Frontier: Elite II. Space's source,
ship geometry, font, names and generated worlds are original and released under
the [Unlicense](LICENSE). Reference screenshots and Frontier assets are not
included in the game.

**Status: playable development prototype, not a finished Frontier-scale game.**
The current build has astronomical-scale moving systems, central/binary stars,
moons and rings, hierarchical flight frames, spherical terrain with continuous LOD
edges, forests, coastlines and generated cities. It includes moving planetary/orbital
ports, station clearance and docking corridors, sparse settlement, exploration
journals, persistent port markets, thirteen contract types, passenger cabins, crew, factions,
police/piracy, mining, six hulls, missiles and portable commander saves.

## Amiga builds

```sh
./build-amiga.sh
./build-amiga.sh tests
```

With no targets the script builds the game for every CPU (`Space.000`, `.020`,
`.030`, `.020fpu`, `.040` and `.060`); named targets build only those. The
script uses an installed `m68k-amigaos-gcc`, `/opt/amiga/bin`, or the
`sacredbanana/amiga-compiler:m68k-amigaos` Docker image. `DEBUG=1` enables debug
builds; `CLEAN=1` rebuilds from scratch. No SDL or OpenGL is linked into the
Amiga executables.

Copy the contents of `dist/amiga` to an Amiga drawer and run from Shell:

```text
Stack 262144
Space.000 --ecs
Space.020 --aga
Space.020 --rtg
Space.020 --rtg16
Space.020 --aga --lowres
```

- **ECS:** PAL 320 x 256 or NTSC 320 x 200, 32 colours, double-buffered bitplanes and a private Copper
  list. Requires Kickstart 3.0+ in this prototype, including on a 68000 machine.
- **AGA:** PAL 320 x 256 or NTSC 320 x 200, 256 colours from the 24-bit palette, eight bitplanes,
  64-bit display fetches and double buffering. Use the 68020 executable.
- **RTG:** 640 x 480, indexed 8-bit or `--rtg16` high-colour output, driver-managed double buffering. Requires
  CyberGraphX V41+ or Picasso96's compatible CGX API. RTG deliberately keeps the
  OS running because portable graphics-card access depends on its driver.

Native modes allocate all display memory before taking over, suspend scheduling
and interrupts, and poll the hardware keyboard. Saving and loading temporarily
restore the OS. Escape restores the display, DMA, interrupts and task scheduling.
The prototype needs expansion RAM: its C renderer and 64-bit coordinate maths
are not yet tuned to stock A500 performance. The depth buffer uses an additional
250 KiB at 320 x 256, 180 KiB at 320 x 200, or 1060 KiB in RTG, allocated
before native takeover and freed on exit. Native output detects the OS video
standard and keeps it: PAL timing is 50 Hz, NTSC is 60 Hz, with the same fixed
physics ticks. `--lowres` selects a centred 320 x 200 display on PAL, reducing
framebuffer/bitplane work by 22% and flight viewport pixels by 28%. It is native
only; NTSC already uses 200 lines. Compact menus use the whole screen to keep
all rows visible. Real-machine speed gains remain unmeasured.

## Host development preview

Requires a C99 compiler and SDL2 with `sdl2-config` in PATH.

```sh
make
./build/space
make test
make sanitize
make navigation-test  # longer, full interplanetary transfer
make preview
```

`--render FILE.ppm --surface` and `--render FILE.ppm --orbit` write deterministic
frames without opening a window. `--render FILE.ppm --city-overhead --rtg`
reproduces a downward view of the starting city at 1232 metres in RTG resolution;
use `--ecs` for the reduced palette. `--lowres` or `--ntsc` previews the 320 x 200
layout on the host; these flags cannot be combined with `--rtg`. The host and Amiga use the same simulation and
renderer. `Space-tests.020` runs the full core suite on an Amiga;
`Space-tests.020 --quick` runs the shorter native smoke suite.
`Space-career-tests.020` runs the careers/resources/equipment regressions.
`Space-ui-tests.020` exercises mouse workflows at all three display layouts.
`Space-RunTests` starts the presentation/core/UI/career suites with an explicit 256 KiB
child stack and writes their result files; use it in a bare boot CLI. Full trajectories
remain in the default suite, which can take a long time under emulation.

## First flight

Start docked at Haven. Press **L**, then **P**, to travel to the orbital Relay.
Press **T** to cycle ports, **Q** to cycle body waypoints, or **Y** to see the
selected destination list. Up/Down changes its target; return to flight with **V**
and engage **P**. Body waypoints stop above the atmosphere, including at gas giants.
Orbital stations rotate and move: autopilot matches port velocity, waits for clearance,
and enters through the marked corridor. **D** requests clearance for manual docking.
Request time compression with **]**, up to **4,096x** (1, 4, 16, 64, 256, 1,024, 4,096);
**[** lowers the requested rate. Compression works in manual flight, with autopilot,
in combat, near terrain and traffic, and while landed or docked. Firing missiles or
switching autopilot/System Drive preserves the selected rate. Arrival, docking and
loss of autopilot thrust authority return to 1x. Remote, unobstructed interplanetary
cruise uses bounded intervals of up to 128 simulated seconds, with orbital-motion,
travel and braking checks before each interval. Departure, approach, docking and
combat retain 1/32-second physics ticks. Simulation work is capped at 128 steps per
rendered frame; the HUD shows one actual multiplier (for example, **TIME 256X**).
The work cap can reduce the delivered rate in local flight. Pause still stops time.

Every ship carries a **System Drive** for fast travel within a system. Press **A**
to toggle the drive independently of **P** (autopilot): neither control changes
the other. In manual flight, engaging the drive selects full cruise speed; W/S
adjusts the setting, and **A** again brakes to a stop. With autopilot engaged,
guidance controls speed using whichever drive setting you select. The drive
provides up to 4,000,000 m/s² acceleration and braking, with a 200,000,000 m/s cruise ceiling. Gravity wells, atmospheres,
stations and nearby ships reduce the permitted speed. The HUD shows DRIVE,
AUTO DRIVE, or DRIVE LOCK when the drive is armed near an exclusion zone.
Autopilot uses System Drive only when you arm it with **A**; otherwise it uses
ordinary engines. It limits speed for terrain/star detours, atmospheric flight
and docking, and brakes for arrival without changing the drive switch. Reaching
a body waypoint zeros the manual speed setting while leaving the drive armed.
Time warp also works with manual drive control; drive cruise batches are limited
to one simulated second.
Drive settings reset on loading, while saved velocity is preserved and fast
cruise momentum retains powerful braking. All existing commander formats still load.

The mouse console uses an original Frontier-inspired layout: a central elliptical
scanner, instrument banks, raised controls and a persistent navigation strip.
**FLY** returns to the cockpit without cycling the camera; **VIEW** cycles cameras.
**GAL**, **NAV**, **SYS**, **PORT**, **LOG**, **KIT**, **CREW** and **HLP** open their
panels. PORT has Contracts/Market/Shipyard tabs. Click a list row to select it, then
use the labelled action buttons to trade, accept or negotiate jobs, buy equipment,
hire crew or service the ship. Dim controls are unavailable in the current state.
Buttons activate on release inside the same control; dragging away cancels a click.

In flight, click **LAUNCH**, **AUTO**, **DRIVE**, **ASSIST**, **GEAR**, **DOCK** or
**LOCK** to operate the ship. Drag the cyan throttle strip; **STOP** commands zero
speed. The time strip selects 1/4/16/64/256/1024/4096x while retaining the existing
safety limits; the cockpit shows delivered/requested compression. **PAUSE/PLAY**
works with the mouse. Click the scanner to show missile/decoy controls and click
its background again to return. In SYS, click a body or station marker to choose
that navigation target; overlapping distant markers are resolved through NAV's
list and BODY/PORT/PAGE buttons. GAL's nine sector tiles are clickable. SAVE and
LOAD are available in the panels. Mouse UI state is not stored in a
commander. All keyboard shortcuts remain available.

For manual flight, hold the **right mouse button** and drag to steer. Drag right/left
for roll and up/down for pitch; the mouse acts as a virtual stick with a small dead
zone. Release the right button to recenter. **Left mouse button** fires when pressed in the flight viewport. Console and panel
clicks never fire weapons. On Amiga,
use the mouse port and a standard digital joystick in **joystick port 2**; directions
steer and its fire button shoots. The host preview uses the first attached joystick
or gamepad. Standard SDL controllers use proportional left-stick steering, A/B/X/Y
for fire/brake/contact/autopilot, shoulders for thrust/reverse and Start to pause.
Unmapped joysticks retain axes 0/1 and button 0 fire. Keyboard arrows,
Ctrl and W/S remain available; W/S controls speed/thrust with either device.
Horizontal steering banks the ship; pitch follows the bank. Vertical controls
are inverted: press down or pull the mouse/stick down to pitch up; push up to
pitch down. This also applies to turret aiming.
Steering also aims the turret in turret views. Autopilot retains steering until P
turns it off; time compression remains limited to autopilot flight.

At a port, **B** opens contracts, **M** opens the market and **U** opens the
shipyard. **J** lists contracts and destinations. **G** opens the galaxy map;
select a sector using arrows and press **X** above 12 km altitude to jump. **C**
centres the map on the current system. Jump range is six one-light-year sectors.
Star occupancy follows a four-arm galactic disc; empty sectors reject jumps. Green
map markers identify visited systems. Unsettled systems have body targets and no
markets or contract boards. The commander journal preserves up to 128 systems with
visited/surveyed body masks. Close approaches below 100 km mark a body surveyed.

Distances are metres, radii are kilometres, and planetary spacing is in AU. System Drive greatly shortens interplanetary journeys; local atmospheric
and docking legs retain ordinary flight speeds. Remote cruise batches time
without running thousands of local physics ticks between rendered frames.
Fuel follows engine impulse, so small cruise corrections cost less than full thrust.
Delivery deadlines allow six to eight simulated hours for the longer local journeys.

V cycles front, rear, external, dorsal turret and ventral turret views while
already in flight. In either turret view, arrows aim independently of the hull,
and Ctrl fires along the turret aim; W/S still controls ship thrust. Turrets have
full yaw and pitch limited just short of the poles. Other views fire the forward
laser. Turret aim and contact selection reset on loading a commander.

R cycles active contacts within the 10 km radar sphere. Red contacts are hostile,
green contacts friendly, and gold marks the selected contact. The radar follows
hull heading, with forward at the top and vertical stems showing height. The
selected contact has a diamond, hull, range and signed closing speed; losing the
contact removes its marker. Navigation targets and autopilot remain independent.

Z opens the live top-down orbital map. C toggles system scale and local scale
around the selected body (or its parent for a moon); Q/T and Up/Down select the
same navigation destinations as Y. Moon and station separations are visible at
local scale. White marks stars, green inhabited bodies, cyan other bodies, gold
the selected destination, and red the ship. Orbits are projected at true scale;
small bodies and ports can overlap in the system view. I opens system/body data:
stars, economy, government, radius, gravity, atmosphere, rings, parent, orbit,
period, inclination, ports, survey status and navigation range. Y retains the
destination list; V returns to flight.

Flight controls: left/right arrows roll, up/down pitch, W/S change set speed, Ctrl fires, E switches flight
assist, K controls landing gear, Space pauses, H opens help, F5 saves, F9 loads,
and Escape opens a paused quit dialog: **Save & quit** (S), **Quit without saving**
(Q), or **Cancel** (Escape/C). A failed save keeps the dialog open.
At ports F refuels and repairs, O upgrades the laser, and N
buys a larger hull. On the market, 1-8 selects a commodity and +/- buys/sells. Both buy and sell
quotes are shown. Each port has independent supply: purchases raise scarcity prices,
while sales increase stock and lower prices. Supply returns towards its generated
level by one tonne per simulated hour, including while travelling elsewhere.
Up to 128 traded ports are preserved in the commander ledger across jumps and saves;
when it fills, existing markets remain usable, but trading at new ports is refused.
On the contract board, 1-8 selects an offer and Return accepts it.

## Contracts

Courier and freight jobs require docking at the named destination port in the
specified sector. Cargo space is reserved when accepting freight. Orbital
patrols and bounties create a hostile above 13 km. Air patrols create a hostile
between 600 and 6000 m. Survey work requires twenty simulated seconds between
500 and 6000 m at less than 200 m/s over the rotating ground. Combat jobs pay on disabling their target;
delivery and survey jobs pay automatically on completion. Rewards are paid once.

## Careers, resources and equipment

Press **B again on the board** to switch between two sets of offers. **+** negotiates
once at reputation 3 or higher: 20% more pay for 25% less time. The board shows
required reputation, cargo tonnes and passengers. All deadlines use simulated time.
Passenger transport reserves cabin seats through delivery; each 600 CR cabin adds
two seats and occupies two tonnes. Rescue reserves one seat, creates a distress pod
above 13 km, and pays after pickup with N and delivery to its named port. Escort
requires staying within 5 km of a civilian for 120 seconds while defending it from
raiders. Its destruction fails the job. Reconnaissance requires 30 seconds within
10 km of the named surface port, at 500–6000 m altitude and below 200 m/s relative
to the ground. Installation attacks create a separate ground target near the named
surface port. Assassination pays on target destruction and creates a 1000 CR fine
with a rival faction. Multi-leg deliveries visit three successive ports in the named
system and pay after the final leg; J shows the current port and leg.

**9** opens crew and faction records. **+** recruits for 200 CR; **-** dismisses.
Crew cost 25 CR each per simulated hour, including compressed flight and jumps.
Unpaid crew leave. Launch requires the hull's minimum crew. Completed jobs raise
reputation; failure lowers it. Military jobs advance service, six ranks and medals
at every five completions. The Aurora Union, Meridian League and Free Reach keep
separate reputation and fines. **F on this page** pays the current faction's fine.

Government 2/3 restricts crystals; government 3 also restricts circuits. **B in the
market** toggles the black market at lawless ports or with local reputation 5+.
Restricted goods cost 50% more there. Customs on departure, arrival and nearby
patrol scans confiscates contraband and fines 100 CR per tonne. Wanted commanders
attract armed police near ports; cargo attracts pirates in lawless orbital space.

**U** opens the six-hull catalogue: **1–6 then Return** buys a hull. Hulls differ in
shape, mass, cargo, crew, acceleration, turret mounts and missile capacity. Cargo
mass reduces ordinary acceleration. A purchase refuses incompatible cargo,
missiles, crew or mounted turrets; sell unsupported equipment first. **0** opens
equipment: **1–8 then Return** buys, **-** sells at half price. **O** upgrades the
selected dorsal/ventral mount or the forward laser otherwise, for 2000 CR per
level (three levels). The starting Cinder has both turret mounts. **R** selects a
radar contact, **comma** launches a homing missile, **period** releases a decoy that
suppresses nearby hostile fire for 12 seconds, and **slash** uses an escape pod.
Pod rescue takes one simulated hour, loses hull/cargo/contracts/crew/equipment,
and returns a loaned Cinder at the nearest inhabited port. Use it before hull
reaches zero; unsettled systems have no rescue port.

**Backslash** cycles eight asteroid waypoints above the first body. P approaches
400 m behind the selected rock. Aim at the rock and press **O** with mining gear
to release ore; approach within 250 m and below 50 m/s relative speed, then **N**
collects it. Rocks have collision geometry and four tonnes each. Depletion survives
jumps/saves in a 32-system ledger; mining a new system is refused when it fills.
Destroyed hostiles also leave collectible salvage. Cargo pods are local encounters
and disappear on jumping; collected cargo and rescued passengers travel with you.

On open ground, **O** deploys a 500 CR mining rig; **N** recovers it within 500 m of
the deployment site. Up to four rigs persist across sectors/saves, producing one
tonne of alloys per simulated hour up to 20 tonnes. Recovery loads available hold
space; an emptied rig is removed and sold for 250 CR. A fuel scoop gathers one
fuel point per second in the upper two thirds of a hydrogen atmosphere at
50–800 m/s relative air speed. Docking, combat, pickups and missiles retain fixed
1/32-second physics. These are bounded game careers and local encounters; the
civilian economy and strategic faction wars are not simulated.

Read [Space.guide](Space.guide) on the Amiga for the controls and prototype limits.
Save format 7 preserves ship roll as well as crew, cabins, mount upgrades, factions, legal status,
contract stages, cargo pods, missiles, mining rigs and finite asteroid deposits,
alongside exploration, markets, frame, fuel-impulse and approach state. Formats
3, 4 and 5 migrate with default faction records and enough crew for their hull.
Formats 3 through 6 load with zero bank. World generation stays at version 3. Version 3 commanders load
with their saved stock assigned to the docked port; other ports start with generated
supply. Version 4 commanders also load; older open-ground landings are reattached
to rotating terrain and docked ships follow their ports. Version 2 remains incompatible.

Research sources are in [docs/research.md](docs/research.md). Remaining full-game
work is tracked in [docs/roadmap.md](docs/roadmap.md), and current test evidence in
[docs/validation.md](docs/validation.md).

## Sound, controllers and release files

Music comes from user-supplied ProTracker MODs in the `music` drawer beside the
executable. Add tracks directly to `battle`, `planetary-flight`, `space-flight`,
`docked`, and `game-over`. The host preview also checks the repository's `music`
folder when launched there. Both `tune.mod` and `MOD.tune` names are recognized,
case-insensitively. No placeholder music or music assets are bundled.

The game randomly chooses a track on situation changes and at song end, avoiding
the previous track in that folder when alternatives exist. A single track repeats;
an empty folder leaves music silent while engine/effects continue. Battle music
has priority when a living hostile is within 12 km. Planetary music applies below
12 km above terrain, including moons; docking/landing and death have their own
folders. Menus retain the current situation. Pause mutes and holds playback;
simulation time compression does not accelerate it.

Micromod supplies integer MOD replay without SDL in native builds. All modules
preload before ECS/AGA takeover: up to 128 tracks, 4 MiB per file and 16 MiB total,
subject to available RAM. Invalid/truncated files are skipped. Add music before
launching; see [music/README.txt](music/README.txt) for format support and limits.
Native ECS/AGA reserves Paula channels for two buffered music outputs, effects
and engine. RTG queues audio.device buffers; the host uses SDL audio. Native music
is serviced by polling DMA completion, with 256 ms blocks; frames that exceed a
block can cause repetition. Real-hardware playback/performance remains unverified.
Native saves flush the filesystem before takeover and resume preloaded music.
Allocation failure permits silent play; `--nosound` disables all audio.

CD32 controllers require lowlevel.library V40+: red fires, blue brakes (sets commanded
speed to zero), yellow cycles radar contacts, green toggles autopilot, forward/reverse
change thrust, and Play pauses. Basic digital port-2 sticks still work without that
library. `--analog` selects a physical proportional joystick using POT counters,
reserves its resource bits and calibrates the released stick at startup. Press C
with the stick centred to recalibrate; C is the camera key when this option is absent.
Analogue steering preserves fractional turn rates. Physical analogue and CD32 devices
remain unverified. Poseidon analogue reports are accepted in RTG, where USB tasks run.

`./build-amiga.sh all variants tests benchmarks release-tools` builds the CPU
executables, test programs, native benchmarks and `Space-Icons`. Choose the binary
for the installed processor:

| Executable | CPU | FPU |
| --- | --- | --- |
| `Space.000` | 68000 or newer | none |
| `Space.020`, `Space.030` | 68020/68030 or newer, including LC/EC040 and LC/EC060 | none |
| `Space.020fpu` | 68020/68030 | 68881/68882 required |
| `Space.040` | full 68040 | built-in FPU required |
| `Space.060` | full 68060 | built-in FPU required |

The FPU builds replace the renderer's chains of 64-bit libgcc divisions and the
bit-by-bit integer square root with FPU arithmetic. Square roots are corrected to
the exact integer result, so simulation, saves and generated worlds are identical on
every build; only rendered pixels may round differently. The 030/040 builds link the
020 libgcc instead of the 68000 fallback, and the 060 build uses the 060 libgcc, which
avoids the 64-bit MULU/DIVU forms the 68060 traps. `make fpu-test` runs the host
suites through the FPU code paths.
`Space-benchmark.<cpu>` measures C2P into Chip RAM and orbit/surface rendering.
`Space.020 --aga --lowres --profile` exits after 120 frames and reports render and
output/synchronization times; native uses CIA display ticks, RTG uses EClock.
Emulated acceleration does not establish physical-machine frame rates.

Run `./package.sh` for ZIP/LHA drawer packages and `dist/Space-0.1-boot.adf`.
ADF generation needs amitools' `xdftool` (`XDFTOOL=/path/to/xdftool`) and vasm;
`build/release-venv/bin/xdftool` is also recognized. On Workbench, open Install
using the system Installer (V42.12+), choose a destination, then use a CPU icon
or Play-AGA/Play-RTG/Play-RTG16. The installer preserves `space.sav`.
Workbench icons request a 256 KiB stack. Set `Stack 262144` in a CLI before
running CPU variants or standalone tests; the bare CLI stack-swap exit path
needs further toolchain QA. CPU icon tooltypes accept `MODE=ECS`, `AGA`, `RTG` or `RTG16`, plus `LOWRES`,
`NOSOUND` and `ANALOG`. The original icons can be regenerated by running
`Space-Icons` beside the executables and copying its `.info` files into `release/`.

The OFS ADF boots directly into Space.000 in ECS 320 x 200 and can store a commander
on a writable disk. It needs a user-supplied Kickstart 3.0+ and enough RAM for the
expanded procedural world and depth buffer (2 MiB Chip plus 8 MiB Fast is the
emulator-tested configuration). It is not an unexpanded A500 release. Escape returns
to the boot CLI; no Workbench files or licensed ROMs are distributed. Installer and
boot evidence, timings and remaining physical QA are recorded in `docs/validation.md`.

Planetary rotation now moves terrain, flora, surface ports and landed ships. Actual
stellar geometry controls day/night, eclipses and an indexed Rayleigh scattering
approximation. Temperate/frozen weather provides curved cloud decks, entry/exit fog,
rain and snow; gas giants have cloud layers, multiple moons and equatorial ring bands.
Weather and vegetation are bounded prototype models. `make environment-preview`
produces reproducible host frames and portable commander fixtures under `build/evidence`.

Solid bodies also have seeded procedural reflectance: regional mineral deposits,
folded rock strata, weathering, wind-bent desert dunes, glacial fracture veins,
vegetation clearings and lunar impact bowls/ejecta. Water has current/depth patches
and small waves. The spherical body-space fields stay fixed through flight and
rotation and cross cube-face boundaries without texture seams. Local 64 m samples
share tile boundaries and interpolate smoothly; finer inclusions and slope lighting
fade before minification. ECS uses its existing ground tones; AGA/RTG use full
material ramps. The tile cache adds 48 KiB over the previous four-corner cache,
plus about 7 KiB for orbital reflectance. Generation uses integer math and
requires no external textures. Reflectance supplements physical terrain:
rocky worlds have hills, folded mountain ranges, valleys and volcanic cones with
calderas; deserts have elevated plateaus and deep winding canyons; ice worlds have
glacial uplands, pressure ridges and rifts; moons have crater bowls, raised rims
and, for some seeds, volcanic terrain. Ocean worlds keep level seas and coasts
with inland mountain ranges. Gas giants have no solid relief. Geometry uses
body-fixed metre-space fields with a bounded 8 km maximum elevation, shared by
rendering and collision. Pads blend smoothly into the surrounding land. Detailed
terrain remains visible to 20 km above sea level, with geometric slope lighting
beyond texture range. Older commanders buried by regenerated terrain are lifted
clear on load; landed saves reattach to the ground. Native hardware performance
for the richer terrain remains unmeasured. `make surface-preview` renders matched
rock/desert/ice/moon fixtures in ECS/AGA; `make terrain-detail-preview` measures the
close-approach renderer at ECS/AGA/RTG resolutions.

`make landform-preview` renders real generated rock/ocean/desert/ice/moon worlds
from two angles at ECS/AGA/RTG resolutions and writes portable commander fixtures
under `build/evidence/landform-*`. Volcanoes are static geometry; eruptions and
lava simulation are not implemented.
