/* SPDX-License-Identifier: Unlicense; orbital planet fields. See planet_field.h. */
#include "planet_field.h"
#include "noise.h"
#include <string.h>
/* Seeds and spacings shared with terrain.c's fields. */
#define CONTINENT_SHIFT 14
#define REGION_SHIFT 13
#define DETAIL_SHIFT 11
#define REGION_SEED 0x434f4e54U
#define DETAIL_SEED 0x4445504fU
static void fill(int16_t *lattice, int size, uint32_t seed) {
    int x, y, z;
    for (z = 0; z < size; z++)
        for (y = 0; y < size; y++)
            for (x = 0; x < size; x++)
                lattice[(z * size + y) * size + x] = (int16_t)noise_sample(seed, x, y, z);
}
void planet_field_init(PlanetField *f, const Planet *p) {
    f->seed = p->seed;
    f->type = (int)p->type;
    f->flat = p->catalog && p->radius < 100000;
    fill(f->continent, FIELD_CONTINENT, p->seed);
    fill(f->region, FIELD_REGION, p->seed ^ REGION_SEED);
    memset(f->detail_key, 0, sizeof(f->detail_key));
}
static int64_t unit_clamp(int64_t v) { return v < -65536 ? -65536 : v > 65536 ? 65536 : v; }
/* noise_value() reading corners from a cached lattice: same cells, same
   fractions, same blend, so the result is bit-identical. Directions are
   clamped to the unit cube first: a slightly long per-pixel normal must
   never index outside the lattice. */
static int lattice_value(const int16_t *lattice, int size, Vec n, int shift) {
    int fx, fy, fz, i, corner[8];
    int x = noise_cell(unit_clamp(n.x), shift, &fx), y = noise_cell(unit_clamp(n.y), shift, &fy),
        z = noise_cell(unit_clamp(n.z), shift, &fz);
    /* The cube's far face (exactly 65536) is a cell's last corner. */
    if (x > size - 2) { x = size - 2; fx = 4096; }
    if (y > size - 2) { y = size - 2; fy = 4096; }
    if (z > size - 2) { z = size - 2; fz = 4096; }
    const int16_t *base = lattice + (z * size + y) * size + x;
    for (i = 0; i < 8; i++)
        corner[i] = base[((i >> 2) * size + (i >> 1 & 1)) * size + (i & 1)];
    return noise_blend(corner, fx, fy, fz);
}
int planet_field_continent(const PlanetField *f, Vec n) {
    return lattice_value(f->continent, FIELD_CONTINENT, n, CONTINENT_SHIFT);
}
int planet_field_base_height(const PlanetField *f, Vec n) {
    return planet_field_continent(f, n) * 3200 / 4096 - 1450;
}
/* Measured share of terrain_material's lowland and forest (Q8; rock takes
   the rest) among land points, by planet-scale base height at 75 m steps
   from 37 m. Local ranges, hills and valleys lift much of the coastal plain
   into rock; tests/test_planet.c re-measures the table against terrain. */
static const uint8_t lowland_share[16] = {150, 27, 26, 28, 23, 11, 7, 5, 4, 3, 3, 2, 2, 1, 1, 0};
static const uint8_t forest_share[16] = {106, 212, 155, 95, 45, 28, 22, 18, 15, 12, 8, 5, 3, 1, 0, 0};
static void land_mix(int h, int mix[3]) {
    int t = (h - 37) * 3495 >> 10, i, frac; /* (h - 37) * 256 / 75 */
    if (t < 0) t = 0;
    if (t > 15 * 256) t = 15 * 256;
    i = t >> 8; frac = t & 255;
    if (i >= 15) { i = 14; frac = 256; }
    mix[0] = lowland_share[i] + ((lowland_share[i + 1] - lowland_share[i]) * frac >> 8);
    mix[1] = forest_share[i] + ((forest_share[i + 1] - forest_share[i]) * frac >> 8);
    mix[2] = 256 - mix[0] - mix[1];
}
int planet_field_classify(const PlanetField *f, Vec n, int h, int mix[3]) {
    mix[0] = mix[1] = mix[2] = 0;
    if (f->type == GAS_GIANT) return 5;
    /* terrain_material's cap test on the Q14 normal (n is Q16). */
    if (f->type == ICE || n.z / 4 > 14000 || n.z / 4 < -14000) return 4;
    if (f->type != OCEAN) {
        int m = f->type == DESERT || f->type == MOON || !f->flat ? 3 : 1;
        mix[m == 3 ? 2 : 0] = 256;
        return m;
    }
    if (h < 0) return 0;
    land_mix(h, mix);
    return mix[2] >= mix[0] && mix[2] >= mix[1] ? 3 : mix[1] >= mix[0] ? 2 : 1;
}
int planet_field_land(const PlanetField *f, Vec n, int mix[3]) {
    return planet_field_classify(f, n, f->type == OCEAN ? planet_field_base_height(f, n) : 0, mix);
}
int planet_field_material(const PlanetField *f, Vec n) {
    int mix[3];
    return planet_field_land(f, n, mix);
}
/* noise_value() on the fine albedo lattice, corners served from the cache.
   Neighbouring pixels share corners, so nearly every lookup hits. */
static int detail_value(PlanetField *f, Vec n) {
    int fx, fy, fz, i, corner[8];
    int x = noise_cell(unit_clamp(n.x), DETAIL_SHIFT, &fx), y = noise_cell(unit_clamp(n.y), DETAIL_SHIFT, &fy),
        z = noise_cell(unit_clamp(n.z), DETAIL_SHIFT, &fz);
    for (i = 0; i < 8; i++) {
        int cx = x + (i & 1), cy = y + (i >> 1 & 1), cz = z + (i >> 2);
        uint32_t key = (uint32_t)((cz * 66 + cy) * 66 + cx) + 1;
        unsigned slot = (unsigned)(key * 2654435761U) >> 22;
        if (f->detail_key[slot] != key) {
            f->detail_key[slot] = key;
            f->detail_value[slot] = (int16_t)noise_sample(f->seed ^ DETAIL_SEED, cx, cy, cz);
        }
        corner[i] = f->detail_value[slot];
    }
    return noise_blend(corner, fx, fy, fz);
}
int planet_field_region(const PlanetField *f, Vec n) {
    return lattice_value(f->region, FIELD_REGION, n, REGION_SHIFT);
}
int planet_field_regional_albedo(PlanetField *f, Vec n, int material) {
    int a, b;
    if (material == 0 || material == 5) return 0;
    a = lattice_value(f->region, FIELD_REGION, n, REGION_SHIFT) - 2048;
    b = detail_value(f, n) - 2048;
    return a / 600 + b / 1000;
}
