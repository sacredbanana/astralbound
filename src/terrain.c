/* SPDX-License-Identifier: Unlicense
   Coherent, immutable terrain sampled by both collision and rendering. */
#include "astralbound.h"
#include "noise.h"
#include <stdlib.h>
/* Metre-space geology keeps mountain slopes visible on large planets and
   small moons alike. Intersecting coherent fields form ranges, winding cuts,
   isolated cones/calderas and crater bowls without longitude/cube-face seams. */
static int landform_height(const Planet *p, Vec n, Vec q) {
    int continent = noise_value(p->seed, n, 14);
    int region = noise_value(p->seed ^ 0x52414e47U, q, 16);
    int fold = noise_value(p->seed ^ 0x464f4c44U, q, 14) - 2048;
    int crossfield = noise_value(p->seed ^ 0x43524f53U, q, 14) - 2048;
    int hills = noise_value(p->seed ^ 0x48494c4cU, q, 12);
    int ridge = 2048 - abs(fold);
    int mountain = ridge * ridge / 2048;
    int cut = abs(crossfield), valley = cut < 400 ? (400-cut) : 0;
    int h, province = region > 1400 ? region - 1400 : 0;
    if (province > 1600) province = 1600;
    mountain = mountain * province / 1600;
    switch (p->type) {
    case OCEAN:
        /* Continents retain sea-level coasts; inland ranges rise above them. */
        h = continent * 3200 / 4096 - 1450;
        if (h > 0) {
            int inland = h < 500 ? h : 500;
            h += (mountain * 2200 / 2048 + hills / 8 - valley * 2) * inland / 500;
        }
        break;
    case DESERT:
        /* Elevated plateaus cut by narrow, deep meandering canyon systems. */
        h = 1800 + region / 4 + mountain * 1900 / 2048 + hills / 10 - valley * 5;
        break;
    case ICE:
        /* Broad glacial uplands, sharp pressure ridges and rift valleys. */
        h = 850 + region / 3 + mountain * 1400 / 2048 + hills / 16 - valley * 3;
        break;
    case MOON: {
        int bowl = (int)isqrt64((uint64_t)((int64_t)fold*fold +
                                             (int64_t)crossfield*crossfield));
        h = 1100 + region / 4 + mountain * 1200 / 2048 + hills / 12;
        /* Bowl floor, raised rim and smooth outer ejecta apron. */
        if (bowl < 650) h -= (650-bowl) * 1400 / 650;
        else if (bowl < 950) h += (bowl-650) * 650 / 300;
        else if (bowl < 1300) h += (1300-bowl) * 650 / 350;
        break;
    }
    default:
        h = 800 + region / 3 + mountain * 3000 / 2048 + hills / 8 - valley * 2;
        break;
    }
    /* Volcanic provinces on rock/desert worlds and some rocky moons. Two
       zero contours intersect in isolated summits with excavated calderas. */
    if ((p->type == ROCKY || p->type == DESERT ||
         (p->type == MOON && (p->seed & 1))) && region > 1900) {
        int cone = (int)isqrt64((uint64_t)((int64_t)fold*fold +
                                             (int64_t)crossfield*crossfield));
        int relief = 0;
        if (cone < 900) relief = (900-cone) * 3200 / 900;
        if (cone < 220) relief -= (220-cone) * 1400 / 220;
        h += relief * (region < 2400 ? region-1900 : 500) / 500;
    }
    return h;
}
int terrain_elevation(const Planet *p, Vec relative) {
    if(p->catalog && p->radius<100000) return 0;
    Vec fixed = body_rotate(p, relative, 1);
    Vec n, q, pad;
    int h;
    int64_t d1, d2, d;
    if (p->type == GAS_GIANT) return 0;
    n = unit(fixed, 65536); q = radial(fixed, p->radius);
    h = landform_height(p, n, q);
    if (h > TERRAIN_MAX_HEIGHT) h = TERRAIN_MAX_HEIGHT;
    if (p->type != OCEAN && h < 0) h = 0;
    pad = radial(forward(120, 180), p->radius);
    d1 = length(sub(q, vec(0, p->radius, 0)));
    d2 = length(sub(q, pad)); d = d1 < d2 ? d1 : d2;
    /* Metre-accurate, smooth pad transitions avoid quantized rings/cliffs. */
    if (d < 1000) h = 0;
    else if (d < 3000) h = h * noise_smooth((int)(d-1000)*4096/2000) / 4096;
    return h;
}
/* Collision follows the renderer's fixed 256 m cube-face triangles. Sampling
   the raw angular noise at a moving position quantizes hills near the pads. */
/* Lattice corners, memoised. Ports, forests and ground markers sample the
   same few dozen corners many times a frame, and each costs a dozen noise
   octaves: a quarter-second a frame on a 68030. The memo is exact: an entry
   holds the corner's full key and the point computed for it. */
#define CORNER_CACHE 512
static struct {
    uint32_t seed;
    int32_t radius, x, y;
    int16_t type, catalog, face, valid;
    Vec point;
} corner_cache[CORNER_CACHE];
static Vec corner_point(const Planet *p, int face, int x, int y) {
    unsigned slot = (hash32((unsigned)x * 2654435761U ^ (unsigned)y * 40503U ^ (unsigned)face) ^ p->seed) & (CORNER_CACHE - 1);
    int sign = face & 1 ? -p->radius : p->radius, axis = face / 2, e;
    Planet fixed;
    Vec d;
    if (corner_cache[slot].valid && corner_cache[slot].seed == p->seed && corner_cache[slot].radius == p->radius &&
        corner_cache[slot].type == (int16_t)p->type && corner_cache[slot].catalog == (int16_t)p->catalog &&
        corner_cache[slot].face == face && corner_cache[slot].x == x && corner_cache[slot].y == y)
        return corner_cache[slot].point;
    /* The body-fixed frame: the same planet at rotation zero. */
    fixed = *p;
    set_rotation(&fixed, 0);
    d = axis == 0 ? vec(sign,x,y) : axis == 1 ? vec(x,sign,y) : vec(x,y,sign);
    e = terrain_elevation(&fixed, d); if (e < 0) e = 0;
    corner_cache[slot].seed = p->seed; corner_cache[slot].radius = p->radius;
    corner_cache[slot].type = (int16_t)p->type; corner_cache[slot].catalog = (int16_t)p->catalog;
    corner_cache[slot].face = (int16_t)face; corner_cache[slot].x = x; corner_cache[slot].y = y;
    corner_cache[slot].valid = 1;
    return corner_cache[slot].point = radial(d, p->radius + e);
}
/* relative and the result share one unit: one = 1 for metres, FP for Q8. */
static int64_t mesh_height(const Planet *p, Vec relative, int64_t one) {
    Vec q = body_rotate(p, relative, 1), a, b, c, hit;
    int axis = 0, face, i;
    int64_t dominant = llabs(q.x), cell = 256 * one, u, v, u0, v0, du, dv, h;
    Vec points[3];
    if (llabs(q.y) > dominant) { axis = 1; dominant = llabs(q.y); }
    if (llabs(q.z) > dominant) { axis = 2; dominant = llabs(q.z); }
    if (!dominant || p->type == GAS_GIANT) return 0;
    face = axis * 2 + ((axis == 0 ? q.x : axis == 1 ? q.y : q.z) < 0);
    q = scale(q, (int64_t)p->radius * one, dominant);
    u = axis == 0 ? q.y : q.x; v = axis == 2 ? q.y : q.z;
    u0 = (u >= 0 ? u / cell : (u - cell + 1) / cell) * cell;
    v0 = (v >= 0 ? v / cell : (v - cell + 1) / cell) * cell;
    du = u - u0; dv = v - v0;
    for (i = 0; i < 3; i++) {
        int x = (int)(u0 / one) + (i == 2 || (i == 1 && du >= dv) ? 256 : 0);
        int y = (int)(v0 / one) + (i == 2 || (i == 1 && du < dv) ? 256 : 0);
        points[i] = corner_point(p, face, x, y);
        if (one != 1) points[i] = scale(points[i], one, 1);
    }
    a = points[0]; b = points[1]; c = points[2];
    hit = du >= dv ? add(a,add(scale(sub(b,a),du,cell),scale(sub(c,b),dv,cell))) :
                       add(a,add(scale(sub(c,b),du,cell),scale(sub(b,a),dv,cell)));
    h = length(hit) - (int64_t)p->radius * one;
    return h > 0 ? h : 0;
}
int terrain_height(const Planet *p, Vec relative) {
    return (int)mesh_height(p, relative, 1);
}
/* The same surface at Q8 precision, for a commander walking on it. */
int64_t terrain_height_fine(const Planet *p, Vec relative_q8) {
    return mesh_height(p, relative_q8, FP);
}
/* Shared land/water/forest/rock/ice classification drives local and globe LOD. */
int terrain_material(const Planet *p, Vec relative) {
    int h = terrain_elevation(p, relative);
    Vec normal = unit(body_rotate(p, relative, 1), 16384);
    if (p->type == GAS_GIANT)
        return 5;
    if (p->type == ICE || normal.z > 14000 || normal.z < -14000)
        return 4;
    /* Pads are levelled to exactly sea level: dry ground, never a pond. A
       pad out at sea becomes a 1 km island where the seabed blend begins. */
    if (p->type == OCEAN && h < 0)
        return 0;
    if (p->type == DESERT || p->type == MOON || h > 500)
        return 3;
    if (p->type == OCEAN && h > 80 && h < 450)
        return 2;
    return 1;
}
/* Orbital deposits use angular scales, so small moons and large planets both
   have recognisable large regions. This same field underlies local geology. */
int terrain_regional_albedo(const Planet *p, Vec fixed_direction, int material) {
    Vec n = unit(fixed_direction, 65536);
    int a, b;
    if (material == 0 || material == 5) return 0;
    a = noise_value(p->seed ^ 0x434f4e54U, n, 13) - 2048;
    b = noise_value(p->seed ^ 0x4445504fU, n, 11) - 2048;
    return a / 600 + b / 1000;
}
/* Geological reflectance in spherical body-space metres. Sampling a 3-D
   field avoids longitude/pole and cube-face seams. Integer coherent noise
   supplies kilometre deposits, 256 m weathering and 64 m ground structure. */
int terrain_albedo(const Planet *p, Vec fixed_direction, int material) {
    if (material == 5) return 0;
    Vec q = radial(fixed_direction, p->radius);
    int broad = noise_value(p->seed ^ 0x52454749U, q, 10) - 2048;
    int weather = noise_value(p->seed ^ 0x57454154U, q, 8) - 2048;
    int fine = noise_value(p->seed ^ 0x534f494cU, q, 6) - 2048;
    int detail = broad / 480 + weather / 640 + fine / 900;
    if (material == 0) {
        /* Broad depth/current variation beneath the filtered small waves. */
        detail = broad / 800 + weather / 800 + fine / 220;
    } else if (p->type == MOON) {
        /* Jittered 512 m impact centres. A section through spherical 3-D
           shells produces dark bowls and bright ejecta rims, without a
           repeated screen-space stamp or additional collision geometry. */
        int x = (int)q.x >> 9, y = (int)q.y >> 9, z = (int)q.z >> 9;
        int i;
        for (i = 0; i < 8; i++) {
            int cx = x + (i & 1), cy = y + ((i >> 1) & 1), cz = z + (i >> 2);
            uint32_t h = hash32(p->seed ^ hash32((unsigned)cx) ^
                hash32((unsigned)cy + 173U) ^ hash32((unsigned)cz + 7919U));
            int dx = (int)q.x - (cx * 512 + (int)(h & 127) - 64);
            int dy = (int)q.y - (cy * 512 + (int)((h >> 7) & 127) - 64);
            int dz = (int)q.z - (cz * 512 + (int)((h >> 14) & 127) - 64);
            int d = dx*dx + dy*dy + dz*dz, r = 160 + (int)((h >> 21) & 127);
            if (d < r*r/2) detail -= 5;
            else if (d < r*r) detail -= 2;
            else if (d < r*r*5/4) detail += 6;
        }
    } else if (material == 4) {
        /* Distorted fracture veins cut through broad glacial deposits. */
        int vein = abs(weather);
        detail = broad / 600 + fine / 1000;
        if (vein < 240) detail -= (240 - vein) / 30;
    } else if (p->type == DESERT) {
        /* Wind-aligned dune crests, slowly bent by the deposit field. */
        int phase = (int)((q.x + q.y / 3 + broad / 12) & 511);
        int ridge = phase < 256 ? phase : 511 - phase;
        detail = broad / 700 + weather / 1100 + (ridge - 128) / 24;
    } else if (material == 3) {
        /* Folded strata with mineral veins and weathered scree. */
        int phase = (int)((q.z + broad / 8 + weather / 16) & 255);
        detail += phase < 40 ? 4 : phase > 200 ? -3 : 0;
    } else if (material == 2) {
        /* Moisture produces continuous vegetation clearings and soil. */
        detail = broad / 650 + weather / 380 + fine / 850;
    }
    detail += terrain_regional_albedo(p, fixed_direction, material);
    if (detail < -10) detail = -10;
    if (detail > 10) detail = 10;
    return detail;
}
Vec terrain_vertex(const Planet *p, Vec anchor, Vec right, Vec front, int x, int y) {
    /* A port asks for dozens of vertices round one anchor: its point on the
       datum sphere (two square roots and two scales) is remembered. */
    static Vec last_anchor, last_base;
    static int32_t last_radius;
    static int last_valid;
    Vec v;
    if (!last_valid || last_radius != p->radius || last_anchor.x != anchor.x || last_anchor.y != anchor.y ||
        last_anchor.z != anchor.z) {
        last_anchor = anchor; last_radius = p->radius; last_valid = 1;
        last_base = radial(anchor, p->radius);
    }
    v = add(last_base, add(scale(right, x, 16384), scale(front, y, 16384)));
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
    if (d > TERRAIN_MAX_HEIGHT + 2000)
        return d;
    return d - terrain_height(p, scale(local, 1, FP));
}
int terrain_impact(const Game *g, Vec from, Vec to, int body) {
    const Planet *p = &g->system.planets[body];
    Vec a = sub(scale(from, 1, FP), p->center), b = sub(scale(to, 1, FP), p->center);
    int i;
    int64_t len = length(sub(b, a));
    if (!swept_sphere(a, b, vec(0, 0, 0), p->radius + TERRAIN_MAX_HEIGHT + 6))
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
    if (dot <= 0 || s->star[star].type==ST_BLACK_HOLE) return 0;
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
static int cloud_cover_fixed(const Planet *p, Vec fixed, uint32_t tick) {
    Vec n = unit(fixed, 65536);
    int drift = (int)(tick / (HZ * 60U) % 8192U);
    if (p->climate != TEMPERATE && p->climate != FROZEN && p->climate != HYDROGEN &&
        p->climate != DENSE) return 0;
    n.x += drift; n.y -= drift / 2;
    return noise_value(p->seed ^ 0x434c4f55U, n, 12);
}
static int cloud_cover(const Planet *p, Vec local, uint32_t tick) {
    return cloud_cover_fixed(p, body_rotate(p, local, 1), tick);
}
/* Cloud floor altitude and the vertical scale of the convective layer. */
static int cloud_floor(const Planet *p) { return p->climate == HYDROGEN ? 13000 : 1200; }
static int cloud_thickness(const Planet *p) { return p->climate == HYDROGEN ? 10000 : 1200; }
int cloud_spacing(const Planet *p) { return p->climate == HYDROGEN ? 16000 : 2000; }
static int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
/* Project a body-fixed point onto one cube face of the cloud shell. Each
   cell belongs to the face its centre is dominant on, so neighbouring faces
   tile the sphere without seams or duplicates. */
int cloud_face_cell(const Planet *p, int face, Vec fixed, int64_t reach, int *u, int *v) {
    int axis = face / 2, cell = cloud_spacing(p);
    int64_t size = p->radius + cloud_floor(p);
    int64_t comp = axis == 0 ? fixed.x : axis == 1 ? fixed.y : fixed.z, a, b;
    if (face & 1) comp = -comp;
    if (comp <= 0) return 0;
    a = (axis == 0 ? fixed.y : fixed.x) * size / comp;
    b = (axis == 2 ? fixed.y : fixed.z) * size / comp;
    if (a > size + reach || -a > size + reach || b > size + reach || -b > size + reach) return 0;
    *u = (int)floor_div(a, cell);
    *v = (int)floor_div(b, cell);
    return 1;
}
/* The field only changes when the cover noise drifts, once a minute, so the
   renderer and fog share a small direct-mapped cache of generated clouds. */
#define CLOUD_CACHE 256
static struct {
    uint32_t key, drift;
    int face, u, v, valid, present;
    Cloud cloud;
} cloud_cache[CLOUD_CACHE];
static int cloud_build(const Planet *p, int face, int u, int v, uint32_t tick, Cloud *c) {
    int axis = face / 2, cell = cloud_spacing(p), thickness = cloud_thickness(p);
    int64_t size = p->radius + cloud_floor(p), a, b, s;
    int strength, radius, height, main, i, sides;
    uint32_t h = hash32(p->seed ^ 0x43554d55U ^ (uint32_t)face * 0x9e3779b9U ^
                        hash32((uint32_t)u * 73856093U ^ (uint32_t)v * 19349663U));
    uint32_t h2 = hash32(h + 0x632be5abU);
    Vec d, up, east;
    a = (int64_t)u * cell + cell / 4 + (int)(h % (unsigned)(cell / 2));
    b = (int64_t)v * cell + cell / 4 + (int)((h >> 11) % (unsigned)(cell / 2));
    if (a >= size || -a > size || b >= size || -b > size) return 0;
    s = face & 1 ? -size : size;
    d = axis == 0 ? vec(s, a, b) : axis == 1 ? vec(a, s, b) : vec(a, b, s);
    strength = cloud_cover_fixed(p, d, tick) - 1500;
    if (strength <= 0) return 0;
    strength = strength >= 1200 ? 256 : strength * 256 / 1200;
    /* Thin cover leaves scattered fair-weather cumulus; heavy cover packs them. */
    if ((int)(h >> 24) >= 40 + strength * 3 / 4) return 0;
    radius = cell * (20 + strength * 16 / 256 + (int)(h2 % 8U)) / 100;
    height = thickness * (40 + strength * 70 / 256 + (int)((h2 >> 3) % 20U)) / 100;
    up = unit(d, 16384);
    east = unit(vec(up.y, -up.x, 0), 16384);
    if (!length(east)) east = vec(16384, 0, 0);
    c->base = radial(d, size);
    c->up = up;
    c->east = east;
    c->north = vec((east.y * up.z - east.z * up.y) / 16384, (east.z * up.x - east.x * up.z) / 16384,
                   (east.x * up.y - east.y * up.x) / 16384);
    c->strength = (int16_t)strength;
    /* A broad flat-based core, a narrower tower when the cell convects
       strongly, and smaller shoulder puffs: the cauliflower cumulus outline. */
    main = radius * 72 / 100;
    c->puff[0].x = c->puff[0].y = 0;
    c->puff[0].radius = (int16_t)main;
    c->puff[0].z = (int16_t)(main * 55 / 100);
    c->puffs = 1;
    if (height > main * 3 / 2) {
        int top = main * 78 / 100, z = height - top;
        /* Keep the tower rooted in the core rather than floating above it. */
        if (z > main * 155 / 100 + top / 2) z = main * 155 / 100 + top / 2;
        c->puff[1].x = (int16_t)(((int)(h2 >> 8 & 63) - 32) * main / 128);
        c->puff[1].y = (int16_t)(((int)(h2 >> 14 & 63) - 32) * main / 128);
        c->puff[1].radius = (int16_t)top;
        c->puff[1].z = (int16_t)z;
        c->puffs = 2;
    } else {
        c->puff[0].z = (int16_t)(height - main > main / 3 ? height - main : main / 3);
    }
    sides = 2 + (strength > 100) + (int)(h2 >> 20 & 1);
    for (i = 0; i < sides && c->puffs < CLOUD_PUFFS; i++) {
        uint32_t k = hash32(h2 + (uint32_t)i * 0x2545f491U);
        int angle = (int)(h2 >> 22) + i * 1024 / sides + (int)(k % 96U) - 48;
        int r = radius * (42 + (int)(k >> 8 & 15)) / 100;
        int reach = radius * (40 + (int)(k >> 12 & 15)) / 100;
        CloudPuff *puff = &c->puff[c->puffs++];
        puff->x = (int16_t)((int64_t)reach * cosi(angle) / 16384);
        puff->y = (int16_t)((int64_t)reach * sini(angle) / 16384);
        puff->radius = (int16_t)r;
        puff->z = (int16_t)(r * 50 / 100 + (int)(k >> 16) % (r / 3 + 1));
    }
    c->radius = (int16_t)(radius + radius * 51 / 100);
    for (i = 0, height = 1; i < c->puffs; i++)
        if (c->puff[i].z + c->puff[i].radius > height) height = c->puff[i].z + c->puff[i].radius;
    c->height = (int16_t)height;
    return 1;
}
const Cloud *cloud_at(const Planet *p, int face, int u, int v, uint32_t tick) {
    uint32_t key = p->seed ^ (uint32_t)p->climate * 0x01000193U ^ (uint32_t)p->radius;
    uint32_t drift = tick / (HZ * 60U);
    unsigned slot = hash32(key ^ (uint32_t)face * 0x27d4eb2dU ^ (uint32_t)u * 0x165667b1U ^
                           (uint32_t)v * 0xd3a2646cU) % CLOUD_CACHE;
    if (!p->atmosphere) return 0;
    if (!cloud_cache[slot].valid || cloud_cache[slot].key != key || cloud_cache[slot].drift != drift ||
        cloud_cache[slot].face != face || cloud_cache[slot].u != u || cloud_cache[slot].v != v) {
        cloud_cache[slot].valid = 1;
        cloud_cache[slot].key = key;
        cloud_cache[slot].drift = drift;
        cloud_cache[slot].face = face;
        cloud_cache[slot].u = u;
        cloud_cache[slot].v = v;
        cloud_cache[slot].present = cloud_build(p, face, u, v, tick, &cloud_cache[slot].cloud);
    }
    return cloud_cache[slot].present ? &cloud_cache[slot].cloud : 0;
}
/* Density rises from just outside each puff's surface to full strength two
   thirds of the way in, so fog thickens as the faceted shell is crossed. */
int cloud_density(const Planet *p, Vec local, uint32_t tick) {
    int64_t alt = length(local) - p->radius;
    int floor = cloud_floor(p), cell = cloud_spacing(p), face, u, v, du, dv, k, best = 0;
    Vec fixed;
    if (!p->atmosphere || alt < floor - cloud_thickness(p) / 4 || alt > floor + cloud_thickness(p) * 2)
        return 0;
    fixed = body_rotate(p, local, 1);
    for (face = 0; face < 6; face++) {
        if (!cloud_face_cell(p, face, fixed, cell * 2, &u, &v)) continue;
        for (dv = -1; dv <= 1; dv++) for (du = -1; du <= 1; du++) {
            const Cloud *c = cloud_at(p, face, u + du, v + dv, tick);
            Vec rel;
            int64_t x, y, z;
            if (!c) continue;
            rel = sub(fixed, c->base);
            x = (rel.x * c->east.x + rel.y * c->east.y + rel.z * c->east.z) / 16384;
            y = (rel.x * c->north.x + rel.y * c->north.y + rel.z * c->north.z) / 16384;
            z = (rel.x * c->up.x + rel.y * c->up.y + rel.z * c->up.z) / 16384;
            if (x > c->radius * 2 || -x > c->radius * 2 || y > c->radius * 2 || -y > c->radius * 2)
                continue;
            for (k = 0; k < c->puffs; k++) {
                const CloudPuff *puff = &c->puff[k];
                int64_t r = (int64_t)puff->radius * 115 / 100, dx = x - puff->x, dy = y - puff->y,
                        dz = z - puff->z, d2 = dx * dx + dy * dy + dz * dz, value;
                if (z < -puff->radius / 8 || d2 >= r * r) continue;
                value = 768 * (r * r - d2) / (r * r);
                if (value > 256) value = 256;
                value = value * (160 + c->strength * 96 / 256) / 256;
                if (value > best) best = (int)value;
            }
        }
    }
    return best;
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
/* Same density units as the air column; the sulphuric cloud of DENSE worlds
   and the haze of HYDROGEN giants hide the sky until well above their floor. */
int sky_opacity(const Planet *p, int64_t altitude) {
    int density = atmosphere_density(p, altitude);
    return p->climate == DENSE || p->climate == HYDROGEN ? density * 4 : density;
}
/* Peaks while a star sits within about 3 degrees of the horizon, holds into
   civil twilight to about 11 degrees below it and fades by 17 degrees above.
   Eclipses are ignored: the band is lit by air beyond the local shadow. */
int twilight(const System *s, const Planet *p, Vec local, Vec *sun) {
    Vec up = unit(local, 16384);
    int i, best = 0, density = atmosphere_density(p, length(local) - p->radius);
    if (!density) return 0;
    for (i = 0; i < s->stars; i++) {
        Vec d;
        int elevation, glow;
        if (s->star[i].type == ST_BLACK_HOLE) continue;
        d = unit(sub(s->star[i].center, add(p->center, local)), 16384);
        elevation = ((int)up.x * (int)d.x + (int)up.y * (int)d.y + (int)up.z * (int)d.z) / 16384;
        glow = elevation <= -3200 || elevation >= 5000 ? 0
               : elevation < 0                         ? 256 * (elevation + 3200) / 3200
               : elevation <= 1000                     ? 256
                                                       : 256 * (5000 - elevation) / 4000;
        if (glow > best) {
            best = glow;
            *sun = d;
        }
    }
    /* Thin CO2 and nitrogen still colour their horizon, only more faintly. */
    return density >= 16 ? best : best * (96 + density * 10) / 256;
}
