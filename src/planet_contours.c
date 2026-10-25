/* SPDX-License-Identifier: Unlicense; coastline loops. See planet_contours.h. */
#include "planet_contours.h"
#include <string.h>
#define HASH_SIZE 16384
/* Lattice points are integer triples on the cube [-N, N]^3 (step 2), so a
   point on a face edge is the same triple from both faces. */
typedef struct { int x, y, z; } Lattice;
static Lattice lattice(int face, int n, int i, int j) {
    int axis = face / 2, sign = face & 1 ? -n : n, u = -n + 2 * i, v = -n + 2 * j;
    Lattice l;
    if (axis == 0) { l.x = sign; l.y = u; l.z = v; }
    else if (axis == 1) { l.x = u; l.y = sign; l.z = v; }
    else { l.x = u; l.y = v; l.z = sign; }
    return l;
}
static uint32_t lattice_key(Lattice l, int n) {
    uint32_t side = (uint32_t)(2 * n + 1);
    return ((uint32_t)(l.x + n) * side + (uint32_t)(l.y + n)) * side + (uint32_t)(l.z + n);
}
static Vec lattice_dir(Lattice l, int n) {
    (void)n;
    return unit(vec(l.x, l.y, l.z), 65536);
}
static struct {
    uint32_t a, b;          /* lattice keys of the crossed edge, a < b */
    int16_t link[2];
    ContourPoint p;
    uint8_t used;
} cross[CONTOUR_POINTS];
static int16_t table[HASH_SIZE];
static int crossings;
static int cross_find(uint32_t a, uint32_t b, Vec da, Vec db, int ha, int hb) {
    uint32_t h;
    if (a > b) { uint32_t t = a; a = b; b = t; { Vec tv = da; da = db; db = tv; } { int th = ha; ha = hb; hb = th; } }
    h = (a * 2654435761U ^ b * 40503U) & (HASH_SIZE - 1);
    while (table[h] >= 0) {
        if (cross[table[h]].a == a && cross[table[h]].b == b) return table[h];
        h = (h + 1) & (HASH_SIZE - 1);
    }
    if (crossings >= CONTOUR_POINTS) return -1;
    {
        /* The sea-level point on the chord, pushed back to the sphere. */
        int64_t t = (int64_t)ha * 4096 / (ha - hb);
        Vec p = vec(da.x + (db.x - da.x) * t / 4096, da.y + (db.y - da.y) * t / 4096, da.z + (db.z - da.z) * t / 4096);
        p = unit(p, 16384);
        cross[crossings].a = a; cross[crossings].b = b;
        cross[crossings].link[0] = cross[crossings].link[1] = -1;
        cross[crossings].p.x = (int16_t)p.x; cross[crossings].p.y = (int16_t)p.y; cross[crossings].p.z = (int16_t)p.z;
        cross[crossings].used = 0;
        table[h] = (int16_t)crossings;
        return crossings++;
    }
}
static void link(int a, int b) {
    if (a < 0 || b < 0 || a == b) return;
    if (cross[a].link[0] < 0) cross[a].link[0] = (int16_t)b; else cross[a].link[1] = (int16_t)b;
    if (cross[b].link[0] < 0) cross[b].link[0] = (int16_t)a; else cross[b].link[1] = (int16_t)a;
}
/* Highland: the base height where planet_field's land mix turns mostly
   to rock (about 260 m). Albedo: the coarse regional reflectance's mean. */
#define HIGHLAND_HEIGHT 262
static int contour_value(PlanetField *f, Vec n, int kind) {
    if (kind == CONTOUR_ALBEDO) return planet_field_region(f, n) - 2048;
    return planet_field_base_height(f, n) - (kind == CONTOUR_HIGHLAND ? HIGHLAND_HEIGHT : 0);
}
int planet_contours_build(PlanetContours *c, PlanetField *f, int n, int kind) {
    static int value[49 * 49];
    static Vec dir[49 * 49];
    static uint32_t key[49 * 49];
    int face, i, j;
    if (n < 4) n = 4;
    if (n > 48) n = 48;
    c->seed = f->seed; c->resolution = n; c->kind = kind; c->loops = c->points = c->truncated = 0;
    c->positive_corners = 0;
    if (kind != CONTOUR_ALBEDO && f->type != OCEAN) return 0;
    if (f->type == GAS_GIANT) return 0;
    for (i = 0; i < 8; i++) {
        Vec corner = unit(vec(i & 1 ? -n : n, i & 2 ? -n : n, i & 4 ? -n : n), 65536);
        if (contour_value(f, corner, kind) >= 0) c->positive_corners |= 1U << i;
    }
    crossings = 0;
    memset(table, 0xff, sizeof(table));
    for (face = 0; face < 6; face++) {
        for (j = 0; j <= n; j++)
            for (i = 0; i <= n; i++) {
                Lattice l = lattice(face, n, i, j);
                int k = j * (n + 1) + i;
                dir[k] = lattice_dir(l, n);
                key[k] = lattice_key(l, n);
                value[k] = contour_value(f, dir[k], kind);
                if (!value[k]) value[k] = 1; /* keep crossings off lattice points */
            }
        for (j = 0; j < n; j++)
            for (i = 0; i < n; i++) {
                int corner[4], edge[4], mask = 0, e;
                /* Corners anticlockwise from (i, j); edges between them. */
                corner[0] = j * (n + 1) + i; corner[1] = corner[0] + 1;
                corner[2] = corner[1] + n + 1; corner[3] = corner[0] + n + 1;
                for (e = 0; e < 4; e++) if (value[corner[e]] >= 0) mask |= 1 << e;
                if (!mask || mask == 15) continue;
                for (e = 0; e < 4; e++) {
                    int a = corner[e], b = corner[(e + 1) & 3];
                    edge[e] = (value[a] >= 0) != (value[b] >= 0)
                        ? cross_find(key[a], key[b], dir[a], dir[b], value[a], value[b]) : -1;
                }
                if (mask == 5 || mask == 10) {
                    /* Saddle: the cell centre decides which corners join. */
                    int centre = value[corner[0]] + value[corner[1]] + value[corner[2]] + value[corner[3]];
                    int joined = (centre >= 0) == (mask == 5);
                    if (joined) { link(edge[0], edge[1]); link(edge[2], edge[3]); }
                    else { link(edge[3], edge[0]); link(edge[1], edge[2]); }
                } else {
                    int ends[2], m = 0;
                    for (e = 0; e < 4 && m < 2; e++) if (edge[e] >= 0) ends[m++] = edge[e];
                    if (m == 2) link(ends[0], ends[1]);
                }
            }
    }
    /* Walk each loop from an unused crossing. */
    for (i = 0; i < crossings; i++) {
        int previous = -1, current = i, start = c->points;
        if (cross[i].used) continue;
        if (c->loops >= CONTOUR_LOOPS) { c->truncated = 1; break; }
        while (current >= 0 && !cross[current].used) {
            int next;
            if (c->points >= CONTOUR_POINTS) { c->truncated = 1; break; }
            cross[current].used = 1;
            c->point[c->points++] = cross[current].p;
            next = cross[current].link[0] != previous ? cross[current].link[0] : cross[current].link[1];
            previous = current; current = next;
        }
        if (c->points - start >= 3) {
            ContourLoop *l = &c->loop[c->loops];
            int64_t sx = 0, sy = 0, sz = 0;
            int k, cosr = 16384;
            Vec centre;
            for (k = start; k < c->points; k++) { sx += c->point[k].x; sy += c->point[k].y; sz += c->point[k].z; }
            centre = unit(vec(sx, sy, sz), 16384);
            if (!centre.x && !centre.y && !centre.z) centre = vec(0, 0, 16384);
            for (k = start; k < c->points; k++) {
                int d = (int)((centre.x * c->point[k].x + centre.y * c->point[k].y + centre.z * c->point[k].z) >> 14);
                if (d < cosr) cosr = d;
            }
            l->first = (uint16_t)start;
            l->count = (uint16_t)(c->points - start);
            l->cx = (int16_t)centre.x; l->cy = (int16_t)centre.y; l->cz = (int16_t)centre.z;
            /* Chords between points bulge outwards by under a cell. */
            l->cosr = (int16_t)(cosr - 256 < -16384 ? -16384 : cosr - 256);
            c->loops++;
        } else c->points = start;
    }
    return c->loops;
}
int planet_contours_positive(const PlanetContours *c, ContourPoint q) {
    int corner = (q.x < 0) | (q.y < 0 ? 2 : 0) | (q.z < 0 ? 4 : 0);
    int sx = q.x < 0 ? -1 : 1, sy = q.y < 0 ? -1 : 1, sz = q.z < 0 ? -1 : 1;
    int32_t rx = sy * q.z - sz * q.y, ry = sz * q.x - sx * q.z, rz = sx * q.y - sy * q.x;
    int positive = (c->positive_corners >> corner) & 1, l, i;
    /* Follow the short great-circle arc from a lattice corner in q's
       octant to q. The corner's field sign is exact at every resolution;
       crossing any extracted edge changes it. No projection or division
       is needed, and the arc is always shorter than a quarter circle. */
    for (l = 0; l < c->loops; l++) {
        const ContourLoop *loop = &c->loop[l];
        const ContourPoint *pt = c->point + loop->first;
        for (i = 0; i < loop->count; i++) {
            const ContourPoint *a = &pt[i], *b = &pt[i + 1 == loop->count ? 0 : i + 1];
            int sa = sx * a->x + sy * a->y + sz * a->z;
            int sb = sx * b->x + sy * b->y + sz * b->z;
            int32_t da, db, ex, ey, ez;
            int64_t start, end;
            if (sa <= 0 && sb <= 0) continue;
            /* Q14 normals keep these three-product sums within int32. */
            da = rx * a->x + ry * a->y + rz * a->z;
            db = rx * b->x + ry * b->y + rz * b->z;
            if ((da > 0) == (db > 0)) continue;
            ex = (int32_t)a->y * b->z - (int32_t)a->z * b->y;
            ey = (int32_t)a->z * b->x - (int32_t)a->x * b->z;
            ez = (int32_t)a->x * b->y - (int32_t)a->y * b->x;
            start = (int64_t)ex * sx + (int64_t)ey * sy + (int64_t)ez * sz;
            end = (int64_t)ex * q.x + (int64_t)ey * q.y + (int64_t)ez * q.z;
            if ((start > 0) == (end > 0)) continue;
            /* Great circles also intersect on the opposite hemisphere.
               Only the intersection facing our reference corner counts. */
            if (da < 0) da = -da;
            if (db < 0) db = -db;
            if ((int64_t)db * sa + (int64_t)da * sb > 0) positive ^= 1;
        }
    }
    return positive;
}
