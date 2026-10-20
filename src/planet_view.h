/* SPDX-License-Identifier: Unlicense
   Backend-neutral planetary geometry. A sphere seen in perspective is
   bounded by a conic; each scanline crosses its forward cone in one interval.
   These routines solve that interval exactly, at any distance, so renderers
   draw smooth limbs and horizons without triangle meshes. They know nothing
   of framebuffers, depth buffers or palettes: a backend fills the spans its
   own way (shaded and depth-tested, or flat and painter-sorted). */
#ifndef ASTRALBOUND_PLANET_VIEW_H
#define ASTRALBOUND_PLANET_VIEW_H
#include "astralbound.h"
#include "xfloat.h"
/* Pinhole projection in pixels: a camera-space point (X, Y, Z), Y up, maps
   to screen (cx + X * focal / Z, cy - Y * focal / Z). Pixel (x, y) samples
   the ray through those integer coordinates, as the triangle rasteriser does.
   Rows top..bottom-1 and columns left..right-1 are drawable. */
typedef struct {
    int cx, cy, focal;
    int left, top, right, bottom;
} Projection;
enum { PLANET_HIDDEN, PLANET_POINT, PLANET_DISC };
typedef struct {
    int kind;
    int top, bottom;       /* rows that may hold the sphere: top <= y < bottom */
    int left, right;       /* likewise for columns */
    int32_t radius_px;     /* angular radius as Q8 pixels at the centre ray */
    int32_t centre_x, centre_y; /* Q8 projection of the centre, when in front */
    /* Exact row solve. Camera-space centre C, radius R, c = |C|^2 - R^2. */
    XFloat x, y, z, c, a;  /* a = Cx^2 - c, the u^2 coefficient */
    XFloat e2, e1, e0;     /* E(v) = e2 v^2 + e1 v + e0, the row discriminant / c */
    Projection proj;
} PlanetView;
/* centre: camera-space position of the sphere's centre; radius and altitude
   (|centre| - radius, measured exactly by the caller) in the same unit.
   Returns the kind; a camera inside the sphere sees it from just above. */
int planet_view(PlanetView *v, const Projection *proj, Vec centre, int64_t radius, int64_t altitude);
/* The covered interval of row y as Q8 screen x: pixel x is covered when
   x0 <= x * 256 <= x1. Bounds are clamped to one pixel beyond the view.
   Returns 0 for an empty row. */
int planet_row(const PlanetView *v, int y, int32_t *x0, int32_t *x1);
/* The same interval as inclusive pixel columns within the view. */
int planet_row_pixels(const PlanetView *v, int y, int *first, int *last);
#endif
