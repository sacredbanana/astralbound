/* SPDX-License-Identifier: Unlicense; exact planetary outlines. See planet_view.h.

   A ray d = (u, v, f) from the eye meets the sphere (C, R) when
       g(u) = (C.d)^2 - c |d|^2 >= 0  and  C.d > 0,      c = |C|^2 - R^2.
   For one scanline (fixed v) g is the quadratic A u^2 + 2 B u + K with
       A = Cx^2 - c,  B = Cx m,  K = m^2 - c (v^2 + f^2),  m = Cy v + Cz f,
   and by Lagrange's identity its discriminant is B^2 - A K = c E(v), where
       E(v) = R^2 (v^2 + f^2) - (Cy f - Cz v)^2.
   E has no catastrophic cancellation for small distant spheres, and c comes
   from the caller's exact altitude, so horizons stay exact in low flight.
   The forward cone is convex: every row meets it in a single interval,
   bounded when A < 0 and running off one side of the screen when A > 0. */
#include "planet_view.h"
#include <string.h>
#define ROW_LIMIT (1LL << 40)
/* Rows (v) or columns (u) where E >= 0: the conic's extent along one axis. */
static int axis_extent(XFloat q2, XFloat q1, XFloat q0, XFloat *lo, XFloat *hi) {
    XFloat disc = xf_sub(xf_mul(q1, q1), xf_mul(xf_fixed(q2.m, q2.e + 2), q0)), root, q;
    if (xf_sign(q2) >= 0) return 0; /* unbounded along this axis */
    if (xf_sign(disc) < 0) return -1; /* empty */
    root = xf_sqrt(disc);
    q = xf_neg(xf_add(q1, xf_sign(q1) < 0 ? xf_neg(root) : root));
    if (!q.m) { *lo = *hi = XF_ZERO; return 1; }
    /* Stable roots of q2 t^2 + q1 t + q0: q / (2 q2) and 2 q0 / q. */
    *lo = xf_div(q, xf_fixed(q2.m, q2.e + 1));
    *hi = xf_div(xf_fixed(q0.m, q0.e + 1), q);
    if (xf_less(*hi, *lo)) { XFloat t = *lo; *lo = *hi; *hi = t; }
    return 1;
}
static int clamp_floor(XFloat t, int limit_lo, int limit_hi) {
    int64_t q8 = xf_to_fixed(t, 8, ROW_LIMIT);
    int64_t w = q8 >= 0 ? q8 >> 8 : -((-q8 + 255) >> 8);
    return w < limit_lo ? limit_lo : w > limit_hi ? limit_hi : (int)w;
}
int planet_view(PlanetView *v, const Projection *proj, Vec centre, int64_t radius, int64_t altitude) {
    XFloat r, f = xf_int(proj->focal), lo, hi;
    int64_t limit = INT32_MAX;
    int found;
    memset(v, 0, sizeof(*v));
    v->proj = *proj;
    v->kind = PLANET_HIDDEN;
    if (radius <= 0) return v->kind;
    /* From inside, see the surface from a hair above: radius = |C| (1 - 2^-11),
       as Frontier did. */
    if (altitude < 1) {
        int64_t d = radius + altitude;
        if (d <= 0) return v->kind;
        altitude = d / 2048 > 0 ? d / 2048 : 1;
        radius = d - altitude;
    }
    if (centre.z + radius <= 0) return v->kind; /* wholly behind the eye */
    r = xf_int(radius);
    v->x = xf_int(centre.x); v->y = xf_int(centre.y); v->z = xf_int(centre.z);
    v->c = xf_mul(xf_int(altitude), xf_int(2 * radius + altitude));
    v->a = xf_sub(xf_mul(v->x, v->x), v->c);
    /* E(v) = (R^2 - Cz^2) v^2 + 2 Cy Cz f v + (R^2 - Cy^2) f^2, with the
       differences of squares formed from exact integer differences. */
    v->e2 = xf_mul(xf_int(radius - centre.z), xf_int(radius + centre.z));
    v->e1 = xf_mul(xf_fixed(xf_mul(v->y, v->z).m, xf_mul(v->y, v->z).e + 1), f);
    v->e0 = xf_mul(xf_mul(xf_int(radius - centre.y), xf_int(radius + centre.y)), xf_mul(f, f));
    /* Row bounds: rows run downwards, v = cy - y. */
    v->top = proj->top; v->bottom = proj->bottom;
    found = axis_extent(v->e2, v->e1, v->e0, &lo, &hi);
    if (found < 0) return v->kind;
    if (found > 0) {
        int y0 = proj->cy - clamp_floor(hi, -(1 << 20), 1 << 20) - 1;
        int y1 = proj->cy - clamp_floor(lo, -(1 << 20), 1 << 20) + 1;
        if (y0 > v->top) v->top = y0;
        if (y1 + 1 < v->bottom) v->bottom = y1 + 1;
    }
    /* Column bounds: the same conic with x and y exchanged. */
    v->left = proj->left; v->right = proj->right;
    {
        XFloat q2 = v->e2, q0 = xf_mul(xf_mul(xf_int(radius - centre.x), xf_int(radius + centre.x)), xf_mul(f, f));
        XFloat xz = xf_mul(v->x, v->z), q1 = xf_mul(xf_fixed(xz.m, xz.e + 1), f);
        found = axis_extent(q2, q1, q0, &lo, &hi);
        if (found < 0) return v->kind;
        if (found > 0) {
            int x0 = proj->cx + clamp_floor(lo, -(1 << 20), 1 << 20) - 1;
            int x1 = proj->cx + clamp_floor(hi, -(1 << 20), 1 << 20) + 1;
            if (x0 > v->left) v->left = x0;
            if (x1 + 1 < v->right) v->right = x1 + 1;
        }
    }
    if (v->top >= v->bottom || v->left >= v->right) return v->kind;
    /* Angular radius at the centre ray: f tan(rho) = f R / sqrt(c). */
    v->radius_px = (int32_t)xf_to_fixed(xf_div(xf_mul(f, r), xf_sqrt(v->c)), 8, INT32_MAX);
    if (centre.z > 0) {
        v->centre_x = (int32_t)xf_to_fixed(xf_add(xf_int(proj->cx), xf_div(xf_mul(v->x, f), v->z)), 8, limit);
        v->centre_y = (int32_t)xf_to_fixed(xf_sub(xf_int(proj->cy), xf_div(xf_mul(v->y, f), v->z)), 8, limit);
    }
    /* Under about a third of a pixel the outline is meaningless; callers
       draw one sample instead, if the centre is in view. */
    v->kind = v->radius_px < 96 && centre.z > 0 ? PLANET_POINT : PLANET_DISC;
    return v->kind;
}
int planet_row(const PlanetView *v, int y, int32_t *x0, int32_t *x1) {
    XFloat row = xf_int(v->proj.cy - y), f = xf_int(v->proj.focal);
    XFloat m, b, k, e, disc, lo, hi, root, q, axis = XF_ZERO;
    int64_t limit_lo = (int64_t)(v->proj.left - 1) * 256, limit_hi = (int64_t)(v->proj.right + 1) * 256;
    int bounded_lo = 1, bounded_hi = 1;
    if (v->kind == PLANET_HIDDEN || y < v->top || y >= v->bottom) return 0;
    m = xf_add(xf_mul(v->y, row), xf_mul(v->z, f));
    e = xf_add(xf_mul(xf_add(xf_mul(v->e2, row), v->e1), row), v->e0);
    b = xf_mul(v->x, m);
    if (!v->a.m) {
        /* Parabolic row: g = 2 B u + K is linear. */
        k = xf_sub(xf_mul(m, m), xf_mul(v->c, xf_add(xf_mul(row, row), xf_mul(f, f))));
        if (!b.m) return 0;
        lo = hi = xf_div(xf_neg(k), xf_fixed(b.m, b.e + 1));
        if (xf_sign(b) > 0) bounded_hi = 0; else bounded_lo = 0;
    } else {
        if (xf_sign(e) < 0) return 0;
        disc = xf_mul(v->c, e);
        root = xf_sqrt(disc);
        q = xf_neg(xf_add(b, xf_sign(b) < 0 ? xf_neg(root) : root));
        k = xf_sub(xf_mul(m, m), xf_mul(v->c, xf_add(xf_mul(row, row), xf_mul(f, f))));
        if (!q.m) lo = hi = XF_ZERO;
        else {
            lo = xf_div(q, v->a);
            hi = xf_div(k, q);
            if (xf_less(hi, lo)) { XFloat t = lo; lo = hi; hi = t; }
        }
        if (xf_sign(v->a) > 0) {
            /* The roots bound two rays; the forward nappe is the one on
               Cx's side of C.d = 0. */
            if (xf_sign(v->x) > 0) { lo = hi; bounded_hi = 0; }
            else { hi = lo; bounded_lo = 0; }
        }
    }
    /* The forward condition C.d = Cx u + m > 0 at a point of the interval:
       its midpoint, or one step inside an unbounded end. */
    if (bounded_lo && bounded_hi) axis = xf_fixed(xf_add(lo, hi).m, xf_add(lo, hi).e - 1);
    else if (bounded_lo) axis = xf_add(lo, xf_int(1));
    else if (bounded_hi) axis = xf_sub(hi, xf_int(1));
    if (xf_sign(xf_add(xf_mul(v->x, axis), m)) <= 0) return 0;
    {
        XFloat centre = xf_int(v->proj.cx);
        int64_t a = bounded_lo ? xf_to_fixed(xf_add(centre, lo), 8, ROW_LIMIT) : limit_lo;
        int64_t z = bounded_hi ? xf_to_fixed(xf_add(centre, hi), 8, ROW_LIMIT) : limit_hi;
        if (a < limit_lo) a = limit_lo;
        if (z > limit_hi) z = limit_hi;
        if (a > z) return 0;
        *x0 = (int32_t)a; *x1 = (int32_t)z;
    }
    return 1;
}
int planet_row_pixels(const PlanetView *v, int y, int *first, int *last) {
    int32_t x0, x1;
    int a, z;
    if (!planet_row(v, y, &x0, &x1)) return 0;
    a = x0 >= 0 ? (x0 + 255) >> 8 : -((-x0) >> 8);
    z = x1 >= 0 ? x1 >> 8 : -((-x1 + 255) >> 8);
    if (a < v->proj.left) a = v->proj.left;
    if (z >= v->proj.right) z = v->proj.right - 1;
    if (a > z) return 0;
    *first = a; *last = z;
    return 1;
}
