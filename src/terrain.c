/* SPDX-License-Identifier: Unlicense
   Coherent, immutable terrain sampled by both collision and rendering. */
#include "space.h"
#include <stdlib.h>
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
    Vec n = unit(body_rotate(p, relative, 1), 65536);
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
/* Collision follows the renderer's fixed 256 m cube-face triangles. Sampling
   the raw angular noise at a moving position quantizes hills near the pads. */
int terrain_height(const Planet *p, Vec relative) {
    Planet fixed = *p;
    Vec q = body_rotate(p, relative, 1), a, b, c, hit;
    int axis = 0, face, u, v, u0, v0, du, dv, h, i;
    int64_t dominant = llabs(q.x);
    Vec points[3];
    if (llabs(q.y) > dominant) { axis = 1; dominant = llabs(q.y); }
    if (llabs(q.z) > dominant) { axis = 2; dominant = llabs(q.z); }
    if (!dominant || p->type == GAS_GIANT) return 0;
    face = axis * 2 + ((axis == 0 ? q.x : axis == 1 ? q.y : q.z) < 0);
    q = scale(q, p->radius, dominant);
    u = (int)(axis == 0 ? q.y : q.x); v = (int)(axis == 2 ? q.y : q.z);
    u0 = (u >= 0 ? u / 256 : (u - 255) / 256) * 256;
    v0 = (v >= 0 ? v / 256 : (v - 255) / 256) * 256;
    du = u - u0; dv = v - v0;
    set_rotation(&fixed, 0);
    for (i = 0; i < 3; i++) {
        int x = u0 + (i == 2 || (i == 1 && du >= dv) ? 256 : 0);
        int y = v0 + (i == 2 || (i == 1 && du < dv) ? 256 : 0);
        int sign = face & 1 ? -p->radius : p->radius;
        Vec d = axis == 0 ? vec(sign,x,y) : axis == 1 ? vec(x,sign,y) : vec(x,y,sign);
        h = terrain_elevation(&fixed, d); if (h < 0) h = 0;
        points[i] = radial(d, p->radius + h);
    }
    a = points[0]; b = points[1]; c = points[2];
    hit = du >= dv ? add(a,add(scale(sub(b,a),du,256),scale(sub(c,b),dv,256))) :
                       add(a,add(scale(sub(c,b),du,256),scale(sub(b,a),dv,256)));
    h = (int)(length(hit) - p->radius);
    return h > 0 ? h : 0;
}
/* Shared land/water/forest/rock/ice classification drives local and globe LOD. */
int terrain_material(const Planet *p, Vec relative) {
    int h = terrain_elevation(p, relative);
    Vec normal = unit(body_rotate(p, relative, 1), 16384);
    if (p->type == GAS_GIANT)
        return 5;
    if (p->type == ICE || normal.z > 14000 || normal.z < -14000)
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

/* Direct illumination from every visible stellar disc. Planet/moon eclipses
   use the same bounded swept-sphere primitive as flight. No render-time clock. */
static int stellar_light(const System *s, const Planet *p, Vec point, Vec normal, int star) {
    Vec sun = unit(sub(s->star[star].center, point), 16384);
    int dot = ((int)normal.x * (int)sun.x + (int)normal.y * (int)sun.y +
               (int)normal.z * (int)sun.z) / 16384;
    int j;
    if (dot <= 0) return 0;
    for (j = 0; j < s->count; j++) {
        const Planet *other = &s->planets[j];
        if (other != p && swept_sphere(point, s->star[star].center, other->center, other->radius))
            return 0;
    }
    return dot;
}
int surface_light(const System *s, const Planet *p, Vec local) {
    Vec normal = unit(local, 16384), point = add(p->center, local);
    int i, light = 0;
    for (i = 0; i < s->stars; i++) light += stellar_light(s, p, point, normal, i);
    return light > 16384 ? 16384 : light;
}
static int cloud_cover(const Planet *p, Vec local, uint32_t tick) {
    Vec n = unit(body_rotate(p, local, 1), 65536);
    int drift = (int)(tick / (HZ * 60U) % 8192U);
    if (p->climate != TEMPERATE && p->climate != FROZEN && p->climate != HYDROGEN &&
        p->climate != DENSE) return 0;
    n.x += drift; n.y -= drift / 2;
    return noise(p->seed ^ 0x434c4f55U, n, 12);
}
int cloud_density(const Planet *p, Vec local, uint32_t tick) {
    int64_t alt = length(local) - p->radius;
    int base = p->climate == HYDROGEN ? 18000 : 1800;
    int thickness = p->climate == HYDROGEN ? 10000 : 1200;
    int cover = cloud_cover(p, local, tick) - 1500;
    int64_t d = alt - base;
    if (d < 0) d = -d;
    if (!p->atmosphere || cover <= 0 || d >= thickness) return 0;
    if (cover > 1200) cover = 1200;
    return (int)((int64_t)cover * (thickness - d) * 256 / (1200LL * thickness));
}
int precipitation(const Planet *p, Vec local, uint32_t tick) {
    int64_t alt = length(local) - p->radius;
    Vec n = unit(body_rotate(p, local, 1), 16384);
    if (!p->atmosphere || alt > 1800 || alt < 0 ||
        (p->climate != TEMPERATE && p->climate != FROZEN) ||
        cloud_cover(p, local, tick) < 2450) return 0;
    return p->climate == FROZEN || n.z > 12000 || n.z < -12000 || alt > 1400 ? 2 : 1;
}

/* Single-scattering Rayleigh approximation: 1+cos^2 phase, optical path
   enhanced towards the horizon, blue spectral response in the indexed palette. */
int rayleigh_scattering(const System *s, const Planet *p, Vec local, Vec view) {
    int i, phase = 0;
    Vec up = unit(local, 16384), direction = unit(view, 16384);
    int elevation = ((int)up.x * (int)direction.x + (int)up.y * (int)direction.y +
                     (int)up.z * (int)direction.z) / 16384;
    int density = atmosphere_density(p, length(local) - p->radius);
    if (!density) return 0;
    for (i = 0; i < s->stars; i++) {
        Vec sun = unit(sub(s->star[i].center, add(p->center,local)), 16384);
        int cosine = ((int)sun.x * (int)direction.x + (int)sun.y * (int)direction.y +
                      (int)sun.z * (int)direction.z) / 16384;
        int light = stellar_light(s, p, add(p->center,local), up, i);
        phase += (8 + cosine * cosine / 33554432) * light / 16384;
    }
    if (elevation < 0) elevation = -elevation;
    phase = phase * (24576 - elevation) / 16384;
    if (density < 32) phase = phase * density / 32;
    return phase > 31 ? 31 : phase;
}
