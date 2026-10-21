/* SPDX-License-Identifier: Unlicense
   Classic renderer rasterisation against exact references: even-odd
   polygon coverage, Bezier edges, discs and planet body spans with
   features that wrap behind the limb. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "classic_raster.h"
#define W 200
#define H 160
static uint8_t pixels[W * H];
static ClRow rows[H];
static ClBodyRow body_rows[H];
static uint16_t toggles[H + 1];
static uint32_t rng = 99;
static int rnd(int n) { rng = rng * 1664525U + 1013904223U; return (int)((rng >> 8) % (unsigned)n); }
/* Even-odd point-in-polygon at the integer sample (x, y), exact in 64 bits.
   Returns -1 when an edge passes within 1/1024 px of the sample (a tie the
   16.16 DDA may round either way). */
static int inside(const int32_t *p, int n, int x, int y) {
    int i, c = 0;
    int64_t sy = (int64_t)y * 16, sx = (int64_t)x * 16;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int64_t x1 = p[i * 2], y1 = p[i * 2 + 1], x2 = p[j * 2], y2 = p[j * 2 + 1];
        if (y1 > y2) { int64_t t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
        if (sy < y1 || sy >= y2) continue;
        /* crossing x (Q4) = x1 + (sy - y1)(x2 - x1)/(y2 - y1); compare to sx */
        {
            int64_t num = (x1 - sx) * (y2 - y1) + (sy - y1) * (x2 - x1), den = y2 - y1;
            if (num < 0 ? -num * 64 < den : num * 64 < den) return -1;
            if (num <= 0) c ^= 1; /* crossing at or left of the sample */
        }
    }
    return c;
}
static void polygons(void) {
    ClSpans s;
    ClSurface f = {pixels, W, 0, 0, W, H};
    int t, x, y, i, checked = 0, ties = 0;
    cl_spans_init(&s, rows, H);
    for (t = 0; t < 3000; t++) {
        int32_t p[16];
        int n = 3 + rnd(6);
        for (i = 0; i < n; i++) { p[i * 2] = rnd(W * 16 + 400) - 200; p[i * 2 + 1] = rnd(H * 16 + 400) - 200; }
        memset(pixels, 0, sizeof(pixels));
        cl_spans_polygon(&s, p, n);
        cl_spans_fill(&s, &f, 1);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int want = inside(p, n, x, y);
                if (want < 0) { ties++; continue; }
                if (want != pixels[y * W + x]) {
                    fprintf(stderr, "polygon %d n %d pixel %d,%d want %d got %d\n", t, n, x, y, want, pixels[y * W + x]);
                    assert(0);
                }
                checked++;
            }
    }
    printf("PASS even-odd polygons: %d samples of 3000 random polygons exact (%d ties skipped)\n", checked, ties);
}
/* The direct filler against the crossing table on random polygons, and on
   convex ones built round a centre: identical pixels wherever it accepts
   a polygon, and it accepts every convex one. */
static uint8_t second[W * H];
static void convex(void) {
    ClSpans s;
    ClSurface f = {pixels, W, 0, 0, W, H}, g = {second, W, 0, 0, W, H};
    int t, i, accepted = 0, convex_ok = 0;
    cl_spans_init(&s, rows, H);
    for (t = 0; t < 6000; t++) {
        int32_t p[32];
        int n = 3 + rnd(t & 1 ? 6 : 13);
        if (t & 1) for (i = 0; i < n; i++) { p[i * 2] = rnd(W * 16 + 400) - 200; p[i * 2 + 1] = rnd(H * 16 + 400) - 200; }
        else {
            /* Convex: points at increasing angles round a centre. */
            int cx = rnd(W * 16), cy = rnd(H * 16), r = 16 + rnd(W * 12), a = rnd(1024);
            for (i = 0; i < n; i++) {
                double ang = ((a + i * 1024 / n + rnd(1024 / n / 2 + 1)) & 1023) * 6.283185307179586 / 1024;
                p[i * 2] = cx + (int32_t)floor(cos(ang) * r);
                p[i * 2 + 1] = cy + (int32_t)floor(sin(ang) * r);
            }
        }
        memset(pixels, 0, sizeof(pixels)); memset(second, 0, sizeof(second));
        cl_spans_polygon(&s, p, n);
        cl_spans_fill(&s, &f, 1);
        if (!cl_convex_fill(&g, p, n, 1)) { assert(t & 1); continue; }
        accepted++; convex_ok += !(t & 1);
        if (memcmp(pixels, second, sizeof(pixels))) {
            fprintf(stderr, "polygon %d (n %d) differs between fillers\n", t, n);
            assert(0);
        }
    }
    printf("PASS direct convex filler matches the crossing table on %d polygons (%d convex)\n", accepted, convex_ok);
}
static void curves(void) {
    ClSpans s;
    ClSurface f = {pixels, W, 0, 0, W, H};
    int32_t p[4][2] = {{20 * 16, 140 * 16}, {40 * 16, 10 * 16}, {160 * 16, 10 * 16}, {180 * 16, 140 * 16}};
    int x, y, covered = 0, wrong = 0;
    cl_spans_init(&s, rows, H);
    memset(pixels, 0, sizeof(pixels));
    /* The arch closed by a straight base: compare with the cubic itself. */
    cl_spans_bezier(&s, p);
    cl_spans_edge(&s, p[3][0], p[3][1], p[0][0], p[0][1]);
    cl_spans_fill(&s, &f, 1);
    for (x = 25; x < 175; x++) {
        /* The true curve's top at this column, by bisection on t. */
        double lo = 0, hi = 1, t, top;
        int k;
        for (k = 0; k < 60; k++) {
            double m = (lo + hi) / 2, u = 1 - m;
            double bx = (u * u * u * p[0][0] + 3 * u * u * m * p[1][0] + 3 * u * m * m * p[2][0] + m * m * m * p[3][0]) / 16;
            if (bx < x) lo = m; else hi = m;
        }
        t = lo;
        top = ((1 - t) * (1 - t) * (1 - t) * p[0][1] + 3 * (1 - t) * (1 - t) * t * p[1][1] +
               3 * (1 - t) * t * t * p[2][1] + t * t * t * p[3][1]) / 16;
        for (y = 0; y < H; y++) {
            int want = y >= top && y < 140;
            if (y > top - 1.5 && y < top + 1.5) continue; /* flattening tolerance */
            covered += pixels[y * W + x];
            wrong += want != pixels[y * W + x];
        }
    }
    assert(!wrong && covered > 10000);
    printf("PASS cubic Bezier edges by forward differencing within 1.5 px of the true curve\n");
}
static void discs(void) {
    ClSurface f = {pixels, W, 0, 0, W, H};
    int r, x, y;
    for (r = 0; r < 70; r += 3) {
        memset(pixels, 0, sizeof(pixels));
        cl_disc(&f, 100, 80, r, 2);
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
            int dx = x - 100, dy = y - 80, want = dx * dx + dy * dy <= r * r + r;
            assert(want == (pixels[y * W + x] == 2));
        }
    }
    /* Clipped at every surface edge without writing outside it. */
    {
        ClSurface clip = {pixels, W, 10, 10, 50, 40};
        memset(pixels, 0, sizeof(pixels));
        cl_disc(&clip, 10, 10, 60, 3);
        cl_line(&clip, -100000, -5000, 100000, 9000, 4);
        for (y = 0; y < H; y++) for (x = 0; x < W; x++)
            if (x < 10 || x >= 50 || y < 10 || y >= 40) assert(!pixels[y * W + x]);
    }
    /* Every line stops at its end point, in every octant: count the drawn
       pixels against the Chebyshev length. */
    {
        ClSurface all = {pixels, W, 0, 0, W, H};
        int t;
        for (t = 0; t < 4000; t++) {
            int x0 = rnd(W), y0 = rnd(H), x1 = rnd(W), y1 = rnd(H), n = 0, len;
            memset(pixels, 0, sizeof(pixels));
            cl_line(&all, x0, y0, x1, y1, 5);
            for (y = 0; y < H; y++) for (x = 0; x < W; x++) n += pixels[y * W + x] == 5;
            len = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
            assert(pixels[y0 * W + x0] == 5 && pixels[y1 * W + x1] == 5 && n == len + 1);
        }
    }
    printf("PASS midpoint discs, Bresenham lines end to end in every octant, clipping\n");
}
/* A disc with one square feature that wraps behind the left limb. The
   feature polygon's hidden part is replaced by a toggle at the limb, as the
   Classic planet renderer does; colour = 1 + mask. */
static int mask_colour(void *ctx, unsigned mask) { (void)ctx; return 1 + (int)mask; }
static void body_spans(void) {
    ClBody b;
    ClSurface f = {pixels, W, 0, 0, W, H};
    int16_t first[H], last[H];
    int x, y, cx = 100, cy = 80, r = 60, bad = 0;
    cl_body_init(&b, body_rows, toggles, H);
    memset(pixels, 0, sizeof(pixels));
    for (y = 0; y < H; y++) {
        int dy = y - cy, h = -1;
        while ((h + 1) * (h + 1) + dy * dy <= r * r) h++;
        first[y] = (int16_t)(h >= 0 ? cx - h : 1); last[y] = (int16_t)(h >= 0 ? cx + h : 0);
    }
    cl_body_begin(&b, cy - r, cy + r + 1);
    /* Visible edges of the feature x <= 90, 50 <= y < 110 (Q4): the right
       side and the top and bottom up to the left limb. */
    cl_body_edge(&b, 90 * 16, 50 * 16, 90 * 16, 110 * 16, 1);
    cl_body_edge(&b, 90 * 16, 50 * 16, (cx - 52) * 16, 50 * 16, 1); /* horizontal: no crossings */
    /* The top and bottom edges leave through the left limb at rows 50 and
       110: toggles there stand in for the hidden part. */
    cl_body_toggle(&b, 50, 1);
    cl_body_toggle(&b, 110, 1);
    cl_body_draw(&b, &f, first, last, 0, mask_colour, 0);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        int dx = x - cx, dy = y - cy, in_disc = x >= first[y] && x <= last[y];
        int in_feature = x < 90 && y >= 50 && y < 110;
        int want = in_disc ? 1 + in_feature : 0;
        (void)dx; (void)dy;
        bad += pixels[y * W + x] != want;
    }
    assert(!bad);
    printf("PASS planet body spans with a feature wrapping behind the left limb\n");
}
int main(void) {
    polygons();
    convex();
    curves();
    discs();
    body_spans();
    return 0;
}
