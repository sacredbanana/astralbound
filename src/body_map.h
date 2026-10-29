/* SPDX-License-Identifier: Unlicense
   Measured maps of Solar System bodies: Earth's coasts and deserts, the
   Moon's maria, Mars's dark plains and caps, the giants' belts and spots.
   Each channel is a cube map of (n + 1)^2 lattice points per face, with
   points on face edges shared, baked offline from NASA/USGS data by
   tools/compile_maps.py (see data/README.md). Body -z is the positive
   (spin) pole and +x the prime meridian, east towards +y: the body spins
   from +x towards +y, so maps show unmirrored and the sun rises in the
   east (Uranus, spinning backwards, has its IAU north at +z). Sampling is
   bilinear on the face a direction falls on, in 32-bit integer arithmetic,
   so collision, terrain and both renderers read identical values on every
   CPU.

   The maps replace only the planet-scale fields: the continent noise, the
   regional albedo and polar ice. Local landforms, ground texture and
   weather stay procedural on top of them. */
#ifndef ASTRALBOUND_BODY_MAP_H
#define ASTRALBOUND_BODY_MAP_H
#include "astralbound.h"
/* Palette ramps a body may use instead of its material's own. */
enum {
    RAMP_MATERIAL, RAMP_GREY, RAMP_RUST, RAMP_TAN, RAMP_BROWN, RAMP_CLOUD,
    RAMP_SKY, RAMP_DEEP, RAMP_SLATE, RAMP_TYPES
};
#define BODY_MAP_VEIL 1 /* an opaque cloud deck or haze hides the ground from orbit */
#define BODY_MAP_ARID 2 /* land brighter than the median is desert (Earth) */
struct BodyMap {
    const char *name;
    int n, flags;
    const uint8_t *continent; /* ocean worlds: the continent field / 16 */
    const uint8_t *relief;    /* other worlds: 128 + metres / relief_step */
    int relief_step;
    const uint8_t *albedo;    /* 128 at the body's median reflectance */
    const uint8_t *ice;       /* 128 at the ice margin, frozen above */
    uint8_t ramp[6];          /* per material, RAMP_MATERIAL keeps the default */
    uint8_t dark_ramp;        /* gas and veil: below-median albedo, or 0 */
};
const BodyMap *body_map_find(const char *name);
/* One channel at a body-fixed direction of any length, as 256 * value. */
int body_map_sample(const BodyMap *m, const uint8_t *channel, Vec direction);
/* The planet-scale fields the procedural generator would otherwise supply,
   in its own units, at a body-fixed Q16 direction. */
int body_map_continent(const BodyMap *m, Vec n);      /* 0..4095 */
int body_map_relief(const BodyMap *m, Vec n);         /* metres */
int body_map_region(const BodyMap *m, Vec n);         /* 0..4095, dark below 2048 */
int body_map_regional_albedo(const BodyMap *m, Vec n);/* -8..7 */
int body_map_ice(const BodyMap *m, Vec n);            /* >= 0 frozen */
#endif
