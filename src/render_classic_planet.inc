/* SPDX-License-Identifier: Unlicense
   Classic planets, after Frontier: Elite II's RenderPlanet. The outline is
   planet_view's exact span per row. Surface features are closed loops on
   the sphere: coastlines, highland and dark-region outlines from
   planet_contours, ice caps, gas-giant bands, and five nested light caps
   around the sub-solar point whose count is the shade (the terminator and
   its twilight bands). Each loop's visible edges become body-span
   crossings carrying one mask bit; a pixel's colour comes from the XOR of
   the bits of every feature containing it.

   A feature that crosses the limb is closed along the limb itself. Where
   that closing path runs up the left limb it crosses the rows to its right,
   so those rows start with the feature's bit toggled (Frontier's
   BODY_TOGGLE_COLOUR). Paths up the right limb cross nothing, because rows
   end there. The starting mask at the disc's bottom-most limb point is
   evaluated directly. */
enum {
    CLB_LAND = 1, CLB_ROCK = 2, CLB_ICE = 4, CLB_DARK = 8,
    CLB_LIGHT = 16,  /* five bits, 16..256 */
    CLB_BAND = 512   /* seven bits, 512..32768 */
};
#define CL_LIGHT_CAPS 5
#define CL_GAS_BANDS 7
#define CL_BODIES 16
/* The light caps' angular radii from the sub-solar point, as cosines
   (Q14): -0.12 (twilight, 97 degrees), then 0.1, 0.3, 0.55 and 0.8. Each
   band takes the mean Lambert shade across it, so the stepped terminator
   averages to the Enhanced renderer's smooth one. */
static const int16_t cl_light_cos[CL_LIGHT_CAPS] = {-1997, 1638, 4915, 9011, 13107};
static const uint8_t cl_light_aga[CL_LIGHT_CAPS + 1] = {0, 1, 3, 6, 9, 13};
static const uint8_t cl_light_ecs[CL_LIGHT_CAPS + 1] = {0, 0, 0, 1, 1, 2};
typedef struct {
    int body;               /* planet index, or -1 - star */
    PlanetView view;
    int64_t c[3], radius;   /* camera-space centre and radius, unit as below */
    int64_t q4;             /* units per Q4 metre: 1 (Q4) or 16 (metres) */
    int32_t cp[3];          /* centre in planet units: radius = 16384 */
    int16_t ch[3], rd;      /* unit centre direction; R/D (Q14) */
    int16_t m[3][3];        /* body-fixed to camera, Q14 */
    int16_t light[3];       /* camera-space direction to the star, Q14 */
    int material, type;     /* base material; planet type */
    int32_t bx, by, tx, ty; /* bottom and top limb points, Q4 screen */
    unsigned bottom_mask;
} ClBodyInfo;
static ClBodyInfo cl_body_info[CL_BODIES];
static int cl_body_count;
static ClBodyRow cl_body_rows[1024];
static uint16_t cl_body_toggles[1025];
static ClBody cl_body;
static int16_t cl_first[1024], cl_last[1024];
/* Classic coastline caches: coast, highland and dark-region loops for the
   two most recently drawn worlds. */
static struct { PlanetContours coast, highland, dark; uint32_t key; unsigned used; } cl_contours[2];
static unsigned cl_contour_clock;
static int cl_contour_slot(const Planet *p) {
    uint32_t key = p->seed ^ (uint32_t)p->type * 0x9e3779b9U ^ (uint32_t)p->radius;
    int i, oldest = 0, n = graphics.planets >= 2 ? 32 : graphics.planets ? 24 : 16;
    for (i = 0; i < 2; i++)
        if (cl_contours[i].used && cl_contours[i].key == key && cl_contours[i].coast.resolution == n) {
            cl_contours[i].used = ++cl_contour_clock;
            return i;
        }
    if (cl_contours[1].used < cl_contours[0].used) oldest = 1;
    {
        PlanetField *f = planet_field_for(p);
        planet_contours_build(&cl_contours[oldest].coast, f, n, CONTOUR_COAST);
        planet_contours_build(&cl_contours[oldest].highland, f, n, CONTOUR_HIGHLAND);
        planet_contours_build(&cl_contours[oldest].dark, f, n * 2 / 3, CONTOUR_ALBEDO);
    }
    cl_contours[oldest].key = key; cl_contours[oldest].used = ++cl_contour_clock;
    return oldest;
}
static int cl_dot14(const int16_t *a, const int16_t *b) {
    return ((int32_t)a[0] * b[0] + (int32_t)a[1] * b[1] + (int32_t)a[2] * b[2]) >> 14;
}
/* A camera-space unit normal (Q14) on this sphere to Q4 screen. */
static int cl_body_project(const ClBodyInfo *b, const int16_t *m, int32_t *x, int32_t *y) {
    ClV v;
    v.x = b->cp[0] + m[0]; v.y = b->cp[1] + m[1]; v.z = b->cp[2] + m[2];
    if (v.z < 64) return 0;
    return cl_project(&v, x, y);
}
static void cl_normalise14(int32_t x, int32_t y, int32_t z, int16_t *out) {
    uint32_t len;
    while (x > 30000 || x < -30000 || y > 30000 || y < -30000 || z > 30000 || z < -30000) { x /= 2; y /= 2; z /= 2; }
    len = cl_isqrt((uint32_t)(x * x + y * y + z * z));
    if (!len) { out[0] = out[1] = 0; out[2] = 16384; return; }
    out[0] = (int16_t)(x * 16384 / (int32_t)len); out[1] = (int16_t)(y * 16384 / (int32_t)len);
    out[2] = (int16_t)(z * 16384 / (int32_t)len);
}
/* Is a camera-space normal on the visible cap: m . (-c) > R/D? */
static int cl_body_visible(const ClBodyInfo *b, const int16_t *m) {
    return -cl_dot14(m, b->ch) > b->rd;
}
/* Left or right of the line from the bottom to the top limb point. */
static int cl_left_limb(const ClBodyInfo *b, int32_t x, int32_t y) {
    /* Screen y grows downwards: points left of the upward chord B->T give
       a negative cross product. */
    int64_t side = (int64_t)(b->tx - b->bx) * (y - b->by) - (int64_t)(b->ty - b->by) * (x - b->bx);
    return side < 0;
}
/* A visible edge of a feature, split while it is long on screen so the
   great-circle arc stays round. */
static void cl_body_arc(const ClBodyInfo *b, const int16_t *ma, const int16_t *mb, int32_t ax, int32_t ay,
                        int32_t bx, int32_t by, unsigned mask, int depth) {
    if (depth < 4 && labs(bx - ax) + labs(by - ay) > 24 * 16) {
        int16_t mid[3];
        int32_t mx, my;
        cl_normalise14(ma[0] + mb[0], ma[1] + mb[1], ma[2] + mb[2], mid);
        if (cl_body_project(b, mid, &mx, &my)) {
            cl_body_arc(b, ma, mid, ax, ay, mx, my, mask, depth + 1);
            cl_body_arc(b, mid, mb, mx, my, bx, by, mask, depth + 1);
            return;
        }
    }
    cl_body_edge(&cl_body, ax, ay, bx, by, (uint16_t)mask);
}
/* The limb point between a visible and a hidden normal, on the chord where
   the visibility margin crosses zero, pushed back onto the sphere. */
static void cl_body_limb(const ClBodyInfo *b, const int16_t *seen, const int16_t *hidden, int16_t *out) {
    int32_t sa = -cl_dot14(seen, b->ch) - b->rd, sb = -cl_dot14(hidden, b->ch) - b->rd;
    int32_t t = sa - sb ? (int32_t)((int64_t)sa * 4096 / (sa - sb)) : 2048;
    cl_normalise14(seen[0] + (hidden[0] - seen[0]) * t / 4096, seen[1] + (hidden[1] - seen[1]) * t / 4096,
                   seen[2] + (hidden[2] - seen[2]) * t / 4096, out);
}
/* One closed loop of camera-space normals. */
static void cl_body_loop(const ClBodyInfo *b, const int16_t (*m)[3], int count, unsigned mask) {
    int i, j;
    int32_t px[2], py[2];
    int vis[2];
    if (count < 3) return;
    vis[0] = cl_body_visible(b, m[0]) && cl_body_project(b, m[0], &px[0], &py[0]);
    for (i = 0; i < count; i++) {
        const int16_t *a = m[i], *c = m[(i + 1) % count];
        j = (i + 1) % count;
        vis[1] = cl_body_visible(b, c) && cl_body_project(b, c, &px[1], &py[1]);
        if (vis[0] && vis[1]) cl_body_arc(b, a, c, px[0], py[0], px[1], py[1], mask, 0);
        else if (vis[0] != vis[1]) {
            int16_t limb[3];
            int32_t lx, ly;
            const int16_t *seen = vis[0] ? a : c;
            cl_body_limb(b, seen, vis[0] ? c : a, limb);
            if (cl_body_project(b, limb, &lx, &ly)) {
                if (vis[0]) cl_body_arc(b, a, limb, px[0], py[0], lx, ly, mask, 0);
                else cl_body_arc(b, limb, c, lx, ly, px[1], py[1], mask, 0);
                /* Rows above the limb point cross the closing path. */
                if (cl_left_limb(b, lx, ly)) cl_body_toggle(&cl_body, ly >= 0 ? (ly + 15) >> 4 : 0, (uint16_t)mask);

            }
        }
        (void)j;
        px[0] = px[1]; py[0] = py[1]; vis[0] = vis[1];
    }
}
/* A small circle on the sphere: axis a (camera space, Q14), angular
   radius given by its cosine; generated as a loop of normals. */
static void cl_body_circle(const ClBodyInfo *b, const int16_t *a, int cosr, unsigned mask, int segments) {
    static int16_t m[64][3];
    int16_t u[3], w[3];
    int32_t sinr = (int32_t)cl_isqrt((uint32_t)(268435456 - cosr * cosr));
    int i;
    /* u, w: an orthonormal pair perpendicular to the axis. */
    if (a[0] > 8000 || a[0] < -8000) cl_normalise14(-a[1], a[0], 0, u);
    else cl_normalise14(0, -a[2], a[1], u);
    cl_normalise14(((int32_t)a[1] * u[2] - (int32_t)a[2] * u[1]) >> 14, ((int32_t)a[2] * u[0] - (int32_t)a[0] * u[2]) >> 14,
                   ((int32_t)a[0] * u[1] - (int32_t)a[1] * u[0]) >> 14, w);
    if (segments > 64) segments = 64;
    for (i = 0; i < segments; i++) {
        int ang = i * 1024 / segments, k;
        int32_t cs = cosi(ang), sn = sini(ang);
        for (k = 0; k < 3; k++)
            m[i][k] = (int16_t)(((int32_t)a[k] * cosr + ((cs * u[k] + sn * w[k]) >> 14) * sinr) >> 14);
    }
    cl_body_loop(b, (const int16_t (*)[3])m, segments, mask);
}
/* Body-fixed Q14 direction to camera space. */
static void cl_body_turn(const ClBodyInfo *b, const ContourPoint *p, int16_t *m) {
    int k;
    for (k = 0; k < 3; k++)
        m[k] = (int16_t)(((int32_t)b->m[k][0] * p->x + (int32_t)b->m[k][1] * p->y + (int32_t)b->m[k][2] * p->z) >> 14);
}
/* Contour loops, Frontier's surface polygons. A loop whose bounding cap
   misses the visible cap has no visible edge and is skipped whole. Within
   a loop, visibility is a three-multiply dot with the view direction
   turned into the body frame; only points on a visible edge are turned
   into camera space (nine multiplies) and projected. */
static void cl_contour_loop(const ClBodyInfo *b, const ContourPoint *pt, int count, const int16_t *view_body,
                            unsigned mask) {
    static int16_t m[CONTOUR_POINTS][3];
    static int32_t px[CONTOUR_POINTS], py[CONTOUR_POINTS];
    /* Per point: bit 0 on the visible cap, bit 1 turned, bit 2 projected
       (seen: on the cap and projectable), bit 3 projection tried. */
    static uint8_t state[CONTOUR_POINTS];
    int i, any = 0;
    for (i = 0; i < count; i++) {
        int32_t margin = -(((int32_t)pt[i].x * view_body[0] + (int32_t)pt[i].y * view_body[1] + (int32_t)pt[i].z * view_body[2]) >> 14);
        state[i] = (uint8_t)(margin > b->rd);
        any |= margin > b->rd - 2048;
    }
    if (!any) return;
    for (i = 0; i < count; i++) {
        int j = i + 1 == count ? 0 : i + 1, k, seen[2];
        if (!((state[i] | state[j]) & 1)) continue;
        for (k = 0; k < 2; k++) {
            int q = k ? j : i;
            if (!(state[q] & 2)) { cl_body_turn(b, &pt[q], m[q]); state[q] |= 2; }
            if ((state[q] & 9) == 1) state[q] |= (uint8_t)(8 | (cl_body_project(b, m[q], &px[q], &py[q]) ? 4 : 0));
            seen[k] = (state[q] & 4) != 0;
        }
        if (seen[0] && seen[1]) cl_body_arc(b, m[i], m[j], px[i], py[i], px[j], py[j], mask, 0);
        else if (seen[0] != seen[1]) {
            int16_t limb[3];
            int32_t lx, ly;
            int a = seen[0] ? i : j, h = seen[0] ? j : i;
            cl_body_limb(b, m[a], m[h], limb);
            if (cl_body_project(b, limb, &lx, &ly)) {
                if (seen[0]) cl_body_arc(b, m[i], limb, px[i], py[i], lx, ly, mask, 0);
                else cl_body_arc(b, limb, m[j], lx, ly, px[j], py[j], mask, 0);
                /* Rows above the limb point cross the closing path. */
                if (cl_left_limb(b, lx, ly)) cl_body_toggle(&cl_body, ly >= 0 ? (ly + 15) >> 4 : 0, (uint16_t)mask);
            }
        }
    }
}
static void cl_body_contours(const ClBodyInfo *b, const PlanetContours *c, unsigned mask) {
    int l, k;
    int32_t sa = (int32_t)cl_isqrt((uint32_t)(268435456 - (int32_t)b->rd * b->rd));
    int16_t view_body[3];
    /* The view direction in the body frame: the transpose applied to ch. */
    for (k = 0; k < 3; k++)
        view_body[k] = (int16_t)(((int32_t)b->m[0][k] * b->ch[0] + (int32_t)b->m[1][k] * b->ch[1] + (int32_t)b->m[2][k] * b->ch[2]) >> 14);
    for (l = 0; l < c->loops; l++) {
        const ContourLoop *loop = &c->loop[l];
        if (loop->cosr >= 0) {
            /* Caps meet when the angle between their centres is below the
               sum of their radii: cos(a + b) = ca cb - sa sb. */
            int32_t sb = (int32_t)cl_isqrt((uint32_t)(268435456 - (int32_t)loop->cosr * loop->cosr));
            int32_t reach = ((int32_t)b->rd * loop->cosr - sa * sb) >> 14;
            int32_t centre = ((int32_t)loop->cx * view_body[0] + (int32_t)loop->cy * view_body[1] + (int32_t)loop->cz * view_body[2]) >> 14;
            if (-centre <= reach - 2048) continue;
        }
        cl_contour_loop(b, &c->point[loop->first], loop->count, view_body, mask);
    }
}
/* Feature mask at a camera-space normal: land, rock and dark regions from
   the planet field; ice and bands from latitude; light from the star. */
static unsigned cl_body_mask(const ClBodyInfo *b, const int16_t *m) {
    const Planet *p = &game->system.planets[b->body];
    int32_t n[3];
    unsigned mask = 0;
    int k;
    for (k = 0; k < 3; k++) /* camera to body: the transpose */
        n[k] = ((int32_t)b->m[0][k] * m[0] + (int32_t)b->m[1][k] * m[1] + (int32_t)b->m[2][k] * m[2]) >> 14;
    for (k = 0; k < CL_LIGHT_CAPS; k++) if (cl_dot14(m, b->light) >= cl_light_cos[k]) mask |= CLB_LIGHT << k;
    if (p->type == GAS_GIANT) {
        for (k = 0; k < CL_GAS_BANDS; k++) if (n[2] > -14336 + k * 4096) mask |= CLB_BAND << k;
        return mask;
    }
    if (p->type != ICE && (n[2] > 14000 || n[2] < -14000)) mask |= CLB_ICE;
    {
        PlanetField *f = planet_field_for(p);
        Vec q = vec(n[0] * 4, n[1] * 4, n[2] * 4);
        if (p->type == OCEAN) {
            int h = planet_field_base_height(f, q);
            if (h >= 0) mask |= CLB_LAND;
            if (h >= 262) mask |= CLB_ROCK;
        }
        if (planet_field_region(f, q) < 2048) mask |= CLB_DARK;
    }
    return mask;
}
static const uint8_t cl_aga_base[] = {80, 64, 64, 96, 112, 96};
static const uint8_t cl_ecs_base[] = {25, 8, 8, 16, 28, 21};
static int cl_body_colour(void *ctx, unsigned mask) {
    const ClBodyInfo *b = ctx;
    int level = 0, k, material, shade;
    for (k = 0; k < CL_LIGHT_CAPS; k++) level += (mask >> (4 + k)) & 1;
    shade = frame->enhanced ? cl_light_aga[level] : cl_light_ecs[level];
    if (b->type == GAS_GIANT) {
        int band = 0;
        for (k = 0; k < CL_GAS_BANDS; k++) band += (mask >> (9 + k)) & 1;
        /* Alternate pale and dark belts, like material_color's bands. */
        shade = shade * (band & 1 ? 11 : 15) / 15 - (band == 3 && shade > 2 ? 1 : 0);
        material = 5;
    } else if (mask & CLB_ICE) material = 4;
    else if (b->type == OCEAN) material = mask & CLB_LAND ? (mask & CLB_ROCK ? 3 : 1) : 0;
    else material = b->material;
    if ((mask & CLB_DARK) && material != 0 && shade > 1) shade -= frame->enhanced ? 2 : 0;
    return frame->enhanced ? cl_aga_base[material] + shade : cl_ecs_base[material] + shade;
}
/* Find the limb points with the largest and smallest screen y: sampled
   round the limb circle, then refined. */
static void cl_body_extremes(ClBodyInfo *b) {
    int16_t u[3], w[3], m[3];
    int32_t sinr = (int32_t)cl_isqrt((uint32_t)(268435456 - (int32_t)b->rd * b->rd)), x, y;
    int pass, i, best[2] = {0, 0}, step = 64;
    int32_t extreme[2] = {INT32_MIN, INT32_MAX};
    if (b->ch[0] > 8000 || b->ch[0] < -8000) cl_normalise14(-b->ch[1], b->ch[0], 0, u);
    else cl_normalise14(0, -b->ch[2], b->ch[1], u);
    cl_normalise14(((int32_t)b->ch[1] * u[2] - (int32_t)b->ch[2] * u[1]) >> 14,
                   ((int32_t)b->ch[2] * u[0] - (int32_t)b->ch[0] * u[2]) >> 14,
                   ((int32_t)b->ch[0] * u[1] - (int32_t)b->ch[1] * u[0]) >> 14, w);
    b->bx = b->tx = (int32_t)b->view.centre_x / 16; b->by = b->ty = (int32_t)b->view.centre_y / 16;
    for (pass = 0; pass < 3; pass++) {
        int from[2], k;
        for (k = 0; k < 2; k++) from[k] = pass ? best[k] - step * 2 : 0;
        for (k = 0; k < 2; k++)
            for (i = 0; i <= (pass ? 4 : 16); i++) {
                int ang = (from[k] + i * (pass ? step : 64)) & 1023, j;
                int32_t cs = cosi(ang), sn = sini(ang);
                /* The limb: -c R/D + sqrt(1 - (R/D)^2) (u cos + w sin). */
                for (j = 0; j < 3; j++)
                    m[j] = (int16_t)((-(int32_t)b->ch[j] * b->rd + ((cs * u[j] + sn * w[j]) >> 14) * sinr) >> 14);
                if (!cl_body_project(b, m, &x, &y)) continue;
                if (k == 0 ? y > extreme[0] : y < extreme[1]) {
                    extreme[k] = y; best[k] = ang;
                    if (k == 0) { b->bx = x; b->by = y; } else { b->tx = x; b->ty = y; }
                }
            }
        step /= 4;
        if (step < 1) step = 1;
    }
    for (i = 0; i < 3; i++)
        m[i] = (int16_t)((-(int32_t)b->ch[i] * b->rd + ((cosi(best[0]) * u[i] + sini(best[0]) * w[i]) >> 14) * sinr) >> 14);
    if (b->body >= 0) b->bottom_mask = cl_body_mask(b, m);
}
static const uint8_t cl_star_colour_ecs[] = {15, 15, 31, 31, 12, 12, 14, 14, 31, 23, 31, 1, 14};
/* Set up a planet (body >= 0) or star (-1 - index) for this frame. */
static int cl_body_setup(ClBodyInfo *b, int body) {
    const Planet *p = body >= 0 ? &game->system.planets[body] : NULL;
    const Star *star = body < 0 ? &game->system.star[-1 - body] : NULL;
    Vec centre = p ? p->center : star->center, off, c;
    int64_t r = p ? p->radius : star->radius, d, alt;
    Projection proj;
    int i, j;
    memset(b, 0, sizeof(*b));
    b->body = body;
    off = sub(scale(centre, FP, 1), camera_position);
    if (llabs(off.x) < (1LL << 43) && llabs(off.y) < (1LL << 43) && llabs(off.z) < (1LL << 43)) {
        b->q4 = 1; off = vec(floor_shift(off.x, 4), floor_shift(off.y, 4), floor_shift(off.z, 4)); r *= 16;
    } else {
        b->q4 = 16; off = sub(centre, scale(camera_position, 1, FP));
    }
    d = length(off);
    c = view_rotate(off);
    if (length(c)) c = scale(c, d, length(c));
    alt = d - r;
    proj.cx = cl.cx; proj.cy = cl.cy; proj.focal = cl.focal;
    proj.left = cl.surface.left; proj.top = cl.surface.top; proj.right = cl.surface.right; proj.bottom = cl.surface.bottom;
    if (planet_view(&b->view, &proj, c, r, alt) == PLANET_HIDDEN) return 0;
    b->c[0] = c.x; b->c[1] = c.y; b->c[2] = c.z; b->radius = r;
    if (d / r > 100000) { b->view.kind = PLANET_POINT; }
    for (i = 0; i < 3; i++) b->cp[i] = (int32_t)muldiv(i == 0 ? c.x : i == 1 ? c.y : c.z, 16384, r);
    b->ch[0] = (int16_t)muldiv(c.x, 16384, d); b->ch[1] = (int16_t)muldiv(c.y, 16384, d); b->ch[2] = (int16_t)muldiv(c.z, 16384, d);
    b->rd = (int16_t)muldiv(r, 16384, d);
    if (!p) return 1;
    b->type = (int)p->type;
    b->material = p->type == ICE ? 4 : p->type == GAS_GIANT ? 5 : p->type == OCEAN ? 0 : 3;
    for (j = 0; j < 3; j++) {
        /* Body axis j in the system frame, then in camera space. */
        Vec e = body_rotate(p, vec(j == 0 ? 16384 : 0, j == 1 ? 16384 : 0, j == 2 ? 16384 : 0), 0);
        for (i = 0; i < 3; i++)
            b->m[i][j] = (int16_t)(((int32_t)cl.m[i][0] * e.x + (int32_t)cl.m[i][1] * e.y + (int32_t)cl.m[i][2] * e.z) >> 14);
    }
    if (game->system.stars) {
        Vec l = unit(sub(game->system.star[0].center, p->center), 16384);
        for (i = 0; i < 3; i++)
            b->light[i] = (int16_t)(((int32_t)cl.m[i][0] * l.x + (int32_t)cl.m[i][1] * l.y + (int32_t)cl.m[i][2] * l.z) >> 14);
    }
    return 1;
}
/* Rows of the disc's limb into cl_first/cl_last, in pixels as
   planet_row_pixels rounds them. The silhouette is convex, so the chord
   between two rows' exact spans lies inside it: planet_row (xfloat, about
   a millisecond on a 68030) runs only where bisection finds the chord more
   than three eighths of a pixel off the outline, and the rows between
   follow the chord. A disc needs a few dozen exact rows, not one per row. */
#define CL_ROW_SLACK 96 /* Q8 */
static void cl_row_store(const PlanetView *v, int y, int32_t x0, int32_t x1) {
    int a = x0 >= 0 ? (x0 + 255) >> 8 : -((-x0) >> 8), z = x1 >= 0 ? x1 >> 8 : -((-x1 + 255) >> 8);
    if (a < v->proj.left) a = v->proj.left;
    if (z >= v->proj.right) z = v->proj.right - 1;
    if (a > z) { cl_first[y] = 1; cl_last[y] = 0; }
    else { cl_first[y] = (int16_t)a; cl_last[y] = (int16_t)z; }
}
static int32_t cl_row_lerp(int32_t a, int32_t b, int t, int n) { return a + (int32_t)((int64_t)(b - a) * t / n); }
static void cl_rows_between(const PlanetView *v, int ya, int32_t a0, int32_t a1, int yb, int32_t b0, int32_t b1) {
    int ym = (ya + yb) / 2, y;
    int32_t m0, m1;
    if (yb - ya < 2) return;
    if (!planet_row(v, ym, &m0, &m1)) {
        /* Not convex after all (degenerate view): every row exactly. */
        for (y = ya + 1; y < yb; y++) {
            if (planet_row(v, y, &m0, &m1)) cl_row_store(v, y, m0, m1);
            else { cl_first[y] = 1; cl_last[y] = 0; }
        }
        return;
    }
    cl_row_store(v, ym, m0, m1);
    if (yb - ya <= 32 && labs(m0 - cl_row_lerp(a0, b0, ym - ya, yb - ya)) <= CL_ROW_SLACK &&
        labs(m1 - cl_row_lerp(a1, b1, ym - ya, yb - ya)) <= CL_ROW_SLACK) {
        for (y = ya + 1; y < ym; y++) cl_row_store(v, y, cl_row_lerp(a0, m0, y - ya, ym - ya), cl_row_lerp(a1, m1, y - ya, ym - ya));
        for (y = ym + 1; y < yb; y++) cl_row_store(v, y, cl_row_lerp(m0, b0, y - ym, yb - ym), cl_row_lerp(m1, b1, y - ym, yb - ym));
        return;
    }
    cl_rows_between(v, ya, a0, a1, ym, m0, m1);
    cl_rows_between(v, ym, m0, m1, yb, b0, b1);
}
static void cl_body_rows_of(const PlanetView *v) {
    int y, top = v->top, bottom = v->bottom - 1;
    int32_t t0 = 0, t1 = 0, b0 = 0, b1 = 0;
    for (y = v->top; y < v->bottom; y++) { cl_first[y] = 1; cl_last[y] = 0; }
    while (top <= bottom && !planet_row(v, top, &t0, &t1)) top++;
    while (bottom > top && !planet_row(v, bottom, &b0, &b1)) bottom--;
    if (top > bottom) return;
    cl_row_store(v, top, t0, t1);
    if (bottom == top) return;
    cl_row_store(v, bottom, b0, b1);
    cl_rows_between(v, top, t0, t1, bottom, b0, b1);
}
/* The halo's thickness in pixels where the disc's edge is on screen: both
   outlines are convex and the halo's holds the disc's, so its first row
   above the disc's top (or below its bottom) is a binary search, or, for a
   disc filling the view's height, its span on the middle row. */
static int cl_halo_thickness(const ClBodyInfo *b, const PlanetView *halo) {
    int y, top = -1, bottom = -1, lo, hi;
    int32_t a, z;
    for (y = b->view.top; y < b->view.bottom; y++) if (cl_first[y] <= cl_last[y]) { if (top < 0) top = y; bottom = y; }
    if (top < 0) return 0;
    /* A whole disc on screen: the angular radii's difference, to a pixel. */
    if (top > cl.surface.top && bottom < cl.surface.bottom - 1 && b->view.centre_x > cl.surface.left * 256 &&
        b->view.centre_x < cl.surface.right * 256 && halo->radius_px - b->view.radius_px < 64 * 256)
        return (halo->radius_px - b->view.radius_px + 128) >> 8;
    if (top > cl.surface.top) {
        lo = cl.surface.top; hi = top; /* row hi is in the halo */
        if (planet_row(halo, lo, &a, &z)) return top - lo;
        while (hi - lo > 1) { int mid = (lo + hi) / 2; if (planet_row(halo, mid, &a, &z)) hi = mid; else lo = mid; }
        return top - hi;
    }
    if (bottom < cl.surface.bottom - 1) {
        lo = bottom; hi = cl.surface.bottom - 1; /* row lo is in the halo */
        if (planet_row(halo, hi, &a, &z)) return hi - bottom;
        while (hi - lo > 1) { int mid = (lo + hi) / 2; if (planet_row(halo, mid, &a, &z)) lo = mid; else hi = mid; }
        return lo - bottom;
    }
    {
        int first, last, t = 0;
        y = (top + bottom) / 2;
        if (!planet_row_pixels(halo, y, &first, &last)) return 0;
        if (cl_first[y] > cl.surface.left) t = cl_first[y] - first;
        if (cl_last[y] < cl.surface.right - 1 && last - cl_last[y] > t) t = last - cl_last[y];
        return t;
    }
}
/* The atmosphere: Frontier's halo bands, here a dithered band round the
   limb in the climate's sky colour. The band is the disc's own rows grown
   by the halo's thickness in pixels, which planet_view's extents of the
   larger sphere give once, rather than a second conic per row. */
static void cl_atmosphere(const ClBodyInfo *b, const Planet *p) {
    PlanetView halo;
    Projection proj = b->view.proj;
    int64_t thick = b->radius / 40, r2, d;
    Vec c = vec(b->c[0], b->c[1], b->c[2]);
    int y, colour, t, top, bottom;
    if (p->climate == VACUUM || b->view.radius_px < 4 * 256) return;
    if (p->atmosphere * b->q4 * 16 / 16 > thick) thick = (int64_t)p->atmosphere * 16 / b->q4;
    r2 = b->radius + thick;
    d = length(c);
    if (planet_view(&halo, &proj, c, r2, d - r2) != PLANET_DISC) return;
    t = cl_halo_thickness(b, &halo);
    if (t < 1) t = 1;
    if (t > 48) t = 48;
    colour = frame->enhanced ? sky_tint[p->climate].day + 14 : ecs_glow[sky_tint[p->climate].warm][1];
    top = b->view.top - t; bottom = b->view.bottom + t;
    if (top < cl.surface.top) top = cl.surface.top;
    if (bottom > cl.surface.bottom) bottom = cl.surface.bottom;
    for (y = top; y < bottom; y++) {
        int a = INT32_MAX, z = INT32_MIN, k, x;
        uint8_t *line;
        /* The rows within t of this one, widened by t. */
        for (k = y - t; k <= y + t; k++) {
            if (k < b->view.top || k >= b->view.bottom || cl_first[k] > cl_last[k]) continue;
            if (cl_first[k] < a) a = cl_first[k];
            if (cl_last[k] > z) z = cl_last[k];
        }
        if (a > z) continue;
        a -= t; z += t;
        if (a < cl.surface.left) a = cl.surface.left;
        if (z >= cl.surface.right) z = cl.surface.right - 1;
        line = frame->pixels + (size_t)y * frame->w;
        for (x = a; x <= z; x++) {
            if (y >= b->view.top && y < b->view.bottom && x >= cl_first[y] && x <= cl_last[y]) { x = cl_last[y]; continue; }
            if (bayer4[(y & 3) * 4 + (x & 3)] < 10) line[x] = (uint8_t)colour;
        }
    }
}
static void cl_draw_planet(ClBodyInfo *b) {
    const Planet *p = &game->system.planets[b->body];
    int k, light_axis[3], segments;
    if (b->view.kind == PLANET_POINT) {
        int x = b->view.centre_x >> 8, y = b->view.centre_y >> 8;
        cl_pixel(&cl.surface, x, y, cl_body_colour(b, CLB_LIGHT * 7));
        return;
    }
    cl_body_rows_of(&b->view);
    cl_atmosphere(b, p);
    cl_body_extremes(b);
    cl_body_begin(&cl_body, b->view.top, b->view.bottom);
    /* Light caps round the sub-solar point. */
    for (k = 0; k < 3; k++) light_axis[k] = b->light[k];
    /* Circles as round as the disc needs: a segment every few pixels. */
    segments = (int)(b->view.radius_px >> 8);
    if (segments < 16) segments = 16;
    if (segments > 48) segments = 48;
    for (k = 0; k < CL_LIGHT_CAPS; k++) {
        int16_t a[3] = {(int16_t)light_axis[0], (int16_t)light_axis[1], (int16_t)light_axis[2]};
        cl_body_circle(b, a, cl_light_cos[k], CLB_LIGHT << k, segments);
    }
    if (p->type == GAS_GIANT) {
        int16_t pole[3] = {b->m[0][2], b->m[1][2], b->m[2][2]};
        for (k = 0; k < CL_GAS_BANDS; k++) cl_body_circle(b, pole, -14336 + k * 4096, CLB_BAND << k, segments);
    } else {
        int16_t north[3] = {b->m[0][2], b->m[1][2], b->m[2][2]};
        int16_t south[3] = {(int16_t)-b->m[0][2], (int16_t)-b->m[1][2], (int16_t)-b->m[2][2]};
        if (p->type != ICE) {
            cl_body_circle(b, north, 14000, CLB_ICE, segments * 2 / 3);
            cl_body_circle(b, south, 14000, CLB_ICE, segments * 2 / 3);
        }
        /* Under six pixels Frontier drew a plain shaded disc: no surface
           features, and none in the bottom mask, so parity holds. */
        if (b->view.radius_px < 6 * 256) b->bottom_mask &= ~(unsigned)(CLB_LAND | CLB_ROCK | CLB_DARK);
        else {
            int slot = cl_contour_slot(p);
            cl_body_contours(b, &cl_contours[slot].coast, CLB_LAND);
            cl_body_contours(b, &cl_contours[slot].highland, CLB_ROCK);
            cl_body_contours(b, &cl_contours[slot].dark, CLB_DARK);
        }
    }
    cl_body_draw(&cl_body, &cl.surface, cl_first, cl_last, b->bottom_mask, cl_body_colour, b);
}
/* Suns: Frontier's filled circles, with a dithered corona. */
static void cl_draw_star(const ClBodyInfo *b) {
    const Star *s = &game->system.star[-1 - b->body];
    int x = b->view.centre_x >> 8, y = b->view.centre_y >> 8, r = b->view.radius_px >> 8;
    int colour = frame->enhanced ? stellar_colour(s->type) : cl_star_colour_ecs[s->type % 13];
    if (s->type == ST_BLACK_HOLE) { cl_disc(&cl.surface, x, y, r, 0); return; }
    if (r < 1) { cl_pixel(&cl.surface, x, y, colour); return; }
    cl_glow(&cl.surface, x, y, r, r * 2 + 2, colour, 9);
    cl_disc(&cl.surface, x, y, r, colour);
    if (r > 3) cl_disc(&cl.surface, x, y, r * 2 / 3, 31);
}

/* Rings: per band, an outer and an inner circle as cubic Bezier arcs,
   filled even-odd as one shape (Frontier's complex polygon). Each band is
   split at the planet into a far half, drawn before it, and a near half.
   A 45-degree arc's handles are 4/3 tan(11.25 deg) = 0.2652 of the radius;
   the control points are projected, as Frontier projected them. */
#define CL_RING_ARCS 4
#define CL_RING_WORDS (2 * (CL_RING_ARCS * 3 + 1) * 2)
static void cl_ring_half(const ClBodyInfo *b, const Planet *p, int band, int front, uint32_t key) {
    static const int inner[3] = {130, 155, 180}, outer[3] = {150, 175, 200};
    Vec eye = sub(scale(camera_position, 1, FP), p->center);
    /* The ring point nearest the eye has direction (sin a, cos a, 0) along
       the eye's projection on the ring plane. */
    int face = attitude_angle(eye.x, eye.y), start;
    {
        /* Make the split self-checking: the half centred on `face` must be
           the one whose midpoint is nearer the camera than the centre. */
        int32_t mx = sini(face), my = cosi(face);
        int64_t mz = ((int64_t)cl.m[2][0] * mx + (int64_t)cl.m[2][1] * my) >> 14;
        if (mz > 0) face += 512;
    }
    start = face + (front ? -256 : 256);
    int32_t pts[CL_RING_WORDS];
    int r, k, n = 0;
    ClNode *node;
    for (r = 0; r < 2; r++) {
        /* Ring radius in planet units, where the planet's radius is 16384. */
        int32_t rad = 16384 * (r ? inner[band] : outer[band]) / 100;
        for (k = 0; k <= CL_RING_ARCS; k++) {
            /* Outer ring forwards, inner ring backwards. */
            int a = r ? start + 512 - k * 128 : start + k * 128, h, sgn = r ? -1 : 1;
            for (h = (k ? -1 : 0); h <= (k < CL_RING_ARCS ? 1 : 0); h++) {
                /* h = -1: the handle before the point, 0 the point, 1 after. */
                int32_t s1 = sini(a), c1 = cosi(a), wx = s1 * rad / 16384, wy = c1 * rad / 16384;
                ClV v;
                if (h) {
                    int32_t hl = rad / 16 * 4345 / 1024 * h * sgn; /* 0.2652 r along the tangent */
                    wx += c1 * hl / 16384; wy -= s1 * hl / 16384;
                }
                /* World ring plane (x, y) into camera space, about the centre. */
                v.x = b->cp[0] + (int32_t)(((int64_t)cl.m[0][0] * wx + (int64_t)cl.m[0][1] * wy) >> 14);
                v.y = b->cp[1] + (int32_t)(((int64_t)cl.m[1][0] * wx + (int64_t)cl.m[1][1] * wy) >> 14);
                v.z = b->cp[2] + (int32_t)(((int64_t)cl.m[2][0] * wx + (int64_t)cl.m[2][1] * wy) >> 14);
                if (v.z < 64 || !cl_project(&v, &pts[n], &pts[n + 1])) return;
                n += 2;
            }
        }
    }
    node = cl_node(key, CL_RING, 17 + band, CL_RING_WORDS);
    if (!node) return;
    memcpy(cl_pool + node->data, pts, sizeof(pts));
}
static void cl_draw_ring(const ClNode *node) {
    const int32_t *d = cl_pool + node->data;
    int r, k, per = CL_RING_ARCS * 3 + 1;
    for (r = 0; r < 2; r++) {
        const int32_t *q = d + r * per * 2;
        for (k = 0; k < CL_RING_ARCS; k++) {
            int32_t c[4][2];
            int j;
            for (j = 0; j < 4; j++) { c[j][0] = q[(k * 3 + j) * 2]; c[j][1] = q[(k * 3 + j) * 2 + 1]; }
            if (bezier_rings) cl_spans_bezier(&cl_spans, (const int32_t (*)[2])c);
            else cl_spans_edge(&cl_spans, c[0][0], c[0][1], c[3][0], c[3][1]);
        }
    }
    /* Radial joins close the half annulus. */
    cl_spans_edge(&cl_spans, d[(per - 1) * 2], d[(per - 1) * 2 + 1], d[per * 2], d[per * 2 + 1]);
    cl_spans_edge(&cl_spans, d[(2 * per - 1) * 2], d[(2 * per - 1) * 2 + 1], d[0], d[1]);
    cl_spans_fill(&cl_spans, &cl.surface, node->colour);
}

/* -------------------------------------------------- terrain and clouds */
/* Near the ground the current body gains a lattice of flat-shaded tiles
   from the shared terrain cache, the same vertices collision uses, in two
   rings: fine around the camera and four times coarser beyond it. Body
   spans carry the ground beyond them to the horizon. Tiles are camera
   relative in body-fixed metres, so offsets usually fit 16 bits. */
#define CL_TERRAIN_ALTITUDE 40000
/* Body-fixed vertex (metres) relative to the camera's body-fixed Q4
   position, rotated into camera space (Q4). */
static int cl_tile_vertex(const int16_t (*bm)[3], Vec cam_fixed4, Vec fixed, ClV *v) {
    int64_t ox = fixed.x * 16 - cam_fixed4.x, oy = fixed.y * 16 - cam_fixed4.y, oz = fixed.z * 16 - cam_fixed4.z;
    if (ox > -32768 && ox < 32768 && oy > -32768 && oy < 32768 && oz > -32768 && oz < 32768) {
        int16_t x = (int16_t)ox, y = (int16_t)oy, z = (int16_t)oz;
        v->x = ((int32_t)bm[0][0] * x + (int32_t)bm[0][1] * y + (int32_t)bm[0][2] * z) >> 14;
        v->y = ((int32_t)bm[1][0] * x + (int32_t)bm[1][1] * y + (int32_t)bm[1][2] * z) >> 14;
        v->z = ((int32_t)bm[2][0] * x + (int32_t)bm[2][1] * y + (int32_t)bm[2][2] * z) >> 14;
        return 1;
    }
    if (llabs(ox) >= (1LL << 31) || llabs(oy) >= (1LL << 31) || llabs(oz) >= (1LL << 31)) return 0;
    {
        int32_t a = (int32_t)ox, b = (int32_t)oy, c = (int32_t)oz;
        v->x = (int32_t)(((int64_t)bm[0][0] * a + (int64_t)bm[0][1] * b + (int64_t)bm[0][2] * c) >> 14);
        v->y = (int32_t)(((int64_t)bm[1][0] * a + (int64_t)bm[1][1] * b + (int64_t)bm[1][2] * c) >> 14);
        v->z = (int32_t)(((int64_t)bm[2][0] * a + (int64_t)bm[2][1] * b + (int64_t)bm[2][2] * c) >> 14);
    }
    return 1;
}
static void cl_terrain(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(viewer(), 1, FP), p->center), fixed, fixed4, sun = vec(0, 0, 0);
    int64_t altitude = length(local) - p->radius, direction[3];
    int32_t lc[3];
    int16_t bm[3][3];
    int face, cu, cv, level, i, j, half = graphics.terrain <= 1 ? 5 : graphics.terrain == 2 ? 7 : 8;
    int step = 256, inner_u0 = 0, inner_u1 = 0, inner_v0 = 0, inner_v1 = 0;
    if (altitude > CL_TERRAIN_ALTITUDE || p->type == GAS_GIANT || (p->catalog && p->radius < 100000)) return;
    fixed = body_rotate(p, local, 1);
    fixed4 = body_rotate(p, vec(floor_shift(viewer().x - p->center.x * FP, 4), floor_shift(viewer().y - p->center.y * FP, 4),
                                floor_shift(viewer().z - p->center.z * FP, 4)), 1);
    direction[0] = fixed.x; direction[1] = fixed.y; direction[2] = fixed.z;
    surface_uv(p, direction, &face, &cu, &cv);
    for (j = 0; j < 3; j++) {
        Vec e = body_rotate(p, vec(j == 0 ? 16384 : 0, j == 1 ? 16384 : 0, j == 2 ? 16384 : 0), 0);
        for (i = 0; i < 3; i++)
            bm[i][j] = (int16_t)(((int32_t)cl.m[i][0] * e.x + (int32_t)cl.m[i][1] * e.y + (int32_t)cl.m[i][2] * e.z) >> 14);
    }
    if (game->system.stars) sun = body_rotate(p, unit(sub(game->system.star[0].center, p->center), 16384), 1);
    for (i = 0; i < 3; i++)
        lc[i] = ((int32_t)bm[i][0] * sun.x + (int32_t)bm[i][1] * sun.y + (int32_t)bm[i][2] * sun.z) >> 14;
    while (step < altitude / 3) step *= 2;
    for (level = 0; level < 2; level++, step *= 4) {
        int u0 = terrain_floor(cu, step) * step - half * step, v0 = terrain_floor(cv, step) * step - half * step;
        static ClV row[2][40];
        static Vec rowf[2][40];
        int x, y, n = half * 2;
        for (y = 0; y <= n; y++) {
            int v = v0 + y * step;
            if (platform_pump && !(y & 3)) platform_pump();
            for (x = 0; x <= n; x++) {
                rowf[y & 1][x] = terrain_fixed_vertex(p, face, u0 + x * step, v);
                if (!cl_tile_vertex((const int16_t (*)[3])bm, fixed4, rowf[y & 1][x], &row[y & 1][x])) row[y & 1][x].z = -1;
            }
            if (!y) continue;
            for (x = 0; x < n; x++) {
                int u = u0 + x * step, k;
                const uint8_t *sample;
                ClV tri[2][16];
                Vec ca = rowf[(y - 1) & 1][x], cc = rowf[y & 1][x + 1];
                int32_t upc[3];
                int64_t cl_len;
                /* The coarse ring underlaps the fine one by a cell: drawn
                   first (farther), it fills any crack at their seam. */
                if (level && u >= inner_u0 + step && u + step <= inner_u1 - step &&
                    v - step >= inner_v0 + step && v <= inner_v1 - step) continue;
                if (row[(y - 1) & 1][x].z < 0 || row[(y - 1) & 1][x + 1].z < 0 || row[y & 1][x].z < 0 || row[y & 1][x + 1].z < 0) continue;
                if (row[(y - 1) & 1][x].z < CL_NEAR && row[(y - 1) & 1][x + 1].z < CL_NEAR &&
                    row[y & 1][x].z < CL_NEAR && row[y & 1][x + 1].z < CL_NEAR) continue;
                {
                    /* The cell's up direction in camera space orients both facets. */
                    Vec up = vec((ca.x + cc.x) / 2, (ca.y + cc.y) / 2, (ca.z + cc.z) / 2);
                    cl_len = length(up);
                    if (!cl_len) continue;
                    up = scale(up, 16384, cl_len);
                    for (i = 0; i < 3; i++)
                        upc[i] = ((int32_t)bm[i][0] * up.x + (int32_t)bm[i][1] * up.y + (int32_t)bm[i][2] * up.z) >> 14;
                }
                sample = patch_samples(p, face, u, v - step, step);
                tri[0][0] = row[(y - 1) & 1][x]; tri[0][1] = row[(y - 1) & 1][x + 1]; tri[0][2] = row[y & 1][x + 1];
                tri[1][0] = row[(y - 1) & 1][x]; tri[1][1] = row[y & 1][x + 1]; tri[1][2] = row[y & 1][x];
                for (k = 0; k < 2; k++) {
                    /* Flat Lambert light of the facet, its normal turned up
                       out of the ground: relief and night in one term. */
                    int64_t ax = tri[k][1].x - tri[k][0].x, ay = tri[k][1].y - tri[k][0].y, az = tri[k][1].z - tri[k][0].z;
                    int64_t bx = tri[k][2].x - tri[k][0].x, by = tri[k][2].y - tri[k][0].y, bz = tri[k][2].z - tri[k][0].z;
                    int64_t nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                    int d, s;
                    if (nx * upc[0] + ny * upc[1] + nz * upc[2] < 0) { nx = -nx; ny = -ny; nz = -nz; }
                    d = cl_face_light(nx, ny, nz, lc);
                    s = d > 0 ? d * 14 / 16384 : 0;
                    if (!frame->enhanced) s = s / 6 * 6;
                    cl_polygon(tri[k], 3, ground_shade(s, sample[k]), 0);
                }
            }
        }
        inner_u0 = u0; inner_v0 = v0; inner_u1 = u0 + n * step; inner_v1 = v0 + n * step;
    }
}
/* Cumulus as Frontier drew round things: each puff a shaded ball. */
static void cl_clouds(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(camera_position, 1, FP), p->center), fixed, sun = vec(0, 0, 16384);
    static const int ranges[] = {1, 2, 3, 5, 8};
    int cell = cloud_spacing(p), rings = ranges[graphics.cloud_range], face, ring, du, dv, k;
    int64_t alt = length(local) - p->radius, range = (int64_t)cell * rings;
    int32_t lx = 0, ly = -4096;
    if (!graphics.clouds || !p->atmosphere || alt > (int64_t)cell * 10) return;
    if (game->system.stars) {
        Vec l = unit(sub(game->system.star[0].center, p->center), 16384);
        int32_t sx = ((int32_t)cl.m[0][0] * l.x + (int32_t)cl.m[0][1] * l.y + (int32_t)cl.m[0][2] * l.z) >> 14;
        int32_t sy = ((int32_t)cl.m[1][0] * l.x + (int32_t)cl.m[1][1] * l.y + (int32_t)cl.m[1][2] * l.z) >> 14;
        int32_t len = (int32_t)cl_isqrt((uint32_t)(sx * sx + sy * sy));
        sun = l;
        if (len) { lx = sx * 4096 / len; ly = -sy * 4096 / len; }
    }
    fixed = body_rotate(p, local, 1);
    for (face = 0; face < 6; face++) {
        int u, v;
        if (!cloud_face_cell(p, face, fixed, range, &u, &v)) continue;
        for (ring = 0; ring <= rings; ring++)
            for (dv = -ring; dv <= ring; dv++)
                for (du = -ring; du <= ring; du++) {
                    const Cloud *c;
                    Vec base;
                    int64_t dist;
                    int light, lit, shadow;
                    if (du != -ring && du != ring && dv != -ring && dv != ring) continue;
                    c = cloud_at(p, face, u + du, v + dv, game->tick);
                    if (!c) continue;
                    base = body_rotate(p, c->base, 0);
                    dist = length(sub(local, base));
                    if (dist > range) continue;
                    light = surface_light(&game->system, p, base) * 3 / 64;
                    if (light > 256) light = 256;
                    lit = cloud_colour(40 + light * 200 / 256);
                    shadow = cloud_colour(30 + light * 110 / 256);
                    for (k = 0; k < c->puffs && k < (graphics.clouds >= 2 ? CLOUD_PUFFS : 3); k++) {
                        const CloudPuff *puff = &c->puff[k];
                        Vec east = body_rotate(p, c->east, 0), north = body_rotate(p, c->north, 0), upv = body_rotate(p, c->up, 0);
                        Vec w = add(add(p->center, base), add(add(scale(east, puff->x, 16384), scale(north, puff->y, 16384)),
                                                            scale(upv, puff->z + puff->radius / 2, 16384)));
                        ClV cv;
                        int32_t px, py, *d;
                        ClNode *node;
                        if (!cl_world(w, &cv) || !cl_project(&cv, &px, &py)) continue;
                        node = cl_node(cl_key(cv.z), CL_BALL, lit, 6);
                        if (!node) return;
                        d = cl_pool + node->data;
                        d[0] = px >> 4; d[1] = py >> 4;
                        d[2] = (int32_t)((int64_t)puff->radius * 16 * cl.focal / cv.z);
                        d[3] = lx; d[4] = ly; d[5] = shadow;
                        if (d[2] < 1) { cl_count--; cl_used -= 6; }
                    }
                }
    }
    (void)sun;
}
/* In cloud, without a depth buffer: an even ordered-dither whiteout by
   density through the fog tables; rain as streaks, as before. */
static void cl_weather(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(camera_position, 1, FP), p->center);
    int density = cloud_density(p, local, game->tick), rain = precipitation(p, local, game->tick);
    int x, y, i, lit = surface_light(&game->system, p, local) > 0;
    if (density) {
        /* Without depth the whole view takes the density's fog, capped as
           the Enhanced fog caps solid ground: dense cloud leaves the world
           a faint silhouette rather than a blank page. */
        int level = density * 80 / 256 > 50 ? 50 : density * 80 / 256;
        if (fog_enhanced != frame->enhanced) fog_tables(frame);
        for (y = cl.surface.top; y < cl.surface.bottom; y++) {
            uint8_t *line = frame->pixels + (size_t)y * frame->w;
            for (x = cl.surface.left; x < cl.surface.right; x++) {
                int q = (level + bayer4[(y & 3) * 4 + (x & 3)]) >> 4;
                if (q >= 4) line[x] = fog_full[lit];
                else if (q) line[x] = fog_map[lit][q - 1][line[x]];
            }
        }
        weather_status |= 1;
    }
    if (rain) {
        for (i = 0; i < (frame->enhanced ? 48 : 24) * (cinema ? 2 : 1); i++) {
            uint32_t h = hash32(p->seed + (unsigned)i * 7919U);
            x = (int)(h % (unsigned)frame->w);
            y = (int)(((h >> 16) + (cinema ? game->ui_clock : game->tick) * (rain == 1 ? 9U : 2U)) % (unsigned)view_h);
            rect(x, y, rain == 1 ? 1 : 2, rain == 1 ? 7 : 2, lit ? 7 : 8);
        }
        weather_status |= rain == 1 ? 2 : 4;
    }
}

/* ------------------------------------------------------------- frame */
static void cl_draw_body(const ClNode *node) {
    ClBodyInfo *b = &cl_body_info[node->data];
    if (b->body < 0) cl_draw_star(b); else cl_draw_planet(b);
}
static void cl_bodies(void) {
    int i;
    cl_body_count = 0;
    for (i = -game->system.stars; i < game->system.count && cl_body_count < CL_BODIES; i++) {
        ClBodyInfo *b = &cl_body_info[cl_body_count];
        const Planet *p = i >= 0 ? &game->system.planets[i] : NULL;
        ClNode *node;
        uint32_t key;
        int64_t d, near;
        if (p && p->catalog) {
            /* As before: hundreds of distant catalog moons are unresolved. */
            Vec c = camera(p->center);
            if (c.z > 0 && (int64_t)p->radius * frame->w < c.z * 2) continue;
        }
        if (!cl_body_setup(b, i)) continue;
        d = length(vec(b->c[0], b->c[1], b->c[2]));
        near = (d - b->radius) * b->q4;
        key = cl_key(near);
        /* Near the ground the current body is the backdrop of everything. */
        if (p && i == game->body && d - b->radius < (b->radius > 16 * PATCH_ALTITUDE / b->q4 ? 16 * PATCH_ALTITUDE / b->q4 : b->radius))
            key = 0xFFFFFF00U;
        node = cl_node(key, CL_BODY, 0, 0);
        if (!node) return;
        node->data = cl_body_count++;
        if (p && p->rings && graphics.rings && b->view.kind == PLANET_DISC) {
            int band;
            uint32_t back = cl_key(d * b->q4), front = key > 0 ? key - 1 : 0;
            for (band = 0; band < 3; band++) {
                cl_ring_half(b, p, band, 0, back);
                cl_ring_half(b, p, band, 1, front);
            }
        }
    }
}
static void world_objects(void);
static void classic_world(void) {
    cl_view();
    if (cl_body.height != (view_h > 1024 ? 1024 : view_h)) cl_body_init(&cl_body, cl_body_rows, cl_body_toggles, view_h > 1024 ? 1024 : view_h);
    cl_count = cl_used = 0;
    cl_bias = 0; cl_emit = 0;
    cl_active = 1;
    world_objects();
    cl_lights(0);
    cl_bodies();
    cl_terrain();
    forests();
    cl_clouds();
    destruction_effects();
    if (!cinema || performance_camera) weapon_effects();
    cl_active = 0;
    cl_flush();
}
