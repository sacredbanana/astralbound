/* SPDX-License-Identifier: Unlicense; Classic span rasteriser. See classic_raster.h. */
#include "classic_raster.h"
#include <string.h>
#define CL_LIMIT 16000 /* crossings clamp here; far beyond any surface */
/* An edge's rows and crossing pixels by an exact integer DDA, as Frontier's
   span renderer stepped its edges: one 32-bit division per edge and, per
   row, an addition, a subtraction and a carry. The crossing at row y is
   ceil(N / D) with N = x1 dy + (16 y - y1) dx and D = 16 dy, kept as
   x D - e with 0 <= e < D, so the arithmetic is the exact rational one and
   an edge shared by two polygons crosses the same pixels for both. Rows
   sampled: y1 <= 16 y < y2 (Q4). */
#define CL_EDGE_LIMIT (1 << 24) /* Q4: a million pixels; projection clamps far inside */
typedef struct { int y0, y1; int32_t x, q, r, d, e; } ClEdge;
static int32_t edge_bound(int32_t v) { return v < -CL_EDGE_LIMIT ? -CL_EDGE_LIMIT : v > CL_EDGE_LIMIT ? CL_EDGE_LIMIT : v; }
/* floor(a / b) for b > 0. */
static int32_t floor_div32(int32_t a, int32_t b) { int32_t q = a / b; return q - (a % b < 0); }
static int edge_setup(ClEdge *e, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int top, int height) {
    int32_t dx, dy, t;
    int64_t n;
    x1 = edge_bound(x1); y1 = edge_bound(y1); x2 = edge_bound(x2); y2 = edge_bound(y2);
    if (y1 > y2) { int32_t s = x1; x1 = x2; x2 = s; s = y1; y1 = y2; y2 = s; }
    dy = y2 - y1;
    if (!dy) return 0;
    e->y0 = y1 >= 0 ? (y1 + 15) >> 4 : -((-y1) >> 4);
    e->y1 = y2 >= 0 ? (y2 + 15) >> 4 : -((-y2) >> 4); /* exclusive */
    if (e->y0 < top) e->y0 = top;
    if (e->y1 > height) e->y1 = height;
    if (e->y0 >= e->y1) return 0;
    dx = x2 - x1;
    e->d = 16 * dy;
    e->q = floor_div32(dx, dy);
    e->r = 16 * (dx - e->q * dy);
    t = 16 * e->y0 - y1;
    if (x1 > -23170 && x1 < 23170 && dx > -23170 && dx < 23170 && dy < 23170 && t < 23170) {
        /* On-screen edges: 16 x 16-bit products (MULS.W) under 2^29 each. */
        int32_t m = (int32_t)(int16_t)x1 * (int16_t)dy + (int32_t)(int16_t)t * (int16_t)dx;
        e->x = m >= 0 ? (m + e->d - 1) / e->d : -((-m) / e->d);
        e->e = e->x * e->d - m;
        return 1;
    }
    n = (int64_t)x1 * dy + (int64_t)t * dx;
    if (n > -(1 << 30) && n < (1 << 30)) {
        int32_t m = (int32_t)n;
        e->x = m >= 0 ? (m + e->d - 1) / e->d : -((-m) / e->d);
    } else e->x = (int32_t)(n >= 0 ? (n + e->d - 1) / e->d : -((-n) / e->d));
    e->e = (int32_t)((int64_t)e->x * e->d - n);
    return 1;
}
/* The crossing pixel of the current row, then advance a row. */
static int16_t edge_step(ClEdge *e) {
    int32_t x = e->x;
    e->x += e->q;
    e->e -= e->r;
    if (e->e < 0) { e->e += e->d; e->x++; }
    return (int16_t)(x < -CL_LIMIT ? -CL_LIMIT : x > CL_LIMIT ? CL_LIMIT : x);
}
void cl_spans_init(ClSpans *s, ClRow *rows, int height) {
    int y;
    s->rows = rows; s->height = height; s->top = height; s->bottom = 0;
    for (y = 0; y < height; y++) rows[y].n = 0;
}
void cl_spans_edge(ClSpans *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    ClEdge e;
    int y;
    if (!edge_setup(&e, x1, y1, x2, y2, 0, s->height)) return;
    if (e.y0 < s->top) s->top = e.y0;
    if (e.y1 > s->bottom) s->bottom = e.y1;
    for (y = e.y0; y < e.y1; y++) {
        ClRow *r = &s->rows[y];
        int16_t x = edge_step(&e);
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
/* A convex polygon straight to the surface: the two chains from the top
   vertex to the bottom one are stepped together and each row filled as
   one run, with no crossing table. The crossings are the edge DDA's, so
   coverage is exactly the even-odd fill's. Returns 0, drawing nothing,
   when the vertices are not y-monotone both ways round (rounding can fold
   a sliver), leaving the general filler to it. */
static int chain_next(int n, int i, int dir) { i += dir; return i < 0 ? n - 1 : i >= n ? 0 : i; }
int cl_convex_fill(const ClSurface *f, const int32_t *xy, int n, int colour) {
    int top = 0, bottom = 0, i, k, y, end;
    int at[2], to[2], dir[2] = {1, -1};
    ClEdge e[2];
    if (n < 3 || n > 16) return 0;
    for (i = 1; i < n; i++) {
        if (xy[i * 2 + 1] < xy[top * 2 + 1]) top = i;
        if (xy[i * 2 + 1] > xy[bottom * 2 + 1]) bottom = i;
    }
    /* Both chains must descend monotonically from top to bottom. */
    for (k = 0; k < 2; k++) {
        int j = top;
        while (j != bottom) {
            int nx = chain_next(n, j, dir[k]);
            if (xy[nx * 2 + 1] < xy[j * 2 + 1]) return 0;
            j = nx;
        }
    }
    y = xy[top * 2 + 1] >= 0 ? (xy[top * 2 + 1] + 15) >> 4 : -((-xy[top * 2 + 1]) >> 4);
    end = xy[bottom * 2 + 1] >= 0 ? (xy[bottom * 2 + 1] + 15) >> 4 : -((-xy[bottom * 2 + 1]) >> 4);
    if (y < f->top) y = f->top;
    if (end > f->bottom) end = f->bottom;
    for (k = 0; k < 2; k++) { at[k] = top; to[k] = top; e[k].y1 = y; }
    for (; y < end; y++) {
        int x[2], a, b;
        for (k = 0; k < 2; k++) {
            /* Advance to the edge whose rows hold y. */
            while (e[k].y1 <= y) {
                at[k] = to[k];
                if (at[k] == bottom) return 1;
                to[k] = chain_next(n, at[k], dir[k]);
                if (!edge_setup(&e[k], xy[at[k] * 2], xy[at[k] * 2 + 1], xy[to[k] * 2], xy[to[k] * 2 + 1], y, f->bottom))
                    e[k].y1 = y; /* flat or above the view: next edge */
            }
            x[k] = edge_step(&e[k]);
        }
        a = x[0] < x[1] ? x[0] : x[1]; b = x[0] < x[1] ? x[1] : x[0];
        if (a < f->left) a = f->left;
        if (b > f->right) b = f->right;
        if (a < b) memset(f->pixels + (size_t)y * f->pitch + a, colour, (size_t)(b - a));
    }
    return 1;
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
    /* Multiplications by powers of two: shifting negatives is undefined. */
    x = (int64_t)p[0][0] * 65536; y = (int64_t)p[0][1] * 65536;
    d3x = 6 * (cx * ((int64_t)1 << (16 - 3 * k))); d3y = 6 * (cy * ((int64_t)1 << (16 - 3 * k)));
    d2x = d3x + 2 * (bx * ((int64_t)1 << (16 - 2 * k))); d2y = d3y + 2 * (by * ((int64_t)1 << (16 - 2 * k)));
    d1x = ax * ((int64_t)1 << (16 - k)) + bx * ((int64_t)1 << (16 - 2 * k)) + cx * ((int64_t)1 << (16 - 3 * k));
    d1y = ay * ((int64_t)1 << (16 - k)) + by * ((int64_t)1 << (16 - 2 * k)) + cy * ((int64_t)1 << (16 - 3 * k));
    for (i = 0; i < steps; i++) {
        int64_t nx = x + d1x, ny = y + d1y;
        /* The last step lands exactly on the end point, so curves joined
           end to end share vertices and leave no parity gaps. */
        if (i + 1 == steps) { nx = (int64_t)p[3][0] * 65536; ny = (int64_t)p[3][1] * 65536; }
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
    if (!edge_setup(&e, x1, y1, x2, y2, b->top, b->bottom)) return;
    for (y = e.y0; y < e.y1; y++) {
        ClBodyRow *r = &b->rows[y];
        int16_t x = edge_step(&e);
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
