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
#include "planet_contours.h"
#include "noise.h"
#include "body_map.h"
#include "solar.h"
#include "galaxy.h"
#include <string.h>
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
/* Both hemispheres need a geographic margin, with reproducible bays and
   tongues rather than an isocircle. Exercise the cached field against raw
   terrain noise, and sample terrain on either side of every meridian's ice
   edge after rotating it into world space. */
static void polar_geography(void) {
    static PlanetField field;
    Planet p = {0};
    int seed, hemisphere, angle, latitude, checked = 0;
    uint32_t previous[2] = {0, 0};
    p.type = OCEAN; p.radius = 6000000; p.parent = -1;
    for (seed = 1; seed <= 12; seed++) {
        p.seed = (uint32_t)seed * 7919U;
        planet_field_init(&field, &p);
        set_rotation(&p, (uint32_t)seed * 173U);
        for (hemisphere = -1; hemisphere <= 1; hemisphere += 2) {
            int min_edge = 256, max_edge = 0;
            uint32_t shape = 0;
            for (angle = 0; angle < 1024; angle += 16) {
                int edge = 0;
                for (latitude = 128; latitude <= 256; latitude++) {
                    Vec n = vec((int64_t)cosi(latitude) * cosi(angle) / 4096,
                                (int64_t)cosi(latitude) * sini(angle) / 4096,
                                (int64_t)hemisphere * sini(latitude) * 4);
                    int value = planet_field_ice(&field, n);
                    assert(value == noise_polar_value(p.seed, n));
                    if (value >= 0) { edge = latitude; break; }
                }
                assert(edge > 128 && edge < 256);
                if (edge < min_edge) min_edge = edge;
                if (edge > max_edge) max_edge = edge;
                shape = shape * 33U + (unsigned)edge;
                /* Stay clear of the Q16 rounding boundary. */
                for (latitude = edge - 3; latitude <= edge + 3; latitude += 6) {
                    Vec n = vec((int64_t)cosi(latitude) * cosi(angle) / 4096,
                                (int64_t)cosi(latitude) * sini(angle) / 4096,
                                (int64_t)hemisphere * sini(latitude) * 4);
                    Vec local = body_rotate(&p, radial(n, p.radius), 0);
                    assert((terrain_material(&p, local) == 4) == (planet_field_ice(&field, n) >= 0));
                    checked++;
                }
            }
            assert(max_edge - min_edge >= 12); /* >= four degrees of coastline variation */
            assert(shape != previous[hemisphere > 0]); /* each seed has its own geography */
            previous[hemisphere > 0] = shape;
        }
        assert(planet_field_ice(&field, vec(65536, 0, 0)) < 0);
        assert(planet_field_ice(&field, vec(0, 0, 65536)) >= 0);
        assert(planet_field_ice(&field, vec(0, 0, -65536)) >= 0);
        /* Planet types retain their material semantics. */
        p.type = ICE; planet_field_init(&field, &p);
        assert(planet_field_material(&field, vec(65536, 0, 0)) == 4);
        p.type = GAS_GIANT; planet_field_init(&field, &p);
        assert(planet_field_material(&field, vec(0, 0, 65536)) == 5);
        p.type = OCEAN;
    }
    printf("PASS irregular north/south ice margins for 12 seeds and %d rotated surface samples\n", checked);
}
/* Extraction preserves the field sign at its lattice vertices. Querying
   loops must preserve that sign too, across face seams, octants and LODs. */
static void contour_parity(void) {
    static PlanetContours contours;
    static PlanetField field;
    static const uint32_t seeds[] = {0x53504143, 37};
    Planet p = {0};
    int seed, n, kind, face, i, j, checked = 0;
    p.type = OCEAN; p.radius = 1000000; p.parent = -1;
    for (seed = 0; seed < 2; seed++) {
        p.seed = seeds[seed]; planet_field_init(&field, &p);
        for (n = 16; n <= 32; n += 8) for (kind = 0; kind <= CONTOUR_ICE; kind++) {
            planet_contours_build(&contours, &field, n, kind);
            assert(!contours.truncated && contours.loops > 0);
            for (face = 0; face < 6; face++) for (i = 0; i <= n; i += 4) for (j = 0; j <= n; j += 4) {
                int axis = face / 2, sign = face & 1 ? -n : n, u = -n + 2 * i, v = -n + 2 * j;
                Vec d = unit(axis == 0 ? vec(sign, u, v) : axis == 1 ? vec(u, sign, v) : vec(u, v, sign), 65536);
                ContourPoint q = {(int16_t)(d.x / 4), (int16_t)(d.y / 4), (int16_t)(d.z / 4)};
                int value = kind == CONTOUR_ICE ? planet_field_ice(&field, d) : kind == CONTOUR_ALBEDO ? planet_field_region(&field, d) - 2048 :
                    planet_field_base_height(&field, d) - (kind == CONTOUR_HIGHLAND ? 262 : 0);
                if (abs(value) < 16) continue; /* Q14 rounding at the boundary */
                if (planet_contours_positive(&contours, q) != (value >= 0))
                    fprintf(stderr, "Contour parity seed %d n %d kind %d face %d i %d j %d value %d\n",
                            seed, n, kind, face, i, j, value);
                assert(planet_contours_positive(&contours, q) == (value >= 0));
                checked++;
            }
        }
    }
    printf("PASS contour parity at %d lattice directions across seams, octants and three resolutions\n", checked);
}
/* A synthetic cube map returns its lattice values exactly, is continuous
   across face edges and does not care about vector length. */
static void map_sampling(void) {
    static uint8_t channel[6 * 9 * 9];
    BodyMap m;
    int face, i, j, k, checked = 0;
    memset(&m, 0, sizeof(m));
    m.n = 8; m.albedo = channel;
    for (face = 0; face < 6; face++)
        for (j = 0; j <= 8; j++)
            for (i = 0; i <= 8; i++) {
                int axis = face / 2, sign = face & 1 ? -8 : 8, u = -8 + 2 * i, v = -8 + 2 * j;
                real x = axis == 0 ? sign : u, y = axis == 1 ? sign : axis == 0 ? u : v, z = axis == 2 ? sign : v;
                real r = sqrtl(x * x + y * y + z * z);
                channel[(face * 9 + j) * 9 + i] = (uint8_t)lrintl(128 + 90 * z / r + 30 * x / r - 20 * y / r);
            }
    for (face = 0; face < 6; face++)
        for (j = 0; j <= 8; j++)
            for (i = 0; i <= 8; i++) {
                int axis = face / 2, sign = face & 1 ? -8 : 8, u = -8 + 2 * i, v = -8 + 2 * j;
                Vec l = axis == 0 ? vec(sign, u, v) : axis == 1 ? vec(u, sign, v) : vec(u, v, sign);
                for (k = 1; k <= 4096; k *= 64)
                    assert(body_map_sample(&m, channel, scale(l, k * 1000, 1)) == channel[(face * 9 + j) * 9 + i] << 8);
            }
    /* Either side of every face edge. */
    for (i = 0; i < 20000; i++) {
        int64_t a = 1000000, b = (int64_t)((uniform() * 2 - 1) * a), sa = uniform() < 0.5 ? -1 : 1, sb = uniform() < 0.5 ? -1 : 1;
        int axis = i % 3, one, two;
        Vec p = axis == 0 ? vec(sa * (a + 1), sb * a, b) : axis == 1 ? vec(sa * (a + 1), b, sb * a) : vec(b, sa * (a + 1), sb * a);
        Vec q = axis == 0 ? vec(sa * a, sb * (a + 1), b) : axis == 1 ? vec(sa * a, b, sb * (a + 1)) : vec(b, sa * a, sb * (a + 1));
        one = body_map_sample(&m, channel, p); two = body_map_sample(&m, channel, q);
        assert(abs(one - two) <= 64);
        checked++;
    }
    printf("PASS cube-map sampling: exact lattice values at three scales, %d face-edge pairs continuous\n", checked);
}
/* Every mapped Solar System body: the orbital field reads the same measured
   coasts, albedo and ice as terrain and collision. */
static void measured_bodies(void) {
    static Game g;
    static PlanetField field;
    int body, i, mapped = 0;
    long samples = 0, agree = 0;
    game_init(&g, 1);
    solar_system(&g.system);
    for (body = 0; body < g.system.count; body++) {
        Planet p = g.system.planets[body];
        if (!p.map) continue;
        mapped++;
        p.spin_sin = p.spin_cos = 0;
        planet_field_init(&field, &p);
        for (i = 0; i < 4000; i++) {
            Vec d = vec((int64_t)((uniform() - 0.5L) * 2e6L), (int64_t)((uniform() - 0.5L) * 2e6L),
                        (int64_t)((uniform() - 0.5L) * 2e6L));
            Vec n = unit(d, 65536);
            int mat = 1 + i % 4;
            if (!length(d)) continue;
            assert(planet_field_regional_albedo(&field, n, mat) == terrain_regional_albedo(&p, d, mat));
            assert(planet_field_ice(&field, n) == body_map_ice(p.map, n));
            if (p.type == OCEAN && p.map->continent) assert(planet_field_continent(&field, n) == body_map_continent(p.map, n));
            if (!field.veil) {
                samples++;
                agree += planet_field_material(&field, n) == terrain_material(&p, radial(d, p.radius));
            }
        }
    }
    printf("PASS %d mapped Solar System bodies: albedo, ice and continents identical in orbit and on the ground\n", mapped);
    if (samples) printf("     material agreement %.2f%% of %ld samples\n", 100.0 * agree / samples, samples);
    assert(agree * 100 >= samples * 97);
}
/* Landmarks in the right places: catches a mirrored, rotated or shifted
   source. Latitude and east longitude in degrees. */
static Vec place(real lat, real lon) {
    real la = lat * 3.14159265358979L / 180, lo = lon * 3.14159265358979L / 180;
    return vec((int64_t)lrintl(cosl(la) * cosl(lo) * 65536), (int64_t)lrintl(cosl(la) * sinl(lo) * 65536),
               (int64_t)lrintl(-sinl(la) * 65536)); /* north is body -z */
}
static int material_at(const System *s, const char *name, real lat, real lon) {
    Planet p = s->planets[solar_body_index(s, name)];
    p.spin_sin = p.spin_cos = 0;
    return terrain_material(&p, radial(place(lat, lon), p.radius));
}
static int albedo_at(const System *s, const char *name, real lat, real lon) {
    return body_map_regional_albedo(s->planets[solar_body_index(s, name)].map, place(lat, lon));
}
static int relief_at(const System *s, const char *name, real lat, real lon) {
    return body_map_relief(s->planets[solar_body_index(s, name)].map, place(lat, lon));
}
static void landmarks(void) {
    static System s;
    solar_system(&s);
    /* Earth: oceans, coasts either side of the Atlantic, deserts, forests, ice. */
    assert(material_at(&s, "Earth", 30, -40) == 0);       /* mid Atlantic */
    assert(material_at(&s, "Earth", 25, -90) == 0);       /* Gulf of Mexico */
    assert(material_at(&s, "Earth", -10, -150) == 0);     /* Pacific */
    assert(material_at(&s, "Earth", 23, 10) == 3);        /* Sahara */
    assert(material_at(&s, "Earth", 22, 47) == 3);        /* Arabia */
    assert(material_at(&s, "Earth", -25, 130) == 3);      /* central Australia */
    assert(material_at(&s, "Earth", 39, -117) == 3);      /* Great Basin */
    assert(material_at(&s, "Earth", -24, -69) == 3);      /* Atacama */
    assert(material_at(&s, "Earth", -3, -60) == 2 || material_at(&s, "Earth", -3, -60) == 1); /* Amazon */
    assert(material_at(&s, "Earth", 0, 22) == 2 || material_at(&s, "Earth", 0, 22) == 1);     /* Congo */
    assert(material_at(&s, "Earth", 34, -84) == 2 || material_at(&s, "Earth", 34, -84) == 1); /* Georgia, US */
    assert(material_at(&s, "Earth", 75, -40) == 4);       /* Greenland */
    assert(material_at(&s, "Earth", -82, 60) == 4);       /* East Antarctica */
    /* Moon: dark maria on the near side, bright highlands; deep far-side basin. */
    assert(albedo_at(&s, "Moon", 33, -16) < 0 && albedo_at(&s, "Moon", 28, 17) < 0); /* Imbrium, Serenitatis */
    assert(albedo_at(&s, "Moon", -30, 10) > 0 && albedo_at(&s, "Moon", -20, 120) > 0);
    assert(relief_at(&s, "Moon", -53, -169) < relief_at(&s, "Moon", 5, -158)); /* South Pole-Aitken */
    /* Mars: Olympus Mons above Hellas; dark Syrtis Major, bright Arabia; caps. */
    assert(relief_at(&s, "Mars", 18.65L, -133.8L) > relief_at(&s, "Mars", -42.4L, 70.5L) + 1000);
    assert(albedo_at(&s, "Mars", 8.4L, 69.5L) < 0 && albedo_at(&s, "Mars", 20, 5) > 0);
    assert(material_at(&s, "Mars", 88, 0) == 4 && material_at(&s, "Mars", 0, 0) == 3);
    /* Jupiter: the Red Spot and the belts are below the zones' brightness. */
    assert(albedo_at(&s, "Jupiter", 0, 0) > albedo_at(&s, "Jupiter", 15, 0));
    printf("PASS landmarks on Earth, the Moon, Mars and Jupiter\n");
}
/* Real spin axes: angles in degrees between body directions and system
   ones, and where a body-fixed direction points. */
static real degrees_between(Vec a, Vec b) {
    real d = ((real)a.x * b.x + (real)a.y * b.y + (real)a.z * b.z) /
             (sqrtl((real)a.x * a.x + (real)a.y * a.y + (real)a.z * a.z) * sqrtl((real)b.x * b.x + (real)b.y * b.y + (real)b.z * b.z));
    return acosl(d > 1 ? 1 : d < -1 ? -1 : d) * 180 / 3.14159265358979L;
}
static void body_latlon(const Planet *p, Vec world, real *lat, real *lon) {
    Vec b = body_rotate(p, world, 1);
    real r = sqrtl((real)b.x * b.x + (real)b.y * b.y + (real)b.z * b.z);
    *lat = asinl(-b.z / r) * 180 / 3.14159265358979L; /* north is body -z */
    *lon = atan2l((real)b.y, (real)b.x) * 180 / 3.14159265358979L;
}
static real obliquity(const System *s, int body) {
    const Planet *p = &s->planets[body];
    Vec r = scale(sub(p->center, s->star[0].center), 1, 1000000), v = p->velocity;
    Vec h = vec(r.y * v.z - r.z * v.y, r.z * v.x - r.x * v.z, r.x * v.y - r.y * v.x);
    return degrees_between(h, body_rotate(p, vec(0, 0, 16384), 0));
}
static void spin_axes(void) {
    static Game g;
    int earth, moon, saturn, uranus, pluto, charon, k;
    real lat, lon, worst = 0;
    game_init(&g, 1);
    setup_system(&g, catalog_sector(0));
    earth = solar_body_index(&g.system, "Earth"); moon = solar_body_index(&g.system, "Moon");
    saturn = solar_body_index(&g.system, "Saturn"); uranus = solar_body_index(&g.system, "Uranus");
    pluto = solar_body_index(&g.system, "Pluto"); charon = solar_body_index(&g.system, "Charon");
    g.tick = 0; update_world(&g);
    {
        const Planet *e = &g.system.planets[earth], *sa = &g.system.planets[saturn], *u = &g.system.planets[uranus];
        Vec north = body_rotate(e, vec(0, 0, -16384), 0);
        Vec sun = sub(g.system.star[0].center, e->center);
        Vec east = body_rotate(e, vec(0, 16384, 0), 0), ground = body_rotate(e, vec(16384, 0, 0), 0);
        /* Obliquities: the spin (body +z) from the orbit's angular momentum.
           Earth's north also leans 23.4 degrees from ecliptic north (+y). */
        assert(fabsl(degrees_between(north, vec(0, 1, 0)) - 23.44L) < 0.05L);
        assert(fabsl(obliquity(&g.system, earth) - 23.44L) < 0.05L);
        assert(fabsl(obliquity(&g.system, saturn) - 26.73L) < 0.1L);
        assert(fabsl(obliquity(&g.system, uranus) - 97.77L) < 0.1L);
        (void)sa; (void)u;
        /* J2000 is noon TT at Greenwich: the sun stands over longitude 0
           (the equation of time puts it 0.8 degrees east). */
        body_latlon(e, sun, &lat, &lon);
        assert(fabsl(lon) < 2 && fabsl(lat + 23.0L) < 0.5L);
        /* The ground moves east, the way Earth goes round the Sun. */
        assert(degrees_between(surface_velocity(e, scale(ground, e->radius, 16384)), east) < 0.01L);
        {
            Vec r = scale(sub(e->center, g.system.star[0].center), 1, 1000000), v = e->velocity;
            Vec h = vec(r.y * v.z - r.z * v.y, r.z * v.x - r.x * v.z, r.x * v.y - r.y * v.x);
            Vec spin = body_rotate(e, vec(0, 0, 16384), 0);
            assert(degrees_between(h, spin) < 30);
        }
    }
    /* The Moon keeps its near side to Earth, and Pluto and Charon face each
       other, through years of their orbits: only their librations move the
       sub-planet point. */
    for (k = 0; k < 400; k++) {
        const Planet *m, *e, *pl, *ch;
        g.tick = (uint32_t)k * (HZ * 86400U * 9U + HZ * 3517U);
        update_world(&g);
        m = &g.system.planets[moon]; e = &g.system.planets[earth];
        body_latlon(m, sub(e->center, m->center), &lat, &lon);
        assert(fabsl(lat) < 7.5L && fabsl(lon) < 8.5L);
        if (fabsl(lon) > worst) worst = fabsl(lon);
        pl = &g.system.planets[pluto]; ch = &g.system.planets[charon];
        body_latlon(pl, sub(ch->center, pl->center), &lat, &lon);
        assert(fabsl(lat) < 1 && fabsl(lon) < 1);
        body_latlon(ch, sub(pl->center, ch->center), &lat, &lon);
        assert(fabsl(lat) < 1 && fabsl(lon) < 1);
    }
    printf("PASS real spin axes: Earth 23.4, Saturn 26.7, Uranus 97.8 degrees; Greenwich noon at J2000;\n"
           "     the Moon's near side faces Earth over 10 years (librations within %.1f degrees)\n", (double)worst);
}
int main(void) {
    spin_axes();
    landmarks();
    map_sampling();
    measured_bodies();
    polar_geography();
    contour_parity();
    xfloat_arithmetic();
    planet_culling();
    planet_fields();
    planet_outlines();
    return 0;
}
