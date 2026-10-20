/* SPDX-License-Identifier: Unlicense; Classic span rasteriser. See classic_raster.h. */
#include "classic_raster.h"
#include <string.h>
#define CL_LIMIT 16000 /* crossings clamp here; far beyond any surface */
static int16_t clamp16(int64_t v) { return (int16_t)(v < -CL_LIMIT ? -CL_LIMIT : v > CL_LIMIT ? CL_LIMIT : v); }
/* First pixel at or right of a Q24 x: the pixel whose sample point the
   crossing passes. */
static int ceil24(int64_t x) {
    int64_t v = x >= 0 ? (x + 0xFFFFFF) >> 24 : -((-x) >> 24);
    return clamp16(v);
}
/* An edge's rows and x per row by a DDA: one 64-bit division per edge and
   one 64-bit addition per row (two instructions on a 68k). A 24-bit
   fraction keeps the drift over a full screen below 2^-16 px, so edges
   shared by two polygons always round the same way. Rows sampled:
   y1 <= 16 y < y2 (Q4). */
typedef struct { int y0, y1; int64_t x, step; } ClEdge;
static int edge_setup(ClEdge *e, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int height) {
    int64_t dy;
    if (y1 > y2) { int32_t t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    dy = (int64_t)y2 - y1;
    if (!dy) return 0;
    e->y0 = y1 >= 0 ? (y1 + 15) >> 4 : -((-y1) >> 4);
    e->y1 = y2 >= 0 ? (y2 + 15) >> 4 : -((-y2) >> 4); /* exclusive */
    if (e->y0 < 0) e->y0 = 0;
    if (e->y1 > height) e->y1 = height;
    if (e->y0 >= e->y1) return 0;
    e->step = ((int64_t)(x2 - x1) << 24) / dy; /* Q24 pixels per row: 16 Q4 units */
    e->x = ((int64_t)x1 << 20) + (((int64_t)e->y0 * 16 - y1) * ((int64_t)(x2 - x1) << 20)) / dy;
    return 1;
}
void cl_spans_init(ClSpans *s, ClRow *rows, int height) {
    int y;
    s->rows = rows; s->height = height; s->top = height; s->bottom = 0;
    for (y = 0; y < height; y++) rows[y].n = 0;
}
void cl_spans_edge(ClSpans *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    ClEdge e;
    int y;
    if (!edge_setup(&e, x1, y1, x2, y2, s->height)) return;
    if (e.y0 < s->top) s->top = e.y0;
    if (e.y1 > s->bottom) s->bottom = e.y1;
    for (y = e.y0; y < e.y1; y++, e.x += e.step) {
        ClRow *r = &s->rows[y];
        int16_t x = (int16_t)ceil24(e.x);
        int i = r->n;
        if (i >= CL_ROW_CROSSINGS) continue;
        while (i > 0 && r->x[i - 1] > x) { r->x[i] = r->x[i - 1]; i--; }
        r->x[i] = x;
        r->n++;
    }
}
void cl_spans_polygon(ClSpans *s, const int32_t *xy, int n) {
    int i;
    for (i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        cl_spans_edge(s, xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1]);
    }
}
/* Frontier's cubic forward differencing: the power-basis coefficients are
   prescaled by the step size h = 2^-k, so each step is three additions.
   Steps follow the control polygon's extent in pixels. */
void cl_spans_bezier(ClSpans *s, const int32_t p[4][2]) {
    int64_t x, y, d1x, d1y, d2x, d2y, d3x, d3y;
    int64_t ax = 3 * ((int64_t)p[1][0] - p[0][0]), ay = 3 * ((int64_t)p[1][1] - p[0][1]);
    int64_t bx = 3 * ((int64_t)p[2][0] - 2 * (int64_t)p[1][0] + p[0][0]);
    int64_t by = 3 * ((int64_t)p[2][1] - 2 * (int64_t)p[1][1] + p[0][1]);
    int64_t cx = (int64_t)p[3][0] - p[0][0] + 3 * ((int64_t)p[1][0] - p[2][0]);
    int64_t cy = (int64_t)p[3][1] - p[0][1] + 3 * ((int64_t)p[1][1] - p[2][1]);
    int64_t extent = 0;
    int i, k, steps;
    for (i = 1; i < 4; i++) {
        int64_t dx = p[i][0] - (int64_t)p[0][0], dy = p[i][1] - (int64_t)p[0][1];
        int64_t m = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (m > extent) extent = m;
    }
    extent >>= 4; /* pixels */
    k = extent < 8 ? 2 : extent < 50 ? 3 : extent < 300 ? 4 : 5;
    steps = 1 << k;
    x = (int64_t)p[0][0] << 16; y = (int64_t)p[0][1] << 16;
    d3x = 6 * (cx << (16 - 3 * k)); d3y = 6 * (cy << (16 - 3 * k));
    d2x = d3x + 2 * (bx << (16 - 2 * k)); d2y = d3y + 2 * (by << (16 - 2 * k));
    d1x = (ax << (16 - k)) + (bx << (16 - 2 * k)) + (cx << (16 - 3 * k));
    d1y = (ay << (16 - k)) + (by << (16 - 2 * k)) + (cy << (16 - 3 * k));
    for (i = 0; i < steps; i++) {
        int64_t nx = x + d1x, ny = y + d1y;
        /* The last step lands exactly on the end point, so curves joined
           end to end share vertices and leave no parity gaps. */
        if (i + 1 == steps) { nx = (int64_t)p[3][0] << 16; ny = (int64_t)p[3][1] << 16; }
        cl_spans_edge(s, (int32_t)((x + 32768) >> 16), (int32_t)((y + 32768) >> 16),
                      (int32_t)((nx + 32768) >> 16), (int32_t)((ny + 32768) >> 16));
        x = nx; y = ny;
        d1x += d2x; d2x += d3x;
        d1y += d2y; d2y += d3y;
    }
}
static void spans_walk(ClSpans *s, const ClSurface *f, int colour, const uint8_t *lut) {
    int y;
    for (y = s->top; y < s->bottom; y++) {
        ClRow *r = &s->rows[y];
        int i;
        if (y >= f->top && y < f->bottom) {
            uint8_t *line = f->pixels + (size_t)y * f->pitch;
            for (i = 0; i + 1 < r->n; i += 2) {
                int a = r->x[i], b = r->x[i + 1];
                if (a < f->left) a = f->left;
                if (b > f->right) b = f->right;
                if (a >= b) continue;
                if (lut) { int x; for (x = a; x < b; x++) line[x] = lut[line[x]]; }
                else memset(line + a, colour, (size_t)(b - a));
            }
        }
        r->n = 0;
    }
    s->top = s->height; s->bottom = 0;
}
void cl_spans_fill(ClSpans *s, const ClSurface *f, int colour) { spans_walk(s, f, colour, 0); }
void cl_spans_remap(ClSpans *s, const ClSurface *f, const uint8_t *lut) { spans_walk(s, f, 0, lut); }

void cl_body_init(ClBody *b, ClBodyRow *rows, uint16_t *toggle, int height) {
    int y;
    b->rows = rows; b->toggle = toggle; b->height = height; b->top = 0; b->bottom = 0;
    for (y = 0; y <= height; y++) toggle[y] = 0;
    for (y = 0; y < height; y++) rows[y].n = 0;
}
void cl_body_begin(ClBody *b, int top, int bottom) {
    if (top < 0) top = 0;
    if (bottom > b->height) bottom = b->height;
    b->top = top; b->bottom = bottom > top ? bottom : top;
}
void cl_body_edge(ClBody *b, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t mask) {
    ClEdge e;
    int y;
    if (!edge_setup(&e, x1, y1, x2, y2, b->height)) return;
    if (e.y0 < b->top) { e.x += e.step * (b->top - e.y0); e.y0 = b->top; }
    if (e.y1 > b->bottom) e.y1 = b->bottom;
    for (y = e.y0; y < e.y1; y++, e.x += e.step) {
        ClBodyRow *r = &b->rows[y];
        int16_t x = (int16_t)ceil24(e.x);
        int i = r->n;
        if (i >= CL_BODY_CROSSINGS) continue;
        while (i > 0 && r->c[i - 1].x > x) { r->c[i] = r->c[i - 1]; i--; }
        r->c[i].x = x; r->c[i].mask = mask;
        r->n++;
    }
}
void cl_body_toggle(ClBody *b, int y, uint16_t mask) {
    /* Rows above y take the toggle. A crossing below the last row affects
       every drawn row (slot height); one above the first affects none. */
    if (y > b->height) y = b->height;
    if (y < 0) return;
    b->toggle[y] ^= mask;
}
void cl_body_draw(ClBody *b, const ClSurface *f, const int16_t *first, const int16_t *last,
                  unsigned bottom_mask, ClBodyColour colour, void *ctx) {
    unsigned acc = bottom_mask;
    int y, k;
    /* Toggles at or below the last drawn row apply to all of them. */
    for (k = b->bottom; k <= b->height; k++) { acc ^= b->toggle[k]; b->toggle[k] = 0; }
    for (y = b->bottom - 1; y >= b->top; y--) {
        ClBodyRow *r = &b->rows[y];
        int x0 = first[y], x1 = last[y];
        if (x0 <= x1 && y >= f->top && y < f->bottom) {
            uint8_t *line = f->pixels + (size_t)y * f->pitch;
            unsigned mask = acc;
            int cur = x0, i;
            if (cur < f->left) cur = f->left;
            if (x1 >= f->right) x1 = f->right - 1;
            for (i = 0; i < r->n; i++) {
                int x = r->c[i].x;
                if (x <= cur) { mask ^= r->c[i].mask; continue; }
                if (x > x1) break;
                memset(line + cur, colour(ctx, mask), (size_t)(x - cur));
                mask ^= r->c[i].mask;
                cur = x;
            }
            if (cur <= x1) memset(line + cur, colour(ctx, mask), (size_t)(x1 - cur + 1));
        }
        r->n = 0;
        acc ^= b->toggle[y];
        b->toggle[y] = 0;
    }
    for (k = 0; k < b->top; k++) b->toggle[k] = 0;
}

void cl_disc(const ClSurface *f, int cx, int cy, int r, int colour) {
    int dy, x = r;
    if (r < 0) return;
    if (cx + r < f->left || cx - r >= f->right || cy + r < f->top || cy - r >= f->bottom) return;
    /* Midpoint: x shrinks monotonically as dy grows, with no square root. */
    for (dy = 0; dy <= r; dy++) {
        int k, a, z;
        while (x > 0 && x * x + dy * dy > r * r + r) x--;
        for (k = dy ? -1 : 1; k <= 1; k += 2) {
            int y = cy + k * dy;
            if (y < f->top || y >= f->bottom) continue;
            a = cx - x < f->left ? f->left : cx - x;
            z = cx + x >= f->right ? f->right - 1 : cx + x;
            if (a <= z) memset(f->pixels + (size_t)y * f->pitch + a, colour, (size_t)(z - a + 1));
        }
    }
}
static const uint8_t cl_bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
void cl_glow(const ClSurface *f, int cx, int cy, int inner, int outer, int colour, int density) {
    int y, x;
    if (outer <= inner || density <= 0) return;
    for (y = cy - outer; y <= cy + outer; y++) {
        int dy = y - cy;
        if (y < f->top || y >= f->bottom) continue;
        for (x = cx - outer; x <= cx + outer; x++) {
            int dx = x - cx, d2 = dx * dx + dy * dy, fade;
            if (x < f->left || x >= f->right || d2 > outer * outer || d2 < inner * inner) continue;
            /* Density falls linearly with radius across the annulus. */
            fade = density * (outer * outer - d2) / (outer * outer - inner * inner + 1);
            if (cl_bayer[(y & 3) * 4 + (x & 3)] < fade) f->pixels[(size_t)y * f->pitch + x] = (uint8_t)colour;
        }
    }
}
/* Liang-Barsky clip of the segment to the surface rectangle, in 64 bits so
   kilometre-long laser beams cannot overflow. */
static int clip_line(const ClSurface *f, int64_t *x0, int64_t *y0, int64_t *x1, int64_t *y1) {
    int64_t p[4], q[4], dx = *x1 - *x0, dy = *y1 - *y0;
    int64_t tn = 0, td = 1, un = 1, ud = 1; /* t0 = tn/td, t1 = un/ud */
    int i;
    p[0] = -dx; q[0] = *x0 - f->left;
    p[1] = dx;  q[1] = f->right - 1 - *x0;
    p[2] = -dy; q[2] = *y0 - f->top;
    p[3] = dy;  q[3] = f->bottom - 1 - *y0;
    for (i = 0; i < 4; i++) {
        if (!p[i]) { if (q[i] < 0) return 0; continue; }
        if (p[i] < 0) { /* entering: t >= q/p */
            int64_t n = q[i], d = p[i];
            if (d < 0) { n = -n; d = -d; }
            if (n * td > tn * d) { tn = n; td = d; }
        } else {
            int64_t n = q[i], d = p[i];
            if (n * ud < un * d) { un = n; ud = d; }
        }
    }
    if (tn * ud > un * td) return 0;
    {
        int64_t ax = *x0 + dx * tn / td, ay = *y0 + dy * tn / td;
        int64_t bx = *x0 + dx * un / ud, by = *y0 + dy * un / ud;
        *x0 = ax; *y0 = ay; *x1 = bx; *y1 = by;
    }
    return 1;
}
static int64_t line_bound(int v) { return v < -1000000 ? -1000000 : v > 1000000 ? 1000000 : v; }
void cl_line(const ClSurface *f, int x0, int y0, int x1, int y1, int colour) {
    /* Bounded endpoints keep the 64-bit clip products below 2^42. */
    int64_t ax = line_bound(x0), ay = line_bound(y0), bx = line_bound(x1), by = line_bound(y1);
    int dx, dy, sx, sy, e, n;
    int cx, cy, tx, ty;
    if (!clip_line(f, &ax, &ay, &bx, &by)) return;
    cx = (int)ax; cy = (int)ay; tx = (int)bx; ty = (int)by;
    dx = tx > cx ? tx - cx : cx - tx; dy = ty > cy ? cy - ty : ty - cy;
    sx = cx < tx ? 1 : -1; sy = cy < ty ? 1 : -1; e = dx + dy;
    for (n = 0; n < 8192; n++) {
        int e2 = 2 * e; /* both steps test the error from before either */
        cl_pixel(f, cx, cy, colour);
        if (cx == tx && cy == ty) break;
        if (e2 >= dy) { e += dy; cx += sx; }
        if (e2 <= dx) { e += dx; cy += sy; }
    }
}
