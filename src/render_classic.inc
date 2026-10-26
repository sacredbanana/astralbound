/* SPDX-License-Identifier: Unlicense
   The Classic renderer: Frontier: Elite II's techniques for a 68030 without
   an FPU. Nothing is depth-buffered or shaded per pixel. Geometry is built
   into a painter's draw list, then filled as flat spans far to near:

   - Objects transform once, as an origin plus a 16-bit rotation; their
     vertices then cost MULS.W multiplies (Frontier's model code).
   - Projection is one 32-bit reciprocal per vertex and two widening
     multiplies, with no 64-bit division.
   - One span filler serves every polygon, ring and curve (classic_raster).
   - Planets are body spans: coastlines, ice caps, gas bands and terminator
     bands become XOR feature masks, with the left-limb parity toggles of
     Frontier's planet renderer. Rings are Bezier ellipses.
   - Suns, clouds and explosions are circles, as Frontier drew them.

   Scene and model data are shared with the Enhanced renderer: ship lofts,
   port layouts, terrain, clouds, planet fields and outlines. Rasterisation
   is not. */
void render_renderer(int renderer) { classic_mode = renderer == RENDERER_CLASSIC; }

/* ---------------------------------------------------------------- view */
/* Camera space is Q4 metres (x right, y up, z ahead). */
static struct {
    int16_t m[3][3];        /* world to camera, Q14 */
    Vec cam4;               /* camera position, Q4 metres */
    int cx, cy, focal;
    int32_t guard_y;        /* vertical guard slope, Q16: G view_h / w */
    ClSurface surface;
} cl;
/* The per-frame vertex cache (see cl_triangle_world). */
#define CL_VERTEX_CACHE 512
static struct { Vec w; ClV v; int32_t x, y; uint16_t frame; uint8_t ok, outcode; } cl_vertex[CL_VERTEX_CACHE];
static uint16_t cl_vertex_frame = 1;
#define CL_NEAR (4 * 16)    /* near plane, Q4: 4 m */
#define CL_GUARD 3          /* guard band: three half-widths each side */
static void cl_view(void) {
    int i, j;
    /* A new camera: every cached vertex transform is stale. */
    if (!++cl_vertex_frame) { memset(cl_vertex, 0, sizeof(cl_vertex)); cl_vertex_frame = 1; }
    view_rotate(vec(0, 0, 0)); /* make sure the cached view matrix is current */
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) cl.m[i][j] = (int16_t)view_matrix.m[i][j];
    cl.cam4 = vec(floor_shift(camera_position.x, 4), floor_shift(camera_position.y, 4), floor_shift(camera_position.z, 4));
    cl.cx = frame->w / 2 + proj_dx; cl.cy = view_h / 2 + proj_dy; cl.focal = frame->w / 2;
    cl.guard_y = (int32_t)(((int64_t)CL_GUARD * view_h << 16) / (frame->w ? frame->w : 1));
    cl.surface.pixels = frame->pixels; cl.surface.pitch = frame->w;
    cl.surface.left = view_left; cl.surface.top = view_top;
    cl.surface.right = frame->w - view_right_inset; cl.surface.bottom = view_h;
}
/* Rotate a Q4 offset from the camera. Within 2 km the offset fits 16 bits
   and the rotation is nine MULS.W; farther points take 32x16 products. */
static int cl_rotate(int64_t ox, int64_t oy, int64_t oz, ClV *v) {
    if (ox > -32768 && ox < 32768 && oy > -32768 && oy < 32768 && oz > -32768 && oz < 32768) {
        int16_t x = (int16_t)ox, y = (int16_t)oy, z = (int16_t)oz;
        v->x = ((int32_t)cl.m[0][0] * x + (int32_t)cl.m[0][1] * y + (int32_t)cl.m[0][2] * z) >> 14;
        v->y = ((int32_t)cl.m[1][0] * x + (int32_t)cl.m[1][1] * y + (int32_t)cl.m[1][2] * z) >> 14;
        v->z = ((int32_t)cl.m[2][0] * x + (int32_t)cl.m[2][1] * y + (int32_t)cl.m[2][2] * z) >> 14;
        return 1;
    }
    if (llabs(ox) >= (1LL << 31) || llabs(oy) >= (1LL << 31) || llabs(oz) >= (1LL << 31)) return 0;
    {
        /* 16 x 32-bit products: widening MULS.L, not 64-bit library calls. */
        int32_t a = (int32_t)ox, b = (int32_t)oy, c = (int32_t)oz;
        int64_t x = ((int64_t)cl.m[0][0] * a + (int64_t)cl.m[0][1] * b + (int64_t)cl.m[0][2] * c) >> 14;
        int64_t y = ((int64_t)cl.m[1][0] * a + (int64_t)cl.m[1][1] * b + (int64_t)cl.m[1][2] * c) >> 14;
        int64_t z = ((int64_t)cl.m[2][0] * a + (int64_t)cl.m[2][1] * b + (int64_t)cl.m[2][2] * c) >> 14;
        if (llabs(x) >= (1LL << 31) || llabs(y) >= (1LL << 31) || llabs(z) >= (1LL << 31)) return 0;
        v->x = (int32_t)x; v->y = (int32_t)y; v->z = (int32_t)z;
    }
    return 1;
}
/* A world point in metres to camera space. */
static int cl_world(Vec w, ClV *v) {
    return cl_rotate(w.x * 16 - cl.cam4.x, w.y * 16 - cl.cam4.y, w.z * 16 - cl.cam4.z, v);
}
/* A Q8 point to the camera's sixteenth of a metre. */
static int cl_world_q8(Vec w, ClV *v) {
    return cl_rotate(floor_shift(w.x, 4) - cl.cam4.x, floor_shift(w.y, 4) - cl.cam4.y,
                     floor_shift(w.z, 4) - cl.cam4.z, v);
}
/* Perspective by reciprocal: z is normalised to 15 bits, one 32-bit
   division gives 2^30/z, and x, y take one widening multiply each.
   Output is Q4 screen coordinates. */
static int cl_project(const ClV *v, int32_t *px, int32_t *py) {
    int32_t z = v->z, r, rf;
    int s = 0;
    int64_t x, y;
    if (z < CL_NEAR) return 0;
    s = 17 - __builtin_clz((uint32_t)z); /* top bit to bit 14: one BFFFO */
    z = s >= 0 ? z >> s : z << -s;
    r = (int32_t)(0x40000000 / z);                  /* 2^15..2^16 */
    rf = r * cl.focal;                              /* below 2^26 */
    x = (int64_t)v->x * rf; y = (int64_t)v->y * rf;
    /* x / z * focal in Q4: x rf / 2^(30 + s) * 16 */
    if (26 + s > 0) { x = floor_shift(x, 26 + s); y = floor_shift(y, 26 + s); }
    else { x *= (int64_t)1 << -(26 + s); y *= (int64_t)1 << -(26 + s); }
    if (x > (1 << 21)) x = 1 << 21;
    if (x < -(1 << 21)) x = -(1 << 21);
    if (y > (1 << 21)) y = 1 << 21;
    if (y < -(1 << 21)) y = -(1 << 21);
    *px = cl.cx * 16 + (int32_t)x;
    *py = cl.cy * 16 - (int32_t)y;
    return 1;
}
/* Clip planes: near, then left/right/top/bottom of a guard band three
   half-widths wide, so projected vertices stay small and edges straight.
   Q4 camera coordinates stay below 2^31, so each test is a 32 x 16-bit
   product and a compare; only the band's vertical slope needs a shift. */
static int64_t cl_plane(const ClV *v, int plane) {
    int64_t z = v->z, hh = ((int64_t)v->z * cl.guard_y) >> 16;
    switch (plane) {
    case 0: return z - CL_NEAR;
    case 1: return CL_GUARD * z + v->x;   /* x >= -G z: G half-widths left */
    case 2: return CL_GUARD * z - v->x;
    case 3: return hh - v->y;             /* the same band above and below */
    default: return hh + v->y;
    }
}
/* The planes a vertex lies outside of, one bit each. */
static unsigned cl_outcode(const ClV *v) {
    int64_t z = v->z, g = CL_GUARD * z, hh = ((int64_t)v->z * cl.guard_y) >> 16;
    return (unsigned)(z < CL_NEAR) | (unsigned)(g + v->x < 0) << 1 | (unsigned)(g - v->x < 0) << 2 |
           (unsigned)(hh - v->y < 0) << 3 | (unsigned)(hh + v->y < 0) << 4;
}
/* Sutherland-Hodgman in camera space, only against the planes some vertex
   is outside of; most polygons need none. Crossings interpolate from the
   lexicographically first end so polygons sharing an edge agree. */
static int cl_clip(ClV *poly, int n, ClV *scratch) {
    int plane, i;
    unsigned outside = 0;
    ClV *in = poly, *out = scratch;
    for (i = 0; i < n; i++) outside |= cl_outcode(&poly[i]);
    if (!outside) return n;
    for (plane = 0; plane < 5 && n >= 3; plane++) {
        int m = 0, any = 0;
        if (!(outside >> plane & 1)) continue;
        for (i = 0; i < n; i++) if (cl_plane(&in[i], plane) < 0) { any = 1; break; }
        if (!any) continue;
        for (i = 0; i < n; i++) {
            ClV p = in[i], q = in[(i + 1) % n];
            int64_t dp = cl_plane(&p, plane), dq = cl_plane(&q, plane);
            if (dp >= 0) out[m++] = p;
            if ((dp >= 0) != (dq >= 0) && m < 15) {
                ClV a = p, b = q, r;
                int64_t da = dp, db = dq;
                if (q.x < p.x || (q.x == p.x && (q.y < p.y || (q.y == p.y && q.z < p.z)))) {
                    a = q; b = p; da = dq; db = dp;
                }
                r.x = (int32_t)(a.x + (int64_t)(b.x - a.x) * da / (da - db));
                r.y = (int32_t)(a.y + (int64_t)(b.y - a.y) * da / (da - db));
                r.z = (int32_t)(a.z + (int64_t)(b.z - a.z) * da / (da - db));
                out[m++] = r;
            }
        }
        n = m;
        { ClV *t = in; in = out; out = t; }
        /* Later planes see the clipped vertices. */
        outside = 0;
        for (i = 0; i < n; i++) outside |= cl_outcode(&in[i]);
    }
    if (in != poly) for (i = 0; i < n; i++) poly[i] = in[i];
    return n;
}

/* ---------------------------------------------------------- draw list */
/* Painter's order, far to near. A node is a polygon (Q4 points in the
   pool), a line, a disc, a planet, or a sprite. */
enum { CL_POLY, CL_LINE, CL_DISC, CL_BODY, CL_SPRITE, CL_PIXEL, CL_BALL, CL_RING, CL_INTRO, CL_AURORA };
typedef struct {
    uint32_t key;     /* larger is farther */
    uint8_t kind, colour, extra;
    uint16_t n;
    int32_t data;     /* pool offset, or an index */
} ClNode;
#define CL_NODES 8192
#define CL_POOL 49152
static ClNode cl_nodes[CL_NODES];
static int32_t cl_pool[CL_POOL];
static uint32_t cl_sort_key[CL_NODES];
static uint16_t cl_order[CL_NODES * 2];
static int cl_count, cl_used;
/* Depth key from a camera-space z (Q4): order-preserving and saturating. */
static uint32_t cl_key(int64_t z) { return z <= 0 ? 0 : z >= 0xFFFFFF00LL ? 0xFFFFFF00U : (uint32_t)z; }
static ClNode *cl_node(uint32_t key, int kind, int colour, int words) {
    ClNode *n;
    if (cl_count >= CL_NODES || cl_used + words > CL_POOL) return NULL;
    n = &cl_nodes[cl_count++];
    n->key = key; n->kind = (uint8_t)kind; n->colour = (uint8_t)colour; n->extra = 0;
    n->n = 0; n->data = cl_used;
    cl_used += words;
    return n;
}
/* A camera-space polygon: clip, project, queue. bias moves it nearer in
   the order (decals, buildings on their ground). */
static void cl_polygon(ClV *v, int n, int colour, int64_t bias) {
    ClV scratch[16];
    int32_t *xy;
    ClNode *node;
    int64_t zsum = 0;
    int i;
    if (n < 3) return;
    n = cl_clip(v, n, scratch);
    if (n < 3) return;
    for (i = 0; i < n; i++) zsum += v[i].z;
    /* The centroid depth: a native 32-bit division when the sum fits. */
    zsum = zsum > -0x7fffffffLL && zsum < 0x7fffffffLL ? (int32_t)zsum / n : zsum / n;
    node = cl_node(cl_key(zsum - bias), CL_POLY, colour, n * 2);
    if (!node) return;
    xy = cl_pool + node->data;
    for (i = 0; i < n; i++) if (!cl_project(&v[i], &xy[i * 2], &xy[i * 2 + 1])) { cl_count--; cl_used -= n * 2; return; }
    node->n = (uint16_t)n;
}
static void cl_draw_body(const ClNode *node);
static void cl_draw_sprite(const ClNode *node);
static void cl_draw_ball(const ClNode *node);
static void cl_draw_ring(const ClNode *node);
static void cl_draw_intro(const ClNode *node);
static void cl_intro_effects(void);
static void cl_flush(void) {
    int i, pass, *unused = NULL;
    uint16_t *order = cl_order, *spare = cl_order + CL_NODES;
    (void)unused;
    /* Stable LSD radix sort of indices, far first: keys inverted. */
    for (i = 0; i < cl_count; i++) { cl_sort_key[i] = ~cl_nodes[i].key; order[i] = (uint16_t)i; }
    for (pass = 0; pass < 32; pass += 8) {
        int count[256], start = 0;
        uint16_t *t;
        memset(count, 0, sizeof(count));
        for (i = 0; i < cl_count; i++) count[cl_sort_key[i] >> pass & 255]++;
        if (cl_count && count[cl_sort_key[0] >> pass & 255] == cl_count) continue;
        for (i = 0; i < 256; i++) { int c = count[i]; count[i] = start; start += c; }
        for (i = 0; i < cl_count; i++) spare[count[cl_sort_key[order[i]] >> pass & 255]++] = order[i];
        t = order; order = spare; spare = t;
    }
    if (cl_spans.height != view_h) cl_spans_init(&cl_spans, cl_rows, view_h > 1024 ? 1024 : view_h);
    for (i = 0; i < cl_count; i++) {
        const ClNode *n = &cl_nodes[order[i]];
        const int32_t *d = cl_pool + n->data;
        if (platform_pump && !(i & 63)) platform_pump();
        switch (n->kind) {
        case CL_POLY:
            if (cl_convex_fill(&cl.surface, d, n->n, n->colour)) break;
            cl_spans_polygon(&cl_spans, d, n->n);
            cl_spans_fill(&cl_spans, &cl.surface, n->colour);
            break;
        case CL_LINE:
            cl_line(&cl.surface, d[0] >> 4, d[1] >> 4, d[2] >> 4, d[3] >> 4, n->colour);
            if (n->extra) cl_line(&cl.surface, d[0] >> 4, (d[1] >> 4) + 1, d[2] >> 4, (d[3] >> 4) + 1, 31);
            break;
        case CL_DISC: cl_disc(&cl.surface, d[0], d[1], d[2], n->colour); break;
        case CL_PIXEL: cl_pixel(&cl.surface, d[0], d[1], n->colour); break;
        case CL_BODY: cl_draw_body(n); break;
        case CL_AURORA: aurora_body(&game->system.planets[n->data], 1); break;
        case CL_SPRITE: cl_draw_sprite(n); break;
        case CL_BALL: cl_draw_ball(n); break;
        case CL_RING: cl_draw_ring(n); break;
        case CL_INTRO: cl_draw_intro(n); break;
        }
    }
    cl_count = cl_used = 0;
}

/* ------------------------------------------------- objects and sinks */
/* World-space triangles from the shared scene code (ports, boxes, forests,
   far-ship tetrahedra, blast fragments) arrive here while a Classic frame
   is built. Each is its own node, sorted by centroid. cl_bias pulls a whole
   object forward (buildings over their own ground); cl_emit adds emission
   order, so decals drawn after their wall stay in front of it. */
/* Shared vertices (a box corner serves four or five triangles) are
   transformed once a frame, as Frontier transformed each model vertex
   once: a direct-mapped cache from world point to camera point, outcode
   and projection. A triangle inside the guard band then goes straight to
   the draw list with no clipping. */
static void cl_vertex_get(Vec w, ClVertex *out) {
    unsigned h = ((uint32_t)w.x * 0x9e3779b1U ^ (uint32_t)w.y * 0x85ebca6bU ^ (uint32_t)w.z * 0xc2b2ae35U) >> 23;
    if (cl_vertex[h].frame != cl_vertex_frame || cl_vertex[h].w.x != w.x || cl_vertex[h].w.y != w.y || cl_vertex[h].w.z != w.z) {
        cl_vertex[h].frame = cl_vertex_frame; cl_vertex[h].w = w;
        cl_vertex[h].ok = (uint8_t)cl_world(w, &cl_vertex[h].v);
        cl_vertex[h].outcode = cl_vertex[h].ok ? (uint8_t)cl_outcode(&cl_vertex[h].v) : 0;
        if (cl_vertex[h].ok && !cl_vertex[h].outcode && !cl_project(&cl_vertex[h].v, &cl_vertex[h].x, &cl_vertex[h].y))
            cl_vertex[h].outcode = 1;
    }
    /* Copied out at once: the next vertex may take the same slot. */
    out->v = cl_vertex[h].v; out->x = cl_vertex[h].x; out->y = cl_vertex[h].y;
    out->ok = cl_vertex[h].ok; out->outcode = cl_vertex[h].outcode;
}
/* A Q8 point, transformed afresh rather than cached by whole metre. */
static void cl_vertex_q8(Vec w, ClVertex *out) {
    out->ok = (uint8_t)cl_world_q8(w, &out->v);
    out->outcode = out->ok ? (uint8_t)cl_outcode(&out->v) : 0;
    if (out->ok && !out->outcode && !cl_project(&out->v, &out->x, &out->y)) out->outcode = 1;
}
static void cl_triangle_vertices(const ClVertex *a, const ClVertex *b, const ClVertex *c, int colour) {
    ClV v[16];
    ClVertex t[3];
    int i;
    t[0] = *a; t[1] = *b; t[2] = *c;
    for (i = 0; i < 3; i++) if (!t[i].ok) return;
    for (i = 0; i < 3; i++) v[i] = t[i].v;
    if (v[0].z < CL_NEAR && v[1].z < CL_NEAR && v[2].z < CL_NEAR) return;
    if (!(t[0].outcode | t[1].outcode | t[2].outcode)) {
        /* Inside the band: cl_polygon's work, already done per vertex. */
        int64_t zsum = (int64_t)v[0].z + v[1].z + v[2].z;
        ClNode *node;
        int32_t *xy;
        zsum = zsum < 0x7fffffffLL ? (int32_t)zsum / 3 : zsum / 3;
        node = cl_node(cl_key(zsum - (cl_bias + cl_emit++ * 2)), CL_POLY, colour, 6);
        if (!node) return;
        xy = cl_pool + node->data;
        for (i = 0; i < 3; i++) { xy[i * 2] = t[i].x; xy[i * 2 + 1] = t[i].y; }
        node->n = 3;
        return;
    }
    cl_polygon(v, 3, colour, cl_bias + cl_emit++ * 2);
}
static void cl_triangle_world(Vec a, Vec b, Vec c, int colour) {
    ClVertex t[3];
    cl_vertex_get(a, &t[0]); cl_vertex_get(b, &t[1]); cl_vertex_get(c, &t[2]);
    cl_triangle_vertices(&t[0], &t[1], &t[2], colour);
}
static uint32_t cl_isqrt(uint32_t n) {
    uint32_t r = 0, b = 1U << 30;
    while (b > n) b >>= 2;
    while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
/* Flat face light in Q14 from an unnormalised camera-space normal. */
static int cl_face_light(int64_t nx, int64_t ny, int64_t nz, const int32_t *light) {
    int64_t m = llabs(nx) | llabs(ny) | llabs(nz);
    int32_t x, y, z, len;
    while (m >= 16384) { nx = floor_shift(nx, 1); ny = floor_shift(ny, 1); nz = floor_shift(nz, 1); m >>= 1; }
    x = (int32_t)nx; y = (int32_t)ny; z = (int32_t)nz;
    len = (int32_t)cl_isqrt((uint32_t)(x * x + y * y + z * z));
    if (!len) return 0;
    return (x * light[0] + y * light[1] + z * light[2]) / len;
}
/* One loft face: oriented away from the loft's axis, culled when it faces
   away from the eye (the origin), lit flat. */
static void cl_ship_face(const ClV *face, int count, const ClV *inside, int tone, const int32_t *lc) {
    ClV poly[16];
    int64_t ax, ay, az, bx, by, bz, nx, ny, nz, mx = 0, my = 0, mz = 0;
    int k, d;
    for (k = 0; k < count; k++) { poly[k] = face[k]; mx += face[k].x; my += face[k].y; mz += face[k].z; }
    mx /= count; my /= count; mz /= count;
    ax = poly[2].x - poly[0].x; ay = poly[2].y - poly[0].y; az = poly[2].z - poly[0].z;
    bx = poly[count - 1].x - poly[1].x; by = poly[count - 1].y - poly[1].y; bz = poly[count - 1].z - poly[1].z;
    nx = ay * bz - az * by; ny = az * bx - ax * bz; nz = ax * by - ay * bx;
    if (!nx && !ny && !nz) return;
    if (nx * (mx - inside->x) + ny * (my - inside->y) + nz * (mz - inside->z) < 0) { nx = -nx; ny = -ny; nz = -nz; }
    if (nx * mx + ny * my + nz * mz >= 0) return;
    d = cl_face_light(nx, ny, nz, lc);
    cl_polygon(poly, count, ship_colour(tone, d > 0 ? 3 + d * 12 / 16384 : 3 + d * 2 / 16384), 0);
}
/* Ships from their lofts, as Frontier drew models: the origin transforms
   once; every model point is origin + its coordinates times per-ship
   camera-space axis coefficients; faces are back-face culled and lit
   flat. Each visible face is its own node. */
static void cl_ship(int ship, Vec origin, Vec right, Vec up, Vec front, Vec light) {
    const ShipSpec *spec = &ship_specs[ship];
    const Vec axes[3] = {right, up, front};
    const int extent[3] = {spec->width, spec->height, spec->depth};
    int64_t coef[3][3];
    int32_t lc[3];
    ClV o;
    int a, c, i, side, reach = spec->width + spec->height + spec->depth;
    if (!cl_world_q8(origin, &o)) return;
    if (o.z + reach * 16 < CL_NEAR) return;
    if (llabs(o.x) - reach * 16 > (int64_t)CL_GUARD * o.z || llabs(o.y) - reach * 16 > (int64_t)CL_GUARD * o.z) return;
    /* coef: camera-space Q4 per model unit, Q18 (model units are 1/256 of
       the extent; Q14 axes). */
    for (a = 0; a < 3; a++)
        for (c = 0; c < 3; c++)
            coef[a][c] = (int64_t)extent[a] * (((int32_t)cl.m[c][0] * axes[a].x + (int32_t)cl.m[c][1] * axes[a].y +
                                                (int32_t)cl.m[c][2] * axes[a].z) >> 14);
    for (c = 0; c < 3; c++)
        lc[c] = ((int32_t)cl.m[c][0] * light.x + (int32_t)cl.m[c][1] * light.y + (int32_t)cl.m[c][2] * light.z) >> 14;
    for (i = 0; i < (graphics.ships ? SHIP_LOFTS : 1); i++) {
        const ShipLoft *l = &ship_lofts[ship][i];
        const int8_t (*profile)[2] = ship_profile[l->profile];
        int n = ship_profile_points[l->profile], s, p, last = l->count - 1;
        if (!l->count) continue;
        for (side = 1; side >= (l->flags & LOFT_MIRROR ? -1 : 1); side -= 2) {
            ClV ring[4][8], axis[4], centre = {0, 0, 0};
            for (s = 0; s < l->count; s++) {
                const ShipStation *st = &l->s[s];
                for (p = 0; p <= n; p++) {
                    int32_t mx = side * (l->x * 16 + (p < n ? profile[p][0] * st->w : 0));
                    int32_t my = st->y * 16 + (p < n ? profile[p][1] * st->h : 0), mz = st->z * 16;
                    ClV *v = p < n ? &ring[s][p] : &axis[s];
                    v->x = o.x + (int32_t)((mx * coef[0][0] + my * coef[1][0] + mz * coef[2][0]) >> 18);
                    v->y = o.y + (int32_t)((mx * coef[0][1] + my * coef[1][1] + mz * coef[2][1]) >> 18);
                    v->z = o.z + (int32_t)((mx * coef[0][2] + my * coef[1][2] + mz * coef[2][2]) >> 18);
                }
                centre.x += axis[s].x / l->count; centre.y += axis[s].y / l->count; centre.z += axis[s].z / l->count;
            }
            for (s = 0; s < last; s++) {
                ClV inside;
                inside.x = (axis[s].x + axis[s + 1].x) / 2; inside.y = (axis[s].y + axis[s + 1].y) / 2;
                inside.z = (axis[s].z + axis[s + 1].z) / 2;
                for (p = 0; p < n; p++) {
                    ClV quad[4];
                    int q = (p + 1) % n;
                    quad[0] = ring[s][p]; quad[1] = ring[s][q]; quad[2] = ring[s + 1][q]; quad[3] = ring[s + 1][p];
                    cl_ship_face(quad, 4, &inside, l->tone, lc);
                }
            }
            cl_ship_face(ring[0], n, &centre, l->tone, lc);
            if (last > 0) cl_ship_face(ring[last], n, &centre, l->flags & LOFT_EXHAUST ? GLOW_TONE : l->tone, lc);
        }
    }
}
/* Lines from world space: clipped in camera space against the near plane
   and the guard band, like polygons, so far endpoints never bend them. */
static void cl_line_world(Vec a, Vec b, int colour, int thick) {
    ClV v[2];
    ClNode *node;
    int32_t *d;
    int plane;
    if (!cl_world(a, &v[0]) || !cl_world(b, &v[1])) return;
    for (plane = 0; plane < 5; plane++) {
        int64_t da = cl_plane(&v[0], plane), db = cl_plane(&v[1], plane);
        if (da < 0 && db < 0) return;
        if ((da < 0) != (db < 0)) {
            ClV *p = da < 0 ? &v[0] : &v[1], q = da < 0 ? v[1] : v[0];
            int64_t dp = da < 0 ? da : db, dq = da < 0 ? db : da;
            p->x = (int32_t)(p->x + (int64_t)(q.x - p->x) * dp / (dp - dq));
            p->y = (int32_t)(p->y + (int64_t)(q.y - p->y) * dp / (dp - dq));
            p->z = (int32_t)(p->z + (int64_t)(q.z - p->z) * dp / (dp - dq));
        }
    }
    node = cl_node(cl_key((v[0].z + (int64_t)v[1].z) / 2), CL_LINE, colour, 4);
    if (!node) return;
    d = cl_pool + node->data;
    if (!cl_project(&v[0], &d[0], &d[1]) || !cl_project(&v[1], &d[2], &d[3])) { cl_count--; cl_used -= 4; return; }
    /* Close beams gain a white second row so they read as bolts. */
    node->extra = (uint8_t)(thick && (v[0].z < 900 * 16 || v[1].z < 900 * 16));
}
static void cl_lights(int first) {
    int i;
    for (i = first; i < port_light_count; i++) {
        ClV v;
        ClNode *node;
        int32_t px, py, *d;
        if (!cl_world(port_lights[i].p, &v) || v.z > 16000 * 16 || !cl_project(&v, &px, &py)) continue;
        node = cl_node(cl_key(v.z - 64 * 16), v.z < 4000 * 16 ? CL_DISC : CL_PIXEL, port_lights[i].color, 3);
        if (!node) return;
        d = cl_pool + node->data;
        d[0] = px >> 4; d[1] = py >> 4; d[2] = v.z < 1500 * 16 ? 1 : 0;
    }
}
/* Explosion and smoke sprites: Frontier drew them as filled circles. */
static void cl_sprite(const DestructionSprite *s) {
    ClV v;
    ClNode *node;
    int32_t px, py, *d;
    v.x = (int32_t)(s->p.x * 16); v.y = (int32_t)(s->p.y * 16); v.z = (int32_t)(s->p.z * 16);
    if (s->p.z <= 4 || s->p.z > 200000 || !cl_project(&v, &px, &py)) return;
    node = cl_node(cl_key(v.z), CL_SPRITE, s->fire, 5);
    if (!node) return;
    d = cl_pool + node->data;
    d[0] = px >> 4; d[1] = py >> 4;
    d[2] = (int32_t)((int64_t)s->radius * cl.focal / s->p.z);
    if (d[2] < 1) d[2] = 1;
    if (d[2] > frame->w) d[2] = frame->w;
    d[3] = s->age; d[4] = (int32_t)s->seed;
}
static void cl_draw_sprite(const ClNode *node) {
    const int32_t *d = cl_pool + node->data;
    int x = d[0], y = d[1], r = d[2], age = d[3];
    if (node->colour) {
        /* Fire: concentric discs cooling outwards and with age. */
        cl_disc(&cl.surface, x, y, r, age < 6 ? 14 : 23);
        if (r > 2) cl_disc(&cl.surface, x, y, r * 2 / 3, age < 10 ? 12 : 14);
        if (r > 4 && age < 12) cl_disc(&cl.surface, x, y, r / 3, age < 4 ? 31 : 15);
    } else {
        /* Smoke thins by dithering out as it ages. */
        int density = 16 - age * 12 / SMOKE_TICKS;
        cl_glow(&cl.surface, x, y, 0, r, (d[4] & 1) ? 16 : 19, density < 2 ? 2 : density);
    }
}
static void cl_draw_ball(const ClNode *node) {
    /* A shaded ball (Frontier's two-shape terminator): the shadowed disc,
       then the lit disc shifted towards the light. */
    const int32_t *d = cl_pool + node->data;
    int r = d[2], dx = d[3], dy = d[4];
    cl_disc(&cl.surface, d[0], d[1], r, d[5]);
    if (r > 1) cl_disc(&cl.surface, d[0] + dx * r / 4096, d[1] + dy * r / 4096, r * 3 / 4, node->colour);
}
