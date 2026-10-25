/* SPDX-License-Identifier: Unlicense
   Integer value noise shared by terrain (collision and close rendering) and
   the orbital planet field. One definition keeps every view of a world, and
   its collision surface, on exactly the same coastlines. */
#ifndef ASTRALBOUND_NOISE_H
#define ASTRALBOUND_NOISE_H
#include "astralbound.h"
/* Q12 blend: a + (b - a) * f, f in 0..4096. */
static inline int noise_mix(int a, int b, int f) {
    return a + (b - a) * f / 4096;
}
/* Q12 smoothstep. */
static inline int noise_smooth(int f) {
    return (int)((int64_t)f * f * (12288 - 2 * f) / (4096LL * 4096));
}
/* Lattice value 0..4095 at integer cell corner (x, y, z). */
static inline int noise_sample(uint32_t seed, int x, int y, int z) {
    return (int)(hash32(seed ^ hash32((unsigned)x) ^ hash32((unsigned)y + 173U) ^
                        hash32((unsigned)z + 7919U)) &
                 4095);
}
/* Trilinear blend of a cell's eight corner values (index bit 0 = x, 1 = y,
   2 = z) with smoothstepped Q12 fractions. */
static inline int noise_blend(const int corner[8], int fx, int fy, int fz) {
    int a = noise_mix(corner[0], corner[1], fx), b = noise_mix(corner[2], corner[3], fx),
        c = noise_mix(corner[4], corner[5], fx), d = noise_mix(corner[6], corner[7], fx);
    return noise_mix(noise_mix(a, b, fy), noise_mix(c, d, fy), fz);
}
/* Cell index and smoothstepped Q12 fraction of one coordinate. Coordinates
   are offset by 65536 so the unit sphere (Q16) and metre fields stay positive
   on the lattice the terrain has always used. */
static inline int noise_cell(int64_t q, int shift, int *fraction) {
    int mask = (1 << shift) - 1;
    *fraction = noise_smooth((int)((q + 65536) & mask) * 4096 / (1 << shift));
    return (int)(q + 65536) >> shift;
}
/* Coherent value noise 0..4095 with lattice spacing 2^shift. */
static inline int noise_value(uint32_t seed, Vec q, int shift) {
    int fx, fy, fz, i, corner[8];
    int x = noise_cell(q.x, shift, &fx), y = noise_cell(q.y, shift, &fy),
        z = noise_cell(q.z, shift, &fz);
    for (i = 0; i < 8; i++)
        corner[i] = noise_sample(seed, x + (i & 1), y + (i >> 1 & 1), z + (i >> 2));
    return noise_blend(corner, fx, fy, fz);
}
/* Signed polar ice coverage on a body-fixed Q16 direction. Continental
   uplands carry ice farther equatorward; regional deposits cut bays and
   glacial tongues into that margin. Cartesian fields have no longitude
   seam or singularity at either pole. The poles remain frozen, while the
   equator cannot acquire ice. Positive (including zero) means ice. */
static inline int noise_polar_ice(Vec n, int continent, int region) {
    int latitude = (int)(n.z < 0 ? -n.z : n.z);
    return latitude - 57000 + 3 * (continent - 2048) + (region - 2048);
}
/* Even the strongest continental lobe cannot freeze below this latitude. */
static inline int noise_polar_possible(Vec n) {
    return n.z < -48000 || n.z > 48000;
}
static inline int noise_polar_value(uint32_t seed, Vec n) {
    if (!noise_polar_possible(n)) return -9000;
    return noise_polar_ice(n, noise_value(seed, n, 14),
                           noise_value(seed ^ 0x434f4e54U, n, 13));
}
#endif
