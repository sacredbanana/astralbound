/* SPDX-License-Identifier: Unlicense
   A planet's appearance at orbital scale, evaluated from the same seeded
   noise as its terrain. Coastlines are the terrain's own sea-level contour
   of the continent field, ice caps its own latitude threshold, so a world
   seen from orbit has exactly the coasts and caps the commander lands on.
   Lattice values are cached per body; every evaluation is bit-identical to
   terrain.c's noise at the same body-fixed direction.

   Directions are body-fixed unit vectors in Q16, as unit(v, 65536) makes. */
#ifndef ASTRALBOUND_PLANET_FIELD_H
#define ASTRALBOUND_PLANET_FIELD_H
#include "astralbound.h"
#define FIELD_CONTINENT 10 /* lattice points per axis at spacing 2^14 */
#define FIELD_REGION 18    /* at spacing 2^13 */
#define FIELD_DETAIL_CACHE 1024 /* recently used points of the 2^11 lattice */
typedef struct {
    uint32_t seed;
    int type, flat; /* flat: catalog bodies without generated relief */
    int16_t continent[FIELD_CONTINENT * FIELD_CONTINENT * FIELD_CONTINENT];
    int16_t region[FIELD_REGION * FIELD_REGION * FIELD_REGION];
    /* The fine albedo lattice (66^3 points) is cached as it is used. */
    uint32_t detail_key[FIELD_DETAIL_CACHE];
    int16_t detail_value[FIELD_DETAIL_CACHE];
} PlanetField;
void planet_field_init(PlanetField *f, const Planet *p);
/* The continent noise, 0..4095, exactly as terrain_elevation samples it. */
int planet_field_continent(const PlanetField *f, Vec n);
/* Height above sea level before local landforms (OCEAN worlds), metres. */
int planet_field_base_height(const PlanetField *f, Vec n);
/* Orbital ground (terrain_material's classes: 0 water, 1 lowland, 2 forest,
   3 rock/desert, 4 ice, 5 gas) from the planet-scale fields. Sea, land and
   ice match terrain_material except at kilometre-scale inlets. Local
   landforms decide lowland, forest and rock below pixel size from orbit, so
   land also reports their expected Q8 shares (lowland, forest, rock; sum
   256), which a renderer can blend; the return value is the largest. */
int planet_field_land(const PlanetField *f, Vec n, int mix[3]);
/* The same from a base height the caller already has (for example one
   interpolated between exact samples), so coasts stay sub-pixel smooth. */
int planet_field_classify(const PlanetField *f, Vec n, int base_height, int mix[3]);
int planet_field_material(const PlanetField *f, Vec n);
/* terrain_regional_albedo, exactly: broad reflectance -10..10. */
int planet_field_regional_albedo(PlanetField *f, Vec n, int material);
#endif
