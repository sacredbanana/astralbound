/* SPDX-License-Identifier: Unlicense
   Coherent, immutable terrain sampled by both collision and rendering. */
#include "space.h"
static int mix(int a, int b, int f) {
    return a + (b - a) * f / 4096;
}
static int smooth(int f) {
    return (int)((int64_t)f * f * (12288 - 2 * f) / (4096LL * 4096));
}
static int sample(uint32_t seed, int x, int y, int z) {
    return (int)(hash32(seed ^ hash32((unsigned)x) ^ hash32((unsigned)y + 173U) ^
                        hash32((unsigned)z + 7919U)) &
                 4095);
}
static int noise(uint32_t seed, Vec q, int shift) {
    int x = (int)(q.x + 65536) >> shift, y = (int)(q.y + 65536) >> shift,
        z = (int)(q.z + 65536) >> shift;
    int mask = (1 << shift) - 1;
    int fx = smooth((int)((q.x + 65536) & mask) * 4096 / (1 << shift)),
        fy = smooth((int)((q.y + 65536) & mask) * 4096 / (1 << shift)),
        fz = smooth((int)((q.z + 65536) & mask) * 4096 / (1 << shift));
    int a = mix(sample(seed, x, y, z), sample(seed, x + 1, y, z), fx),
        b = mix(sample(seed, x, y + 1, z), sample(seed, x + 1, y + 1, z), fx),
        c = mix(sample(seed, x, y, z + 1), sample(seed, x + 1, y, z + 1), fx),
        d = mix(sample(seed, x, y + 1, z + 1), sample(seed, x + 1, y + 1, z + 1), fx);
    return mix(mix(a, b, fy), mix(c, d, fy), fz);
}
int terrain_elevation(const Planet *p, Vec relative) {
    Vec n = unit(relative, 65536);
    int h =
        noise(p->seed, n, 14) * 1400 / 4096 + noise(p->seed ^ 0x8537U, n, 12) * 250 / 4096 - 650;
    Vec pad = scale(forward(120, 180), 4, 1);
    int64_t d1 = length(sub(n, vec(0, 65536, 0))), d2 = length(sub(n, pad)), d = d1 < d2 ? d1 : d2;
    if (p->type == GAS_GIANT)
        return 0;
    if (p->type != OCEAN && h < 0)
        h = 0;
    /* Flatten landing sites and blend out to surrounding terrain. */
    d = d * p->radius / 65536;
    if (d < 1000)
        h = 0;
    else if (d < 2000)
        h = h * (int)(d - 1000) / 1000;
    return h;
}
int terrain_height(const Planet *p, Vec relative) {
    int h = terrain_elevation(p, relative);
    return h < 0 ? 0 : h;
}
/* Shared land/water/forest/rock/ice classification drives local and globe LOD. */
int terrain_material(const Planet *p, Vec relative) {
    int h = terrain_elevation(p, relative);
    Vec normal = unit(relative, 16384);
    if (p->type == GAS_GIANT)
        return 5;
    if (p->type == ICE || normal.y > 14000 || normal.y < -14000)
        return 4;
    if (p->type == OCEAN && h <= 0)
        return 0;
    if (p->type == DESERT || p->type == MOON || h > 500)
        return 3;
    if (p->type == OCEAN && h > 80 && h < 450)
        return 2;
    return 1;
}
Vec terrain_vertex(const Planet *p, Vec anchor, Vec right, Vec front, int x, int y) {
    Vec v = add(radial(anchor, p->radius), add(scale(right, x, 16384), scale(front, y, 16384)));
    return add(p->center, radial(v, p->radius + terrain_height(p, v)));
}
int atmosphere_density(const Planet *p, int64_t altitude) {
    int base;
    int64_t remaining;
    if (!p->atmosphere || altitude >= p->atmosphere)
        return 0;
    if (altitude < 0)
        altitude = 0;
    base = p->climate == THIN       ? 12
           : p->climate == FROZEN   ? 6
           : p->climate == DENSE    ? 220
           : p->climate == HYDROGEN ? 180
                                    : 64;
    remaining = p->atmosphere - altitude;
    return (int)((int64_t)base * remaining / p->atmosphere * remaining / p->atmosphere);
}
int nearest_body(const Game *g, Vec pos) {
    int i, best = 0;
    int64_t distance = INT64_MAX;
    for (i = 0; i < g->system.count; i++) {
        const Planet *p = &g->system.planets[i];
        int64_t d = length(sub(pos, scale(p->center, FP, 1))) / FP - p->radius;
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
int64_t altitude_at(const Game *g, Vec pos) {
    const Planet *p = &g->system.planets[nearest_body(g, pos)];
    Vec local = sub(pos, scale(p->center, FP, 1));
    int64_t d = length(local) / FP - p->radius;
    if (d > 2000)
        return d;
    return d - terrain_height(p, scale(local, 1, FP));
}
int terrain_impact(const Game *g, Vec from, Vec to, int body) {
    const Planet *p = &g->system.planets[body];
    Vec a = sub(scale(from, 1, FP), p->center), b = sub(scale(to, 1, FP), p->center);
    int i;
    int64_t len = length(sub(b, a));
    if (!swept_sphere(a, b, vec(0, 0, 0), p->radius + 1000))
        return 0; /* Sample within the broad phase, using up to 65 terrain probes. Fast
crossing of the whole planet is separately covered by the inner sphere. */
    if (swept_sphere(a, b, vec(0, 0, 0), p->radius + 6))
        return 1;
    if (len > 3200)
        len = 3200;
    for (i = 0; i <= (int)(len / 50) + 1; i++) {
        Vec point = add(a, scale(sub(b, a), i, (int)(len / 50) + 1));
        if (length(point) <= p->radius + terrain_height(p, point) + 6)
            return 1;
    }
    return 0;
}
