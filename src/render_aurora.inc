/* SPDX-License-Identifier: Unlicense
   Shared, body-fixed magnetic ovals. Curtains rise radially through the upper
   atmosphere, so the same folds are seen from the ground and from orbit.
   Ordered transparency uses existing ECS/AGA colours, without palette changes,
   allocation, simulation state, or the world's geometry budget. */
#include "aurora_tables.h"

static int aurora_visible(const Planet *p, int64_t distance) {
    return p->atmosphere > 0 && p->climate != VACUUM && p->radius > 0 &&
           distance < (int64_t)p->radius * frame->w / 6;
}
/* Classic has a painter's list rather than scene depth. Reject curtain
   fragments behind their own globe analytically, including the far oval. */
static int aurora_behind_globe(const Disc *s, int x, int y, uint32_t depth) {
    int64_t u = x - s->view.proj.cx, v = s->view.proj.cy - y, f = s->view.proj.focal;
    int64_t b = (s->ch[0] * u + s->ch[1] * v + s->ch[2] * f) >> 14;
    int64_t discriminant = b * b - s->kappa * (u * u + v * v + f * f);
    uint64_t inverse;
    if (b <= 0 || discriminant < 0) return 0;
    inverse = (uint64_t)(b + disc_sqrt((uint64_t)discriminant)) * s->depth_m;
    inverse = s->depth_s >= 0 ? (s->depth_s < 64 ? inverse >> s->depth_s : 0)
                             : (-s->depth_s < 8 && !(inverse >> (63 + s->depth_s)) ? inverse << -s->depth_s : 0xFFFFFFFFU);
    return inverse > depth;
}
static void aurora_raster(const Tri *t, int density, const Disc *blocker, int painter) {
    Edge edges[3];
    Plane plane;
    int i, y, top = t->y[0], bottom = top;
    int64_t ax = t->x[1] - t->x[0], ay = t->y[1] - t->y[0];
    int64_t bx = t->x[2] - t->x[0], by = t->y[2] - t->y[0];
    int64_t area = ax * by - bx * ay;
    int64_t za = (int64_t)t->inverse_z[1] - t->inverse_z[0];
    int64_t zb = (int64_t)t->inverse_z[2] - t->inverse_z[0];
    if (!area) return;
    plane_setup(&plane, za * by - zb * ay, ax * zb - bx * za, area);
    for (i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        if (t->y[i] < top) top = t->y[i];
        if (t->y[i] > bottom) bottom = t->y[i];
        edge_setup(&edges[i], t->x[i], t->y[i], t->x[j], t->y[j]);
    }
    if (top < view_top) top = view_top;
    if (bottom > view_h) bottom = view_h;
    for (y = top; y < bottom; y++) {
        int xs[3], n = 0, x, left, right;
        for (i = 0; i < 3; i++)
            if (edges[i].lo <= y && y < edges[i].hi) xs[n++] = edge_x(&edges[i], y, 1);
        if (n < 2) continue;
        left = xs[0] < xs[1] ? xs[0] : xs[1];
        right = xs[0] > xs[1] ? xs[0] : xs[1];
        if (left < view_left) left = view_left;
        if (right >= frame->w - view_right_inset) right = frame->w - view_right_inset - 1;
        for (x = left; x <= right; x++) {
            int64_t z;
            size_t at;
            if (bayer4[(y & 3) * 4 + (x & 3)] >= density) continue;
            z = (int64_t)t->inverse_z[0] + plane_at(&plane, x - t->x[0], y - t->y[0]);
            if (z <= 0 || z > UINT32_MAX) continue;
            at = (size_t)y * frame->w + x;
            if (painter ? (blocker && aurora_behind_globe(blocker, x, y, (uint32_t)z))
                        : (uint32_t)z < depth_buffer[at]) continue;
            frame->pixels[at] = t->color;
        }
    }
}
/* One pose is 4 KiB. Camera motion, radius, light and centre do not change
   its directions. Cache keys cover every input, including legacy zero spin.
   Lengths are filled only when a visible curtain first uses that vertex. */
typedef struct { int16_t x, y, z; uint16_t length; } AuroraDirection;
static struct {
    AuroraDirection direction[2][256];
    int phase, tilt, valid;
    int32_t spin_sin, spin_cos;
} aurora_pose;
static AuroraDirection (*aurora_profile(const Planet *p, int phase))[256] {
    int tilt = (int)(p->seed % 12), pole, i;
    int32_t sn = aurora_tilt[tilt][0], cs = aurora_tilt[tilt][1];
    if (aurora_pose.valid && aurora_pose.phase == phase && aurora_pose.tilt == tilt &&
        aurora_pose.spin_sin == p->spin_sin && aurora_pose.spin_cos == p->spin_cos)
        return aurora_pose.direction;
    for (pole = 0; pole < 2; pole++) for (i = 0; i < 256; i++) {
        int angle = i * 4;
        int latitude = aurora_wave[(angle * 3 + phase) & 1023].latitude5 +
                       aurora_wave[(angle * 7 - phase * 2) & 1023].latitude2 + 7;
        int32_t x = (int32_t)aurora_circle[i][1] * aurora_latitude[latitude][1] / 16384;
        int32_t y = (int32_t)aurora_circle[i][0] * aurora_latitude[latitude][1] / 16384;
        int32_t z = (pole ? 1 : -1) * aurora_latitude[latitude][0];
        Vec n = body_rotate(p, vec((x * cs + z * sn) / 16384, y, (z * cs - x * sn) / 16384), 0);
        AuroraDirection *d = &aurora_pose.direction[pole][i];
        d->x = (int16_t)n.x; d->y = (int16_t)n.y; d->z = (int16_t)n.z; d->length = 0;
    }
    aurora_pose.phase = phase; aurora_pose.tilt = tilt;
    aurora_pose.spin_sin = p->spin_sin; aurora_pose.spin_cos = p->spin_cos; aurora_pose.valid = 1;
    return aurora_pose.direction;
}
/* Preserve radial()'s exact two-stage normalization. The first square root
   is looked up once per direction; adjacent height bands share endpoints. */
static Vec aurora_radial(AuroraDirection *d, int64_t radius) {
    Vec n = vec(d->x, d->y, d->z), p;
    int64_t l;
    if (!d->length) d->length = (uint16_t)length(n);
    p = d->length ? scale(n, radius, d->length) : vec(0, 0, 0);
    l = length(p);
    return l ? scale(p, radius, l) : p;
}
static void aurora_quad(Vec a, Vec b, Vec c, Vec d, int colour, int density,
                        const Disc *blocker, int painter) {
    int i;
    nt = 0;
    triangle(a, b, c, colour); triangle(a, c, d, colour);
    for (i = 0; i < nt; i++) aurora_raster(&triangles[i], density, blocker, painter);
    nt = 0;
}
static void aurora_body(const Planet *p, int painter) {
    static Tri scratch[16];
    Tri *saved_triangles = triangles;
    int saved_limit = triangle_limit;
    Disc globe;
    AuroraDirection (*profile)[256];
    const Disc *blocker = NULL;
    const Planet *saved_surface = terrain_surface;
    Vec light[DISC_STARS], eye = sub(scale(camera_position, 1, FP), p->center);
    int64_t distance = length(eye), base, height;
    int pole, angle, layer, lights = 0, i, saved_nt = nt;
    int phase = (int)((game->tick / 4 + (p->seed & 1023)) & 1023);
    if (!graphics.aurorae || !aurora_visible(p, distance)) return;
    /* Small worlds retain a proportional shell; Earth-sized bodies emit at
       90--270 km, above weather and terrain. Giants have taller curtains. */
    base = p->radius / 70;
    if (base < 1000) base = 1000;
    if (base > 180000) base = 180000;
    height = base * 2;
    if (painter && disc_setup(&globe, p, 0)) blocker = &globe;
    for (i = 0; i < game->system.stars && lights < DISC_STARS; i++)
        if (game->system.star[i].type != ST_BLACK_HOLE)
            light[lights++] = unit(sub(game->system.star[i].center, p->center), 16384);
    terrain_surface = NULL;
    triangles = scratch; triangle_limit = 16;
    profile = aurora_profile(p, phase);
    for (pole = 0; pole < 2; pole++) for (angle = 0; angle < 1024; angle += 4) {
        AuroraDirection *n0 = &profile[pole][angle / 4];
        AuroraDirection *n1 = &profile[pole][((angle / 4) + 1) & 255];
        Vec normal = vec(n0->x, n0->y, n0->z), upper0 = {0}, upper1 = {0};
        int have_upper = 0;
        int elevation = -16384, fade, strength;
        int fold = 192 + aurora_wave[(angle * 11 + phase * 3) & 1023].fold48 +
                         aurora_wave[(angle * 23 - phase) & 1023].fold24;
        int64_t tall = height * (192 + aurora_wave[(angle * 5 - phase) & 1023].fold48) / 256;
        for (i = 0; i < lights; i++) {
            int e = (int)(dot(normal, light[i]) / 16384);
            if (e > elevation) elevation = e;
        }
        fade = elevation <= -2800 ? 16 : elevation >= 2200 ? 0 : aurora_fade[2200 - elevation];
        if (!fade) continue;
        strength = aurora_strength[fade][fold - 120];
        for (layer = 2; layer >= 0; layer--) {
            static const int levels[] = {0, 32, 112, 256};
            static const uint8_t ecs[] = {11, 13, 27}, aga[] = {11, 13, 186};
            int density = aurora_density[layer][strength];
            Vec a, b, c, d;
            if (density < 1) continue;
            a = add(p->center, aurora_radial(n0, p->radius + base + tall * levels[layer] / 256));
            b = add(p->center, aurora_radial(n1, p->radius + base + tall * levels[layer] / 256));
            if (!have_upper) {
                upper0 = add(p->center, aurora_radial(n0, p->radius + base + tall * levels[layer + 1] / 256));
                upper1 = add(p->center, aurora_radial(n1, p->radius + base + tall * levels[layer + 1] / 256));
                have_upper = 1;
            }
            c = upper1; d = upper0;
            aurora_quad(a, b, c, d, frame->enhanced ? aga[layer] : ecs[layer], density, blocker, painter);
            upper0 = a; upper1 = b;
        }
    }
    terrain_surface = saved_surface;
    nt = saved_nt; triangles = saved_triangles; triangle_limit = saved_limit;
}
