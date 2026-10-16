# Astralbound astronomical catalog

Snapshot retrieved on 16 October 2026. This is a bounded game catalog, with
explicit provenance; it is not a live astronomical database or precision ephemeris.

## Stars

`stars.csv` contains 94 real stellar systems/entries, including Sol, the nearest
named HYG systems, Tau Ceti, 61 Cygni, Epsilon Indi, Polaris, Rigel, Betelgeuse,
Antares, Deneb and the Luhman 16 brown-dwarf pair. Proxima is a separate, precisely
positioned destination within the wider Alpha Centauri triple; Alpha Centauri AB
shares one destination. Search includes catalog aliases (Sun, Alpha Centauri C,
Epsilon Eridani, 40 Eridani, and others). Components of unresolved close binaries
share their system's catalog position. Some additional companions are not modeled.

Source: [David Nash's HYG v4.1](https://github.com/astronexus/HYG-Database/tree/main/hyg/CURRENT),
compiled from Hipparcos, Yale and Gliese catalogs. The HYG IDs in the CSV identify
each source record. Coordinate extraction uses HYG Cartesian parsecs multiplied
by 3261.563777 to give millilightyears. X points towards RA 0h, Y north and Z RA 6h;
Sol is exactly (0,0,0). Rounding is at most 0.0005 ly per axis. Pairwise map and jump
distances use these coordinates, not rounded sector IDs. Distances and spectral
classifications inherit the source's age and observational uncertainty, particularly
for distant giants and unresolved multiple systems; no proper-motion propagation
is performed. Positions are in the HYG J2000 reference frame.

HYG-derived CSV and `src/star_catalog.inc` are adapted data under **CC BY-SA 4.0**,
with attribution to David Nash / Astronomy Nexus. See `LICENSE-HYG.txt` and
[the license](https://creativecommons.org/licenses/by-sa/4.0/).
The game's original code remains Unlicense.

Explicit companion classifications supplement HYG's primary spectra for Alpha Cen,
Sirius, Procyon, Keid, Groombridge 34, Struve 2398, Kruger 60, EZ Aqr, p Eridani,
Porrima, Castor, Capella, 61 Cygni, Epsilon Indi, Antares and Polaris. These are
representative classifications; binary orbit solutions and stellar radii in flight
are gameplay approximations. See [ESO's Alpha Centauri description](https://www.eso.org/public/news/eso0307/).
Luhman 16's designation supplies its sky direction; the adopted 6.5 ly distance is
from [NASA's Hubble description](https://science.nasa.gov/missions/hubble/hubble-applauds-waltzing-dwarfs/).

Reproduce the extract with a downloaded `hygdata_v41.csv`:

```sh
python3 tools/import_stars.py /path/to/hygdata_v41.csv
python3 tools/compile_stars.py
```

The second command is offline and rebuilds the checked-in C include from the CSV.
The compiler assigns unique sector keys; Sol uses key (0,-1,0) so the original Hetha
starting system and its saved world/port indices remain intact. Sector keys are
identities, not measured coordinates. Fictional systems have deterministic positions
within their sectors. The spiral galaxy, settlements, economies and all non-Sol
planetary systems are fictional. Overview points sample occupied sectors; zooming
to 8 ly or closer resolves every system in the local 17-sector cube. Real catalog,
visited, current, target and waypoint entries are included independently of sampling.

## Sol

Sol contains the Sun plus **484 navigable bodies**:

- Eight planets, with J2000 orbital elements, mean radii and sidereal periods.
- Five recognized dwarf planets: Ceres, Pluto, Eris, Haumea and Makemake.
- Every unique satellite in the retrieved **JPL planetary satellite mean-element
  table: 459 moons**, including provisional designations and Pluto's five moons.
- Dysnomia, Hi'iaka, Namaka and Makemake's MK2: **463 moons in total**.
- Vesta, Pallas, Hygiea, Eros, Halley, 67P, Arrokoth and Sedna.

Sources:
[JPL planetary orbital elements](https://ssd.jpl.nasa.gov/planets/approx_pos.html),
[JPL physical parameters](https://ssd.jpl.nasa.gov/planets/phys_par.html),
[JPL satellite elements](https://ssd.jpl.nasa.gov/sats/elem/),
[JPL satellite physical parameters](https://ssd.jpl.nasa.gov/sats/phys_par/),
[NASA dwarf planet moons](https://science.nasa.gov/solar-system/moons/facts/).

`solar-moons.csv` retains NAIF IDs, source epochs, reference frames, eccentricities,
angles, and missing physical measurements. Puck appears twice under alternative
solutions in the source HTML; the final published row is retained once. This is
complete for that planetary satellite table, not an exhaustive catalog of satellites
of small asteroids, dwarf-planet candidates, or discoveries absent from the table.

`solar-primary.csv` records the planets, dwarf planets and representative minor
bodies. Planet elements follow the JPL J2000 table; minor-body elements and phases
are representative rather than date-accurate Horizons solutions. Four dwarf moons
outside the JPL table have approximate sizes/orbits and unknown phase/plane.

The fixed-point Kepler solver includes eccentricity, periapsis, inclination and
ascending node. Satellite mean anomalies are advanced from their individual source
epochs to J2000. Laplace/equatorial planes are converted to the common ecliptic frame;
planetary gravity and rotation remain bounded game models. There are no N-body
perturbations, tidal dynamics, planetary oblateness, real axial tilts, comet tails,
or DE440 accuracy. Atmospheres and Earth/Mars/Moon ports are game approximations.

Only 46 satellite rows have physical parameters in the physical table. Other small
moons use a 1 km collision/render proxy and minimum game gravity; DATA labels these
as **unknown/proxy geometry**. They are not claimed to have measured 1 km radii.
The four supplemental dwarf moons also carry this label. Small catalog bodies omit
procedural terrain mountains to avoid terrain larger than the body itself.

Reproduce the satellite extract from downloaded HTML:

```sh
python3 tools/import_solar.py /path/to/elements.html /path/to/physical.html
python3 tools/compile_solar.py
```

The compiler is offline and needs only Python's standard library. The C data is
embedded into every executable; Amiga users need no CSV files or network access.
Sol's body capacity is 512, while procedural generation retains its 12-body cap.
Terrain globe caches retain 12 slots and skip unresolved catalog bodies. Sol's full
survey bitset and the optional galaxy waypoint are stored in save format 8; formats
3–7 migrate with empty new records. Native regression fixtures need a 1 MiB stack;
the game retains its documented 256 KiB CLI/Workbench stack requirement.
