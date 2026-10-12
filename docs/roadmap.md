# Space full-game work

The user requested a full, original Frontier-style Amiga game. The current
prototype establishes flight, world generation and platform integration. This
list remains part of that request; it is not a claim of feature completeness.

## World and navigation

- Real astronomical scales, hierarchical local frames and a star at the system centre.
- Moving orbital bodies, moons, rings, binary stars and varied atmospheric models.
- Persistent exploration records, sparse settlement and galaxy density/spiral arms.
- Terrain LOD stitching, forests/roads, clouds, coastlines and richer generated cities.
- Station rotation, clearance queues, dock geometry and traffic corridors.
- Autopilot tests across every generated body type, authority limit and relative frame.

## Careers and economy

- Passenger cabins and transport, crew recruitment/wages and ship-specific crew needs.
- Escort, rescue, assassination, reconnaissance, surface-installation attack and
  multi-leg contracts with negotiable terms and reputation gates.
- Original factions, military ranks, medals, reputation and legal status.
- Persistent markets and stock, contraband/black markets, police and piracy.
- Asteroid mining, surface mining rigs, cargo collection and fuel scooping.
- Ship catalogue with distinct geometry, mass, acceleration, equipment and hardpoints.
- Missiles, countermeasures, multiple weapon mounts, turrets and escape pods.

## Presentation and hardware

- Turret views, targeting/radar, a system orbital map and richer system data.
- Original procedural sound effects and music; the current build is silent.
- Mouse and joystick flight; the current native input path is keyboard only.
- NTSC native modes and lower-resolution workload options for slow machines.
- Tuned 68000/020 rasterization, native blitter work, measured C2P and CPU variants.
- RTG high-colour output and card-specific acceleration where the driver exposes it.
- A release installer, Workbench icons, ADF/bootable distribution and hardware QA.

## Prototype boundaries

Planets are deliberately small and remain at static generated positions. Gravity
uses the nearest planet. Ships do not currently collide with city buildings or
station walls. Hostile targets are simple mission encounters with a standoff
controller. Contracts, markets and commander saves are bounded rather than a full
persistent world economy. The renderer uses a painter's list and a camera-centred
terrain patch; LOD seams and polygon ordering still need production treatment.
No claim is made that stock A500 frame rates or all RTG cards have been validated.
