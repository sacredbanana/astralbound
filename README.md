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
journals, trading, six contract types, combat, upgrades and portable commander saves.

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
Space.000 --ecs
Space.020 --aga
Space.020 --rtg
```

- **ECS:** PAL 320 x 256, 32 colours, double-buffered bitplanes and a private Copper
  list. Requires Kickstart 3.0+ in this prototype, including on a 68000 machine.
- **AGA:** PAL 320 x 256, 256 colours from the 24-bit palette, eight bitplanes,
  64-bit display fetches and double buffering. Use the 68020 executable.
- **RTG:** 640 x 480, 256 colours, driver-managed double buffering. Requires
  CyberGraphX V41+ or Picasso96's compatible CGX API. RTG deliberately keeps the
  OS running because portable graphics-card access depends on its driver.

Native modes allocate all display memory before taking over, suspend scheduling
and interrupts, and poll the hardware keyboard. Saving and loading temporarily
restore the OS. Escape restores the display, DMA, interrupts and task scheduling.
The prototype needs expansion RAM: its C renderer and 64-bit coordinate maths
are not yet tuned to stock A500 performance. NTSC native output is not implemented.

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
frames without opening a window. The host and Amiga use the same simulation and
renderer. `Space-tests.020` runs the full core suite on an Amiga;
`Space-tests.020 --quick` runs the shorter native smoke suite. Full trajectories
remain in the default suite, which can take a long time under emulation.

## First flight

Start docked at Haven. Press **L**, then **P**, to travel to the orbital Relay.
Press **T** to cycle ports, **Q** to cycle body waypoints, or **Y** to see the
selected destination list. Up/Down changes its target; return to flight with **V**
and engage **P**. Body waypoints stop above the atmosphere, including at gas giants.
Orbital stations rotate and move: autopilot matches port velocity, waits for clearance,
and enters through the marked corridor. **D** requests clearance for manual docking.
Request time compression with **]**; the safety controller reduces the effective rate near
terrain, a port, a braking limit or another ship. It never enlarges a physics tick.

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

V cycles front/rear/external views while already in flight.

Flight controls: arrows steer, W/S change set speed, Ctrl fires, E switches flight
assist, K controls landing gear, Space pauses, H opens help, F5 saves, F9 loads,
and Escape exits. At ports F refuels and repairs, O upgrades the laser, and N
buys a larger hull. On the market, 1-8 selects a commodity and +/- buys/sells.
On the contract board, 1-8 selects an offer and Return accepts it.

## Contracts

Courier and freight jobs require docking at the named destination port in the
specified sector. Cargo space is reserved when accepting freight. Orbital
patrols and bounties create a hostile above 13 km. Air patrols create a hostile
between 600 and 6000 m. Survey work requires twenty simulated seconds between
500 and 6000 m at less than 200 m/s. Combat jobs pay on disabling their target;
delivery and survey jobs pay automatically on completion. Rewards are paid once.

Read [Space.guide](Space.guide) on the Amiga for the controls and prototype limits.
Save generator version 3 includes exploration, frame, fuel-impulse and approach state.
Version 2 prototype commanders are rejected rather than loaded into incompatible worlds.

Research sources are in [docs/research.md](docs/research.md). Remaining full-game
work is tracked in [docs/roadmap.md](docs/roadmap.md), and current test evidence in
[docs/validation.md](docs/validation.md).
