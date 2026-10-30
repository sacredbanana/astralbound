/* SPDX-License-Identifier: Unlicense
   Coastlines as closed loops of body-fixed directions: the sea-level
   contour of planet_field's continent noise, the same coasts the landed
   terrain has. Frontier: Elite II stored continents as polygons in its
   model data; Astralbound derives them from the planet's seed, so a
   renderer that fills polygons (the Classic renderer) draws real coasts.

   Extraction is marching squares on the six cube faces. Crossings are keyed
   by their 3D lattice edge, so loops stitch seamlessly across faces. */
#ifndef ASTRALBOUND_PLANET_CONTOURS_H
#define ASTRALBOUND_PLANET_CONTOURS_H
#include "planet_field.h"
#define CONTOUR_POINTS 4096
#define CONTOUR_LOOPS 512
typedef struct { int16_t x, y, z; } ContourPoint; /* unit direction, Q14 */
/* A loop's points, and the cap holding them all: centre direction (Q14)
   and the cosine of its angular radius (Q14), so a renderer can drop loops
   on the far side of the planet with one dot product. */
typedef struct { uint16_t first, count; int16_t cx, cy, cz, cosr; } ContourLoop;
typedef struct {
    uint32_t seed;
    int resolution, kind, loops, points, truncated;
    unsigned positive_corners; /* field sign at the eight cube corners */
    ContourLoop loop[CONTOUR_LOOPS];
    ContourPoint point[CONTOUR_POINTS];
} PlanetContours;
/* Which contour: the coast (sea level), the highland edge where inland
   rock begins to dominate, the edge of dark regional albedo, the geographic
   polar ice margin, or the edge of a giant's deep spots (its map's deep
   ramp, regional albedo -5 or less: Jupiter's Red Spot). */
enum { CONTOUR_COAST, CONTOUR_HIGHLAND, CONTOUR_ALBEDO, CONTOUR_ICE, CONTOUR_DEEP };
/* Contour loops at `resolution` cells per cube-face edge (4 to 48). Coasts
   and highlands exist only on ocean worlds. Returns the loop count. */
int planet_contours_build(PlanetContours *c, PlanetField *f, int resolution, int kind);
/* Positive side of the extracted loops at a body-fixed Q14 direction.
   Use this for fill parity: the original field can disagree with the
   simplified contour close to an edge. */
int planet_contours_positive(const PlanetContours *c, ContourPoint direction);
#endif
