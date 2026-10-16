# Astralbound full-game work

The user requested a full, original Frontier-style Amiga game. The current
prototype establishes flight, world generation and platform integration. This
list remains part of that request; it is not a claim of feature completeness.

## World and navigation

Implemented in the current development build:

- [x] Interactive 3D galaxy navigation, rotation/pan/zoom, exact shared picking,
  global real-star name/alias search, local/journal search, component data, target
  selection, independent persistent waypoint and physical jump distances.
- [x] 94 catalog entries with J2000 positions, up to six stellar components,
  normal/giant/red-giant/white-dwarf/brown-dwarf/neutron/black-hole types.
- [x] Exactly one billion systems in a finite disc, with indexed overview sampling,
  real catalog positions and nine preserved starter systems.
- [x] Sol: eight planets, five dwarf planets and 21 major moons; eccentric
  fixed-point Kepler orbits and long periods. See `data/README.md` for limits.
- [x] Save v9 body-identity migration and safe relocation from removed systems/bodies,
  waypoint/Sol surveys, 512 catalog-body capacity, original 12-body procedural cap
  and bounded 12-slot terrain globe cache.

- [x] Astronomical metre scales, translated hierarchical local frames, and a central star
  (or a binary pair orbiting the system barycentre).
- [x] Analytic moving orbits, parented moons, rendered rings, and vacuum, thin,
  temperate, dense, frozen and hydrogen atmospheric models.
- [x] Portable exploration journals, sparse settlement, and star occupancy following
  galaxy disc density, a central bulge and four spiral arms.
- [x] Continuous terrain LOD edges and outer skirts, anchored forests, generated city
  streets, permeable cloud decks, rain/snow, coastlines and varied city blocks/towers.
- [x] Rotating orbital stations, traffic clearance queues, swept hollow dock geometry,
  marked approach corridors and moving-port velocity matching.
- [x] Regression coverage for all six generated body types, 58 generated port arrivals,
  launch/re-entry, lost thrust authority, local/system frame transitions, clearance
  queues and portable exploration/approach state.

Q cycles body waypoints, T cycles inhabited ports, Y shows the selected destination
list, and D requests clearance. Autopilot requests clearance automatically. Empty
sectors cannot be jumped to; unsettled star systems can be explored using body targets.

- [x] Axial rotation of body-fixed terrain, flora, surface ports and landed ships;
  ground-relative atmospheric assist, drag, landing and survey speeds.
- [x] Live stellar day/night and eclipse lighting, indexed Rayleigh approximation,
  cloud entry/exit fog and climate/latitude-driven rain and snow.
- [x] Multiple gas-giant moons and separated equatorial ring bands.
- [x] Version 5 saves with version 3/4 ground/docked commander migration.

These are bounded game models: circular generated orbits and catalog Kepler ellipses rather than an
N-body solver, a 128-system exploration journal, decorative flora/buildings and procedural weather,
and scheduled clearance traffic rather than a persistent civilian fleet. The local
terrain grid has shared edges and a skirt to the globe, and uses a raster depth buffer. Hardware frame rates and long-duration play remain QA work.

## Careers and economy

- [x] Passenger cabins and transport, crew recruitment/wages and ship-specific crew needs.
- [x] Escort, rescue, assassination, reconnaissance, surface-installation attack and
  multi-leg contracts with negotiable terms and reputation gates.
- [x] Original factions, military ranks, medals, reputation and legal status.
- [x] Independent port stock, scarcity prices, hourly supply recovery, separate buy/sell
  quotes, and a portable ledger of 128 traded ports, with version 3 save migration.
- [x] Contraband/black markets, police and piracy.
- [x] Asteroid mining, surface mining rigs, cargo collection and fuel scooping.
- [x] Ship catalogue with distinct geometry, mass, acceleration, equipment and hardpoints.
- [x] Missiles, countermeasures, multiple weapon mounts, turrets and escape pods.

The career model has thirteen contract templates with two board pages, six ranks,
medals every five military jobs, three original factions, hourly crew wages, local
police/pirate encounters, eight finite asteroid deposits per system (32 recorded
systems), four persistent rigs, eight local cargo pods and eight active missiles.
Passenger cabins reserve seats; hull mass/cargo changes acceleration. Turret mounts
have separate upgrades, decoys suppress enemy fire, and escape pods offer bounded
port rescue. These features do not simulate persistent strategic wars or fleets.

## Presentation and hardware

- [x] Independent dorsal/ventral turret aim and firing, contact targeting and 10 km
  heading radar, live system/local orbital maps and system/body data. Turret
  aim, contact selection and map display reset on load; moon/station separation
  uses local scale.
- [x] Situation-based random MOD playlists (user-supplied), engine, laser, damage, explosion,
  docking/launch and jump sounds. Paula DMA in native modes; queued audio.device
  stereo under RTG and SDL audio on the host. `--nosound` is available.
- [x] Mouse virtual-stick flight, digital Amiga joystick port 2, and host joystick/gamepad input.
- [x] Calibrated proportional Amiga POT-counter joystick input (`--analog`, C to
  centre), CD32 seven-button mapping through lowlevel.library and SDL controller
  mappings. Physical controllers remain hardware QA.
- [x] PAL/NTSC native layout selection and a centred PAL `--lowres` 320 x 200 option,
  height-aware bitplanes/C2P and compact menus. NTSC ADF boot is emulator checked; real hardware remains QA work (see validation notes).
- [x] A bounded 32-bit raster depth loop with a wide fallback, paired C2P stores,
  native blitter plane clearing and 68000/020/030/040/060 CPU builds. Native C2P/
  scene benchmarks and `--profile` report timings for the tested configuration.
- [x] RTG high-colour output (`--rtg16`) through CGX/P96 driver colour lookup
  conversion and screen buffers. Acceleration follows the installed driver; no
  direct card-register assumptions or new true-colour scene shading.
- [x] Release Installer script, original Workbench icons, ZIP/LHA drawer packages
  and a self-booting ECS ADF with an original boot entry (no bundled ROM or OS files).
- [ ] Physical PAL/NTSC ECS/AGA machines, analogue/CD32 devices, 68000 performance,
  other RTG cards and long-duration audio/display lifecycle QA.

## Prototype boundaries

Orbits and moving ports now use astronomical scales; flight velocity is relative to
its translated body frame or the system frame. Gravity uses the nearest body and includes
solar gravity in the system frame outside body influence regions. Ships collide
with terrain and station docking walls; decorative city buildings and forests do not
have collision geometry. Careers use bounded local encounters, three faction records and six hulls. Contracts,
markets and commander saves remain bounded; civilian production, consumption and trade
fleets are not simulated.
No claim is made that stock A500 frame rates or all RTG cards have been validated.
