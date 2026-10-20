/* SPDX-License-Identifier: Unlicense
   Shared planetary geometry against independent long-double references:
   brute-force ray/sphere coverage of every pixel and bisected limb positions,
   from a million radii out down to a few metres above the ground. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "astralbound.h"
#include "xfloat.h"
#include "planet_view.h"
#include "planet_field.h"
#include "noise.h"
typedef long double real;
static uint32_t rng = 12345;
static real uniform(void) {
    rng = rng * 1664525U + 1013904223U;
    return (rng >> 8) / (real)(1 << 24);
}
static real xf_real(XFloat a) { return ldexpl((real)a.m, a.e); }
static void xfloat_arithmetic(void) {
    real worst = 0;
    int i;
    for (i = 0; i < 200000; i++) {
        real a = (uniform() - 0.5L) * powl(2, (int)(uniform() * 120) - 60);
        real b = (uniform() - 0.5L) * powl(2, (int)(uniform() * 120) - 60);
        XFloat x = xf_fixed((int64_t)(a * powl(2, 62 - (int)log2l(fabsl(a) + 1e-30L) - 2)),
                            -(62 - (int)log2l(fabsl(a) + 1e-30L) - 2));
        XFloat y = xf_fixed((int64_t)(b * powl(2, 62 - (int)log2l(fabsl(b) + 1e-30L) - 2)),
                            -(62 - (int)log2l(fabsl(b) + 1e-30L) - 2));
        real xa = xf_real(x), ya = xf_real(y), e;
        e = fabsl(xf_real(xf_mul(x, y)) - xa * ya) / fabsl(xa * ya);
        if (e > worst) worst = e;
        e = fabsl(xf_real(xf_div(x, y)) - xa / ya) / fabsl(xa / ya);
        if (e > worst) worst = e;
        e = fabsl(xf_real(xf_sqrt(xf_mul(x, x))) - fabsl(xa)) / fabsl(xa);
        if (e > worst) worst = e;
        /* Sums are compared against the larger operand's magnitude. */
        e = fabsl(xf_real(xf_add(x, y)) - (xa + ya)) / fmaxl(fabsl(xa), fabsl(ya));
        if (e > worst) worst = e;
    }
    assert(worst < 4e-9L);
    assert(xf_to_fixed(xf_int(-3), 8, 1 << 30) == -768);
    assert(xf_to_fixed(xf_fixed(3, -1), 0, 100) == 2); /* 1.5 rounds up */
    assert(xf_to_fixed(xf_int(1LL << 40), 8, 1000) == 1000);
    printf("PASS xfloat multiply, divide, root and sum within %.2Le relative\n", worst);
}
/* Long-double ray/sphere test for the pixel-sample ray (u, v, f). */
static int hit(real cx, real cy, real cz, real c, real u, real v, real f) {
    real b = cx * u + cy * v + cz * f;
    return b > 0 && b * b >= c * (u * u + v * v + f * f);
}
typedef struct { int w, h; } Screen;
static const Screen screens[] = {{320, 256}, {640, 480}, {320, 200}};
static long pixels_checked, rows_checked, mismatches, limbs_checked, views[3], open_views, covered_rows, unbounded_rows;
static real worst_limb, worst_mismatch;
static void check_view(real cx, real cy, real cz, real radius, real altitude, const Screen *s) {
    Projection p = {s->w / 2, s->h / 2, s->w / 2, 0, 0, s->w, s->h};
    PlanetView v;
    real c = altitude * (2 * radius + altitude), f = s->w / 2;
    int y, x, kind;
    kind = planet_view(&v, &p, vec((int64_t)llroundl(cx), (int64_t)llroundl(cy), (int64_t)llroundl(cz)),
                       (int64_t)llroundl(radius), (int64_t)llroundl(altitude));
    cx = llroundl(cx); cy = llroundl(cy); cz = llroundl(cz);
    views[kind]++;
    if (kind == PLANET_DISC && xf_sign(v.a) > 0) open_views++;
    for (y = 0; y < s->h; y++) {
        int first = 0, last = -1, any = 0, ref_first = -1, ref_last = -1;
        real row = p.cy - y;
        if (kind != PLANET_HIDDEN) any = planet_row_pixels(&v, y, &first, &last);
        if (!any) { first = 0; last = -1; }
        else {
            int32_t x0, x1;
            covered_rows++;
            planet_row(&v, y, &x0, &x1);
            if (x0 <= (p.left - 1) * 256 || x1 >= (p.right + 1) * 256) unbounded_rows++;
        }
        for (x = 0; x < s->w; x++) {
            int expect = hit(cx, cy, cz, c, x - p.cx, row, f), got = x >= first && x <= last;
            if (expect) { if (ref_first < 0) ref_first = x; ref_last = x; }
            pixels_checked++;
            if (expect != got) {
                /* Distance from the sample to the true limb along the row:
                   the nearest point within a pixel where the reference flips. */
                real d = 1, t;
                for (t = 1.0L / 8192; t <= 1; t += 1.0L / 8192)
                    if (hit(cx, cy, cz, c, x + t - p.cx, row, f) != expect ||
                        hit(cx, cy, cz, c, x - t - p.cx, row, f) != expect) { d = t; break; }
                mismatches++;
                if (d > worst_mismatch) worst_mismatch = d;
                if (d > 0.02L) {
                    fprintf(stderr, "mismatch R %.0Lf alt %.1Lf C (%.0Lf %.0Lf %.0Lf) %dx%d y %d x %d expect %d got %d..%d dist %.4Lf\n",
                            radius, altitude, cx, cy, cz, s->w, s->h, y, x, expect, first, last, d);
                    assert(0);
                }
            }
        }
        rows_checked++;
        /* Sub-pixel limb accuracy on the row's left and right ends. */
        if (any && ref_first > 0 && ref_first == first) {
            int32_t x0, x1;
            real a = ref_first - 1, b = ref_first, err;
            int k;
            planet_row(&v, y, &x0, &x1);
            for (k = 0; k < 50; k++) {
                real mid = (a + b) / 2;
                if (hit(cx, cy, cz, c, mid - p.cx, row, f)) b = mid; else a = mid;
            }
            err = fabsl(b - x0 / 256.0L);
            if (err > worst_limb) worst_limb = err;
            limbs_checked++;
            if (err > 1.0L / 64) {
                fprintf(stderr, "limb R %.0Lf alt %.1Lf C (%.0Lf %.0Lf %.0Lf) y %d true %.4Lf got %.4Lf\n",
                        radius, altitude, cx, cy, cz, y, b, x0 / 256.0L);
                assert(0);
            }
        }
        (void)ref_last;
    }
}
static void planet_outlines(void) {
    static const real radii[] = {303146, 1700000, 8202371, 66368838};
    int t;
    for (t = 0; t < 3000; t++) {
        real radius = radii[t % 4] * 256; /* Q8 metres, as the renderer passes */
        real altitude, d, cx, cy, cz;
        const Screen *s = &screens[t % 3];
        if (t % 3 == 2) {
            /* Low flight: the centre below, a grazing horizon in view. */
            real elevation = (uniform() - 0.3L) * 1.2L, yaw = (uniform() - 0.5L) * 1.5L;
            altitude = 256 * 5 * powl(10, uniform() * 5);
            d = radius + altitude;
            cx = d * sinl(yaw) * cosl(elevation);
            cy = -d * cosl(yaw) * cosl(elevation) * 0.99L - d * 0.01L;
            cz = d * sinl(elevation) + d * 0.05L * (uniform() - 0.5L);
        } else {
            real theta = acosl(1 - 2 * uniform()), phi = uniform() * 2 * 3.14159265358979L;
            altitude = 256 * 5 * powl(10, uniform() * log10l(radius / 256 * 1e6 / 5));
            d = radius + altitude;
            cx = d * sinl(theta) * cosl(phi); cy = d * sinl(theta) * sinl(phi); cz = d * cosl(theta);
            if (t % 3 == 1) { cx *= 0.3L; cy *= 0.3L; cz = sqrtl(d * d - cx * cx - cy * cy); }
        }
        /* The caller measures altitude exactly from the rounded centre. */
        {
            real rx = llroundl(cx), ry = llroundl(cy), rz = llroundl(cz);
            altitude = sqrtl(rx * rx + ry * ry + rz * rz) - llroundl(radius);
            altitude = floorl(altitude);
            if (altitude < 1) continue;
        }
        check_view(cx, cy, cz, llroundl(radius), altitude, s);
    }
    printf("PASS exact planet outlines: %ld pixels in %ld rows, %ld near-limb samples disagree "
           "(all within %.4Lf px of the limb); %ld limbs within %.5Lf px\n",
           pixels_checked, rows_checked, mismatches, worst_mismatch, limbs_checked, worst_limb);
    printf("     views: %ld hidden, %ld point, %ld disc (%ld with open, hyperbolic rows); "
           "%ld covered rows, %ld reaching past a screen edge\n",
           views[0], views[1], views[2], open_views, covered_rows, unbounded_rows);
    assert(open_views > 100 && unbounded_rows > 10000 && views[1] > 20);
}
static void planet_culling(void) {
    Projection p = {160, 128, 160, 0, 0, 320, 256};
    PlanetView v;
    /* Behind the eye, beside the view, and a distant point. */
    assert(planet_view(&v, &p, vec(0, 0, -10000000), 1000000, 9000000) == PLANET_HIDDEN);
    assert(planet_view(&v, &p, vec(50000000, 0, 10000000), 1000000, 49990000) == PLANET_HIDDEN);
    assert(planet_view(&v, &p, vec(0, 0, 1000000000), 1000, 999999000) == PLANET_POINT);
    assert(planet_view(&v, &p, vec(0, 0, 10000000), 1000000, 9000000) == PLANET_DISC);
    assert(v.top > 100 && v.bottom < 160 && v.left > 130 && v.right < 190);
    printf("PASS planet culling, point and disc classification and tight bounds\n");
}
/* The orbital field reproduces terrain noise bit for bit, and classifies the
   ground as terrain_material does wherever planet-scale fields decide it. */
static void planet_fields(void) {
    static Game g;
    static PlanetField field;
    static const char *names[] = {"rocky", "ocean", "desert", "ice", "gas", "moon"};
    long exact = 0, samples[6] = {0}, agree[6] = {0}, wet = 0, wet_agree = 0, caps = 0, caps_agree = 0;
    long land = 0, truth[3] = {0}, share[3] = {0};
    unsigned seed;
    int body, i;
    for (seed = 1; seed <= 24; seed++) {
        game_init(&g, seed * 7919U);
        for (body = 0; body < g.system.count && body < 8; body++) {
            Planet p = g.system.planets[body];
            p.spin_sin = p.spin_cos = 0; /* body-fixed frame = system frame */
            planet_field_init(&field, &p);
            for (i = 0; i < 4000; i++) {
                Vec d = vec((int64_t)((uniform() - 0.5L) * 2e6L), (int64_t)((uniform() - 0.5L) * 2e6L),
                            (int64_t)((uniform() - 0.5L) * 2e6L));
                Vec n = unit(d, 65536), local;
                int got, want, mat;
                if (!length(d)) continue;
                assert(planet_field_continent(&field, n) == noise_value(p.seed, n, 14));
                mat = 1 + i % 4;
                assert(planet_field_regional_albedo(&field, n, mat) == terrain_regional_albedo(&p, d, mat));
                exact++;
                local = radial(d, p.radius);
                want = terrain_material(&p, local);
                got = planet_field_material(&field, n);
                samples[p.type]++;
                agree[p.type] += got == want;
                if (p.type == OCEAN) { wet++; wet_agree += (got == 0) == (want == 0); }
                if (p.type == OCEAN && got >= 1 && got <= 3 && want >= 1 && want <= 3) {
                    int mix[3], k;
                    planet_field_land(&field, n, mix);
                    land++; truth[want - 1]++;
                    for (k = 0; k < 3; k++) share[k] += mix[k];
                }
                if (want == 4 || got == 4) { caps++; caps_agree += got == want; }
            }
        }
    }
    printf("PASS orbital fields: %ld continent and albedo samples bit-identical to terrain\n", exact);
    printf("     sea/land agreement %.2f%% of %ld ocean-world samples; ice %.2f%% of %ld\n",
           100.0 * wet_agree / wet, wet, 100.0 * caps_agree / caps, caps);
    for (i = 0; i < 6; i++)
        if (samples[i]) printf("     %-6s material agreement %.1f%% of %ld\n", names[i], 100.0 * agree[i] / samples[i], samples[i]);
    printf("     ocean-world land: lowland/forest/rock %.1f/%.1f/%.1f%% expected, %.1f/%.1f/%.1f%% in terrain\n",
           100.0 * share[0] / (256.0 * land), 100.0 * share[1] / (256.0 * land), 100.0 * share[2] / (256.0 * land),
           100.0 * truth[0] / land, 100.0 * truth[1] / land, 100.0 * truth[2] / land);
    assert(wet_agree * 1000 >= wet * 985 && caps_agree * 1000 >= caps * 995);
    for (i = 0; i < 3; i++) assert(llabs(share[i] / 256 - truth[i]) * 100 <= land * 3);
}
int main(void) {
    xfloat_arithmetic();
    planet_culling();
    planet_fields();
    planet_outlines();
    return 0;
}
