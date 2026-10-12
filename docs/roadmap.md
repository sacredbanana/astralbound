# Space full-game work

The user requested a full, original Frontier-style Amiga game. The current
prototype establishes flight, world generation and platform integration. This
list remains part of that request; it is not a claim of feature completeness.

## World and navigation

Implemented in the current development build:

- [x] Astronomical metre scales, translated hierarchical local frames, and a central star
  (or a binary pair orbiting the system barycentre).
- [x] Analytic moving orbits, parented moons, rendered rings, and vacuum, thin,
  temperate, dense, frozen and hydrogen atmospheric models.
- [x] Portable exploration journals, sparse settlement, and star occupancy following
  galaxy disc density, a central bulge and four spiral arms.
- [x] Continuous terrain LOD edges and outer skirts, anchored forests, generated city
  streets, moving sky clouds, coastlines and varied city blocks/towers.
- [x] Rotating orbital stations, traffic clearance queues, swept hollow dock geometry,
  marked approach corridors and moving-port velocity matching.
- [x] Regression coverage for all six generated body types, 58 generated port arrivals,
  launch/re-entry, lost thrust authority, local/system frame transitions, clearance
  queues and portable exploration/approach state.

Q cycles body waypoints, T cycles inhabited ports, Y shows the selected destination
list, and D requests clearance. Autopilot requests clearance automatically. Empty
sectors cannot be jumped to; unsettled star systems can be explored using body targets.

These are bounded procedural game models: circular inclined orbits rather than an
N-body solver, a 128-system exploration journal, decorative forests/buildings/clouds,
and scheduled clearance traffic rather than a persistent civilian fleet. The local
terrain grid has shared edges and a skirt to the globe, but the renderer still uses
painter sorting. Hardware frame rates and long-duration play remain QA work.

## Careers and economy

- Passenger cabins and transport, crew recruitment/wages and ship-specific crew needs.
- Escort, rescue, assassination, reconnaissance, surface-installation attack and
  multi-leg contracts with negotiable terms and reputation gates.
- Original factions, military ranks, medals, reputation and legal status.
- [x] Independent port stock, scarcity prices, hourly supply recovery, separate buy/sell
  quotes, and a portable ledger of 128 traded ports, with version 3 save migration.
- Contraband/black markets, police and piracy.
- Asteroid mining, surface mining rigs, cargo collection and fuel scooping.
- Ship catalogue with distinct geometry, mass, acceleration, equipment and hardpoints.
- Missiles, countermeasures, multiple weapon mounts, turrets and escape pods.

## Presentation and hardware

- Turret views, targeting/radar, a system orbital map and richer system data.
- Original procedural sound effects and music; the current build is silent.
- Mouse and joystick flight; the current native input path is keyboard only.
- [x] PAL/NTSC native layout selection and a centred PAL `--lowres` 320 x 200 option,
  height-aware bitplanes/C2P and compact menus. NTSC runtime QA, real hardware and performance
  measurements remain QA work (see validation notes).
- Tuned 68000/020 rasterization, native blitter work, measured C2P and CPU variants.
- RTG high-colour output and card-specific acceleration where the driver exposes it.
- A release installer, Workbench icons, ADF/bootable distribution and hardware QA.

## Prototype boundaries

Orbits and moving ports now use astronomical scales; flight velocity is relative to
its translated body frame or the system frame. Gravity uses the nearest body and includes
solar gravity in the system frame outside body influence regions. Ships collide
with terrain and station docking walls; decorative city buildings and forests do not
have collision geometry. Hostile targets remain simple mission encounters. Contracts,
markets and commander saves remain bounded; civilian production, consumption and trade
fleets are not simulated.
No claim is made that stock A500 frame rates or all RTG cards have been validated.
