/* SPDX-License-Identifier: Unlicense
   Classic renderer rasterisation, after Frontier: Elite II. Polygons, curves
   and planets are all reduced to per-scanline x crossings, then filled as
   flat spans into an 8-bit chunky surface: no depth buffer, no per-pixel
   shading, and no division per scanline.

   Coordinates are Q4 subpixels (16 per pixel). A pixel (x, y) is covered
   when its sample point (x, y), the integer coordinate, lies inside, the same
   convention as planet_view, so outlines and features meet exactly. */
#ifndef ASTRALBOUND_CLASSIC_RASTER_H
#define ASTRALBOUND_CLASSIC_RASTER_H
#include <stdint.h>
typedef struct {
    uint8_t *pixels;
    int pitch;
    int left, top, right, bottom; /* drawable: left <= x < right, top <= y < bottom */
} ClSurface;

/* Polygon crossings, filled even-odd (Frontier's SpanRenderer). One table
   serves every shape: convex faces, rings with holes, Bezier outlines. */
#define CL_ROW_CROSSINGS 24
typedef struct {
    int16_t n;
    int16_t x[CL_ROW_CROSSINGS]; /* crossing x, rounded up to a pixel */
} ClRow;
typedef struct {
    ClRow *rows;
    int height;      /* rows allocated: the surface's bottom */
    int top, bottom; /* rows touched since the last fill */
} ClSpans;
void cl_spans_init(ClSpans *s, ClRow *rows, int height);
/* Add one edge, Q4 endpoints; any direction, any length. */
void cl_spans_edge(ClSpans *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
/* A cubic Bezier edge, Q4 control points, by forward differencing. */
void cl_spans_bezier(ClSpans *s, const int32_t p[4][2]);
/* A closed polygon of n Q4 points. */
void cl_spans_polygon(ClSpans *s, const int32_t *xy, int n);
/* A convex polygon of n (3..16) Q4 points filled directly, exactly as
   cl_spans_polygon and cl_spans_fill would; 0 (nothing drawn) if the
   points are not y-monotone, for the general path. */
int cl_convex_fill(const ClSurface *f, const int32_t *xy, int n, int colour);
/* Fill the accumulated crossings in one colour and clear the table. */
void cl_spans_fill(ClSpans *s, const ClSurface *f, int colour);
/* Remap the covered pixels through a 256-entry table instead (shadows,
   fog, darkening), then clear. */
void cl_spans_remap(ClSpans *s, const ClSurface *f, const uint8_t *lut);

/* Planet "body spans": crossings that carry an XOR mask of feature bits.
   Each row is walked from the left limb; a pixel's colour is a function of
   the mask of every feature containing it. Features hidden behind the limb
   repair their parity through row toggles (Frontier's BODY_TOGGLE_COLOUR). */
#define CL_BODY_CROSSINGS 48
typedef struct { int16_t x; uint16_t mask; } ClCross;
typedef struct { int16_t n; ClCross c[CL_BODY_CROSSINGS]; } ClBodyRow;
typedef struct {
    ClBodyRow *rows;
    uint16_t *toggle; /* per row: XOR applied to every row above it */
    int height, top, bottom;
} ClBody;
void cl_body_init(ClBody *b, ClBodyRow *rows, uint16_t *toggle, int height);
void cl_body_begin(ClBody *b, int top, int bottom);
void cl_body_edge(ClBody *b, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t mask);
/* A feature edge left the visible disc through the left limb at row y:
   every row above y starts with mask toggled. */
void cl_body_toggle(ClBody *b, int y, uint16_t mask);
/* Fill rows top..bottom-1 between each row's limb columns [first, last]
   (first > last: empty). bottom_mask is the feature mask at the bottom of
   the disc; colour() maps a mask to a palette index. */
typedef int (*ClBodyColour)(void *ctx, unsigned mask);
void cl_body_draw(ClBody *b, const ClSurface *f, const int16_t *first, const int16_t *last,
                  unsigned bottom_mask, ClBodyColour colour, void *ctx);

/* Filled circle by the midpoint algorithm (Frontier's CIRCLE opcode). */
void cl_disc(const ClSurface *f, int cx, int cy, int r, int colour);
/* Ordered-dither annulus, for glows. density: 0..16 of the 16 Bayer cells. */
void cl_glow(const ClSurface *f, int cx, int cy, int inner, int outer, int colour, int density);
/* Bresenham line, clipped to the surface. */
void cl_line(const ClSurface *f, int x0, int y0, int x1, int y1, int colour);
static inline void cl_pixel(const ClSurface *f, int x, int y, int colour) {
    if (x >= f->left && x < f->right && y >= f->top && y < f->bottom)
        f->pixels[y * f->pitch + x] = (uint8_t)colour;
}
#endif
