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
journals, persistent port markets, six contract types, combat, upgrades and portable commander saves.

## Amiga builds

```sh
./build-amiga.sh
./build-amiga.sh tests
```

The script uses an installed `m68k-amigaos-gcc`, `/opt/amiga/bin`, or the
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
`Space-RunTests` starts the presentation/core suites with an explicit 256 KiB
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
**[** lowers the requested rate; the safety controller reduces the effective rate near
terrain, a port, a braking limit or another ship. It never enlarges a physics tick.

For manual flight, hold the **right mouse button** and drag to steer. Drag right/left
for yaw and up/down for pitch; the mouse acts as a virtual stick with a small dead
zone. Release the right button to recenter. **Left mouse button** fires. On Amiga,
use the mouse port and a standard digital joystick in **joystick port 2**; directions
steer and its fire button shoots. The host preview uses the first attached joystick
or gamepad. Standard SDL controllers use proportional left-stick steering, A/B/X/Y
for fire/brake/contact/autopilot, shoulders for thrust/reverse and Start to pause.
Unmapped joysticks retain axes 0/1 and button 0 fire. Keyboard arrows,
Ctrl and W/S remain available; W/S controls speed/thrust with either device.
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

Distances are metres, radii are kilometres, and planetary spacing is in AU. Journeys
can take hours or days of simulated time; compression repeats fixed physics ticks.
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

Flight controls: arrows steer, W/S change set speed, Ctrl fires, E switches flight
assist, K controls landing gear, Space pauses, H opens help, F5 saves, F9 loads,
and Escape exits. At ports F refuels and repairs, O upgrades the laser, and N
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

Read [Space.guide](Space.guide) on the Amiga for the controls and prototype limits.
Save format 5 includes the market ledger alongside exploration, frame, fuel-impulse
and approach state. World generation stays at version 3. Version 3 commanders load
with their saved stock assigned to the docked port; other ports start with generated
supply. Version 4 commanders also load; older open-ground landings are reattached
to rotating terrain and docked ships follow their ports. Version 2 remains incompatible.

Research sources are in [docs/research.md](docs/research.md). Remaining full-game
work is tracked in [docs/roadmap.md](docs/roadmap.md), and current test evidence in
[docs/validation.md](docs/validation.md).

## Sound, controllers and release files

The original eight-second PCM score and procedural effects use no external assets.
Native ECS/AGA reserves all four Paula channels through audio.device before takeover:
left/right music, effects and engine. RTG queues stereo audio.device buffers while the
OS runs; the host uses SDL audio. Allocation failure continues silently; `--nosound`
explicitly disables sound. Audio tracks wall time, so simulation compression does
not accelerate the music. Native saves flush the filesystem before takeover and restart the score; pause mutes playback.

CD32 controllers require lowlevel.library V40+: red fires, blue brakes (sets commanded
speed to zero), yellow cycles radar contacts, green toggles autopilot, forward/reverse
change thrust, and Play pauses. Basic digital port-2 sticks still work without that
library. `--analog` selects a physical proportional joystick using POT counters,
reserves its resource bits and calibrates the released stick at startup. Press C
with the stick centred to recalibrate; C is the camera key when this option is absent.
Analogue steering preserves fractional turn rates. Physical analogue and CD32 devices
remain unverified. Poseidon analogue reports are accepted in RTG, where USB tasks run.

`./build-amiga.sh all variants tests benchmarks release-tools` builds 68000, 020,
030, 040 and 060 software-integer executables, test programs, native benchmarks and
`Space-Icons`. Choose the CPU binary for the installed processor; no FPU is required.
`Space-benchmark.020` (or `.000`) measures C2P into Chip RAM and orbit/surface rendering.
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
requires no external textures. These are visual
surface features; terrain geometry, landing pads, collision heights and commander
saves retain their existing behaviour. `make surface-preview` renders matched
rock/desert/ice/moon fixtures in ECS/AGA; `make terrain-detail-preview` measures the
close-approach renderer at ECS/AGA/RTG resolutions.
