# Reference research — 12 October 2026

Space uses original code and content. The references below inform behaviour and
visual goals; they are not game assets and no reverse-engineered Frontier source
was imported.

## Gameplay scope

The original manual describes three flight modes, momentum and gravity, planetary
landings, orbital docking, inter-system navigation, time compression, trade,
ship/equipment purchases, crew, passengers, contracts, and asteroid/surface mining.
It also covers weapon types, missiles, countermeasures, fuel handling, law and
police, system data, and multiple camera views. These are full-game requirements;
the current prototype implements only the subset identified in the README.

Primary reference: [Frontier: Elite II manual transcript](https://www.x-speed.de/computer/elite/manual_txt_frontier.html).

Mission research also considers delivery, passenger, assassination, military
reconnaissance and installation-attack work. Space's first contract templates are
courier, freight, orbital patrol, atmospheric patrol, survey and bounty.
See the [mission guide](https://gamefaqs.gamespot.com/cd32/971966-frontier-elite-ii/faqs/80509/missions)
and [Frontierverse](https://sharoma.com/frontierverse/).

## Screenshots actually inspected

The Amiga CD32 [city screenshot](https://sharoma.com/frontierverse/images/pictures/cd32/city.png)
shows solid polygon buildings, simple green ground, blue atmospheric bands,
sparse clouds and a compact cockpit strip. The [space screenshot](https://sharoma.com/frontierverse/images/pictures/cd32/space.png)
shows a sparse star field, central sight, speed/relative-frame readouts and the
same compact lower controls. Space adopts the broad visual approach with its own
palette, glyphs, cockpit, cities and ship geometry.

The [reference gallery](https://www.mobygames.com/game/802/frontier-elite-ii/screenshots/)
was searchable, but direct inspection there was blocked by site verification;
the screenshots above were inspected through the browser instead.

## Failure modes to prevent

The [Frontierverse flight guide](https://sharoma.com/frontierverse/basics.htm)
reports autopilots flying straight through intervening planets, atmospheric
burn-up and orbiting without arriving. Its passenger discussion also describes
failed contracts leaving occupied cabins behind. Space's tests must cover route
obstacles, bounded braking, hazard-triggered compression reduction, terminal
contract states and resource release. Enemy collision avoidance is additionally
an explicit user requirement.

The historical [flight guide](https://gamefaqs.gamespot.com/cd32/971966-frontier-elite-ii/faqs/80509/basic-ship-operations)
also documents manual docking/landing hazards. Crash-free does not mean invulnerable:
manual surface impacts, excessive re-entry speed and hostile fire remain intended
hazards. Passing bounded regression scenarios is not proof of absence of all bugs.

## Amiga architecture sources

API signatures and lifecycle details were checked in the Amiga Guru SDK cache,
corresponding to [graphics autodocs](https://github.com/sacredbanana/AmigaSDK-gcc/blob/master/amigaos3/docs/Autodocs/graphics.doc),
[intuition autodocs](https://github.com/sacredbanana/AmigaSDK-gcc/blob/master/amigaos3/docs/Autodocs/intuition.doc),
[exec autodocs](https://github.com/sacredbanana/AmigaSDK-gcc/blob/master/amigaos3/docs/Autodocs/exec.doc),
[timer autodocs](https://github.com/sacredbanana/AmigaSDK-gcc/blob/master/amigaos3/docs/Autodocs/timer.doc),
and [CyberGraphX autodocs](https://github.com/sacredbanana/AmigaSDK-gcc/blob/master/amigaos3/docs/Autodocs/cybergraphics.doc).
The [Commodore hardware manual's CIA register map](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node012E.html)
identifies the vertical-sync event counter used for native timing. AGA fetch setup
is verified through emulator output, including the 320-pixel row layout.
