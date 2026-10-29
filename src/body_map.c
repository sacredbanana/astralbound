/* SPDX-License-Identifier: Unlicense; measured body maps. See body_map.h. */
#include "body_map.h"
#include <string.h>
#include "body_maps.inc"
const BodyMap *body_map_find(const char *name) {
    int i;
    for (i = 0; i < (int)(sizeof(body_maps) / sizeof(body_maps[0])); i++)
        if (!strcmp(body_maps[i].name, name)) return &body_maps[i];
    return NULL;
}
/* Faces and their lattice axes match planet_contours.c: face = 2 * axis +
   (negative), and the first and second remaining axes run i and j. The
   gnomonic coordinate s / dom needs no unit vector, only a shift that keeps
   (s + dom) * n * 256 within 31 bits. */
int body_map_sample(const BodyMap *m, const uint8_t *c, Vec v) {
    int64_t ax = v.x < 0 ? -v.x : v.x, ay = v.y < 0 ? -v.y : v.y, az = v.z < 0 ? -v.z : v.z;
    int64_t dom, s, t;
    int n = m->n, side = n + 1, face, i, j, fu, fv, top, bottom;
    uint32_t gu, gv;
    if (ax >= ay && ax >= az) { face = v.x < 0; dom = ax; s = v.y; t = v.z; }
    else if (ay >= az) { face = 2 + (v.y < 0); dom = ay; s = v.x; t = v.z; }
    else { face = 4 + (v.z < 0); dom = az; s = v.x; t = v.y; }
    if (!dom) return c[0] << 8;
    while (dom > 65536) { dom >>= 1; s >>= 1; t >>= 1; }
    if (s < -dom) s = -dom;
    if (t < -dom) t = -dom;
    gu = (uint32_t)(s + dom) * (uint32_t)(n * 128) / (uint32_t)dom;
    gv = (uint32_t)(t + dom) * (uint32_t)(n * 128) / (uint32_t)dom;
    i = (int)(gu >> 8); fu = (int)(gu & 255);
    j = (int)(gv >> 8); fv = (int)(gv & 255);
    if (i >= n) { i = n - 1; fu = 256; }
    if (j >= n) { j = n - 1; fv = 256; }
    c += (face * side + j) * side + i;
    top = c[0] * 256 + (c[1] - c[0]) * fu;
    bottom = c[side] * 256 + (c[side + 1] - c[side]) * fu;
    return (top * 256 + (bottom - top) * fv) >> 8;
}
int body_map_continent(const BodyMap *m, Vec n) {
    return body_map_sample(m, m->continent, n) >> 4;
}
int body_map_relief(const BodyMap *m, Vec n) {
    return m->relief ? (body_map_sample(m, m->relief, n) - 32768) * m->relief_step / 256 : 0;
}
int body_map_region(const BodyMap *m, Vec n) {
    return m->albedo ? body_map_sample(m, m->albedo, n) >> 4 : 2048;
}
/* Floor, so every point darker than the median is below zero. */
int body_map_regional_albedo(const BodyMap *m, Vec n) {
    int a = m->albedo ? body_map_sample(m, m->albedo, n) - 32768 : 0;
    return a >= 0 ? a >> 12 : -((-a + 4095) >> 12);
}
int body_map_ice(const BodyMap *m, Vec n) {
    return m->ice ? (body_map_sample(m, m->ice, n) - 32768) / 4 : -9000;
}
