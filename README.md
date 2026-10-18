# Astralbound

An original space trading and combat game for the Commodore Amiga, inspired by
the scope and solid-polygon presentation of Frontier: Elite II. Astralbound's source,
ship geometry, font and generated worlds are original and released under
the [Unlicense](LICENSE). The adapted HYG star catalog is CC BY-SA 4.0; see
[data sources and approximation limits](data/README.md). Reference screenshots and Frontier assets are not
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

With no targets the script builds the game for every CPU (`Astralbound.000`, `.020`,
`.030`, `.020fpu`, `.040` and `.060`); named targets build only those. The
script uses an installed `m68k-amigaos-gcc`, `/opt/amiga/bin`, or the
`sacredbanana/amiga-compiler:m68k-amigaos` Docker image. `DEBUG=1` enables debug
builds; `CLEAN=1` rebuilds from scratch. No SDL or OpenGL is linked into the
Amiga executables.

Copy the contents of `dist/amiga` to an Amiga drawer and run from Shell:

```text
Stack 262144
Astralbound.000 --ecs
Astralbound.020 --aga
Astralbound.020 --rtg
Astralbound.020 --rtg16
Astralbound.020 --aga --lowres
```

- **ECS:** PAL 320 x 256 or NTSC 320 x 200, 32 colours, double-buffered bitplanes and a private Copper
  list. Requires Kickstart 3.0+ in this prototype, including on a 68000 machine.
- **AGA:** PAL 320 x 256 or NTSC 320 x 200, 256 colours from the 24-bit palette, eight bitplanes,
  64-bit display fetches and double buffering. Use the 68020 executable.
- **RTG:** 320 x 240 through 1024 x 768, indexed 8-bit or `--rtg16` high-colour output, driver-managed double buffering. Requires
  CyberGraphX V41+ or Picasso96's compatible CGX API. RTG deliberately keeps the
  OS running because portable graphics-card access depends on its driver.

Native modes allocate all display memory before taking over, suspend scheduling
and interrupts, and poll the hardware keyboard. Saving and loading temporarily
restore the OS. Escape restores the display, DMA, interrupts and task scheduling.
The prototype needs expansion RAM: its C renderer and 64-bit coordinate maths
are not yet tuned to stock A500 performance. The depth buffer uses an additional
320 KiB at 320 x 256, 250 KiB at 320 x 200, or 1200 KiB at 640 x 480, allocated
before native takeover and freed on exit. Native output detects the OS video
standard and keeps it: PAL timing is 50 Hz, NTSC is 60 Hz, with the same fixed
physics ticks. `--lowres` selects a centred 320 x 200 display on PAL, reducing
framebuffer/bitplane work by 22% and flight viewport pixels by 28%. It is native
only; NTSC already uses 200 lines. Compact menus use the whole screen to keep
all rows visible. Real-machine speed gains remain unmeasured.

## Startup and graphics settings

Every interactive launch opens a flat, OS-managed setup menu **before the intro**.
Choose ECS, AGA, RTG 8-bit or RTG 16-bit; unavailable Amiga backends are skipped.
Native output offers 320 x 200 or native PAL/NTSC height. RTG offers 320 x 240,
640 x 480, 800 x 600 and 1024 x 768, subject to graphics-card modes and memory.
Arrow keys move/change, Return selects, Escape backs out, and the mouse or a
joystick/CD32 pad can operate the menu. Setup redraws only when needed on Amiga.
If a selected display cannot open or allocate memory, setup lets you lower it.

**Low** is the first-launch Amiga default: 320 x 200, a 2x pixel 3D view, coarse
flat-shaded terrain, simple ports/hulls, reduced effects and no volumetric clouds,
forests or twilight glow. **Medium** restores material sampling and some scenery.
**High** uses full-resolution, fine textured terrain and the full visual effects.
**Ultra** increases terrain tessellation, cloud/forest/scenery distances, stars,
rings and geometry capacity for fast accelerators and emulators. Presets affect
renderer work; selecting AGA or RTG by itself does not make the CPU renderer faster.

Select **Custom**, then **Custom Options**, for three pages of 18 controls: 3D
pixel size (1x to 4x), terrain mesh, surface shading, planet facets, cloud mesh and
distance, forests, port detail and distance, smoke/debris, stars, rings, twilight,
weather/fog, port lights, body labels, triangle budget (512 to 16384), and ship
detail. Custom begins with the current preset's values. The console and menus stay
at output resolution. Minimum settings deliberately sacrifice appearance; visual
terrain detail never changes collision, autopilot, weather physics or save data.
Native C2P still converts the selected output size, even with a smaller 3D view.

**Run Performance Test** runs all 14 scenes or the scene chosen in **Test Scene**:
orbit/rings, low/desert/ice terrain, a city, an orbital station, rain/forests,
inside-cloud fog, snow, ships/blasts/debris, hyperspace, system and galaxy maps,
and service portraits. The actual selected renderer, output conversion and display
synchronization are used. Live FPS includes the whole displayed frame, with render
and output milliseconds shown separately. One warmup frame per scene is excluded;
each scene runs for at least five seconds and four frames, with no frame-count
cap that can shorten a fast run. The 3D fixtures use the full display without
the cockpit HUD, target markers, or world labels; map and service tests retain
the interface they exercise. Weather scenes use upright planetary horizons and
wall-time flight paths through rain, a cloud core, and snow, with animated
precipitation. A large scene name and timing strip stay visible above the effects.
Native render/output splits have display-tick precision (20 ms PAL, about 16.7 ms
NTSC); zero can mean below one timer tick. Total displayed FPS uses the whole sample.
Escape, click or fire stops the test; completed and partial results return to setup.
Results retain the previous run's FPS for comparison in this session. Fixtures and
scene preparation are excluded, and tests never save a commander. These figures
belong to the machine/emulator and settings used; they are not a hardware rating.
`make benchmark` separately compares preset **host CPU render times**, excluding
C2P, synchronization, sound and physics, and must not be read as Amiga FPS.

Video mode, display size, preset, every Custom control, intro and sound are saved
on Save Settings, Start, Test or Quit. The versioned `astralbound.cfg` lives beside
the Amiga executable (`PROGDIR:`), or in the host working directory. It is separate
from `astralbound.sav`. Invalid values are bounded and unsupported preference
versions use defaults. A failed replacement preserves the previous file when
possible; a read-only disk cannot persist changes. Deleting the config restores
first-launch defaults. The installer preserves both preferences and commanders.

`--nomenu` (Workbench `NOMENU`) bypasses setup using saved settings. Mode flags,
`--lowres`, `--nosound` and `--nointro` prefill setup; `--preset low|medium|high|ultra|custom`
selects a preset. `--benchmark` runs the suite directly and prints its results. `--profile` on Amiga retains
its 120-frame flight timing mode and bypasses setup. Headless host frame previews
bypass setup and use deterministic defaults unless flags select another preset;
`--render-menu FILE.ppm [--custom]` previews the setup pages. Host `--config PATH`
uses a separate preferences file for interactive testing.

## Host development preview

Requires a C99 compiler and SDL2 with `sdl2-config` in PATH.

```sh
make
./build/astralbound
make test
make sanitize
make navigation-test  # longer, full interplanetary transfer
make preview
```

After Start Game, the optional 3 minute 18 second title sequence plays: a Kestrel courier lifts
off from Haven at dawn, meets a Wayfarer escort in daylight rain under the clouds and
climbs through them wing to wing, is ambushed by raiders in orbit, jumps with its escort
to a ringed gas giant and docks under police protection. It is staged with the real
systems, ports, hulls, weather, explosions and hyperspace effects (no prerecorded
video), letterboxed, in ECS, AGA and RTG.
Any key, mouse button, fire or controller button skips it (Escape too); `--nointro`
or the `NOINTRO` tooltype disables it. Host preview flags that stage a scene
(`--surface`, `--market` and the like) also skip it.
`--render-intro SECONDS FILE.ppm` writes one frame of the sequence. `make intro-preview`
writes a contact sheet to `build/intro-sheet.ppm`; `build/intro-preview --stream`
writes raw frames and a WAV for encoding a video (usage in `tools/intro_preview.c`).

`--render FILE.ppm --surface` and `--render FILE.ppm --orbit` write deterministic
frames without opening a window. `--render FILE.ppm --city-overhead --rtg`
reproduces a downward view of the starting city at 1232 metres in RTG resolution;
use `--ecs` for the reduced palette. `--lowres` or `--ntsc` previews the 320 x 200
layout on the host; these flags cannot be combined with `--rtg`. The host and Amiga use the same simulation and
renderer. `Astralbound-tests.020` runs the full core suite on an Amiga;
`Astralbound-tests.020 --quick` runs the shorter native smoke suite.
`Astralbound-career-tests.020` runs the careers/resources/equipment regressions.
`Astralbound-ui-tests.020` exercises mouse workflows at all three display layouts.
`Astralbound-galaxy-tests.020` validates the catalog, Sol, map interactions and waypoint saves.
Standalone native test programs require `Stack 1048576`.
`Astralbound-RunTests` starts the galaxy/presentation/core/UI/career suites with an explicit 1 MiB
child stack and writes their result files; use it in a bare boot CLI. Full trajectories
remain in the default suite, which can take a long time under emulation.

## First flight

Start docked at Haven. Press **L**, then **P**, to travel to the orbital Relay.
Press **T** to cycle ports, **Q** to cycle body waypoints, or **Y** to see the
selected destination list. Up/Down changes its target; return to flight with **V**
and engage **P**. Body waypoints stop above the atmosphere, including at gas giants.
Orbital stations rotate and move: autopilot matches port velocity, waits for clearance,
and enters through the marked corridor. Station control grants clearance
automatically to a manual pilot closing within 10 km.
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
provides up to 20,000,000 m/s² acceleration and braking, with a **30c** cruise ceiling (c is the speed of light). Gravity wells, atmospheres,
stations and nearby ships reduce the permitted speed. Inside an exclusion zone,
or with an empty tank, the drive cannot run: the **DRIVE** button is greyed out
and **A** is refused. An armed drive that loses the ability to run disengages
itself, zeroing the set speed so thrust and the engine note stop together; the
message line gives the reason. The HUD shows DRIVE or AUTO DRIVE while it runs,
and DRIVE LOCK while autopilot or weapons fire briefly withhold it.
Autopilot uses System Drive only when you arm it with **A**; otherwise it uses
ordinary engines. It limits speed for terrain/star detours, atmospheric flight
and docking, and brakes for arrival. Reaching a body waypoint zeros the manual
speed setting.
Atmospheric flight caps air-relative speed at 1,200 m/s instead of causing
overspeed damage. Descending into an atmosphere from space under manual
control fires an emergency brake that always stops the descent above 10 km
altitude (over atmospheres thinner than 20 km it arms at 20 km), then hands back
a hover with flight assist on and zero throttle; autopilot landings fly their
own descent. Terrain and station impacts still cause damage. The speed
readout uses m/s, then km/s, and switches to **c** at 0.01c.
Hyperspace arrival places you close to the host star, 1.1 million km above its
surface and just outside its drive exclusion shell. While in space, bodies and
stars in the field of view have name labels, including distant unresolved bodies.
Moons are labelled only once you are within a few moon-system radii of their
host planet, so distant planets are not buried under satellite names.

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

In flight, click **LAUNCH**, **AUTO**, **DRIVE**, **ASSIST**, **EXIT**/**BOARD**,
**DOCK**/**LAND**/**USE** or **LOCK** to operate the ship. Drag the cyan throttle strip; **STOP** commands zero
speed. The time strip selects 1/4/16/64/256/1024/4096x while retaining the existing
safety limits; the cockpit shows delivered/requested compression. **PAUSE/PLAY**
works with the mouse. Click the scanner to show missile/decoy controls and click
its background again to return. In SYS, click a body or station marker to choose
that navigation target; overlapping distant markers are resolved through NAV's
list and BODY/PORT/PAGE buttons. GAL is a navigable 3D star map. SAVE and
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
shipyard, where the selected hull turns in a 3D preview beside stat bars compared
with your current ship (1-6 or Up/Down select, Return buys). **J** lists contracts and destinations. **G** opens the galaxy map;
drag to rotate, use **+/-** or the mouse wheel to zoom, and click a star to target it.
**PAN** changes dragging to camera translation; WASD pans in the horizontal plane
and Q/E moves vertically. Arrows rotate. **HOME/C** centres on the current system,
**FOCUS/F** centres on the target, and **GALAXY** opens the spiral-disc overview.
**SEARCH** or **/** focuses name entry; type a partial name or alias, use Up/Down
or PREV/NEXT to browse matches, then Return or a result row to select it. Backspace
edits, Escape leaves search, and CLEAR clears the query. Search covers all 94 real
catalog entries, the local sampled field and visited systems. **INFO** shows component
spectra, distance, economy, government, bodies, ports and exploration status.
**WAYPOINT** pins a separate cyan diamond and route line; clicking it again for the
same target removes the pin. The selected target has a gold box, current system a
red cross, and visited systems a green dot. Hover reveals a star's name.

Press **X/JUMP** above 12 km altitude to jump. The hyperspace drive charges
for 3 seconds, crosses a tunnel and drops into the destination. Nearby traffic
arriving during the charge aborts at no cost; only pause works during the sequence. Jump range is six light-years;
range and fuel use the actual 3D positions, including sub-sector catalog precision.
The map reports launch, altitude, range, fuel and traffic restrictions. Overview
points sample the procedural galaxy; zoom to 8 ly or closer to resolve every system
in the local 17-sector cube. Catalog and journal/navigation entries are retained at
all scales. Empty sectors reject jumps. All axes are navigable; the overview is a
fictional four-arm galactic disc, not an observational Milky Way reconstruction.

Real entries include Sol, Proxima/Alpha Centauri, Sirius, Procyon, Tau Ceti, Vega,
Arcturus, Aldebaran, Polaris, Rigel, Betelgeuse and Deneb. Multiple systems support
up to six stellar components. Stellar classes include normal O/B/A/F/G/K/M stars,
giants/red giants, white and brown dwarfs, neutron stars and black holes. Catalog
stars use HYG J2000 positions rounded to 0.001 ly; their close binary orbits and
flight radii are gameplay approximations. The galaxy contains exactly **1,000,000,000
systems**, including the 94 real catalog entries and nine starter systems. Generated
systems occupy a finite fictional disc 20,000 ly across and 600 ly thick, with empty
space beyond it. Sol contains **34 navigable bodies**: eight planets, five dwarf
planets, and 21 major moons. Minor bodies and small irregular/provisional moons are
omitted. Sol uses eccentric Kepler orbits and real radii/periods; Earth/Mars/Moon ports are
fictional. See [catalog provenance](data/README.md) for epochs and limitations. Unsettled systems have body targets and no
markets or contract boards. The commander journal preserves up to 128 systems with
visited/surveyed body masks. Close approaches below 100 km mark a body surveyed.
Save format 9 remaps retained Sol bodies from older saves. Commanders on removed
bodies move to a safe Earth orbit; removed systems return to Hetha's starting port.
Removed mining rigs are refunded and contracts to removed systems are canceled
without a reputation penalty. Exploration history is retained.

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

Player, turret and mining lasers emit travelling bolts in the 3D world. Raiders
and police also show their outgoing fire, and homing missiles have bright engine
streaks. Ships and terrain occlude these effects. Minimum graphics keeps weapon
fire visible; higher Smoke and Debris settings add bright cores and longer missile
exhausts. The visuals preserve existing weapon damage, range and cooldowns.

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
small bodies and ports can overlap in the system view. I, or clicking an already
selected body in the list or orbital map, opens its data sheet: radius, density,
mass, surface gravity, escape speed, mean temperature, surface pressure,
atmospheric composition, day length, orbital period, distance, ports, survey
status and navigation range. Solar System values come from NASA fact sheets;
other systems show values derived from each world's size, gravity and climate. Y retains the
destination list; V returns to flight.

Flight controls: left/right arrows roll, up/down pitch, W/S change set speed, Ctrl fires, E switches flight
assist, D docks or lands (or uses a ground site on foot), K leaves or re-boards a
landed ship, Space pauses, H opens help, F5 saves, F9 loads,
and Escape opens a paused quit dialog: **Save & quit** (S), **Quit without saving**
(Q), or **Cancel** (Escape/C). A failed save keeps the dialog open.
At ports F refuels and repairs, O upgrades the laser, and N
buys a larger hull. On the market, 1-8 selects a commodity and +/- buys/sells. Both buy and sell
quotes are shown. Each port has independent supply: purchases raise scarcity prices,
while sales increase stock and lower prices. Supply returns towards its generated
level by one tonne per simulated hour, including while travelling elsewhere.
Up to 128 traded ports are preserved in the commander ledger across jumps and saves;
when it fills, existing markets remain usable, but trading at new ports is refused.
**S** or **PORT** calls station services: a bulletin-board directory of the contract
agent, commodity exchange, ship broker, outfitter, engineer and port liaison.
Select a contact to see their portrait; **Call / Return** opens their service.
The engineer's **Authorise 100 CR** restores hull, shields and fuel.
**B** opens contract notices directly. Click a notice or press 1-8 to read it;
Return first opens the briefing, then accepts an offer from its briefing.

## Docking, landing and going ashore

Ships no longer carry landing gear: any contact with solid ground below 8 m/s is a
landing, and anything faster is an impact. The console's context button (**D**)
parks the ship:

- **DOCK**, within 20 km of any port: the autopilot takes the port as its target,
  requests clearance, flies the approach and settles onto the pad or into the
  station. Pressed from behind or beside a station, it swings wide of the hull and
  comes round to the corridor. Press **D** again to abort and hold a hover.
- **LAND**, elsewhere below 30 km above solid ground: the ship lands where it is.
  It returns over the point that was beneath it when pressed, levels, and
  descends on its engines, slowing to about 2 m/s at touchdown. The ground must
  be dry, no steeper than about 1:2 under the hull, the engines must outlift
  gravity and no hostile may be within 5 km; otherwise the message line gives the
  reason. **D** aborts the descent into an assisted hover.
- In space, away from ports, the button is dimmed.

Landed on a planet (open ground or a surface pad, not a station berth), **K**
(**EXIT**) steps outside. On foot, W/S walk forward/back (3 m/s, half pace
backwards), left/right turn and up/down look. Water, cliffs steeper than about
50° and the suit's 1.5 km range from the ship stop you without ending the
excursion. The HUD marks the ship and the nearest **ground site**, and the scope
plots everything within 500 m. Walk within 12 m of a site and press **D** (**USE**):
mineral outcrops yield a tonne of crystals, wreckage a tonne of alloys or
machinery (hold space permitting), and survey beacons sell their data for
credits. Each site is used once per session. Sites are deterministic: the same
ground always has the same sites, and they are visible from low flight, so you can
land beside one. Return within reach of the hull and press **K** (**BOARD**) to go
back aboard; launching, autopilot and time compression wait until you do. A save
made outside restores you aboard the landed ship.

## Contracts

Every notice has a named client and portrait, a story, flight orders, and terms.
**Story / Orders / Terms** separates the narrative from precise completion rules,
rewards, time remaining, freight, cabin seats, faction and reputation requirements.
Acceptance blockers appear under Terms. **J** opens the selectable contract log;
accepted, completed and failed contracts retain their briefings. Acceptance opens
the live contract in this log, including the current port on multi-stop jobs.

**Destination** opens the galaxy map focused on the mission system. Use its
**Waypoint** control to mark the route; opening a briefing destination preserves
any previous waypoint until you change it. **Briefing** returns to the same
contract and tab. For a destination in the current system, Destination also selects
the mission port for local navigation. Use **NAV / SYS** to inspect that port.


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
patrol scans confiscates contraband and fines 100 CR per tonne. Two police ships
circle every port and station while you are within 50 km. Firing a laser or missile
within 10 km of police costs a 500 CR fine; firing again after a two-second warning,
or shooting at a police ship, sets every patrol on you until the fine is paid with
F at a port. Returning fire on an attacker is lawful. Police hulls are armoured
against a tenth of any hit and repair themselves, and engaged patrols fire twice a
second out to 5 km, so fighting them is near suicide. Destroyed police are replaced
after three minutes, and fines of 1000 CR or more count as a warrant near ports.
Cargo attracts pirates in lawless orbital space.
Nearby traffic flies the same hull models as the shipyard, pointed along its course
through the local air or frame and rolled level with the nearest body; once a hull
shrinks below about 16 pixels long it falls back to a four-triangle silhouette.
`make npc-preview` renders oriented encounters in ECS and AGA.

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

Read [Astralbound.guide](Astralbound.guide) on the Amiga for the controls and prototype limits.
Save format 9 stores the independent galaxy waypoint and Sol survey masks, and
migrates the reduced population and body catalog from earlier saves.
It preserves ship roll as well as crew, cabins, mount upgrades, factions, legal status,
contract stages, cargo pods, missiles, mining rigs and finite asteroid deposits,
alongside exploration, markets, frame, fuel-impulse and approach state. Formats
3, 4 and 5 migrate with default faction records and enough crew for their hull.
Formats 3 through 6 load with zero bank; version 7 retains bank. Formats 3–7 load with empty new catalog records. World generation stays at version 3. Version 3 commanders load
with their saved stock assigned to the docked port; other ports start with generated
supply. Version 4 commanders also load; older open-ground landings are reattached
to rotating terrain and docked ships follow their ports. Version 2 remains incompatible.

Research sources are in [docs/research.md](docs/research.md). Remaining full-game
work is tracked in [docs/roadmap.md](docs/roadmap.md), and current test evidence in
[docs/validation.md](docs/validation.md).

## Sound, controllers and release files

The supplied ProTracker MODs are installed in the `music` drawer beside the
executable. You can add more tracks directly to `battle`, `planetary-flight`, `space-flight`,
`docked`, `game-over` and `intro` (the title sequence; without it, space-flight
music plays). The host preview also checks the repository's `music`
folder when launched there. Both `tune.mod` and `MOD.tune` names are recognized,
case-insensitively. The ZIP/LHA packages include the repository music tracks.

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
executables, test programs, native benchmarks and `Astralbound-Icons`. Choose the binary
for the installed processor:

| Executable | CPU | FPU |
| --- | --- | --- |
| `Astralbound.000` | 68000 or newer | none |
| `Astralbound.020`, `Astralbound.030` | 68020/68030 or newer, including LC/EC040 and LC/EC060 | none |
| `Astralbound.020fpu` | 68020/68030 | 68881/68882 required |
| `Astralbound.040` | full 68040 | built-in FPU required |
| `Astralbound.060` | full 68060 | built-in FPU required |

The FPU builds replace the renderer's chains of 64-bit libgcc divisions and the
bit-by-bit integer square root with FPU arithmetic. Square roots are corrected to
the exact integer result, so simulation, saves and generated worlds are identical on
every build; only rendered pixels may round differently. The 030/040 builds link the
020 libgcc instead of the 68000 fallback, and the 060 build uses the 060 libgcc, which
avoids the 64-bit MULU/DIVU forms the 68060 traps. `make fpu-test` runs the host
suites through the FPU code paths.
`Astralbound-benchmark.<cpu>` measures C2P into Chip RAM and orbit/surface rendering.
`Astralbound.020 --aga --lowres --profile` exits after 120 frames and reports render and
output/synchronization times; native uses CIA display ticks, RTG uses EClock.
Emulated acceleration does not establish physical-machine frame rates.

Run `./package.sh` to build all six CPU variants and create
`dist/Astralbound-0.1-amiga.zip`, `dist/Astralbound.lha` and
`dist/Astralbound-0.1-boot.adf`. An archive-capable `lha` command must be in
PATH; a missing archiver is an error. Both archives include `Astralbound.readme`,
the executables, installer, icons, manual, license notices and all non-hidden
files in the repository music tree. ZIP and LHA contents are extracted and byte-checked.
`make package-test` checks music staging and both archive formats with asset fixtures.
The packager also copies `dist/Astralbound.readme` beside the LHA, with the
matching, version-free basename required for Aminet uploads. Submit that pair
when uploading to Aminet; packaging does not upload anything. The readme follows
[Aminet's readme format](https://wiki.aminet.net/The_Readme_file) and is checked
for required metadata, ASCII text, LF endings and lines of at most 78 characters.
ADF generation needs amitools' `xdftool` (`XDFTOOL=/path/to/xdftool`) and vasm;
`build/release-venv/bin/xdftool` is also recognized. The ADF crunches
Astralbound.000 with [Cranker](https://aminet.net/package/util/pack/cranker) 0.72:
`tools/cranker.py` fetches the pinned Aminet archive, checks its SHA-256, builds it
into `build/cranker/` (set `CRANKER=/path/to/cranker` to use another build) and
checks that the crunched file unpacks to the original code, data and relocations.
Cranker's GPLv2 cruncher is not distributed; only its public-domain decrunch
header is inside the crunched executable. On Workbench, open Install
using the system Installer (V42.12+), tick one or more CPU builds and choose a
parent drawer (for example `Work:Games`). The installer creates
`Work:Games/Astralbound`, copies the selected builds and all shared assets, and
preserves `astralbound.sav` and `astralbound.cfg`. For an upgrade, selecting the
existing Astralbound drawer also works. Unselected builds from an earlier install
are left in place. Installed Play shortcuts use a selected executable: preferably
020 for AGA/RTG, and 000 for ECS low-resolution, when selected.
Workbench icons request a 256 KiB stack. CLI launches allocate the required stack
automatically using Exec StackSwap and restore the original shell stack before
exit; no `Stack` command is needed. Native tests allocate their declared stack
in the same way. CPU icons use saved preferences by default; optional tooltypes accept `MODE=ECS`, `AGA`, `RTG` or `RTG16`, plus `LOWRES`,
`NOSOUND`, `ANALOG`, `NOINTRO` and `NOMENU`. The original icons can be regenerated by running
`Astralbound-Icons` beside the executables and copying its `.info` files into `release/`.

The OFS ADF boots directly into Astralbound.000 in ECS 320 x 200 and can store a commander
on a writable disk. It needs a user-supplied Kickstart 3.0+ and enough RAM for the
expanded procedural world and depth buffer (2 MiB Chip plus 8 MiB Fast is the
emulator-tested configuration). It is not an unexpanded A500 release. Escape returns
to the boot CLI. The floppy holds the space-flight (also used by the intro),
docked and battle tracks; planetary-flight and game-over are silent because the
complete music tree does not fit. Use the ZIP/LHA installation for all music. No Workbench files or
licensed ROMs are distributed. Installer and
boot evidence, timings and remaining physical QA are recorded in `docs/validation.md`.

Planetary rotation now moves terrain, flora, surface ports and landed ships. Actual
stellar geometry controls day/night, eclipses and an indexed Rayleigh scattering
approximation. Temperate/frozen weather provides shaded cumulus fields, a depth-graded
in-cloud tint, rain and snow; gas giants have cloud layers, multiple moons and equatorial ring bands.
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
