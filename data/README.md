# Astralbound astronomical catalog

Snapshot retrieved on 16 October 2026. This is a bounded game catalog, with
explicit provenance; it is not a live astronomical database or precision ephemeris.

## Stars

`stars.csv` contains 685 real stellar systems/entries: every star in the IAU
Catalog of Star Names, every other proper name in HYG, and the original 94 entries
(Sol, the nearest named HYG systems, Tau Ceti, 61 Cygni, Epsilon Indi and the Luhman
16 brown-dwarf pair). The original 94 come first and keep their sector keys; the rest
follow nearest first. Proxima is a separate, precisely
positioned destination within the wider Alpha Centauri triple; Alpha Centauri AB
shares one destination. Search includes catalog aliases (Sun, Alpha Centauri C,
Epsilon Eridani, 40 Eridani, and others). Components of unresolved close binaries
share their system's catalog position. Some additional companions are not modeled.

### Named stars

`iau-names.csv` is the IAU Working Group on Star Names catalog of 652 names, as
published on [exopla.net](https://exopla.net/star-names/modern-iau-star-names/) on
30 October 2026 (names adopted up to 15 October 2026). Names are folded to ASCII for
the game font (Batsũ̀ becomes Batsu); the IAU spelling stays in the CSV. Each star's
Bayer or Flamsteed letter (spelled out: "Eta Tau") and catalog designation (HR, HD,
WASP-121 ...) become search aliases. A second IAU name for a star already included
becomes an alias: Pulcherrima of Izar, Rigil Kentaurus of Alpha Centauri, and Siwarha
(Betelgeuse's companion) of Betelgeuse. HYG proper names outside the IAU list
(Graffias, La Superba ...) are included too; 3C 273, a quasar, and secondary
components named "... B" are not.

Hipparcos stars take their HYG positions. The 77 IAU stars HYG cannot place
(exoplanet hosts, pulsars, protostars and Galactic Centre stars, plus a few HYG
rows without parallax) come from `extra-stars.csv`, whose `source` column records
each distance: NASA Exoplanet Archive system distances and spectral types
(a dwarf class from effective temperature where none is listed), SIMBAD Gaia DR3
parallaxes, or a published distance where the parallax is missing or unusable
(Calvera 3.3 kpc, Mereghetti et al. 2021; Garnet Star 940 pc, Davies & Beasor 2020;
cloud distances for the Taurus, rho Ophiuchi and Orion protostars; 8 kpc for the
Galactic Centre's Pistol and Peony stars). Where SIMBAD lists no spectral type the
row says the class is representative. Pulsars use the spectral tag `PSR` (neutron
star), Wolf-Rayet classes play as O stars and the luminous blue variable as a giant.
These distances are far less certain than the Hipparcos ones.

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

Reproduce the extract with a downloaded `hygdata_v41.csv` and a saved copy of the
WGSN table page:

```sh
python3 tools/import_iau_names.py /path/to/modern-iau-star-names.html
python3 tools/import_stars.py /path/to/hygdata_v41.csv
python3 tools/compile_stars.py
python3 tools/compile_population.py
```

`extra-stars.csv` is curated by hand from the sources above. The last two commands
are offline and rebuild the checked-in C includes from the CSVs. The compiler also
writes a sector-sorted index, so looking up a catalog star is a binary search.
The compiler assigns unique sector keys, moving a taken key to the nearest free one
(Alpha Centauri and Kautoki) and never using the nine starter keys; Sol uses key (0,-1,0) so the original Hetha
starting system and its saved world/port indices remain intact. Sector keys are
identities, not measured coordinates. Fictional systems have deterministic positions
within their sectors. The spiral galaxy, settlements, economies and all non-Sol
planetary systems are fictional. Overview points sample occupied sectors; zooming
to 8 ly or closer resolves every system in the local 17-sector cube. Real catalog,
visited, current, target and waypoint entries are included independently of sampling.

## Finite population

The total is exactly **1,000,000,000 systems for every seed**, including all 685
catalog destinations and nine starter systems. `tools/compile_population.py`
apportions the remaining 999,999,306 systems across a finite fictional disc,
20,000 ly in diameter and 600 ly thick. Only cells completely inside the radial
boundary receive population; real catalog positions remain unchanged even outside
that disc. There is no generated background population beyond the boundary.

`src/population.inc` contains exact integer cell quotas. An invertible integer
permutation selects unique sector keys in each cell. Reserved catalog/starter keys
are skipped by the permutation and counted separately. The same existence test
is used by generation, maps and jumps. The inverse mapping lets the overview
sample actual systems without storing a billion records. Regenerate the quotas
when changing the real-star catalog:

```sh
python3 tools/compile_population.py
```

## Sol

Sol contains the Sun plus **34 navigable bodies**:

- Eight planets, with J2000 orbital elements, mean radii and sidereal periods.
- Five recognized dwarf planets: Ceres, Pluto, Eris, Haumea and Makemake.
- Moon, Phobos, Deimos, Io, Europa, Ganymede and Callisto.
- Mimas, Enceladus, Tethys, Dione, Rhea, Titan and Iapetus.
- Miranda, Ariel, Umbriel, Titania, Oberon, Triton and Charon.

These 21 major moons are the explicit gameplay selection in `solar-selection.json`.
Minor bodies, small irregular/provisional moons and the other dwarf-planet moons
are omitted from the active catalog. `solar-v8-layout.csv` contains old names and
indices only for save migration; it does not generate any bodies.

Sources:
[JPL planetary orbital elements](https://ssd.jpl.nasa.gov/planets/approx_pos.html),
[JPL physical parameters](https://ssd.jpl.nasa.gov/planets/phys_par.html),
[JPL satellite elements](https://ssd.jpl.nasa.gov/sats/elem/),
[JPL satellite physical parameters](https://ssd.jpl.nasa.gov/sats/phys_par/).

`solar-moons.csv` retains the selected moons' NAIF IDs, source epochs, reference
frames, eccentricities, angles and physical measurements. The importer applies the
selection file so a future source refresh cannot silently restore the full small
moon catalog. `solar-primary.csv` records the eight planets and five dwarf planets.

The fixed-point Kepler solver includes eccentricity, periapsis, inclination and
ascending node. Satellite mean anomalies are advanced from their individual source
epochs to J2000. Laplace/equatorial planes are converted to the common ecliptic frame;
planetary gravity and rotation remain bounded game models. There are no N-body
perturbations, tidal dynamics, planetary oblateness, real axial tilts, comet tails,
or DE440 accuracy. Atmospheres and Earth/Mars/Moon ports are game approximations.

Reproduce the selected satellite extract from downloaded HTML:

```sh
python3 tools/import_solar.py /path/to/elements.html /path/to/physical.html
python3 tools/compile_solar.py
```

The compilers are offline and need only Python's standard library. C data is
embedded into every executable; Amiga users need no CSV files or network access.
Sol's body capacity remains 512, while procedural generation retains its 12-body
cap and terrain globe caches retain 12 slots. Save format 9 remaps old Sol survey,
target, flight-frame and rig indices by identity. Removed bodies relocate a local
commander to Earth; removed systems relocate to Hetha. Removed rigs refund 500 CR;
contracts to removed destinations cancel without a reputation penalty. Historical
exploration and trade records remain. Native regression fixtures need a 1 MiB stack;
the game retains its documented 256 KiB CLI/Workbench stack requirement.


### Atmospheric metadata

The primary and moon CSVs explicitly record bulk gas family and visible haze.
These are separate from the climate category used by bounded density/weather
rules. Pluto's `THIN` atmosphere is explicitly N2/CH4 (with trace CO in its
composition sheet), with blue haze; it is not inferred to be CO2. Titan has a
cold N2/CH4 atmosphere with organic haze, and Triton has thin N2/CH4/CO air with
blue haze. Titan and Triton no longer receive the default vacuum assigned to
other moons. Uranus/Neptune explicitly include methane absorption, distinct
from the hydrogen/helium giants.

The catalogue compiler consumes these fields and the JPL importer preserves
them during orbital-data refreshes. `atmosphere_km` remains a finite gameplay
cutoff, not a measured outer boundary; Titan uses 600 km and Triton 50 km.
Airless bodies may still have trace exospheres listed in their physical sheets;
these are not treated as bulk scattering atmospheres.

Sources: [NASA Pluto facts](https://science.nasa.gov/dwarf-planets/pluto/facts/),
[NASA Pluto composition sheet](https://nssdc.gsfc.nasa.gov/planetary/factsheet/plutofact.html),
[NASA Titan facts](https://science.nasa.gov/saturn/moons/titan/facts/),
[NASA Triton](https://science.nasa.gov/neptune/moons/triton/).

### Surface and cloud-top maps

Sixteen Sol bodies take their planet-scale appearance from measured global maps
instead of seeded noise: Earth's coasts, deserts and ice sheets; Mars's relief,
albedo and residual caps; the Moon's relief and maria; Mercury; Jupiter's belts
and Red Spot; Saturn, Uranus and Neptune's bands; Io, Europa, Ganymede,
Callisto, Pluto and Charon. Venus and Titan show their opaque cloud deck or haze
from orbit. Local landforms, ground texture and weather stay procedural on top.

Each source was area-averaged to 1 degree by `tools/import_maps.py` into
`data/maps/*.pgm` (equirectangular, -180..180 east, north up; heights 16-bit with
their metre range in a `# metres` comment). `tools/compile_maps.py` bakes those,
with the per-body choices in `maps.csv` (cube-map size, relief scale, albedo
contrast, palette ramps), into `src/body_maps.inc`: about 120 KB of cube maps,
24 to 48 cells per face edge. Both steps are offline; the second needs only
Python's standard library, the first numpy and macOS `sips`.

| Body | Product | Source |
|---|---|---|
| Earth height and depth | ETOPO5, 5 arc-minute (`ETOPO5.DAT`) | [NOAA NGDC](https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO5/TOPO/ETOPO5/ETOPO5.DAT) |
| Earth deserts and ice | Blue Marble 2002, land surface (`land_shallow_topo_2048.jpg`) | [NASA Earth Observatory](https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57752/land_shallow_topo_2048.jpg) |
| Mars relief | MOLA MEGDR 4 px/deg (`megt90n000cb.img`) | [PDS Geosciences](https://pds-geosciences.wustl.edu/mgs/mgs-m-mola-5-megdr-l3-v1/mgsl_300x/meg004/megt90n000cb.img) |
| Mars albedo and caps | TES bolometric albedo | [USGS Astrogeology](https://planetarymaps.usgs.gov/mosaic/Mars_MGS_TES_Albedo_mosaic_global_7410m.tif) |
| Moon relief | LOLA LDEM_4 | [PDS Geosciences](https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/cylindrical/img/ldem_4.img) |
| Moon albedo | LROC WAC colour, CGI Moon Kit | [NASA SVS 4720](https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_1k.jpg) |
| Mercury | MESSENGER MDIS basemap, 1024 preview | [USGS Astrogeology](https://astrogeology.usgs.gov/ckan/dataset/17ebee80-ed66-485a-9b06-5ffd10e935e3) |
| Jupiter | Cassini cylindrical map, December 2000 (PIA07782) | [NASA/JPL/SSI](https://assets.science.nasa.gov/content/dam/science/psd/photojournal/pia/pia07/pia07782/PIA07782.jpg) |
| Saturn, Uranus, Neptune | Hubble OPAL global maps (Saturn 2025a, Uranus 2024a, Neptune 2025b) | [STScI MAST](https://archive.stsci.edu/hlsps/opal/) |
| Io, Europa, Ganymede, Callisto | Galileo SSI/Voyager global mosaics, 1024 previews | [USGS Astrogeology](https://astrogeology.usgs.gov/ckan/dataset) |
| Pluto, Charon | New Horizons global mosaics, July 2017, 1024 previews | [USGS Astrogeology](https://astrogeology.usgs.gov/ckan/dataset) |

Retrieved on 30 October 2026. Each product's longitude convention was checked
against a landmark, not assumed: Caloris at 162.7E, Pele at 255W, Europa's dark
trailing hemisphere, Ganymede's Galileo Regio, Callisto's Asgard, Sputnik
Planitia west in Pluto's heart, and the turbulent wake trailing west of Jupiter's
Red Spot. `tests/test_planet.c` asserts landmarks on the baked maps: oceans,
deserts, forests and ice sheets on Earth, the Moon's maria and South Pole-Aitken,
Olympus Mons above Hellas, Syrtis Major and the Martian caps, Jupiter's belts.

Approximations:
- Earth's dryness is a colour index of the cloud-free Blue Marble (red over green,
  and bright); land brighter than its margin is desert, and vegetation reaches up
  to rock above a base height of 900 m. Ice is the bright grey land poleward of
  55 degrees, and all land south of 60 S. Seasonal snow and sea ice are not mapped.
- Heights are folded into the generator's ranges. Earth keeps every coast exactly;
  deep ocean and high plateaus compress into -1450..1750 m of base height. Mars and
  the Moon add a tenth of their measured relief to the local landforms.
- Saturn and the ice giants keep only their zonal bands: their spots change within
  months, and Hubble sees part of each. Latitudes Hubble could not see (Uranus's
  south, Neptune's far north) repeat the nearest observed band. Jupiter keeps its
  two-dimensional map, by whiteness (blue over red) rather than brightness, since
  the Red Spot is redder than its zone but not darker than a belt; it is blended
  halfway to each latitude's median so belts stay continuous. The Red Spot is
  where Cassini saw it in 2000, and all bands rotate together with System III.
- Only Saturn draws rings. Jupiter's, Uranus's and Neptune's are faint dust and dark
  ice, all but invisible in sunlight, and are left out.
- Unimaged regions (Pluto and Charon's south, edges of the Galilean mosaics) take
  the body's median brightness.
- Images are brightness, not calibrated albedo, apart from TES; each body's map is
  relative to its own median, with a contrast chosen per body in `maps.csv`.
- Map north is body -z: the game's world is left-handed and its bodies spin from
  +x towards +y, so this keeps every map unmirrored and the sun rising in the east.
  Spin axes are not tilted to their real poles (see above), so the Moon does not
  keep its near side towards Earth.

Credits: NOAA NGDC; NASA Earth Observatory (Reto Stöckli); NASA/GSFC MOLA and LOLA
science teams via PDS; P. Christensen (ASU) and USGS; NASA's Scientific
Visualization Studio (Ernie Wright); NASA/JPL/Space Science Institute; NASA/ESA HST,
OPAL program (PI: Simon, GO13937), archived by STScI, doi:10.17909/T9G593;
NASA/JPL-Caltech, JHUAPL, CIW, SwRI and USGS Astrogeology. All are US-government
works except Blue Marble and the SVS kit (free with credit) and OPAL (NASA/ESA,
free with the acknowledgement above).

Reproduce the maps from the downloaded files, each in its own folder under one
directory (`earth-height/`, `earth-colour/`, `mars-height/`, `mars-albedo/`,
`moon-height/`, `moon-albedo/`, `mercury/`, `jupiter/`, `saturn/`, `uranus/`,
`neptune/`, `io/`, `europa/`, `ganymede/`, `callisto/`, `pluto/`, `charon/`):

```sh
python3 -I tools/import_maps.py /path/to/sources
python3 tools/compile_maps.py
make sol-preview   # both renderers, ECS and AGA, in build/evidence/
```
