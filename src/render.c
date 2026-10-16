/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "ui.h"
#include "galaxy.h"
#include "solar.h"
#include "fpu.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define TRI_MAX 4096
typedef struct {
    int x[3], y[3];
    uint32_t inverse_z[3];
    int64_t z;
    uint8_t color;
    const Planet *surface;
    int64_t surface_depth[3];
    int surface_x[3], surface_y[3]; /* Q8 projected vertices for colour mapping */
    int8_t slope; /* Geometric illumination for distant resolved landforms. */
} Tri;
static Tri triangles[TRI_MAX];
static int nt;
static const Planet *terrain_surface;
static int surface_detail(const Planet *p, int face, int u, int v, uint32_t depth, int footprint, int slope);
static Vec terrain_fixed_vertex(const Planet *p, int face, int u, int v);
/* Reciprocal depth is linear in screen space. Keep the buffer in ordinary
   RAM; only the final indexed image is copied to Amiga display memory. */
static uint32_t *depth_buffer;
static size_t depth_capacity;
static Frame *frame;
static const Game *game;
static int view_h;
static int draw_scale = 1;
static Vec camera_position;
static int camera_yaw, camera_pitch, camera_roll;
/* A sub-viewport for inset scenes: the projection centre moves by proj_dx/dy
   from the frame centre and rasterisation is scissored to the view rectangle.
   All zero (the full frame) except while an inset is being drawn. */
static int proj_dx, proj_dy, view_left, view_top, view_right_inset;
/* Q4 coordinates retain fractional ray hits between fixed mesh vertices. */
#ifndef ASTRALBOUND_FPU
static Vec camera_to_local(const Planet *p, Vec v) {
    int sy = sini(camera_yaw), cy = cosi(camera_yaw);
    int sp = sini(camera_pitch), cp = cosi(camera_pitch);
    if (camera_roll) {
        int cr = cosi(camera_roll), sr = sini(camera_roll);
        v = vec((v.x * cr + v.y * sr) / 16384,
                (v.y * cr - v.x * sr) / 16384, v.z);
    }
    int64_t y = (v.y * cp + v.z * sp) / 16384;
    int64_t z = (v.z * cp - v.y * sp) / 16384;
    Vec world = vec((v.x * cy + z * sy) / 16384, y, (z * cy - v.x * sy) / 16384);
    return body_rotate(p, add(world, sub(scale(camera_position, 1, FP / 16), scale(p->center, 16, 1))), 1);
}
#endif

static void (*platform_pump)(void);
static uint32_t plane_low[256], plane_high[256];
static int tables_ready;
static Vec globe_vertices[GENERATED_PLANETS][17][33];
static uint32_t globe_keys[GENERATED_PLANETS];
static int globe_valid[GENERATED_PLANETS];

static int globe_enhanced;

void render_set_pump(void (*pump)(void)) {
    platform_pump = pump;
}
static void plane_tables(void) {
    int i;
    for (i = 0; i < 256; i++) {
        unsigned n = (unsigned)i;
        plane_low[i] = (n & 1) | ((n & 2) << 7) | ((n & 4) << 14) | ((n & 8) << 21);
        n >>= 4;
        plane_high[i] = (n & 1) | ((n & 2) << 7) | ((n & 4) << 14) | ((n & 8) << 21);
    }
    tables_ready = 1;
}
static const uint8_t glyphs[][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
static void pixel(int x, int y, int c) {
    int xx, yy;
    x *= draw_scale; y *= draw_scale;
    for (yy=0; yy<draw_scale; yy++) for(xx=0;xx<draw_scale;xx++)
        if(x+xx>=0 && y+yy>=0 && x+xx<frame->w && y+yy<frame->h)
            frame->pixels[(y+yy)*frame->w+x+xx]=(uint8_t)c;
}
static void rect(int x, int y, int w, int h, int c) {
    int yy;
    x *= draw_scale; y *= draw_scale; w *= draw_scale; h *= draw_scale;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > frame->w)
        w = frame->w - x;
    if (y + h > frame->h)
        h = frame->h - y;
    if (w <= 0 || h <= 0)
        return;
    for (yy = y; yy < y + h; yy++)
        memset(frame->pixels + yy * frame->w + x, c, (size_t)w);
}
static void text(int x, int y, int c, const char *s) {
    int i, j;
    while (*s) {
        char ch = *s++;
        const uint8_t *p = 0;
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (ch >= 'A' && ch <= 'Z')
            p = glyphs[ch - 'A'];
        else if (ch >= '0' && ch <= '9')
            p = glyphs[26 + ch - '0'];
        else if (ch == '%') {
            static const uint8_t percent[7] = {24, 25, 2, 4, 8, 19, 3};
            p = percent;
        }
        if (p) {
            for (j = 0; j < 7; j++)
                for (i = 0; i < 5; i++)
                    if (p[j] & (16 >> i))
                        pixel(x + i, y + j, c);
        } else if (ch == '-')
            rect(x, y + 3, 5, 1, c);
        else if (ch == '.')
            pixel(x + 2, y + 6, c);
        else if (ch == ':') {
            pixel(x + 2, y + 2, c);
            pixel(x + 2, y + 5, c);
        } else if (ch == '/')
            for (j = 0; j < 5; j++)
                pixel(x + 4 - j, y + 1 + j, c);
        else if (ch == '+') {
            rect(x, y + 3, 5, 1, c);
            rect(x + 2, y + 1, 1, 5, c);
        } else if(ch=='<') {
            pixel(x+3,y+1,c);pixel(x+2,y+2,c);pixel(x+1,y+3,c);pixel(x+2,y+4,c);pixel(x+3,y+5,c);
        } else if (ch == '>') {
            pixel(x + 1, y + 1, c);
            pixel(x + 2, y + 2, c);
            pixel(x + 3, y + 3, c);
            pixel(x + 2, y + 4, c);
            pixel(x + 1, y + 5, c);
        }
        x += 6;
    }
}
static void line(int x, int y, int xx, int yy, int c) {
    int dx = abs(xx - x), sx = x < xx ? 1 : -1, dy = -abs(yy - y), sy = y < yy ? 1 : -1,
        e = dx + dy, e2, n = 0;
    while (n++ < 4096) {
        pixel(x, y, c);
        if (x == xx && y == yy)
            break;
        e2 = e * 2;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
}
static Vec camera(Vec p) {
    Vec r = sub(p, scale(camera_position, 1, FP));
    int sy = sini(camera_yaw), cy = cosi(camera_yaw), sp = sini(camera_pitch),
        cp = cosi(camera_pitch);
    int64_t x = (r.x * cy - r.z * sy) / 16384, z = (r.x * sy + r.z * cy) / 16384;
    int64_t y = (r.y * cp - z * sp) / 16384;
    int64_t depth = (r.y * sp + z * cp) / 16384;
    if (camera_roll) {
        int cr = cosi(camera_roll), sr = sini(camera_roll);
        return vec((x * cr - y * sr) / 16384, (y * cr + x * sr) / 16384, depth);
    }
    return vec(x, y, depth);
}
static int screen_coord(int64_t value) {
    return value < -32760 ? -32760 : value > 32760 ? 32760 : (int)value;
}
/* Marker range switches unit so docking approaches read in metres and
   interplanetary legs stay short enough to sit beside the box. */
static void target_range_text(int64_t metres, char *s, size_t n) {
    if (metres < 10000)
        snprintf(s, n, "%ld M", (long)metres);
    else if (metres < AU_METRES / 10)
        snprintf(s, n, "%ld KM", (long)(metres / 1000));
    else
        snprintf(s, n, "%ld.%02ld AU", (long)(metres / AU_METRES),
                 (long)(metres % AU_METRES * 100 / AU_METRES));
}
/* Integer formatting keeps the 68k HUD independent of floating point. */
static void speed_text(int64_t speed, char *s, size_t n) {
    if (speed >= LIGHT_SPEED / 100) {
        int64_t hundredths = (speed * 100 + LIGHT_SPEED / 2) / LIGHT_SPEED;
        snprintf(s, n, "V %ld.%02ld C", (long)(hundredths / 100),
                 (long)(hundredths % 100));
    } else
        snprintf(s, n, "V %ld %s", (long)(speed >= 100000 ? speed / 1000 : speed),
                 speed >= 100000 ? "KM/S" : "M/S");
}
/* Keep locked targets inside the flight viewport. Rear targets use their
   camera-space bearing too, so perspective cannot mirror them across the HUD. */
static void target_marker_position(Vec p, int top, int *x, int *y) {
    int cx = frame->w / 2, cy = view_h / 2;
    int left = 10, right = frame->w - 11, bottom = view_h - 32;
    int64_t dx = p.x, dy = -p.y, ox, oy;
    if (p.z > 0) {
        ox = dx * (frame->w / 2) / p.z;
        oy = dy * (frame->w / 2) / p.z;
        if (cx + ox >= left && cx + ox <= right && cy + oy >= top && cy + oy <= bottom) {
            *x = cx + (int)ox;
            *y = cy + (int)oy;
            return;
        }
    }
    /* Exactly astern has no unique turn direction; consistently choose right.
       A coincident target belongs at the centre rather than disappearing. */
    if (!dx && !dy && p.z >= 0) {
        *x = cx;
        *y = cy;
        return;
    }
    if (!dx && !dy)
        dx = 1;
    if (dx) {
        ox = dx > 0 ? right - cx : left - cx;
        oy = dy * ox / dx;
        if (cy + oy < top || cy + oy > bottom) {
            oy = dy > 0 ? bottom - cy : top - cy;
            ox = dx * oy / dy;
        }
    } else {
        ox = 0;
        oy = dy > 0 ? bottom - cy : top - cy;
    }
    *x = cx + (int)ox;
    *y = cy + (int)oy;
}
static void emit(Vec a, Vec b, Vec c, int color, int slope) {
    Tri *t;
    Vec v[3] = {a, b, c};
    int i;
    if (nt >= TRI_MAX)
        return;
    t = &triangles[nt];
    t->surface = terrain_surface;
    t->slope = (int8_t)slope;
#ifdef ASTRALBOUND_FPU
    /* One reciprocal replaces six 64-bit divisions per vertex. Clipping
       guarantees z >= 8, so every projected quantity stays finite. */
    for (i = 0; i < 3; i++) {
        double reciprocal = 1.0 / fpu_double(v[i].z);
        double px = fpu_double(v[i].x) * reciprocal, py = fpu_double(v[i].y) * reciprocal;
        t->inverse_z[i] = (uint32_t)(int32_t)(1073741824.0 * reciprocal);
        t->x[i] = screen_coord(frame->w / 2 + proj_dx + fpu_int(px * (frame->w / 2), 65536));
        t->y[i] = screen_coord(view_h / 2 + proj_dy - fpu_int(py * (frame->w / 2), 65536));
        if (t->surface) {
            t->surface_depth[i] = fpu_int64(274877906944.0 * reciprocal);
            t->surface_x[i] = frame->w * 128 + fpu_int(px * (frame->w * 128), 1 << 30);
            t->surface_y[i] = (view_h / 2) * 256 - fpu_int(py * (frame->w * 128), 1 << 30);
        }
    }
#else
    for (i = 0; i < 3; i++) {
        t->inverse_z[i] = (uint32_t)(1073741824LL / v[i].z);
        t->x[i] = screen_coord(frame->w / 2 + proj_dx + v[i].x * (frame->w / 2) / v[i].z);
        t->y[i] = screen_coord(view_h / 2 + proj_dy - v[i].y * (frame->w / 2) / v[i].z);
        if (t->surface) {
            t->surface_depth[i] = 274877906944LL / v[i].z;
            t->surface_x[i] = frame->w * 128 + (int)(v[i].x * (frame->w * 128) / v[i].z);
            t->surface_y[i] = (view_h / 2) * 256 - (int)(v[i].y * (frame->w * 128) / v[i].z);
        }
    }
#endif
    if ((t->x[0] < 0 && t->x[1] < 0 && t->x[2] < 0) ||
        (t->x[0] >= frame->w && t->x[1] >= frame->w && t->x[2] >= frame->w) ||
        (t->y[0] < 0 && t->y[1] < 0 && t->y[2] < 0) ||
        (t->y[0] >= view_h && t->y[1] >= view_h && t->y[2] >= view_h))
        return;
    t->z = (a.z + b.z + c.z) / 3;
    t->color = (uint8_t)color;
    nt++;
}
/* Clip in camera space before projection. Clamping projected coordinates
   bends the reciprocal-depth plane of large nearby terrain faces. */
static int64_t clip_distance(Vec v, int plane) {
    switch (plane) {
    case 0: return v.z - 8;
    case 1: return v.z + v.x;
    case 2: return v.z - v.x;
    case 3: return v.z * (view_h / 2) - v.y * (frame->w / 2);
    default: return v.z * (view_h - view_h / 2) + v.y * (frame->w / 2);
    }
}
static Vec cross(Vec a, Vec b);
#ifdef ASTRALBOUND_FPU
/* Same Q14 facet lighting as the integer path, with one square root per
   direction instead of length/unit chains of 64-bit divisions. */
static void fpu_unit(Vec v, double *out) {
    double x = fpu_double(v.x), y = fpu_double(v.y), z = fpu_double(v.z);
    double l = fpu_sqrt(x * x + y * y + z * z);
    l = l > 0 ? 1.0 / l : 0;
    out[0] = x * l; out[1] = y * l; out[2] = z * l;
}
static int terrain_face_slope(const Planet *p, Vec a, Vec b, Vec c) {
    Vec local = sub(a,p->center);
    double up[3], e1[3], e2[3], normal[3], sun[3], l;
    int i, slope=0;
    if (length(local) < p->radius || !surface_light(&game->system,p,local)) return 0;
    fpu_unit(local, up);
    fpu_unit(sub(b,a), e1);
    fpu_unit(sub(c,a), e2);
    normal[0] = e1[1] * e2[2] - e1[2] * e2[1];
    normal[1] = e1[2] * e2[0] - e1[0] * e2[2];
    normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
    l = fpu_sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (normal[0] * up[0] + normal[1] * up[1] + normal[2] * up[2] < 0) l = -l;
    l = l ? 1.0 / l : 0;
    for (i = 0; i < 3; i++) normal[i] = normal[i] * l - up[i];
    for (i=0;i<game->system.stars;i++) {
        fpu_unit(sub(game->system.star[i].center,a), sun);
        slope+=(int)(16384.0 * (normal[0]*sun[0]+normal[1]*sun[1]+normal[2]*sun[2]));
    }
    slope=slope*14/16384;
    return slope < -12 ? -12 : slope > 12 ? 12 : slope;
}
#else
static int terrain_face_slope(const Planet *p, Vec a, Vec b, Vec c) {
    Vec local = sub(a,p->center), up, normal, delta;
    int i, slope=0;
    if (length(local) < p->radius || !surface_light(&game->system,p,local)) return 0;
    up=unit(local,16384);
    normal=unit(cross(unit(sub(b,a),16384),unit(sub(c,a),16384)),16384);
    if (normal.x*up.x+normal.y*up.y+normal.z*up.z < 0) normal=scale(normal,-1,1);
    delta=sub(normal,up);
    for (i=0;i<game->system.stars;i++) {
        Vec sun=unit(sub(game->system.star[i].center,a),16384);
        slope+=(int)((delta.x*sun.x+delta.y*sun.y+delta.z*sun.z)/16384);
    }
    slope=slope*14/16384;
    return slope < -12 ? -12 : slope > 12 ? 12 : slope;
}
#endif
static void triangle(Vec a, Vec b, Vec c, int color) {
    Vec vertices[2][10];
    int plane, i, count = 3, current = 0;
    vertices[0][0] = camera(a);
    vertices[0][1] = camera(b);
    vertices[0][2] = camera(c);
    for (plane = 0; plane < 5 && count >= 3; plane++) {
        Vec *in = vertices[current], *out = vertices[1 - current];
        int n = 0;
        for (i = 0; i < count; i++) {
            Vec p = in[i], q = in[(i + 1) % count];
            int64_t dp = clip_distance(p, plane), dq = clip_distance(q, plane);
            if (dp >= 0)
                out[n++] = p;
            if ((dp >= 0) != (dq >= 0))
                out[n++] = add(p, scale(sub(q, p), dp, dp - dq));
        }
        count = n;
        current = 1 - current;
    }
    if (count >= 3) {
        int slope=terrain_surface ? terrain_face_slope(terrain_surface,a,b,c) : 0;
        for (i = 1; i + 1 < count; i++)
            emit(vertices[current][0], vertices[current][i], vertices[current][i + 1], color, slope);
    }
}
static int sort_tri(const void *a, const void *b) {
    const Tri *x = a, *y = b;
    /* Opaque depth-tested geometry can draw nearest first, avoiding surface
       lookups for the coarse globe and far faces hidden by local terrain. */
    return x->z < y->z ? -1 : x->z > y->z ? 1 : 0;
}
/* Map a body-fixed surface point to the planet's fixed metre lattice. */
static void surface_uv(const Planet *p, const int64_t *value, int *face, int *u, int *v) {
    Vec q = vec(value[0], value[1], value[2]);
    int axis = 0;
    int64_t dominant = llabs(q.x);
    if (llabs(q.y) > dominant) { axis = 1; dominant = llabs(q.y); }
    if (llabs(q.z) > dominant) { axis = 2; dominant = llabs(q.z); }
    *face = axis * 2 + ((axis == 0 ? q.x : axis == 1 ? q.y : q.z) < 0);
    if (!dominant) { *u = *v = 0; return; }
    q = scale(q, p->radius, dominant);
    *u = (int)(axis == 0 ? q.y : q.x);
    *v = (int)(axis == 2 ? q.y : q.z);
}
/* Reconstruct the hit from the pixel ray and subpixel depth. Rounded polygon
   vertices must not move the colour plane independently of the camera ray. */
#ifdef ASTRALBOUND_FPU
/* camera_to_local folded into one matrix: camera roll/pitch/yaw, then the
   inverse body spin, applied to the Q4 ray and planet offset. It is rebuilt
   only when the camera or body changes, normally once per frame. */
static struct {
    const Planet *planet;
    Vec camera, center;
    int yaw, pitch, roll;
    int32_t spin_sin, spin_cos, radius;
    double m[3][3], t[3];
} ray;
static void fpu_camera_to_world(const double in[3], double out[3]) {
    double sy = sini(camera_yaw) / 16384.0, cy = cosi(camera_yaw) / 16384.0;
    double sp = sini(camera_pitch) / 16384.0, cp = cosi(camera_pitch) / 16384.0;
    double x = in[0], y = in[1], z = in[2], yy, zz;
    if (camera_roll) {
        double cr = cosi(camera_roll) / 16384.0, sr = sini(camera_roll) / 16384.0;
        double rx = x * cr + y * sr;
        y = y * cr - x * sr;
        x = rx;
    }
    yy = y * cp + z * sp;
    zz = z * cp - y * sp;
    out[0] = x * cy + zz * sy;
    out[1] = yy;
    out[2] = zz * cy - x * sy;
}
static void fpu_body_inverse(const Planet *p, double v[3]) {
    double sn = -p->spin_sin / 1073741824.0, cs = p->spin_cos / 1073741824.0, x;
    if (!p->spin_sin && !p->spin_cos) return;
    x = v[0] * cs - v[1] * sn;
    v[1] = v[0] * sn + v[1] * cs;
    v[0] = x;
}
static void ray_prepare(const Planet *p) {
    Vec offset = sub(scale(camera_position, 1, FP / 16), scale(p->center, 16, 1));
    int i, j;
    for (i = 0; i < 3; i++) {
        double basis[3] = {i == 0, i == 1, i == 2}, column[3];
        fpu_camera_to_world(basis, column);
        fpu_body_inverse(p, column);
        for (j = 0; j < 3; j++) ray.m[j][i] = column[j];
    }
    ray.t[0] = fpu_double(offset.x);
    ray.t[1] = fpu_double(offset.y);
    ray.t[2] = fpu_double(offset.z);
    fpu_body_inverse(p, ray.t);
    ray.planet = p; ray.camera = camera_position; ray.center = p->center;
    ray.yaw = camera_yaw; ray.pitch = camera_pitch; ray.roll = camera_roll;
    ray.spin_sin = p->spin_sin; ray.spin_cos = p->spin_cos; ray.radius = p->radius;
}
/* Q4 camera-space point to Q4 body-fixed planet coordinates. */
static void ray_local(const Planet *p, double x, double y, double z, double q[3]) {
    int i;
    if (ray.planet != p || ray.yaw != camera_yaw || ray.pitch != camera_pitch ||
        ray.roll != camera_roll || ray.spin_sin != p->spin_sin || ray.spin_cos != p->spin_cos ||
        ray.radius != p->radius || ray.camera.x != camera_position.x ||
        ray.camera.y != camera_position.y || ray.camera.z != camera_position.z ||
        ray.center.x != p->center.x || ray.center.y != p->center.y || ray.center.z != p->center.z)
        ray_prepare(p);
    for (i = 0; i < 3; i++)
        q[i] = ray.m[i][0] * x + ray.m[i][1] * y + ray.m[i][2] * z + ray.t[i];
}
static void surface_ray_uv(const Planet *p, int x, int y, int64_t depth,
                           int *face, int *u, int *v) {
    double z, k, q[3], dominant, s;
    int axis = 0;
    z = depth > 0 ? 4398046511104.0 / fpu_double(depth) : 1;
    k = z / (frame->w / 2);
    ray_local(p, (x - frame->w / 2) * k, (view_h / 2 - y) * k, z, q);
    dominant = __builtin_fabs(q[0]);
    if (__builtin_fabs(q[1]) > dominant) { axis = 1; dominant = __builtin_fabs(q[1]); }
    if (__builtin_fabs(q[2]) > dominant) { axis = 2; dominant = __builtin_fabs(q[2]); }
    *face = axis * 2 + (q[axis] < 0);
    if (dominant < 1) { *u = *v = 0; return; }
    s = p->radius / dominant;
    *u = (int)((axis == 0 ? q[1] : q[0]) * s);
    *v = (int)((axis == 2 ? q[1] : q[2]) * s);
}
#else
static void surface_ray_uv(const Planet *p, int x, int y, int64_t depth,
                           int *face, int *u, int *v) {
    int64_t z = depth > 0 ? 4398046511104LL / depth : 1;
    Vec point = vec((x - frame->w / 2) * z / (frame->w / 2),
                    (view_h / 2 - y) * z / (frame->w / 2), z);
    Vec local = camera_to_local(p, point);
    int64_t value[3] = {local.x, local.y, local.z};
    surface_uv(p, value, face, u, v);
}
#endif
static void raster(const Tri *t) {
    int y, miny = t->y[0], maxy = t->y[0], i;
    int64_t ax = t->x[1] - t->x[0], ay = t->y[1] - t->y[0];
    int64_t bx = t->x[2] - t->x[0], by = t->y[2] - t->y[0];
    int64_t az = (int64_t)t->inverse_z[1] - t->inverse_z[0];
    int64_t bz = (int64_t)t->inverse_z[2] - t->inverse_z[0];
    int64_t area = ax * by - bx * ay;
    int64_t zx = az * by - bz * ay, zy = ax * bz - bx * az;
    int64_t dz;
    int64_t depth_x = 0, depth_y = 0;
    if (!area)
        return;
    dz = zx * 256 / area;
    if (t->surface) {
        int64_t ax = t->surface_x[1] - t->surface_x[0];
        int64_t ay = t->surface_y[1] - t->surface_y[0];
        int64_t bx = t->surface_x[2] - t->surface_x[0];
        int64_t by = t->surface_y[2] - t->surface_y[0];
        int64_t surface_area = ax * by - bx * ay;
        if (!surface_area) return;
        {
            int64_t a = t->surface_depth[1] - t->surface_depth[0];
            int64_t b = t->surface_depth[2] - t->surface_depth[0];
            depth_x = (a * by - b * ay) * 256 / surface_area;
            depth_y = (ax * b - bx * a) * 256 / surface_area;
            dz = depth_x;
        }
    }
    for (i = 1; i < 3; i++) {
        if (t->y[i] < miny)
            miny = t->y[i];
        if (t->y[i] > maxy)
            maxy = t->y[i];
    }
    if (miny < view_top)
        miny = view_top;
    if (maxy >= view_h)
        maxy = view_h - 1;
    for (y = miny; y <= maxy; y++) {
        int xs[3], n = 0, j;
        for (i = 0; i < 3; i++) {
            j = (i + 1) % 3;
            if ((t->y[i] <= y && t->y[j] > y) || (t->y[j] <= y && t->y[i] > y)) {
                int dx = t->x[j] - t->x[i];
                /* Clamped coordinates keep |y - y[i]| below 65536; a 32-bit
                   product is exact unless a clipped edge spans 32768 pixels. */
                xs[n] = t->x[i] + (abs(dx) < 32768
                    ? (y - t->y[i]) * dx / (t->y[j] - t->y[i])
                    : (int)((int64_t)(y - t->y[i]) * dx / (t->y[j] - t->y[i])));
                n++;
            }
        }
        if (n >= 2) {
            int l = xs[0] < xs[1] ? xs[0] : xs[1], r = xs[0] > xs[1] ? xs[0] : xs[1];
            int64_t z;
            if (l < view_left)
                l = view_left;
            /* Evaluate the triangle's depth plane at the actual pixel, not
               at a rounded scanline edge. This keeps shared faces coherent. */
            z = ((int64_t)t->inverse_z[0] +
                (zx * (l - t->x[0]) + zy * (y - t->y[0])) / area) * 256;
            if (t->surface)
                z = t->surface_depth[0] +
                    (depth_x * (l * 256 - t->surface_x[0]) +
                     depth_y * (y * 256 - t->surface_y[0])) / 256;
            if (r >= frame->w - view_right_inset)
                r = frame->w - view_right_inset - 1;
            if (r >= l) {
                int x;
                uint8_t *pixels = frame->pixels + (size_t)y * frame->w + l;
                uint32_t *depths = depth_buffer + (size_t)y * frame->w + l;
                int64_t end = z + dz * (r - l);
                /* Most distant polygons fit Q8 depth in 32 bits. Keep an
                   exact wide fallback for nearby/clipped geometry. */
                if (t->surface) {
                    int block, left = t->x[0], right = t->x[0], block_width;
                    for (i = 1; i < 3; i++) {
                        if (t->x[i] < left) left = t->x[i];
                        if (t->x[i] > right) right = t->x[i];
                    }
                    /* Small fixed terrain facets need exact pixel rays; a
                       moving span endpoint would otherwise jitter a coast. */
                    block_width = right - left <= 16 ? 1 : 8;
                    for (block = l; block <= r;) {
                        /* Fixed screen intervals keep the perspective
                           approximation from relocating with polygon edges. */
                        int stop = ((block / block_width) + 1) * block_width - 1;
                        if (stop > r) stop = r;
                        int face0, face1, u0, v0, u1, v1;
                        int visible = 0, footprint;
                        for (x = block; x <= stop; x++) {
                            int64_t candidate = z + dz * (x - block);
                            if (candidate >= 0 && (uint32_t)(candidate >> 8) >= depths[x - block]) {
                                visible = 1; break;
                            }
                        }
                        if (!visible) {
                            int count = stop - block + 1;
                            pixels += count; depths += count; z += dz * count;
                            block = stop + 1;
                            continue;
                        }
                        surface_ray_uv(t->surface, block, y, z, &face0, &u0, &v0);
                        if (stop > block)
                            surface_ray_uv(t->surface, stop, y, z + dz * (stop - block),
                                           &face1, &u1, &v1);
                        else { face1 = face0; u1 = u0; v1 = v0; }
                        footprint = stop > block ? (abs(u1-u0) + abs(v1-v0)) / (stop-block) : 0;
                        /* Blocks span at most eight pixels, so ordinary lattice
                           deltas interpolate exactly without 64-bit division. */
                        int narrow = abs(u1 - u0) < INT32_MAX / 8 && abs(v1 - v0) < INT32_MAX / 8;
                        for (x = block; x <= stop; x++, pixels++, depths++, z += dz) {
                            int face = face0, u = u0, v = v0;
                            if (z < 0 || (uint32_t)(z >> 8) < *depths) continue;
                            if (face0 != face1) {
                                surface_ray_uv(t->surface, x, y, z, &face, &u, &v);
                            } else if (stop > block && narrow) {
                                u += (u1 - u0) * (x - block) / (stop - block);
                                v += (v1 - v0) * (x - block) / (stop - block);
                            } else if (stop > block) {
                                u += (int)((int64_t)(u1 - u0) * (x - block) / (stop - block));
                                v += (int)((int64_t)(v1 - v0) * (x - block) / (stop - block));
                            }
                            *depths = (uint32_t)(z >> 8);
                            *pixels = (uint8_t)surface_detail(t->surface, face, u, v, (uint32_t)(z >> 8), footprint, t->slope);
                        }
                        block = stop + 1;
                    }
                } else if (z >= 0 && end >= 0 && z <= INT32_MAX && end <= INT32_MAX &&
                    dz >= INT32_MIN && dz <= INT32_MAX) {
                    uint32_t value = (uint32_t)z;
                    int32_t step = (int32_t)dz;
                    for (x = l; x <= r; x++, pixels++, depths++, value += step) {
                        uint32_t d = value >> 8;
                        if (d >= *depths) { *depths = d; *pixels = t->color; }
                    }
                } else for (x = l; x <= r; x++, pixels++, depths++, z += dz) {
                    if (z >= 0 && (uint32_t)(z >> 8) >= *depths) {
                        *depths = (uint32_t)(z >> 8); *pixels = t->color;
                    }
                }
            }
        }
    }
}
static Vec cross(Vec a, Vec b) {
    return vec((a.y * b.z - a.z * b.y) / 16384, (a.z * b.x - a.x * b.z) / 16384,
               (a.x * b.y - a.y * b.x) / 16384);
}
static void box(Vec center, Vec up, int size, int height, int color) {
    Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), f;
    Vec v[8];
    int i;
    static const int faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                                     {0, 4, 5}, {0, 5, 1}, {1, 5, 6}, {1, 6, 2},
                                     {2, 6, 7}, {2, 7, 3}, {3, 7, 4}, {3, 4, 0}};
    if (!length(right))
        right = vec(16384, 0, 0);
    f = cross(right, up);
    for (i = 0; i < 8; i++)
        v[i] =
            add(center,
                add(scale(up, i >= 4 ? height : 0, 16384),
                    add(scale(right, (i == 1 || i == 2 || i == 5 || i == 6) ? size : -size, 16384),
                        scale(f, (i == 2 || i == 3 || i == 6 || i == 7) ? size : -size, 16384))));
    for (i = 0; i < 12; i++)
        triangle(v[faces[i][0]], v[faces[i][1]], v[faces[i][2]], color + (i / 2) % 3);
}
/* Hulls are lofts: a convex profile swept through up to four stations, nose
   to tail. Units are sixteenths of the ShipSpec half-width (x), height (y)
   and depth (z), so each model keeps its catalogue proportions. */
typedef struct { int8_t z, y, w, h; } ShipStation;
typedef struct { uint8_t profile, tone, flags, count; int8_t x; ShipStation s[4]; } ShipLoft;
enum { HULL_TONE, TRIM_TONE, GLASS_TONE, ACCENT_TONE, GLOW_TONE };
enum { HEX_PROFILE, BOX_PROFILE, BLADE_PROFILE, POD_PROFILE, WEDGE_PROFILE };
#define LOFT_MIRROR 1 /* also drawn reflected across the centreline */
#define LOFT_EXHAUST 2 /* tail cap glows */
#define SHIP_LOFTS 5
static const int8_t ship_profile[5][8][2] = {
    {{0, 16}, {16, 6}, {16, -6}, {0, -16}, {-16, -6}, {-16, 6}},
    {{-16, 16}, {16, 16}, {16, -16}, {-16, -16}},
    {{0, 16}, {16, 0}, {0, -16}, {-16, 0}},
    {{0, 16}, {11, 11}, {16, 0}, {11, -11}, {0, -16}, {-11, -11}, {-16, 0}, {-11, 11}},
    {{0, 16}, {16, -4}, {12, -16}, {-12, -16}, {-16, -4}}};
static const uint8_t ship_profile_points[5] = {6, 4, 4, 8, 5};
static const ShipLoft ship_lofts[SHIPS][SHIP_LOFTS] = {
    { /* CINDER: runabout dart */
     {WEDGE_PROFILE, HULL_TONE, 0, 4, 0, {{16, -4, 0, 0}, {4, 0, 7, 12}, {-8, 0, 9, 14}, {-12, 0, 7, 10}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{9, 8, 0, 0}, {3, 11, 3, 4}, {-4, 10, 2, 3}}},
     {BLADE_PROFILE, TRIM_TONE, 0, 3, 0, {{2, -3, 8, 2}, {-10, -3, 16, 2}, {-12, -3, 15, 2}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 2, 5, {{-9, -1, 3, 5}, {-14, -1, 3, 5}}}},
    { /* LOOM: light trader with side containers */
     {BOX_PROFILE, HULL_TONE, 0, 4, 0, {{15, -2, 6, 6}, {9, 0, 10, 12}, {-9, 0, 10, 12}, {-12, 0, 8, 10}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{16, 5, 0, 0}, {12, 8, 5, 5}, {7, 9, 5, 4}}},
     {BOX_PROFILE, ACCENT_TONE, LOFT_MIRROR, 2, 12, {{6, -2, 3, 9}, {-8, -2, 3, 9}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 2, 5, {{-10, 0, 3, 5}, {-15, 0, 3, 5}}}},
    { /* HEARTH: heavy freighter on twin engine pods */
     {HEX_PROFILE, HULL_TONE, 0, 4, 0, {{15, 0, 5, 5}, {8, 0, 12, 12}, {-8, 0, 12, 14}, {-12, 0, 10, 12}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{13, 9, 0, 0}, {9, 12, 4, 4}, {3, 12, 3, 3}}},
     {BOX_PROFILE, ACCENT_TONE, 0, 2, 0, {{5, -13, 7, 4}, {-10, -13, 7, 4}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 3, 13, {{0, -2, 3, 7}, {-12, -2, 3, 7}, {-14, -2, 2, 5}}},
     {BLADE_PROFILE, HULL_TONE, LOFT_MIRROR, 2, 8, {{-2, -2, 5, 1}, {-9, -2, 5, 1}}}},
    { /* KESTREL: swept-wing interceptor */
     {HEX_PROFILE, HULL_TONE, 0, 4, 0, {{16, 0, 0, 0}, {6, 0, 4, 10}, {-6, 0, 5, 14}, {-12, 0, 4, 10}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{9, 8, 0, 0}, {3, 11, 2, 5}, {-4, 10, 2, 3}}},
     {BLADE_PROFILE, HULL_TONE, 0, 3, 0, {{4, 0, 4, 2}, {-8, 0, 16, 3}, {-11, 0, 16, 3}}},
     {BLADE_PROFILE, ACCENT_TONE, LOFT_MIRROR, 3, 4, {{-3, 6, 1, 0}, {-10, 12, 1, 6}, {-12, 12, 1, 6}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 2, 3, {{-10, 0, 2, 6}, {-14, 0, 2, 6}}}},
    { /* QUARRY: mining spine with forward grapples */
     {BOX_PROFILE, HULL_TONE, 0, 4, 0, {{13, -2, 4, 6}, {9, 0, 6, 10}, {-8, 0, 6, 10}, {-12, 0, 5, 8}}},
     {BLADE_PROFILE, ACCENT_TONE, LOFT_MIRROR, 2, 4, {{16, -9, 2, 3}, {6, -6, 3, 5}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{12, 8, 0, 0}, {8, 11, 4, 4}, {3, 11, 3, 3}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 3, 12, {{3, 0, 4, 8}, {-12, 0, 4, 8}, {-14, 0, 3, 6}}},
     {BLADE_PROFILE, HULL_TONE, LOFT_MIRROR, 2, 8, {{0, 0, 4, 1}, {-8, 0, 4, 1}}}},
    { /* WAYFARER: long-range liner on outrigger nacelles */
     {HEX_PROFILE, HULL_TONE, 0, 4, 0, {{16, 0, 0, 0}, {8, 0, 7, 9}, {-6, 0, 9, 12}, {-12, 0, 7, 9}}},
     {HEX_PROFILE, GLASS_TONE, 0, 3, 0, {{11, 7, 0, 0}, {6, 10, 4, 4}, {0, 10, 3, 3}}},
     {POD_PROFILE, TRIM_TONE, LOFT_MIRROR | LOFT_EXHAUST, 3, 13, {{4, -4, 3, 6}, {-12, -4, 3, 6}, {-14, -4, 2, 5}}},
     {BLADE_PROFILE, HULL_TONE, LOFT_MIRROR, 2, 9, {{0, -3, 5, 1}, {-8, -3, 5, 1}}},
     {BLADE_PROFILE, ACCENT_TONE, 0, 3, 0, {{-2, 10, 1, 0}, {-10, 14, 1, 5}, {-12, 14, 1, 5}}}}};
typedef struct {
    Vec origin, right, up, front, eye, light; /* Q14 basis and light */
    int unit; /* world units per metre */
    const ShipSpec *spec;
} ShipView;
static int64_t dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
/* Model coordinates are 256ths of the spec extents. */
static Vec ship_point(const ShipView *v, int x, int y, int z) {
    int64_t d = 16384LL * 256;
    return add(v->origin,
               add(scale(v->right, (int64_t)x * v->spec->width * v->unit, d),
                   add(scale(v->up, (int64_t)y * v->spec->height * v->unit, d),
                       scale(v->front, (int64_t)z * v->spec->depth * v->unit, d))));
}
static int ship_colour(int tone, int light) {
    static const uint8_t ecs_hull[9] = {1, 16, 19, 28, 29, 30, 20, 18, 31};
    static const uint8_t aga_hull[16] = {112, 113, 114, 115, 116, 117, 118, 119,
                                         120, 121, 122, 123, 124, 126, 20, 18};
    static const uint8_t glass[4] = {25, 26, 27, 13}, accent[4] = {21, 22, 23, 24};
    if (tone == GLOW_TONE)
        return 12;
    if (frame->enhanced) switch (tone) {
        case HULL_TONE: return aga_hull[light];
        case TRIM_TONE: return aga_hull[light * 2 / 3];
        case GLASS_TONE: return light >= 13 ? 13 : 82 + light;
        default: return 128 + light * 2;
        }
    switch (tone) {
    case HULL_TONE: return ecs_hull[light * 8 / 15];
    case TRIM_TONE: return ecs_hull[light * 5 / 15];
    case GLASS_TONE: return glass[light * 3 / 15];
    default: return accent[light * 3 / 15];
    }
}
/* Faces are oriented away from their loft's axis, so profiles and caps need
   no hand-kept winding; back faces are dropped before clipping. */
static void ship_face(const ShipView *v, Vec a, Vec b, Vec c, Vec inside, int tone) {
    Vec e1 = sub(b, a), e2 = sub(c, a), mid, n;
    int64_t d;
    n = vec(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
    if (!n.x && !n.y && !n.z)
        return;
    mid = scale(add(a, add(b, c)), 1, 3);
    if (dot(n, sub(mid, inside)) < 0)
        n = scale(n, -1, 1);
    if (dot(n, sub(mid, v->eye)) >= 0)
        return;
    d = dot(unit(n, 16384), v->light) / 16384;
    triangle(a, b, c, ship_colour(tone, d > 0 ? 3 + (int)(d * 12 / 16384) : 3 + (int)(d * 2 / 16384)));
}
static void ship_loft(const ShipView *v, const ShipLoft *l, int side) {
    const int8_t (*profile)[2] = ship_profile[l->profile];
    Vec ring[4][8], axis[4], centre = vec(0, 0, 0);
    int s, p, n = ship_profile_points[l->profile], x = l->x * side * 16, last = l->count - 1;
    for (s = 0; s < l->count; s++) {
        const ShipStation *st = &l->s[s];
        axis[s] = ship_point(v, x, st->y * 16, st->z * 16);
        centre = add(centre, axis[s]);
        for (p = 0; p < n; p++)
            ring[s][p] = ship_point(v, x + profile[p][0] * side * st->w,
                                    st->y * 16 + profile[p][1] * st->h, st->z * 16);
    }
    centre = scale(centre, 1, l->count);
    for (s = 0; s < last; s++) {
        Vec inside = scale(add(axis[s], axis[s + 1]), 1, 2);
        for (p = 0; p < n; p++) {
            int q = (p + 1) % n;
            ship_face(v, ring[s][p], ring[s][q], ring[s + 1][q], inside, l->tone);
            ship_face(v, ring[s][p], ring[s + 1][q], ring[s + 1][p], inside, l->tone);
        }
    }
    for (p = 1; p + 1 < n; p++) {
        ship_face(v, ring[0][0], ring[0][p], ring[0][p + 1], centre, l->tone);
        ship_face(v, ring[last][0], ring[last][p], ring[last][p + 1], centre,
                  l->flags & LOFT_EXHAUST ? GLOW_TONE : l->tone);
    }
}
/* Shared by the chase camera (unit 1, metres) and the shipyard preview. */
static void ship_mesh(int ship, Vec origin, Vec right, Vec up, Vec front, int unit, Vec light) {
    ShipView v;
    int i;
    v.origin = origin; v.right = right; v.up = up; v.front = front;
    v.eye = scale(camera_position, 1, FP); v.light = light;
    v.unit = unit; v.spec = &ship_specs[ship];
    for (i = 0; i < SHIP_LOFTS; i++) {
        const ShipLoft *l = &ship_lofts[ship][i];
        if (!l->count)
            continue;
        ship_loft(&v, l, 1);
        if (l->flags & LOFT_MIRROR)
            ship_loft(&v, l, -1);
    }
}
/* Model extents in sixteenths: widest |x|, tallest |y| and the z span. */
static void ship_extent(int ship, int *x, int *y, int *front, int *back) {
    int i, s;
    *x = *y = 0; *front = -128; *back = 127;
    for (i = 0; i < SHIP_LOFTS; i++) {
        const ShipLoft *l = &ship_lofts[ship][i];
        for (s = 0; s < l->count; s++) {
            const ShipStation *st = &l->s[s];
            if (abs(l->x) + st->w > *x) *x = abs(l->x) + st->w;
            if (abs(st->y) + st->h > *y) *y = abs(st->y) + st->h;
            if (st->z > *front) *front = st->z;
            if (st->z < *back) *back = st->z;
        }
    }
}
static void draw_triangles(void) {
    int i;
    qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
    for (i = 0; i < nt; i++) {
        if (platform_pump && i % 32 == 0)
            platform_pump();
        raster(&triangles[i]);
    }
}
static int material_color(const Planet *planet, Vec local, int material) {
    int shade = surface_light(&game->system, planet, local) * 14 / 16384;
    static const int ecs[] = {25, 8, 8, 16, 28, 21};
    static const int aga[] = {80, 64, 64, 96, 112, 96};
    if (material == 5) {
        Vec fixed = unit(body_rotate(planet, local, 1), 16384);
        shade = shade * (8 + (int)((fixed.z + 16384) / 1800) % 8) / 15;
        /* Broad body-fixed longitude variation makes gas rotation visible. */
        shade = shade * (12 + (int)fixed.x * 2 / 16384 + (int)fixed.y / 16384) / 15;
    }
    return frame->enhanced ? aga[material] + shade : ecs[material] + shade / 6;
}
#define SURFACE_CACHE_SIZE 4096
#define SURFACE_TILE 256
/* Distant ground samples coarser material tiles once a 256 m tile shrinks
   below a third of a pixel. From 5 to 20 km a frame otherwise touches about
   one tile per pixel, each a full geology evaluation, overflowing the cache. */
#define SURFACE_LEVELS 8
#define SURFACE_LOD_PIXELS 3
static struct {
    int u, v, face, level, valid, material, color, slope, slope_ready, lit;
    uint32_t grain, slope_epoch;
    int8_t albedo[25]; /* Shared 64 m boundary samples, bilinear in each cell. */
    uint8_t albedo_ready;
    uint32_t epoch;
} surface_cache[SURFACE_CACHE_SIZE];
static uint32_t surface_seed, surface_system;
static int surface_enhanced, surface_radius, surface_type;
/* lod_depth[i]: pixels at or beyond this reciprocal depth use level i + 1. */
static uint32_t lod_depth[SURFACE_LEVELS];
static int64_t lod_altitude = -1;
static int lod_radius, lod_width;
/* Ground metres per pixel grow with distance and with grazing incidence,
   approximated as altitude/distance plus the planet's curvature. Rebuilt only
   when altitude changes by 1/32, as each step needs a square root. */
static void surface_lod_prepare(const Planet *p, int64_t altitude) {
    int64_t d;
    int level = 0;
    if (altitude < 50) altitude = 50;
    if (lod_radius == p->radius && lod_width == frame->w && lod_altitude >= 0 &&
        llabs(altitude - lod_altitude) <= lod_altitude / 32)
        return;
    lod_altitude = altitude; lod_radius = p->radius; lod_width = frame->w;
    memset(lod_depth, 0, sizeof(lod_depth));
    for (d = 2000; d < 4000000 && level < SURFACE_LEVELS; d += d / 8) {
        int64_t grazing = altitude * 65536 / d + d * 32768 / p->radius;
        int64_t root, metres;
        if (grazing < 4096) grazing = 4096;
        if (grazing > 65536) grazing = 65536;
        root = (int64_t)isqrt64((uint64_t)grazing);
        metres = 2 * d * 256 / (frame->w * root);
        while (level < SURFACE_LEVELS && SURFACE_LOD_PIXELS * metres > (SURFACE_TILE << level))
            lod_depth[level++] = (uint32_t)(1073741824LL / d);
    }
}
static int surface_level(uint32_t depth) {
    int level = 0;
    while (level < SURFACE_LEVELS && depth <= lod_depth[level])
        level++;
    return level;
}
static unsigned surface_lookup_level(const Planet *p, int face, int u, int v, int level) {
    unsigned slot;
    int shift = 8 + level, size = SURFACE_TILE << level;
    /* Floor division by the power-of-two tile size, without a per-pixel DIVS. */
    int tile_u = u >= 0 ? u >> shift : ~(~u >> shift);
    int tile_v = v >= 0 ? v >> shift : ~(~v >> shift);
    uint32_t epoch = game->tick / HZ;
    Vec fixed, local;
    if (surface_seed != p->seed || surface_system != game->system.seed ||
        surface_enhanced != frame->enhanced || surface_radius != p->radius ||
        surface_type != (int)p->type) {
        memset(surface_cache, 0, sizeof(surface_cache));
        surface_seed = p->seed; surface_system = game->system.seed;
        surface_enhanced = frame->enhanced;
        surface_radius = p->radius; surface_type = (int)p->type;
    }
    slot = hash32((unsigned)tile_u ^ hash32((unsigned)tile_v) ^ (unsigned)face * 7919U ^
                  (unsigned)level * 0x9e3779b9U) & (SURFACE_CACHE_SIZE - 1);
    if (surface_cache[slot].valid && surface_cache[slot].u == tile_u &&
        surface_cache[slot].v == tile_v && surface_cache[slot].face == face &&
        surface_cache[slot].level == level && surface_cache[slot].epoch == epoch)
        return slot;
    u = tile_u * size + size / 2;
    v = tile_v * size + size / 2;
    {
        int sign = face & 1 ? -p->radius : p->radius;
        fixed = face / 2 == 0 ? vec(sign,u,v) : face / 2 == 1 ? vec(u,sign,v) : vec(u,v,sign);
    }
    local = body_rotate(p, radial(fixed, p->radius), 0);
    if (!surface_cache[slot].valid || surface_cache[slot].u != tile_u ||
        surface_cache[slot].v != tile_v || surface_cache[slot].face != face ||
        surface_cache[slot].level != level) {
        surface_cache[slot].material = terrain_material(p, local);
        surface_cache[slot].grain = hash32(p->seed ^ (unsigned)tile_u ^ hash32((unsigned)tile_v) ^ face*7919U);
        surface_cache[slot].slope_ready = 0;
        surface_cache[slot].albedo_ready = 0;
    }
    surface_cache[slot].u = tile_u; surface_cache[slot].v = tile_v;
    surface_cache[slot].face = face; surface_cache[slot].valid = 1;
    surface_cache[slot].level = level; surface_cache[slot].epoch = epoch;
    surface_cache[slot].color = material_color(p, local, surface_cache[slot].material);
    return slot;
}
/* Generate only when a visible fragment resolves the geological layer.
   Distant/minified tiles need their material/light but no texture samples. */
static void surface_albedo(const Planet *p, unsigned slot) {
    int x, y, face=surface_cache[slot].face;
    if (surface_cache[slot].albedo_ready) return;
    for (y=0;y<5;y++) for (x=0;x<5;x++) {
        int cu=surface_cache[slot].u*SURFACE_TILE+x*64;
        int cv=surface_cache[slot].v*SURFACE_TILE+y*64;
        int sign=face & 1 ? -p->radius : p->radius;
        Vec direction=face/2==0 ? vec(sign,cu,cv) :
            face/2==1 ? vec(cu,sign,cv) : vec(cu,cv,sign);
        surface_cache[slot].albedo[y*5+x]=(int8_t)terrain_albedo(p,direction,
            surface_cache[slot].material);
    }
    surface_cache[slot].albedo_ready=1;
}
/* Cache slope lighting only for nearby tiles. Distant colour lookup keeps
   its original cost; rotating normals and suns refresh with illumination. */
static void surface_slope(const Planet *p, unsigned slot, int face, int tu, int tv) {
    Vec a, b, d, normal, local, up;
    int star, slope=0;
    uint32_t epoch=game->tick/HZ;
    if (surface_cache[slot].slope_ready && surface_cache[slot].slope_epoch == epoch) return;
    a=terrain_fixed_vertex(p,face,tu*256,tv*256);
    b=terrain_fixed_vertex(p,face,tu*256+256,tv*256);
    d=terrain_fixed_vertex(p,face,tu*256,tv*256+256);
    normal=unit(cross(unit(sub(b,a),16384),unit(sub(d,a),16384)),16384);
    if (normal.x*a.x+normal.y*a.y+normal.z*a.z < 0) normal=scale(normal,-1,1);
    normal=body_rotate(p,normal,0);
    local=body_rotate(p,a,0); up=unit(local,16384);
    for (star=0;star<game->system.stars;star++) {
        Vec sun=unit(sub(game->system.star[star].center,add(p->center,local)),16384);
        Vec delta=sub(normal,up);
        slope+=(int)((delta.x*sun.x+delta.y*sun.y+delta.z*sun.z)/16384);
    }
    surface_cache[slot].lit=surface_light(&game->system,p,local)>0;
    surface_cache[slot].slope=surface_cache[slot].lit ? slope*14/16384 : 0;
    surface_cache[slot].slope_ready=1; surface_cache[slot].slope_epoch=epoch;
}
/* Metre-scale albedo, never a screen-space stipple or animated texture.
   Broad patches survive approach; small inclusions fade before minification.
   No extra geometry, height noise, floating point or terrain sampling per pixel. */
static int surface_detail(const Planet *p, int face, int u, int v, uint32_t depth, int footprint, int slope) {
    /* Only the untextured distant path below uses coarse material levels. */
    unsigned slot=surface_lookup_level(p,face,u,v,
        depth <= 1073741824U/2000 ? surface_level(depth) : 0);
    int color=surface_cache[slot].color, material, shade, variation, strength;
    int tu = u >= 0 ? u/256 : (u-255)/256;
    int tv = v >= 0 ? v/256 : (v-255)/256;
    uint32_t grain = surface_cache[slot].grain;
    static const int ecs[] = {25,8,8,16,28,21};
    static const int aga[] = {80,64,64,96,112,96};
    material = surface_cache[slot].material;
    if (material == 5) return color;
    if (depth <= 1073741824U/2000 || footprint >= 64) {
        /* Facet normals retain mountain/valley shape beyond texture range.
           Ocean pixels stay level even next to steep coastal geometry. */
        if (material == 0) return color;
        shade = frame->enhanced ? color-aga[material] : (color-ecs[material])*6;
        shade += slope;
        if (shade < 0) shade=0;
        if (shade > 14) shade=14;
        return (frame->enhanced ? aga[material] : ecs[material]) +
               (frame->enhanced ? shade : shade/6);
    }
    surface_albedo(p,slot);
    strength = (int)((depth - 1073741824U/2000) / 3000);
    if (strength > 256) strength = 256;
    if (footprint > 32) strength = strength*(64-footprint)/32;
    {
        int x=u & 63, y=v & 63;
        int cell=((v & 255)/64)*5+(u & 255)/64;
        int a=surface_cache[slot].albedo[cell], b=surface_cache[slot].albedo[cell+1];
        int c=surface_cache[slot].albedo[cell+5], d=surface_cache[slot].albedo[cell+6];
        /* Q6 interpolation preserves dark/bright bands through zero rather
           than truncating each intermediate reflectance to a whole shade. */
        a=a*64+(b-a)*x; c=c*64+(d-c)*x;
        variation=(a*64+(c-a)*y)/4096;
    }
    /* Water adds filtered wave highlights to the generated current field.
       Solid bodies get their medium-scale structure from geology, avoiding
       the former repeated circular soil stamps on every material. */
    if (material == 0 && footprint < 16) {
        uint32_t h = hash32(grain ^ (unsigned)(u & 192)*173U ^ (unsigned)(v & 192)*7919U);
        int wave=(v+(u & 63)*(u & 63)/128+(int)(h & 15)) & 63;
        if (wave<8) variation += 3*(16-footprint)/16;
    }
    /* Fine 16 m inclusions supply optic flow in the last few hundred metres. */
    if (material != 0 && depth > 1073741824U/500 && footprint < 4) {
        uint32_t h = hash32(grain ^ (unsigned)(u & 240)*31U ^ (unsigned)(v & 240)*8191U);
        int x=(u & 15)-8, y=(v & 15)-8;
        int fine=(int)((depth-1073741824U/500)/12000);
        if (fine>256) fine=256;
        if (x*x+y*y < 12+(int)(h & 31))
            variation += ((h & 64) ? 3 : -3)*fine*(4-footprint)/1024;
    }
    shade = frame->enhanced ? color-aga[material] : (color-ecs[material])*6;
    /* Keep the existing eclipse/night illumination; slope detail cannot glow. */
    if (shade == 0 && frame->enhanced) variation /= 2;
    surface_slope(p,slot,face,tu,tv);
    {
        int slope=surface_cache[slot].slope;
        if (!frame->enhanced) { if (slope>3) slope=3; if (slope<-3) slope=-3; }
        variation += slope;
    }
    /* Centre the coarse ECS quantizer so adjacent ground tones survive even
       in ambient light. AGA/RTG retain the full material luminance ramp. */
    if (!frame->enhanced) shade += 3 + 3*strength/256;
    if (material != 0) shade += slope*(256-strength)/256;
    shade += variation*strength/256;
    if (shade < 0) shade = 0;
    if (shade > 14) shade = 14;
    return frame->enhanced ? aga[material]+shade : ecs[material]+shade/6;
}
static uint8_t globe_material[GENERATED_PLANETS][17][33];
static int8_t globe_albedo[GENERATED_PLANETS][17][33];
static void cache_globes(void) {
    if(globe_enhanced!=frame->enhanced) memset(globe_valid,0,sizeof(globe_valid));
    globe_enhanced=frame->enhanced;
}
static int cache_globe(int body) {
    int x,y,slot=body%GENERATED_PLANETS;
    Planet fixed=game->system.planets[body]; const Planet *p=&fixed;
    if(globe_valid[slot] && globe_keys[slot]==p->seed) return slot;
    set_rotation(&fixed,0);
    for(y=0;y<=16;y++) {
        if(platform_pump) platform_pump();
        for(x=0;x<=32;x++) {
            Vec dir=forward(x*32,y*32-256),local=radial(dir,p->radius+terrain_height(p,dir));
            globe_vertices[slot][y][x]=local;
            globe_material[slot][y][x]=(uint8_t)terrain_material(p,local);
            globe_albedo[slot][y][x]=(int8_t)terrain_regional_albedo(p,local,globe_material[slot][y][x]);
        }
    }
    globe_keys[slot]=p->seed; globe_valid[slot]=1;
    return slot;
}
/* Geometry clipmaps use a fixed body-space lattice. Camera motion chooses
   visible cells, but never relocates the terrain samples. Outer vertices
   continuously converge on the next level's triangles before cells retire. */
#define TERRAIN_GRID 8
#define TERRAIN_FINE_STEP 256
#define TERRAIN_VERTEX_CACHE 8192
static struct {
    int u, v, face, valid;
    Vec point;
} terrain_vertices[TERRAIN_VERTEX_CACHE];
static uint32_t terrain_vertex_seed;
static int terrain_vertex_radius, terrain_vertex_type;
static int terrain_floor(int value, int step) {
    return value >= 0 ? value / step : (value - step + 1) / step;
}
static Vec terrain_fixed_vertex(const Planet *p, int face, int u, int v) {
    unsigned slot;
    if (terrain_vertex_seed != p->seed || terrain_vertex_radius != p->radius ||
        terrain_vertex_type != (int)p->type) {
        memset(terrain_vertices, 0, sizeof(terrain_vertices));
        terrain_vertex_seed = p->seed; terrain_vertex_radius = p->radius;
        terrain_vertex_type = (int)p->type;
    }
    slot = hash32((unsigned)u ^ hash32((unsigned)v) ^ (unsigned)face * 7919U) &
           (TERRAIN_VERTEX_CACHE - 1);
    if (!terrain_vertices[slot].valid || terrain_vertices[slot].u != u ||
        terrain_vertices[slot].v != v || terrain_vertices[slot].face != face) {
        Planet fixed = *p;
        int sign = face & 1 ? -p->radius : p->radius;
        Vec direction = face / 2 == 0 ? vec(sign,u,v) :
                        face / 2 == 1 ? vec(u,sign,v) : vec(u,v,sign);
        set_rotation(&fixed, 0);
        int height = terrain_elevation(&fixed, direction);
        terrain_vertices[slot].point = radial(direction, p->radius + (height > 0 ? height : 0));
        terrain_vertices[slot].u = u; terrain_vertices[slot].v = v;
        terrain_vertices[slot].face = face; terrain_vertices[slot].valid = 1;
    }
    return terrain_vertices[slot].point;
}
static Vec terrain_grid_vertex(const Planet *p, int face, int u, int v, int step,
                               int camera_u, int camera_v) {
    Vec point = terrain_fixed_vertex(p, face, u, v);
    int distance = abs(u - camera_u), dv = abs(v - camera_v);
    int parent_step = step * 2, blend;
    if (dv > distance) distance = dv;
    blend = (int)((int64_t)(distance - (TERRAIN_GRID - 3) * step) * 65536 / step);
    if (blend > 0) {
        int pu = terrain_floor(u, parent_step) * parent_step;
        int pv = terrain_floor(v, parent_step) * parent_step;
        int du = u - pu, dv = v - pv;
        Vec a = terrain_fixed_vertex(p, face, pu, pv);
        Vec c = terrain_fixed_vertex(p, face, pu + parent_step, pv + parent_step);
        Vec parent;
        if (du >= dv) {
            Vec b = terrain_fixed_vertex(p, face, pu + parent_step, pv);
            parent = add(a, add(scale(sub(b,a),du,parent_step),
                                scale(sub(c,b),dv,parent_step)));
        } else {
            Vec d = terrain_fixed_vertex(p, face, pu, pv + parent_step);
            parent = add(a, add(scale(sub(c,d),du,parent_step),
                                scale(sub(d,a),dv,parent_step)));
        }
        if (blend > 65536) blend = 65536;
        /* Smoothstep keeps the slope continuous at both ends of the fade. */
        blend = (int)((int64_t)blend * blend * (196608 - 2 * blend) / 4294967296LL);
        point = add(point, scale(sub(parent, point), blend, 65536));
    }
    return add(p->center, body_rotate(p, point, 0));
}
static void terrain_clipmap(const Planet *p, Vec local) {
    Vec fixed = body_rotate(p, local, 1);
    int64_t direction[3] = {fixed.x, fixed.y, fixed.z};
    int face, camera_u, camera_v, step, x, y;
    int inner_u0 = 0, inner_v0 = 0, inner_u1 = 0, inner_v1 = 0;
    int span = (int)isqrt64((uint64_t)(2LL * p->radius * (TERRAIN_DETAIL_ALTITUDE + TERRAIN_MAX_HEIGHT))) * 2;
    Vec rows[2][TERRAIN_GRID * 2 + 1];
    surface_uv(p, direction, &face, &camera_u, &camera_v);
    for (step = TERRAIN_FINE_STEP;; step *= 2) {
        int u0 = terrain_floor(camera_u, step * 2) * step * 2 - TERRAIN_GRID * step;
        int v0 = terrain_floor(camera_v, step * 2) * step * 2 - TERRAIN_GRID * step;
        int u1 = u0 + TERRAIN_GRID * 2 * step;
        int v1 = v0 + TERRAIN_GRID * 2 * step;
        for (y = 0; y <= TERRAIN_GRID * 2; y++) {
            int v = v0 + y * step;
            if (platform_pump) platform_pump();
            for (x = 0; x <= TERRAIN_GRID * 2; x++) {
                int u = u0 + x * step;
                if (step != TERRAIN_FINE_STEP && u > inner_u0 && u < inner_u1 &&
                    v > inner_v0 && v < inner_v1) continue;
                rows[y % 2][x] = terrain_grid_vertex(p,face,u,v,step,camera_u,camera_v);
            }
            if (y) for (x = 0; x < TERRAIN_GRID * 2; x++) {
                int u = u0 + x * step;
                if (step != TERRAIN_FINE_STEP && u >= inner_u0 && u + step <= inner_u1 &&
                    v - step >= inner_v0 && v <= inner_v1) continue;
                Vec a = rows[(y - 1) % 2][x], b = rows[(y - 1) % 2][x + 1];
                Vec c = rows[y % 2][x + 1], d = rows[y % 2][x];
                triangle(a,b,c,0); triangle(a,c,d,0);
            }
        }
        if (step * TERRAIN_GRID >= span) {
            /* Only the distant outer perimeter descends into the fallback
               globe. Adjacent clipmap levels share their morphed edges. */
            for (y = 0; y < 4; y++) for (x = 0; x < TERRAIN_GRID * 2; x++) {
                int u = y < 2 ? (y ? u1 : u0) : u0 + x * step;
                int v = y >= 2 ? (y == 3 ? v1 : v0) : v0 + x * step;
                int nu = y < 2 ? u : u + step, nv = y >= 2 ? v : v + step;
                Vec a = terrain_grid_vertex(p,face,u,v,step,camera_u,camera_v);
                Vec b = terrain_grid_vertex(p,face,nu,nv,step,camera_u,camera_v);
                Vec c = add(p->center, radial(sub(b,p->center),p->radius * 9LL / 10));
                Vec d = add(p->center, radial(sub(a,p->center),p->radius * 9LL / 10));
                triangle(a,b,c,0); triangle(a,c,d,0);
            }
            break;
        }
        inner_u0 = u0; inner_v0 = v0; inner_u1 = u1; inner_v1 = v1;
    }
}
static void planet_mesh(int body) {
    const Planet *p = &game->system.planets[body];
    Vec cam_local = sub(scale(game->pos, 1, FP), p->center);
    int slot, radius = p->radius, x, y, lons = frame->enhanced ? 32 : 16, lats = lons / 2;
    int64_t distance = length(cam_local), altitude = distance - radius;
    terrain_surface = body == game->body && altitude < TERRAIN_DETAIL_ALTITUDE ? p : NULL;
    if (terrain_surface) surface_lod_prepare(p, altitude);
    {
        Vec c = camera(p->center);
        if (c.z + radius < 8) {
            terrain_surface = NULL;
            return;
        }
        if(p->catalog && c.z>0) {
            /* Hundreds of distant moons are unresolved. Cull before building
               terrain samples, keeping Sol within the original cache budget. */
            if((int64_t)radius*frame->w<c.z*2) { terrain_surface=NULL; return; }
            if(llabs(c.x)>(c.z+radius)*2 || llabs(c.y)>(c.z+radius)*2) { terrain_surface=NULL; return; }
        }
        if (distance > radius * 80LL) {
            lons = 8;
            lats = 4;
        }
        {
            int step = 32 / lons;
            slot=cache_globe(body);
            for (y = 0; y < lats; y++)
                for (x = 0; x < lons; x++) {
                    int row = y * step, column = x * step;
                    Vec a = add(p->center, body_rotate(p, globe_vertices[slot][row][column], 0)),
                        b = add(p->center, body_rotate(p, globe_vertices[slot][row][column + step], 0));
                    Vec c2 = add(p->center, body_rotate(p, globe_vertices[slot][row + step][column + step], 0)),
                        d = add(p->center, body_rotate(p, globe_vertices[slot][row + step][column], 0));
                    int color = material_color(p, sub(a, p->center), globe_material[slot][row][column]);
                    if (!terrain_surface) {
                        static const int ecs[] = {25,8,8,16,28,21};
                        static const int aga[] = {80,64,64,96,112,96};
                        int material=globe_material[slot][row][column];
                        int base=frame->enhanced ? aga[material] : ecs[material];
                        int shade=(color-base)*(frame->enhanced ? 1 : 6);
                        int variation=globe_albedo[slot][row][column];
                        shade += variation*shade/14;
                        if (shade<0) shade=0;
                        if (shade>14) shade=14;
                        color=base+(frame->enhanced ? shade : shade/6);
                    }
                    if (terrain_surface) {
                        int inset = radius * 19 / 20;
                        a = add(p->center,radial(sub(a,p->center),inset));
                        b = add(p->center,radial(sub(b,p->center),inset));
                        c2 = add(p->center,radial(sub(c2,p->center),inset));
                        d = add(p->center,radial(sub(d,p->center),inset));
                    }
                    triangle(a, b, c2, color);
                    triangle(a, c2, d, color);
                }
        }
    }
    if (body == game->body && altitude < TERRAIN_DETAIL_ALTITUDE)
        terrain_clipmap(p, cam_local);
    terrain_surface = NULL;
}
/* Decorative forests use the same terrain sampler as flight collision.
   Only nearby land has geometry; the globe material carries distant forests. */
static void forests(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(game->pos, 1, FP), p->center), up = unit(local, 16384);
    Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), front;
    int x, y;
    if (altitude_at(game, game->pos) > 3000 || p->type != OCEAN)
        return;
    if (!length(right))
        right = vec(16384, 0, 0);
    front = cross(right, up);
    /* A cube-face metre lattice anchors trees to the planet, independent
       of camera motion and LOD. All six face orientations use the same rule. */
    {
        int axis = 0;
        Vec fixed = body_rotate(p, local, 1);
        int64_t dominant = llabs(fixed.x), u, v, size = p->radius;
        if (llabs(fixed.y) > dominant) {
            axis = 1;
            dominant = llabs(fixed.y);
        }
        if (llabs(fixed.z) > dominant) {
            axis = 2;
            dominant = llabs(fixed.z);
        }
        if (!dominant)
            return;
        u = (axis == 0 ? fixed.y : fixed.x) * size / dominant;
        v = (axis == 2 ? fixed.y : fixed.z) * size / dominant;
        for (y = -4; y <= 4; y++)
            for (x = -4; x <= 4; x++) {
                int64_t a = (u / 400 + x) * 400, b = (v / 400 + y) * 400;
                int64_t sign = (axis == 0   ? fixed.x
                                : axis == 1 ? fixed.y
                                            : fixed.z) < 0
                                   ? -size
                                   : size;
                Vec d = axis == 0 ? vec(sign, a, b) : axis == 1 ? vec(a, sign, b) : vec(a, b, sign);
                d = body_rotate(p, d, 0);
                uint32_t h = hash32(p->seed ^ (unsigned)(a / 400) ^ (unsigned)(b / 400) * 7919U);
                Vec pos = radial(d, p->radius + terrain_height(p, d));
                if (h % 3 || terrain_material(p, pos) != 2)
                    continue;
                up = unit(pos, 16384);
                pos = add(p->center, pos);
                triangle(add(pos, scale(right, -15, 16384)), add(pos, scale(right, 15, 16384)),
                         add(pos, scale(up, 20 + h % 55, 16384)),
                         surface_light(&game->system, p, sub(pos,p->center)) ? 10 : 8);
                triangle(add(pos, scale(front, -15, 16384)), add(pos, scale(front, 15, 16384)),
                         add(pos, scale(up, 20 + h % 55, 16384)), 8);
            }
    }
}
static void ring_mesh(const Planet *p) {
    int i, band;
    for (band = 0; band < 3; band++) {
        int64_t inner = (int64_t)p->radius * (130 + band * 25) / 100;
        int64_t outer = (int64_t)p->radius * (150 + band * 25) / 100;
        for (i = 0; i < 32; i++) {
            Vec a = vec(sini(i * 32), cosi(i * 32), 0);
            Vec b = vec(sini((i + 1) * 32), cosi((i + 1) * 32), 0);
            Vec c = add(p->center, scale(b, outer, 16384));
            Vec d = add(p->center, scale(a, outer, 16384));
            a = add(p->center, scale(a, inner, 16384));
            b = add(p->center, scale(b, inner, 16384));
            triangle(a, b, c, 17 + band);
            triangle(a, c, d, 17 + band);
        }
    }
}
static void stellar_discs(void) {
    int i, j;
    for (i = 0; i < game->system.stars; i++) {
        const Star *s = &game->system.star[i];
        Vec center = s->center, normal = unit(sub(scale(camera_position, 1, FP), center), 16384);
        Vec right = unit(cross(normal, vec(0, 16384, 0)), 16384), up;
        if (!length(right))
            right = vec(16384, 0, 0);
        up = cross(right, normal);
        for (j = 0; j < 16; j++) {
            Vec a = add(center, add(scale(right, (int64_t)cosi(j * 64) * s->radius / 16384, 16384),
                                    scale(up, (int64_t)sini(j * 64) * s->radius / 16384, 16384)));
            Vec b = add(center,
                        add(scale(right, (int64_t)cosi((j + 1) * 64) * s->radius / 16384, 16384),
                            scale(up, (int64_t)sini((j + 1) * 64) * s->radius / 16384, 16384)));
            triangle(center, a, b, stellar_colour(s->type));
        }
    }
}
/* Cloud shading ramp: AGA/RTG use the dedicated 224-255 ramp, ECS borrows
   the base palette's neutral greys from night slate to sunlit white. */
static const uint8_t ecs_cloud[9] = {1, 16, 28, 29, 30, 20, 18, 15, 31};
static int cloud_colour(int shade) {
    if (shade < 0) shade = 0;
    if (shade > 255) shade = 255;
    return frame->enhanced ? 224 + shade / 8 : ecs_cloud[shade * 9 / 256];
}
/* Elevations (1024 per turn) of each ring from the top pole to the bottom
   pole; rings below the puff's floor collapse onto the flat cloud base. */
static const int cloud_rings[3][5] = {{256, 150, 50, -60, -256}, {256, 90, -40, -256, 0},
                                      {256, 20, -256, 0, 0}};
static const int cloud_ring_count[3] = {5, 4, 3}, cloud_segments[3] = {8, 6, 5};
typedef struct {
    Vec origin, axis[3];   /* world position of the cloud base; Q14 tangent axes */
    int64_t camera[3];     /* camera in tangent metres */
    int sun[3], sunlit, haze, height;
} CloudView;
static void cloud_puff(const CloudView *view, const CloudPuff *puff, int size, int lod, uint32_t seed) {
    int64_t t[26][3];
    Vec w[26];
    int start[5], ring, k, n = 0, rings = cloud_ring_count[lod], segments = cloud_segments[lod];
    int64_t px = puff->x * size / 256, py = puff->y * size / 256, pz = puff->z * size / 256;
    int64_t r = puff->radius * size / 256;
    int twist = (int)(seed & 127);
    if (r < 8) return;
    {
        /* Whole puffs behind or beside the view never reach the clipper. */
        Vec centre = add(view->origin, add(add(scale(view->axis[0], px, 16384), scale(view->axis[1], py, 16384)),
                                           scale(view->axis[2], pz, 16384)));
        Vec v = camera(centre);
        int64_t bound = r * 5 / 4;
        if (v.z < -bound || v.x - bound > v.z || -v.x - bound > v.z ||
            (v.y > 0 ? v.y : -v.y) * (frame->w / 2) > (v.z + bound) * (view_h / 2) + bound * (frame->w / 2))
            return;
    }
    for (ring = 0; ring < rings; ring++) {
        int e = cloud_rings[lod][ring], count = ring == 0 || ring == rings - 1 ? 1 : segments;
        start[ring] = n;
        for (k = 0; k < count; k++, n++) {
            /* Per-vertex lumps give each puff a cauliflower silhouette. */
            int lump = 226 + (int)(hash32(seed + (uint32_t)n * 0x9e3779b9U) % 44U);
            int64_t rr = r * lump / 256, flat = rr * cosi(e) / 16384;
            int az = k * 1024 / segments + twist;
            t[n][0] = px + (count > 1 ? flat * cosi(az) / 16384 : 0);
            t[n][1] = py + (count > 1 ? flat * sini(az) / 16384 : 0);
            t[n][2] = pz + rr * sini(e) * 13 / (16384 * 16); /* slightly oblate */
            if (t[n][2] < 0) t[n][2] = 0;
            w[n] = add(view->origin, add(add(scale(view->axis[0], t[n][0], 16384),
                                             scale(view->axis[1], t[n][1], 16384)),
                                         scale(view->axis[2], t[n][2], 16384)));
        }
    }
    for (ring = 0; ring + 1 < rings; ring++) {
        for (k = 0; k < segments; k++) {
            int a0 = start[ring] + (ring == 0 ? 0 : k), a1 = start[ring] + (ring == 0 ? 0 : (k + 1) % segments);
            int b0 = start[ring + 1] + (ring + 2 == rings ? 0 : k);
            int b1 = start[ring + 1] + (ring + 2 == rings ? 0 : (k + 1) % segments);
            int face[2][3] = {{a0, a1, b1}, {a0, b1, b0}}, f;
            for (f = 0; f < 2; f++) {
                const int *v = face[f];
                int64_t c[3], normal[3], dot;
                int i, ndot, up, shade, height, wrap;
                if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) continue; /* pole fans */
                for (i = 0; i < 3; i++) c[i] = (t[v[0]][i] + t[v[1]][i] + t[v[2]][i]) / 3;
                if (!t[v[0]][2] && !t[v[1]][2] && !t[v[2]][2]) {
                    normal[0] = normal[1] = 0;
                    normal[2] = -r;
                } else {
                    normal[0] = c[0] - px; normal[1] = c[1] - py; normal[2] = c[2] - pz;
                }
                dot = normal[0] * (c[0] - view->camera[0]) + normal[1] * (c[1] - view->camera[1]) +
                      normal[2] * (c[2] - view->camera[2]);
                if (dot >= 0) continue; /* back face: also hides the puff from inside */
                /* Multiple scattering keeps shadow sides a soft grey: a high
                   floor, widely wrapped diffuse, skylight on top, darker bases. */
                ndot = (int)((normal[0] * view->sun[0] + normal[1] * view->sun[1] + normal[2] * view->sun[2]) / r);
                up = (int)(normal[2] * 16384 / r);
                if (ndot > 16384) ndot = 16384;
                if (ndot < -16384) ndot = -16384;
                if (up > 16384) up = 16384;
                if (up < -16384) up = -16384;
                wrap = (ndot + 10000) * 256 / 26384;
                if (wrap < 0) wrap = 0;
                height = (int)(c[2] * 256 / view->height);
                if (height > 256) height = 256;
                shade = (124 + wrap * 112 / 256 + (up + 16384) * 20 / 32768) * (200 + height * 56 / 256) / 256;
                shade = 24 + (shade - 24) * view->sunlit / 256;
                shade += (170 - shade) * view->haze / 256;
                triangle(w[v[0]], w[v[1]], w[v[2]], cloud_colour(shade));
            }
        }
    }
}
/* Cumulus field in body-fixed coordinates, nearest cells first, fading in
   at the edge of range so clouds grow out of the haze instead of popping. */
static void cloud_deck(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(camera_position, 1, FP), p->center), fixed;
    int cell = cloud_spacing(p), rings = 5, face, ring, du, dv, k;
    int64_t alt = length(local) - p->radius, range = (int64_t)cell * rings;
    if (!p->atmosphere || alt > (int64_t)cell * 10) return;
    fixed = body_rotate(p, local, 1);
    for (face = 0; face < 6; face++) {
        int u, v;
        if (!cloud_face_cell(p, face, fixed, range, &u, &v)) continue;
        for (ring = 0; ring <= rings; ring++)
            for (dv = -ring; dv <= ring; dv++)
                for (du = -ring; du <= ring; du++) {
                    const Cloud *c;
                    CloudView view;
                    Vec base, rel;
                    int64_t dist;
                    int fade, lod, light, i;
                    if (du != -ring && du != ring && dv != -ring && dv != ring) continue;
                    if (nt > TRI_MAX - 1000) return; /* leave room for ports and ships */
                    c = cloud_at(p, face, u + du, v + dv, game->tick);
                    if (!c) continue;
                    base = body_rotate(p, c->base, 0);
                    rel = sub(local, base);
                    dist = length(rel);
                    fade = (int)((range - dist) * 256 / (cell * 2));
                    if (fade <= 16) continue;
                    if (fade > 256) fade = 256;
                    lod = dist < cell * 2 ? 0 : dist < cell * 7 / 2 ? 1 : 2;
                    view.origin = add(p->center, base);
                    view.axis[0] = body_rotate(p, c->east, 0);
                    view.axis[1] = body_rotate(p, c->north, 0);
                    view.axis[2] = body_rotate(p, c->up, 0);
                    for (i = 0; i < 3; i++)
                        view.camera[i] = (rel.x * view.axis[i].x + rel.y * view.axis[i].y +
                                          rel.z * view.axis[i].z) / 16384;
                    light = surface_light(&game->system, p, base);
                    view.sunlit = light * 3 / 64 > 256 ? 256 : light * 3 / 64;
                    for (i = 0; i < 3; i++) view.sun[i] = 0;
                    if (game->system.stars) {
                        Vec sun = unit(sub(game->system.star[0].center, view.origin), 16384);
                        for (i = 0; i < 3; i++)
                            view.sun[i] = (int)((sun.x * view.axis[i].x + sun.y * view.axis[i].y +
                                                 sun.z * view.axis[i].z) / 16384);
                    }
                    view.haze = (int)(dist * 120 / (range + 1));
                    view.height = c->height * fade / 256;
                    if (view.height < 1) view.height = 1;
                    for (k = 0; k < c->puffs && k < 6 - lod * 2; k++)
                        cloud_puff(&view, &c->puff[k], fade, lod,
                                   hash32((uint32_t)(u + du) * 0x45d9f3bU ^ (uint32_t)(v + dv) * 0x119de1f3U ^
                                          (uint32_t)face ^ (uint32_t)k << 24));
                }
    }
}
/* In-cloud vision: an indexed-colour tint. Each palette entry has nearest
   matches for 25/50/75% blends toward the cloud colour (lit and unlit), and
   ordered dithering between those levels gives smooth whiteout with depth. */
#define FOG_STEPS 16
static uint8_t fog_map[2][3][256], fog_full[2];
static int fog_enhanced = -1;
static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
/* -ln(1 - n/16) in Q8: distance multiples of visibility for each fog step. */
static const uint16_t fog_log[FOG_STEPS] = {0, 17, 34, 53, 74, 96, 120, 147,
                                            177, 212, 251, 298, 355, 429, 532, 710};
static void palette_ramp(int index, int *lo, int *hi) {
    static const int edges[] = {0, 32, 64, 80, 96, 112, 128, 160, 192, 224, 256};
    int i;
    for (i = 0; index >= edges[i + 1]; i++) {}
    *lo = edges[i]; *hi = edges[i + 1];
}
static int fog_nearest(const Frame *f, uint32_t rgb, int index) {
    int best = 0, error = 0x7fffffff, j, lo, hi, pass;
    int r = (int)(rgb >> 16 & 255), g = (int)(rgb >> 8 & 255), b = (int)(rgb & 255);
    palette_ramp(index, &lo, &hi);
    /* Base colours, the source's own ramp and the cloud ramp keep the
       search to ~96 candidates, quick enough for a 68020 at start-up. */
    for (pass = 0; pass < (f->enhanced ? 3 : 1); pass++) {
        int from = pass == 0 ? 0 : pass == 1 ? lo : 224, to = pass == 0 ? 32 : pass == 1 ? hi : 256;
        for (j = from; j < to; j++) {
            int dr = r - (int)(f->palette[j] >> 16 & 255), dg = g - (int)(f->palette[j] >> 8 & 255),
                db = b - (int)(f->palette[j] & 255), e = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (e < error) { error = e; best = j; }
        }
    }
    return best;
}
static void fog_tables(const Frame *f) {
    static const uint32_t target[2] = {0x262e40, 0xb8c2cc};
    int lit, level, i, sources = f->enhanced ? 256 : 32;
    for (lit = 0; lit < 2; lit++) {
        fog_full[lit] = (uint8_t)fog_nearest(f, target[lit], f->enhanced ? 224 : 0);
        for (level = 0; level < 3; level++)
            for (i = 0; i < 256; i++) {
                uint32_t c = f->palette[i], out = 0;
                int ch, mix = (level + 1) * 64;
                if (i >= sources) { fog_map[lit][level][i] = fog_full[lit]; continue; }
                for (ch = 16; ch >= 0; ch -= 8) {
                    int a = (int)(c >> ch & 255), b = (int)(target[lit] >> ch & 255);
                    out |= (uint32_t)(a + (b - a) * mix / 256) << ch;
                }
                fog_map[lit][level][i] = (uint8_t)fog_nearest(f, out, i);
            }
    }
    fog_enhanced = f->enhanced;
}
static void cloud_fog(int density, int lit) {
    uint32_t threshold[FOG_STEPS + 1];
    int tint = density * 7 / 256, visibility = 120 + (256 - density) * 3;
    int x, y, q, cols = frame->w / 16 + 2, rows = view_h / 16 + 2;
    int shift_x = (camera_yaw & 1023) * frame->w / 256, shift_y = -camera_pitch * frame->w / 256;
    unsigned period = (unsigned)frame->w / 4; /* one full turn of yaw, in 16-pixel cells */
    uint32_t phase = game->tick / 12U;
    int frac = (int)(game->tick % 12U) * 256 / 12;
    static int8_t wisp[48][88];
    if (fog_enhanced != frame->enhanced) fog_tables(frame);
    /* Inverse-depth thresholds: a pixel at least this far gets step q. */
    for (q = 0; q <= FOG_STEPS; q++) {
        int n = q <= tint ? 0 : (q - tint) * FOG_STEPS / (FOG_STEPS - tint);
        if (q == FOG_STEPS) threshold[q] = 0;
        else if (!n) threshold[q] = 0xffffffffU;
        else threshold[q] = (uint32_t)((1LL << 38) / ((int64_t)visibility * fog_log[n]));
    }
    /* Slowly churning wisps, anchored to view direction so turning sweeps
       them across the screen instead of dragging them along. */
    if (rows > 48) rows = 48;
    if (cols > 88) cols = 88;
    for (y = 0; y < rows; y++)
        for (x = 0; x < cols; x++) {
            uint32_t cx = (uint32_t)(x + (shift_x >> 4)) % period, cy = (uint32_t)(y + (shift_y >> 4));
            int a = (int)(hash32(cx * 0x9e3779b9U ^ cy * 0x85ebca6bU ^ phase) & 15) - 8;
            int b = (int)(hash32(cx * 0x9e3779b9U ^ cy * 0x85ebca6bU ^ (phase + 1)) & 15) - 8;
            wisp[y][x] = (int8_t)(a + (b - a) * frac / 256);
        }
    for (y = 0; y < view_h; y++) {
        uint8_t *pixels = frame->pixels + (size_t)y * frame->w;
        const uint32_t *depths = depth_buffer + (size_t)y * frame->w;
        int sy = y + (shift_y & 15), by = sy >> 4, fy = sy & 15;
        const uint8_t *dither = bayer4 + (y & 3) * 4;
        if (platform_pump && !(y % 16)) platform_pump();
        for (x = 0; x < frame->w; x++) {
            int sx = x + (shift_x & 15), bx = sx >> 4, fx = sx & 15, w, level;
            uint32_t d = depths[x];
            w = (wisp[by][bx] * (16 - fx) + wisp[by][bx + 1] * fx) * (16 - fy) +
                (wisp[by + 1][bx] * (16 - fx) + wisp[by + 1][bx + 1] * fx) * fy;
            for (q = FOG_STEPS; q && d > threshold[q]; q--) {}
            level = q * 4 + w * density / (256 * 256 / 2);
            if (level < 0) level = 0;
            /* Open sky whites out; solid ground stays a faint silhouette. */
            if (level > (d ? 50 : 64)) level = d ? 50 : 64;
            level = (level * 4 + dither[x & 3] * 4) >> 6;
            if (level == 4) pixels[x] = fog_full[lit];
            else if (level) pixels[x] = fog_map[lit][level - 1][pixels[x]];
        }
    }
}
static void weather_overlay(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(camera_position, 1, FP), p->center);
    int density = cloud_density(p, local, game->tick), rain = precipitation(p, local, game->tick);
    int x, y, i, lit = surface_light(&game->system, p, local) > 0;
    if (density) {
        cloud_fog(density, lit);
        text(8, 32, 15, "IN CLOUD");
    }
    if (rain) {
        for (i = 0; i < (frame->enhanced ? 48 : 24); i++) {
            uint32_t h = hash32(p->seed + (unsigned)i * 7919U);
            x = (int)(h % (unsigned)frame->w);
            y = (int)(((h >> 16) + game->tick * (rain == 1 ? 9U : 2U)) % (unsigned)view_h);
            rect(x, y, rain == 1 ? 1 : 2, rain == 1 ? 7 : 2, lit ? 7 : 8);
        }
        text(8, 44, 15, rain == 1 ? "RAIN" : "SNOW");
    }
}
/* NPC hulls point along their velocity through the local air when inside an
   atmosphere (as the autopilot reckons it), else through the frame, and roll
   level with the nearest body. Below 2 m/s they hold a fixed level heading. */
static void npc_ship(const Npc *n, int ship, Vec p) {
    const ShipSpec *spec = &ship_specs[ship];
    const Planet *body = &game->system.planets[nearest_body(game, n->pos)];
    Vec local = sub(p, body->center), level = unit(local, 16384), vel = n->vel, front, right, up;
    int64_t distance = length(sub(p, scale(camera_position, 1, FP)));
    if (!length(level))
        level = vec(0, 16384, 0);
    if (length(local) < (int64_t)body->radius + body->atmosphere + TERRAIN_MAX_HEIGHT + 2000)
        vel = sub(vel, surface_velocity(body, local));
    if (length(vel) < 2 * FP) {
        right = cross(level, vec(0, 0, 16384));
        if (length(right) < 2048)
            right = cross(level, vec(16384, 0, 0));
        right = unit(right, 16384);
        front = unit(cross(right, level), 16384);
    } else {
        front = unit(vel, 16384);
        right = cross(level, front);
        /* Climbing or diving straight down the vertical: any roll will do,
           but it must not flip frame to frame. */
        if (length(right) < 2048)
            right = cross(abs((int)front.z) < 12000 ? vec(0, 0, 16384) : vec(16384, 0, 0), front);
        right = unit(right, 16384);
    }
    up = cross(front, right);
    /* Past the range where the half-length spans 8 pixels the loft is a few
       pixels of noise; an oriented tetrahedron keeps 68000 frame time flat. */
    if (distance * 8 > (int64_t)spec->depth * (frame->w / 2)) {
        Vec a = add(p, scale(front, spec->depth, 16384));
        Vec tail = sub(p, scale(front, spec->depth / 2, 16384));
        Vec b = sub(tail, scale(right, spec->width, 16384)), c = add(tail, scale(right, spec->width, 16384));
        Vec d = add(sub(p, scale(front, spec->depth / 4, 16384)), scale(up, spec->height, 16384));
        triangle(a, b, d, 23);
        triangle(a, d, c, 24);
        triangle(b, c, d, 22);
        triangle(a, c, b, 21);
        return;
    }
    ship_mesh(ship, p, right, up, front, 1, unit(sub(game->system.star[0].center, p), 16384));
}
static void world(void) {
    int i, x, b;
    cache_globes();
    stellar_discs();
    for (b = 0; b < game->system.count; b++) {
        planet_mesh(b);
        if (game->system.planets[b].rings)
            ring_mesh(&game->system.planets[b]);
    }
    forests();
    cloud_deck();
    for (i = 0; i < port_count(game); i++) {
        const Port *port = &game->ports[i];
        const Planet *body = &game->system.planets[port->body];
        Vec p = scale(port->pos, 1, FP), local = sub(p, body->center), up = unit(local, 16384);
        Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), front;
        if (!length(right))
            right = vec(16384, 0, 0);
        front = cross(right, up);
        if (length(sub(p, scale(camera_position, 1, FP))) > 80000)
            continue;
        if (port->surface) {
            for (x = 0; x < 36; x++) {
                uint32_t h = hash32(game->system.seed + (unsigned)x * 173 + (unsigned)i);
                int ox = (x % 6 - 3) * 180 + 70, oz = (x / 6 - 3) * 180 + 70;
                Vec offset, v;
                if (abs(ox) < 150 && abs(oz) < 150)
                    ox += 350;
                offset = add(scale(right, ox, 16384), scale(front, oz, 16384));
                v = add(local, offset);
                v = unit(v, body->radius + terrain_height(body, v));
                box(add(body->center, v), up, 25 + h % 30,
                    x % 7 == 0 ? 200 + h % 250 : 40 + h % 100, 16);
                /* Streets meet the actual curved ground, between city blocks. */
                {
                    Vec a = terrain_vertex(body, local, right, front, ox - 80, oz - 80);
                    Vec b2 = terrain_vertex(body, local, right, front, ox + 100, oz - 80);
                    Vec c = terrain_vertex(body, local, right, front, ox + 100, oz - 60);
                    Vec d = terrain_vertex(body, local, right, front, ox - 80, oz - 60);
                    triangle(a, b2, c, 3);
                    triangle(a, c, d, 3);
                    a = terrain_vertex(body, local, right, front, ox - 80, oz - 80);
                    b2 = terrain_vertex(body, local, right, front, ox - 60, oz - 80);
                    c = terrain_vertex(body, local, right, front, ox - 60, oz + 100);
                    d = terrain_vertex(body, local, right, front, ox - 80, oz + 100);
                    triangle(a, b2, c, 3);
                    triangle(a, c, d, 3);
                }
            }
            box(add(body->center, unit(local, body->radius)), up, 65, 4, 13);
        } else {
            for (x = 0; x < 8; x++) {
                Vec d = add(scale(right, cosi(x * 128 + port->rotation) * 160 / 16384, 16384),
                            scale(front, sini(x * 128 + port->rotation) * 160 / 16384, 16384));
                box(add(p, d), up, 45, 50, 16);
            }
            for (x = 0; x < 16; x++) {
                Vec da = add(scale(right, cosi(x * 64 + port->rotation), 16384),
                             scale(front, sini(x * 64 + port->rotation), 16384));
                Vec db = add(scale(right, cosi((x + 1) * 64 + port->rotation), 16384),
                             scale(front, sini((x + 1) * 64 + port->rotation), 16384));
                Vec a = add(p, scale(da, 60, 16384)), b2 = add(p, scale(db, 60, 16384));
                Vec c = add(p, scale(db, 240, 16384)), d = add(p, scale(da, 240, 16384));
                triangle(a, b2, c, 18);
                triangle(a, c, d, 18);
            }
            if (i == game->target)
                for (x = 1; x <= 4; x++) {
                    Vec gate = add(p, scale(up, x * 500, 16384));
                    Vec a = add(gate, scale(right, -60, 16384));
                    Vec b2 = add(gate, scale(right, 60, 16384));
                    triangle(a, b2, add(b2, scale(front, 3, 16384)), 13);
                    triangle(a, add(b2, scale(front, 3, 16384)), add(a, scale(front, 3, 16384)),
                             13);
                }
            /* The central approach is open, rather than a solid box in the docking path. */
        }
    }
    for (i = 0; i < MAX_NPC; i++)
        if (game->npc[i].active) {
            Vec p = scale(game->npc[i].pos, 1, FP);
            if (game->npc_role[i] == INSTALLATION_TARGET) {
                const Planet *body = &game->system.planets[nearest_body(game, game->npc[i].pos)];
                Vec up = unit(sub(p, body->center), 16384);
                box(p, up, 75, 100, 23);
                box(add(p, scale(up, 100, 16384)), up, 20, 60, 24);
                continue;
            }
            npc_ship(&game->npc[i], game->npc_ship[i], p);
        }
    for (i = 0; i < ASTEROIDS; i++) {
        Vec p = scale(asteroid_position(game, i), 1, FP);
        if (length(sub(p, scale(camera_position, 1, FP))) < 100000) {
            box(add(p, vec(0, -90, 0)), vec(0, 16384, 0), 90, 180, 6);
        }
    }
    for (i = 0; i < game->rig_count; i++) {
        const MiningRig *r = &game->rigs[i];
        if (r->sector.x == game->system.sector.x && r->sector.y == game->system.sector.y &&
            r->sector.z == game->system.sector.z) {
            const Planet *p = &game->system.planets[r->body];
            Vec pos = rig_position(game, i);
            box(scale(pos, 1, FP), unit(sub(pos, scale(p->center, FP, 1)), 16384), 12, 20, 12);
        }
    }
    for (i = 0; i < MAX_PODS; i++) if (game->pods[i].active) {
        Vec p = scale(game->pods[i].pos, 1, FP);
        box(p, vec(0, 16384, 0), 12, 24, game->pods[i].mission >= 0 ? 13 : 12);
    }
    for (i = 0; i < MAX_MISSILES; i++) if (game->projectiles[i].active)
        box(scale(game->projectiles[i].pos, 1, FP), vec(0, 16384, 0), 4, 8, 11);
    if (game->camera_mode == 2) {
        Vec p = scale(game->pos, 1, FP), f = forward(game->yaw, game->pitch);
        Vec right = vec(cosi(game->yaw), 0, -sini(game->yaw)), up = unit(cross(f, right), 16384);
        Vec level_right = right;
        right = sub(scale(right, cosi(game->roll), 16384), scale(up, sini(game->roll), 16384));
        up = add(scale(level_right, sini(game->roll), 16384), scale(up, cosi(game->roll), 16384));
        ship_mesh(game->ship, p, right, up, f, 1, unit(sub(game->system.star[0].center, p), 16384));
    }
    draw_triangles();
}
static void panel(const char *title) {
    rect(4, 4, frame->w - 8, view_h - 19, 1);
    rect(4, 4, frame->w - 8, 2, 13);
    text(12, 13, 13, title);
}
static int selected_body(void) {
    return game->target >= 0 ? game->ports[game->target].body : game->target_body;
}
/* A top-down true-scale projection. Local mode resolves moons and ports that
   are necessarily coincident at AU scale. All arithmetic stays in metres. */
static void map_point(Vec p, Vec origin, int64_t extent, int *x, int *y) {
    ui_map_point(p, origin, extent, x, y);
}
static void orbit_path(Vec origin, Vec center, int64_t radius, int inclination,
                       int64_t extent) {
    int a, px = 0, py = 0;
    for (a = 0; a <= 1024; a += 16) {
        Vec p = add(center, scale(vec(sini(a), 0, (int64_t)cosi(a) * cosi(inclination) / 16384),
                                  radius, 16384));
        int x, y;
        map_point(p, origin, extent, &x, &y);
        if (a && x >= 12 && x < frame->w - 12 && y >= 30 && y <= 162 &&
            px >= 12 && px < frame->w - 12 && py >= 30 && py <= 162)
            line(px, py, x, y, 3);
        px = x;
        py = y;
    }
}
static void orbital_map(void) {
    int i, x, y, b = selected_body(), parent = -1;
    int64_t extent = 1;
    Vec origin = vec(0, 0, 0);
    char s[96];
    ui_orbit_geometry(game, &origin, &extent, &parent);
    panel(game->map_local ? "LOCAL ORBITS / TOP DOWN" : "SYSTEM ORBITS / TOP DOWN");
    for (i = 0; i < game->system.count; i++) {
        const Planet *p = &game->system.planets[i];
        Vec center = p->parent >= 0 ? game->system.planets[p->parent].center : vec(0, 0, 0);
        if (game->map_local && i != parent && p->parent != parent)
            continue;
        if ((!game->map_local || i != parent) && p->catalog && p->orbit*62/extent>=2) {
            int a,x1,y1,x2,y2;
            for(a=0;a<1024;a+=32) {
                map_point(add(center,body_orbit_sample(p,a)),origin,extent,&x1,&y1);
                map_point(add(center,body_orbit_sample(p,a+32)),origin,extent,&x2,&y2);
                line(x1,y1,x2,y2,3);
            }
        } else if ((!game->map_local || i != parent) && !p->catalog)
            orbit_path(origin, center, p->orbit, p->inclination, extent);
        map_point(p->center, origin, extent, &x, &y);
        rect(x - 2, y - 2, 5, 5, i == b ? 12 : p->settled ? 11 : 13);
        if (i == b || game->map_local)
            text(x + 5, y - 3, i == b ? 12 : 7, p->name);
    }
    if (!game->map_local)
        for (i = 0; i < game->system.stars; i++) {
            orbit_path(origin, vec(0, 0, 0), game->system.star[i].orbit, 0, extent);
            map_point(game->system.star[i].center, origin, extent, &x, &y);
            rect(x - 2, y - 2, 5, 5, 15);
        }
    for (i = 0; i < game->nports; i++) {
        if (i != game->target && (!game->map_local || game->ports[i].body != parent))
            continue;
        map_point(scale(game->ports[i].pos, 1, FP), origin, extent, &x, &y);
        if (x >= 12 && x < frame->w - 12 && y >= 30 && y <= 162) {
            line(x - 3, y, x + 3, y, i == game->target ? 12 : 7);
            line(x, y - 3, x, y + 3, i == game->target ? 12 : 7);
        }
    }
    map_point(scale(game->pos, 1, FP), origin, extent, &x, &y);
    if (x >= 12 && x < frame->w - 12 && y >= 30 && y <= 162) {
        line(x - 3, y + 3, x, y - 3, 14);
        line(x, y - 3, x + 3, y + 3, 14);
    }
    snprintf(s, sizeof(s), "RADIUS %ld %s / SHIP RED / TARGET GOLD",
             (long)(extent / (game->map_local ? 1000 : AU_METRES / 100)),
             game->map_local ? "KM" : "0.01 AU");
    text(12, 161, 7, s);

}
/* Durations pick hours, days or years so every body fits one short field. */
static void duration_text(char *s, size_t n, uint64_t seconds) {
    uint64_t tenths = seconds / 360;
    if (tenths < 480) snprintf(s, n, "%lu.%lu H", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    else if ((tenths /= 24) < 10000) snprintf(s, n, "%lu.%lu D", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    else { tenths = seconds / 3155760; snprintf(s, n, "%lu.%lu Y", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10)); }
}
static void pressure_text(char *s, size_t n, int32_t pa) {
    if (pa < 0) snprintf(s, n, "NO SOLID SURFACE");
    else if (pa == 0) snprintf(s, n, "VACUUM");
    else if (pa >= 100000) snprintf(s, n, "%ld.%02ld BAR", (long)(pa / 100000), (long)(pa / 1000 % 100));
    else if (pa >= 100) snprintf(s, n, "%ld MBAR", (long)(pa / 100));
    else snprintf(s, n, "%ld PA", (long)pa);
}
static void system_data(void) {
    static const char *economies[] = {"AGRICULTURE", "INDUSTRY", "TECHNOLOGY", "EXTRACTION"};
    static const char *governments[] = {"COUNCIL", "FEDERATION", "CORPORATE", "INDEPENDENT"};
    const Planet *p = &game->system.planets[selected_body()];
    int j = exploration_index(game, game->system.sector), b = selected_body(), ports = 0, i, y = 34;
    int64_t earths, gees;
    BodyFacts f;
    char s[96], a[24], c[24];
    body_facts(&game->system, b, &f);
    panel("SYSTEM / BODY DATA");
    snprintf(s, sizeof(s), "%s / %s%s", p->name, body_names[p->type], p->rings ? " / RINGS" : "");
    text(12, y, 12, s); y += 9;
    if (p->catalog == 2) {
        text(12, y, 15, "SIZE/GRAVITY UNKNOWN - PROXY GEOMETRY"); y += 9;
    } else {
        snprintf(s, sizeof(s), "RADIUS %ld KM / DENSITY %d.%02d G/CM3", (long)(p->radius / 1000),
                 f.density / 100, f.density % 100);
        text(12, y, 15, s); y += 9;
        /* Earth masses in thousandths: mass/100 * 10^e / 5.97e24. */
        earths = f.mass_exponent >= 20 ? (int64_t)f.mass * 10000000 / 5970 : 0;
        for (i = f.mass_exponent; i < 27; i++) earths /= 10;
        if (earths >= 10000) snprintf(a, sizeof(a), " / %ld EARTHS", (long)(earths / 1000));
        else if (earths) snprintf(a, sizeof(a), " / %ld.%03ld EARTHS", (long)(earths / 1000), (long)(earths % 1000));
        else a[0] = 0;
        snprintf(s, sizeof(s), "MASS %d.%02dE%d KG%s", f.mass / 100, f.mass % 100, f.mass_exponent, a);
        text(12, y, 15, s); y += 9;
        if (f.gravity >= 1000) snprintf(a, sizeof(a), "%ld.%02ld", (long)(f.gravity / 1000), (long)(f.gravity / 10 % 100));
        else snprintf(a, sizeof(a), "0.%03ld", (long)f.gravity);
        if (f.escape >= 1000) snprintf(c, sizeof(c), "%ld.%ld KM/S", (long)((f.escape + 50) / 1000), (long)((f.escape + 50) / 100 % 10));
        else snprintf(c, sizeof(c), "%ld M/S", (long)f.escape);
        /* Standard gravities, with a fourth decimal for small moons. */
        gees = ((int64_t)f.gravity * 10000 + 4903) / 9807;
        if (gees >= 1000) snprintf(s, sizeof(s), "GRAVITY %s M/S2 / %ld.%03ld G / ESCAPE %s", a,
                                   (long)((gees + 5) / 10000), (long)((gees + 5) / 10 % 1000), c);
        else snprintf(s, sizeof(s), "GRAVITY %s M/S2 / 0.%04ld G / ESCAPE %s", a, (long)gees, c);
        text(12, y, 15, s); y += 9;
    }
    snprintf(s, sizeof(s), "%s %d K / %d C", f.pressure < 0 ? "TEMPERATURE AT 1 BAR" : "MEAN TEMPERATURE",
             f.temperature, f.temperature - 273);
    text(12, y, 15, s); y += 9;
    pressure_text(a, sizeof(a), f.pressure);
    if (p->atmosphere) snprintf(s, sizeof(s), "PRESSURE %s / ATMOSPHERE %ld KM DEEP", a, (long)(p->atmosphere / 1000));
    else snprintf(s, sizeof(s), "PRESSURE %s", a);
    text(12, y, 15, s); y += 9;
    snprintf(s, sizeof(s), "COMPOSITION %s", f.air);
    text(12, y, 15, s); y += 9;
    duration_text(a, sizeof(a), rotation_period(p));
    duration_text(c, sizeof(c), p->period);
    snprintf(s, sizeof(s), "DAY %s / ORBIT %s / INC %d DEG", a, c, p->inclination * 360 / ANGLE);
    text(12, y, 15, s); y += 9;
    if (p->parent >= 0) snprintf(a, sizeof(a), "%ld KM", (long)(p->orbit / 1000));
    else snprintf(a, sizeof(a), "%ld.%02ld AU", (long)(p->orbit * 100 / AU_METRES / 100), (long)(p->orbit * 100 / AU_METRES % 100));
    snprintf(s, sizeof(s), "DISTANCE %s FROM %.18s", a, p->parent >= 0 ? game->system.planets[p->parent].name : "STARS");
    if (p->catalog) snprintf(s + strlen(s), sizeof(s) - strlen(s), " / E %d.%03d", p->eccentricity / 1000000, p->eccentricity / 1000 % 1000);
    text(12, y, 15, s); y += 9;
    for (i = 0; i < game->nports; i++)
        ports += game->ports[i].body == b;
    snprintf(s, sizeof(s), "%d PORTS / %s / RANGE %ld KM", ports,
             (same_sector(game->system.sector,catalog_sector(0)) ? (game->solar_surveyed[b/32] & (1U << (b%32))) : j >= 0 && b<GENERATED_PLANETS && (game->explored[j].surveyed & (1U << b))) ? "SURVEYED" : "UNSURVEYED",
             (long)(length(sub(navigation_position(game), game->pos)) / (FP * 1000)));
    text(12, y, 13, s); y += 9;
    snprintf(s, sizeof(s), "%s / %d STARS / %d BODIES / %s", game->system.name, game->system.stars,
             game->system.count, governments[game->system.government]);
    text(12, y, 7, s); y += 9;
    snprintf(s, sizeof(s), "%s ECONOMY", economies[game->system.economy]);
    text(12, y, 7, s);
}
/* Radar follows the hull heading even when a turret or rear camera is used.
   Horizontal distance uses X/Z; a short stem shows signed vertical separation. */
static void sensors(void) {
    int selected = combat_target(game);
    char s[80];
    text(7, 20, 7, game->camera_mode == 0 ? "FRONT" : game->camera_mode == 1 ? "REAR" :
         game->camera_mode == 2 ? "EXTERNAL" : game->camera_mode == 3 ? "DORSAL TURRET" : "VENTRAL TURRET");
    if (selected >= 0) {
        const Npc *n = &game->npc[selected];
        Vec rel = sub(n->pos, game->pos), rv = sub(n->vel, game->vel), dir = unit(rel, 16384);
        Vec p = camera(scale(n->pos, 1, FP));
        int64_t closing = -(rv.x * dir.x + rv.y * dir.y + rv.z * dir.z) / (16384 * FP);
        snprintf(s, sizeof(s), "R %s %d / HULL %d", n->hostile ? "HOSTILE" : game->npc_role[selected] == POLICE ? "POLICE" : "CONTACT", selected + 1, n->hull);
        text(7, 32, 12, s);
        snprintf(s, sizeof(s), "%ld M / CLOSING %ld M/S", (long)(length(rel) / FP), (long)closing);
        text(7, 44, 12, s);
        int x, y;
        target_marker_position(p, 56, &x, &y);
        line(x - 6, y, x, y - 6, 12);
        line(x, y - 6, x + 6, y, 12);
        line(x + 6, y, x, y + 6, 12);
        line(x, y + 6, x - 6, y, 12);
    } else
        text(7, 32, 7, "R SELECT RADAR CONTACT");
}
static void menus(void) {
    char s[110];
    int i;
    switch (game->page) {
    case MAP: break;
    case SYSTEM_MAP: {
        if (game->system_display == 1) {
            orbital_map();
            break;
        }
        if (game->system_display == 2) {
            system_data();
            break;
        }
        int first = game->target < 0 ? 0 : game->target / 8 * 8;
        panel("SYSTEM / FLIGHT DESTINATIONS");
        if (game->target < 0) {
            int first_body = game->target_body / 8 * 8;
            for (i = first_body; i < first_body + 8 && i < game->system.count; i++) {
                const Planet *p = &game->system.planets[i];
                int journal = exploration_index(game, game->system.sector);
                snprintf(s, sizeof(s), "%s %s %ld KM %s", p->name, body_names[p->type],
                         (long)(p->radius / 1000),
                         (same_sector(game->system.sector,catalog_sector(0)) ? (game->solar_surveyed[i/32] & (1U << (i%32))) : journal >= 0 && i<GENERATED_PLANETS && (game->explored[journal].surveyed & (1U << i))) ? "SCAN"
                                                                                        : "");
                text(12, 32 + (i - first_body) * 16, i == game->target_body ? 12 : 15, s);
            }
            text(12, 174, 7, "Q NEXT BODY / T PORT / P AUTO");
            break;
        }
        for (i = first; i < first + 8 && i < port_count(game); i++) {
            snprintf(s, sizeof(s), "%2d %s", i + 1, game->ports[i].name);
            text(12, 32 + (i - first) * 16, i == game->target ? 12 : 15, s);
        }
        if (game->target >= 0) {
            const Planet *p = &game->system.planets[game->ports[game->target].body];
            snprintf(s, sizeof(s), "%s R %ld KM / %s", body_names[p->type],
                     (long)(p->radius / 1000), p->parent >= 0 ? "LOCAL MOON FRAME" : "SOLAR ORBIT");
            text(12, 158, 13, s);
        }
        text(12, 174, 7, "UP DOWN PORT / Q BODY / P AUTO");
        break;
    }
    case BOARD:
        panel("PORT / CONTRACT EXCHANGE");
        if (game->docked < 0) {
            text(12, 40, 12, "DOCK TO ACCEPT CONTRACTS");
            break;
        }
        for (i = 0; i < MAX_MISSIONS; i++) {
            const Mission *m = &game->board[i];
            snprintf(s, sizeof(s), "%d %s %d CR %s", i + 1, mission_names[m->type], m->reward,
                     m->state == OFFERED ? "" : "TAKEN");
            text(12, 31 + i * 16, i == game->selection ? 12 : 15, s);
        }
        {
            const Mission *m = &game->board[game->selection];
            snprintf(s, sizeof(s), "%ld/%ld P%d %dT %dP REP%d", (long)m->destination.x,
                     (long)m->destination.z, m->port + 1, m->quantity, m->passengers, m->required_rep);
            text(12, 158, 13, s);
        }
        text(12, 174, 7, "ENTER ACCEPT / + TERMS / B MORE");
        break;
    case MARKET:
        snprintf(s, sizeof(s), "MARKET / %s",
                 game->docked >= 0 && game->docked < game->nports
                     ? game->ports[game->docked].name : "DOCK TO TRADE");
        panel(s);
        text(12, 28, 7, "ITEM         BUY SELL STOCK HOLD");
        for (i = 0; i < GOODS; i++) {
            snprintf(s, sizeof(s), "%d %-9s %4d %4d %4d %3d", i + 1, goods_names[i], price(game, i),
                     sell_price(game, i), market_stock(game, i), game->cargo[i]);
            text(12, 42 + i * 14, i == game->selection ? 12 : 15, s);
        }
        snprintf(s, sizeof(s), "HOLD %d/%d T - 1-8 SELECT  +/- TRADE", cargo_used(game),
                 ship_capacity[game->ship]);
        text(12, 168, 7, s);
        text(12, 181, 7, game->black_market ? "B REGULATED / BLACK MARKET" : "B BLACK MARKET / REGULATED");
        break;
    case SHIPYARD:
        panel("PORT / SHIP CATALOGUE");
        for (i = 0; i < SHIPS; i++) {
            const ShipSpec *spec = &ship_specs[i];
            snprintf(s, sizeof(s), "%d %-9s %3dT %dCREW %5dCR", i + 1,
                     spec->name, spec->capacity, spec->crew, spec->cost);
            text(12, 30 + i * 16, i == game->selection ? 12 : game->ship == i ? 13 : 15, s);
        }
        { const ShipSpec *spec = &ship_specs[game->ship];
          snprintf(s, sizeof(s), "%s %dT MASS / %d M/S2 / CREW %d", spec->name, spec->mass,
                   spec->acceleration, game->crew); text(12, 132, 13, s); }
        text(12, 150, 7, "1-6 ENTER BUY / F SERVICE / O LASER");
        text(12, 170, 7, "0 EQUIPMENT / 9 CREW AND FACTIONS");
        break;
    case EQUIPMENT:
        panel("PORT / EQUIPMENT AND HARDPOINTS");
        for (i = 0; i < EQUIP_TYPES; i++) {
            int count = i == CABIN ? game->cabins : i == MISSILE_RACK ? game->missiles :
                        i == COUNTERMEASURE ? game->countermeasures : equipped(game, (Equipment)i);
            snprintf(s, sizeof(s), "%d %-19s %4dCR %d", i + 1, equipment_names[i], equipment_cost[i], count);
            text(12, 30 + i * 16, i == game->selection ? 12 : 15, s);
        }
        text(12, 165, 7, "ENTER BUY / - SELL / O MOUNT LASER");
        break;
    case CAREER:
        panel("COMMANDER / CREW AND FACTIONS");
        snprintf(s, sizeof(s), "CREW %d/%d / %d CR PER HOUR", game->crew,
                 ship_specs[game->ship].crew, game->crew * 25);
        text(12, 30, 15, s);
        snprintf(s, sizeof(s), "PASSENGERS %d/%d / CABINS %d", passengers_used(game), game->cabins * 2, game->cabins);
        text(12, 46, 15, s);
        for (i = 0; i < FACTIONS; i++) {
            snprintf(s, sizeof(s), "%s REP %d FINE %d", faction_names[i], game->reputation[i], game->wanted[i]);
            text(12, 70 + i * 29, system_faction(game) == i ? 12 : 13, s);
            snprintf(s, sizeof(s), "%s / SERVICE %d MEDALS %d", military_rank(game, i), game->service[i], game->medals[i]);
            text(12, 81 + i * 29, 7, s);
        }
        text(12, 163, 7, "+ HIRE 200 / - DISMISS / F PAY FINE");
        text(12, 177, 7, "0 EQUIPMENT / B CONTRACTS / V FLIGHT");
        break;
    case LOG:
        panel("COMMANDER / CONTRACT LOG");
        for (i = 0; i < MAX_MISSIONS; i++) {
            const Mission *m = &game->contracts[i];
            if (!m->id)
                continue;
            snprintf(s, sizeof(s), "%s %s", mission_names[m->type],
                     m->state == ACTIVE     ? "ACTIVE"
                     : m->state == COMPLETE ? "PAID"
                                            : "FAILED");
            text(12, 30 + i * 13, 13, s);
            snprintf(s, sizeof(s), "  %ld/%ld P%d LEG%d/%d %ldS", (long)m->destination.x,
                     (long)m->destination.z, m->port + 1, m->leg, m->legs,
                     (long)(m->deadline > game->tick ? (m->deadline - game->tick) / HZ : 0));
            text(12, 37 + i * 13, 7, s);
        }
        break;
    case HELP:
        panel("ASTRALBOUND / FLIGHT MANUAL");
        {
            const char *help[] = {
                "ARROWS ROLL PITCH / W S SET SPEED",  "CTRL FIRE / E ASSIST / A DRIVE",
                "L LAUNCH / Y PORTS / T PORT / Q BODY", "K GEAR / D CLEARANCE / F SERVICE",
                "[ ] TIME TO 4096X / SPACE PAUSE",          "G MAP / DRAG ROTATE / +/- ZOOM",
                "B CONTRACTS / M MARKET / U SHIPYARD",  "J CONTRACT LOG / V FLIGHT OR CAMERA",
                "F5 SAVE / F9 LOAD / ESC QUIT DIALOG",         "R CONTACT / Z ORBITS / I BODY DATA",
                "0 EQUIP / 9 CREW / , MISSILE . DECOY", "BACKSLASH ROCK / O MINE N CARGO / / POD"};
            for (i = 0; i < 12; i++)
                text(12, 30 + i * 9, i < 9 ? 15 : 7, help[i]);
        }
        break;
    default:
        break;
    }
}
static void bevel(int x,int y,int w,int h,int selected) {
    rect(x,y,w,h,selected?19:16);
    line(x,y,x+w-1,y,selected?3:20); line(x,y,x,y+h-1,selected?3:20);
    line(x,y+h-1,x+w-1,y+h-1,selected?20:0); line(x+w-1,y,x+w-1,y+h-1,selected?20:0);
}
static void gui_rows(void) {
    char s[100]; int i;
    for(i=0;i<8;i++) {
        switch(game->page) {
        case MARKET:
            snprintf(s,sizeof(s),"%-9s %4d %4d %4d %3d",goods_names[i],price(game,i),sell_price(game,i),market_stock(game,i),game->cargo[i]); break;
        case BOARD:
            snprintf(s,sizeof(s),"%-14s %5d CR  %s",mission_names[game->board[i].type],game->board[i].reward,game->board[i].state==OFFERED?"OFFER":"TAKEN"); break;
        case EQUIPMENT:
            snprintf(s,sizeof(s),"%-19s %4d CR  %d",equipment_names[i],equipment_cost[i],i==CABIN?game->cabins:i==MISSILE_RACK?game->missiles:i==COUNTERMEASURE?game->countermeasures:equipped(game,(Equipment)i)); break;
        case SYSTEM_MAP: {
            int first=game->target<0?game->target_body/8*8:game->target/8*8;
            if(first+i>=(game->target<0?game->system.count:game->nports)) continue;
            if(game->target<0) snprintf(s,sizeof(s),"%-20s %s",game->system.planets[first+i].name,body_names[game->system.planets[first+i].type]);
            else snprintf(s,sizeof(s),"%-20s %s",game->ports[first+i].name,game->ports[first+i].surface?"SURFACE":"ORBITAL");
            break;
        }
        default: continue;
        }
        text(13,44+i*12,i==game->selection && game->page!=SYSTEM_MAP?12:15,s);
    }
}
/* Rotating hull against a starfield, rendered into a scissored inset of the
   frame. Coordinates are logical (320-wide); the inset itself is native. */
static void ship_preview(int ship, int x, int y, int w, int h) {
    static const int elevation = 32; /* about 11 degrees above the hull */
    const ShipSpec *spec = &ship_specs[ship];
    int s = draw_scale, vx = x * s, vy = y * s, vw = w * s, vh = h * s, old_h = view_h;
    int ex, ey, front_z, back_z, i, spin = (int)(game->ui_clock * 16 / 5 + 640) & (ANGLE - 1);
    int64_t rx, ry, rz, reach, rise, distance, near;
    Vec f, right, eye, centre;
    if ((size_t)(vy + vh) * frame->w > depth_capacity || !depth_buffer)
        return;
    for (i = 0; i < 40; i++) {
        uint32_t r = hash32((unsigned)i * 2654435761U + 7);
        pixel(x + 2 + (int)(r % (unsigned)(w - 4)), y + 2 + (int)((r >> 12) % (unsigned)(h - 4)), i % 7 ? 7 : 15);
    }
    /* Fit the swept bounding cylinder, units of 1/16 m, with room for
       perspective: the nearest point is up to a third of a radius closer. */
    ship_extent(ship, &ex, &ey, &front_z, &back_z);
    rx = (int64_t)ex * spec->width; ry = (int64_t)ey * spec->height;
    rz = (int64_t)(front_z - back_z) * spec->depth / 2;
    reach = (int64_t)isqrt64((uint64_t)(rx * rx + rz * rz));
    rise = ry * cosi(elevation) / 16384 + reach * sini(elevation) / 16384;
    distance = (int64_t)(frame->w / 2) * reach * 100 / (vw * 48);
    near = (int64_t)(frame->w / 2) * rise * 100 / (vh * 46);
    distance = (distance > near ? distance : near) + reach / 3;
    eye = vec(0, distance * sini(elevation) / 16384, -distance * cosi(elevation) / 16384);
    camera_position = scale(eye, FP, 1);
    camera_yaw = 0; camera_pitch = 0; camera_roll = 0;
    aim_angles(sub(vec(0, 0, 0), eye), &camera_yaw, &camera_pitch);
    view_h = vy + vh; view_top = vy; view_left = vx; view_right_inset = frame->w - vx - vw;
    proj_dx = vx + vw / 2 - frame->w / 2; proj_dy = vy + vh / 2 - view_h / 2;
    terrain_surface = NULL;
    nt = 0;
    memset(depth_buffer + (size_t)vy * frame->w, 0, (size_t)vh * frame->w * sizeof(*depth_buffer));
    f = forward(spin, 0);
    right = vec(cosi(spin), 0, -sini(spin));
    centre = scale(f, -(int64_t)(front_z + back_z) * spec->depth / 2, 16384);
    ship_mesh(ship, centre, right, vec(0, 16384, 0), f, 16, unit(vec(-7000, 12000, -9000), 16384));
    draw_triangles();
    nt = 0;
    view_h = old_h; view_top = view_left = view_right_inset = proj_dx = proj_dy = 0;
}
static void shipyard(void) {
    static const char *roles[SHIPS] = {"RUNABOUT", "LIGHT TRADER", "HEAVY FREIGHTER",
                                       "INTERCEPTOR", "MINING HAULER", "LONG RANGE LINER"};
    static const char *labels[6] = {"HOLD", "CREW", "ACCL", "MASS", "MSL", "TUR"};
    const ShipSpec *spec, *own = &ship_specs[game->ship];
    int h = frame->h / draw_scale, top = 36, bottom = h - 88, i, sel = game->selection;
    const char *refusal;
    char s[40];
    if (sel < 0 || sel >= SHIPS) sel = 0;
    spec = &ship_specs[sel];
    for (i = 0; i < SHIPS; i++) {
        const ShipSpec *row = &ship_specs[i];
        int c = i == sel ? 12 : i == game->ship ? 13 : 15;
        text(12, top + 3 + i * 13, c, row->name);
        if (i == game->ship) snprintf(s, sizeof(s), "OWNED");
        else snprintf(s, sizeof(s), "%d", row->cost);
        text(114 - (int)strlen(s) * 6, top + 3 + i * 13, i != game->ship && row->cost > game->credits ? 7 : c, s);
    }
    /* Purchase status mirrors buy_ship, so a refusal is explained before ENTER. */
    refusal = ship_refusal(game, sel);
    snprintf(s, sizeof(s), "PRICE %d CR", spec->cost);
    text(8, top + 81, 15, s);
    if (refusal && game->credits < spec->cost && sel != game->ship)
        snprintf(s, sizeof(s), "NEED %d MORE", spec->cost - game->credits);
    else snprintf(s, sizeof(s), "%s", refusal ? refusal : "READY TO BUY");
    text(8, top + 91, refusal ? (sel == game->ship ? 13 : 14) : 11, s);
    text(8, top + 101, 7, "UP DOWN / ENTER");
    bevel(122, top, 190, bottom - top, 0);
    rect(123, top + 1, 188, bottom - top - 2, 0);
    ship_preview(sel, 123, top + 1, 188, bottom - top - 2);
    for (i = 0; i < 4; i++) {
        int cx = i & 1 ? 309 : 125, cy = i & 2 ? bottom - 3 : top + 2, dx = i & 1 ? -6 : 6, dy = i & 2 ? -4 : 4;
        line(cx, cy, cx + dx, cy, 13); line(cx, cy, cx, cy + dy, 13);
    }
    text(128, top + 5, 15, spec->name);
    text(128, top + 14, 7, roles[sel]);
    if (sel == game->ship) text(304 - 12 * 6, top + 5, 13, "CURRENT HULL");
    /* Catalogue bars scale to the best hull; the white tick is the current one. */
    for (i = 0; i < 6; i++) {
        int x = 122 + (i / 3) * 96, y = bottom + 4 + (i % 3) * 10, j, best = 1, value, mine, c;
        for (j = 0; j < SHIPS; j++) {
            const ShipSpec *k = &ship_specs[j];
            int v = i == 0 ? k->capacity : i == 1 ? k->crew : i == 2 ? k->acceleration :
                    i == 3 ? k->mass : i == 4 ? k->missiles : (k->mounts & 1) + (k->mounts >> 1 & 1);
            if (v > best) best = v;
        }
        value = i == 0 ? spec->capacity : i == 1 ? spec->crew : i == 2 ? spec->acceleration :
                i == 3 ? spec->mass : i == 4 ? spec->missiles : (spec->mounts & 1) + (spec->mounts >> 1 & 1);
        mine = i == 0 ? own->capacity : i == 1 ? own->crew : i == 2 ? own->acceleration :
               i == 3 ? own->mass : i == 4 ? own->missiles : (own->mounts & 1) + (own->mounts >> 1 & 1);
        c = i == 3 || value == mine ? 13 : value > mine ? 11 : 14;
        text(x, y, 7, labels[i]);
        if (i == 5) snprintf(s, sizeof(s), "%s", spec->mounts == 3 ? "DV" : spec->mounts == 1 ? "D" : spec->mounts == 2 ? "V" : "-");
        else snprintf(s, sizeof(s), "%d", value);
        text(x + 47 - (int)strlen(s) * 6, y, 15, s);
        rect(x + 50, y + 1, 42, 5, 0);
        rect(x + 50, y + 1, value * 42 / best, 5, c);
        rect(x + 50 + (mine * 41 / best), y, 1, 7, 15);
    }
}
static void cockpit_scan(int top) {
    int cx=160,cy=top+24,i,a,px=0,py=0;
    rect(102,top+3,115,36,0);
    if(game->ui_weapons) { text(107,top+5,13,"WEAPON CONTROL"); return; }
    for(a=0;a<=ANGLE;a+=32) {
        int x=cx+sini(a)*53/16384,y=cy+cosi(a)*12/16384;
        if(a) line(px,py,x,y,5);
        px=x; py=y;
    }
    line(106,cy,214,cy,3); line(cx,top+11,cx,top+36,3);
    line(134,top+13,186,top+35,3); line(186,top+13,134,top+35,3);
    for(i=0;i<MAX_NPC;i++) {
        const Npc *npc=&game->npc[i]; Vec r;
        int x,y,stem,c;
        if(!npc->active || length(sub(npc->pos,game->pos))>10000LL*FP) continue;
        r=scale(sub(npc->pos,game->pos),1,FP);
        x=(int)((r.x*cosi(game->yaw)-r.z*sini(game->yaw))/16384*50/10000);
        y=(int)((r.x*sini(game->yaw)+r.z*cosi(game->yaw))/16384*10/10000);
        stem=(int)(r.y*8/10000); c=i==combat_target(game)?12:npc->hostile?14:11;
        line(cx+x,cy-y,cx+x,cy-y-stem,c); rect(cx+x-1,cy-y-stem-1,3,3,c);
    }
    rect(cx-1,cy-1,3,3,15); text(130,top+4,7,"SCAN 10KM");
}
static int gui_buttons(UiWidget *v,int n,int s) {
    int i,hover=-1;
    for(i=0;i<n;i++) {
        UiWidget *b=&v[i]; int over=game->ui_visible && game->ui_x/s>=b->x && game->ui_x/s<b->x+b->w && game->ui_y/s>=b->y && game->ui_y/s<b->y+b->h;
        int pressed=game->ui_down && game->ui_capture==b->action && over;
        if(over) hover=i;
        if(b->action==UI_SCAN || (b->action>=UI_BODY && b->action<UI_BODY+MAX_PLANETS) || (b->action>=UI_PORT && b->action<UI_PORT+MAX_PORTS)) continue;
        if(b->action==UI_THROTTLE) {
            rect(b->x,b->y,b->w,b->h,0); rect(b->x+1,b->y+1,game->throttle*(b->w-2)/4000,b->h-2,13); continue;
        }
        if(b->action==GAL_DRAG) continue;
        bevel(b->x,b->y,b->w,b->h,pressed || b->active);
        if(over && b->enabled) line(b->x+1,b->y+1,b->x+b->w-2,b->y+1,13);
        if(b->label[0]) text(b->x+(b->w-(int)strlen(b->label)*6)/2,b->y+(b->h-7)/2,b->enabled?(b->active?12:15):7,b->label);

    }
    return hover;
}
static void galaxy_line(Vec a,Vec b,int colour) {
    int x,y,xx,yy,d;
    if(galaxy_project(game,frame,a,&x,&y,&d) && galaxy_project(game,frame,b,&xx,&yy,&d)) line(x,y,xx,yy,colour);
}
static void galaxy_map(void) {
    int n,i,x,y,d,h=frame->h/draw_scale;
    const GalaxyMarker *m=galaxy_markers(game,frame,&n);
    int64_t step=game->galaxy_extent/4;
    Vec home=galaxy_position(game->system.sector);
    rect(6,36,200,h-104,0);
    for(i=-4;i<=4;i++) {
        galaxy_line(add(game->galaxy_center,vec(i*step,0,-4*step)),add(game->galaxy_center,vec(i*step,0,4*step)),2);
        galaxy_line(add(game->galaxy_center,vec(-4*step,0,i*step)),add(game->galaxy_center,vec(4*step,0,i*step)),2);
    }
    if(game->galaxy_extent>1000000) {
        int arm,r;
        /* Dim arm contours describe aggregate disc density at overview scale. */
        for(arm=0;arm<4;arm++) for(r=1000;r<GALAXY_RADIUS;r+=250) {
            int a=r/16+arm*256,b=(r+250)/16+arm*256;
            galaxy_line(vec((int64_t)r*1000*sini(a)/16384,0,(int64_t)r*1000*cosi(a)/16384),
                vec((int64_t)(r+250)*1000*sini(b)/16384,0,(int64_t)(r+250)*1000*cosi(b)/16384),2);
        }
    }
    for(i=0;i<32;i++) {
        Vec a=add(home,vec(6000LL*sini(i*32)/16384,0,6000LL*cosi(i*32)/16384));
        Vec b=add(home,vec(6000LL*sini((i+1)*32)/16384,0,6000LL*cosi((i+1)*32)/16384));
        galaxy_line(a,b,8);
    }
    if(game->waypoint_set) galaxy_line(home,galaxy_position(game->waypoint),13);
    galaxy_line(home,galaxy_position(game->selected),12);
    for(i=0;i<n;i++) {
        int c=stellar_colour((StellarType)m[i].type),r=m[i].catalog>=0 && game->galaxy_extent<1000000?1:0;
        x=m[i].x; y=m[i].y;
        if(m[i].catalog>=0 && (m[i].type==ST_GIANT || m[i].type==ST_RED_GIANT)) r=2;
        if(m[i].type==ST_BLACK_HOLE && (m[i].catalog>=0 || game->galaxy_extent<=4000)) { line(x-2,y-2,x+2,y-2,25); line(x-2,y+2,x+2,y+2,25); pixel(x-2,y,13); pixel(x+2,y,13); }
        else if(m[i].type==ST_NEUTRON && (m[i].catalog>=0 || game->galaxy_extent<=4000)) { line(x-2,y,x+2,y,13); line(x,y-3,x,y+3,13); pixel(x,y,15); }
        else if(r==1) { line(x-1,y,x+1,y,c); line(x,y-1,x,y+1,c); }
        else rect(x-r,y-r,2*r+1,2*r+1,c);
        if(exploration_index(game,m[i].sector)>=0) pixel(x+3,y+3,11);
    }
    if(game->galaxy_extent<=80000) {
        int boxes[8][4],labels=0,j;
        for(i=0;i<n && labels<8;i++) if(m[i].catalog>=0) {
            const char *name=star_catalog[m[i].catalog].name;
            int w=(int)strlen(name)*6,l=m[i].x+4,t=m[i].y-9,overlap=0;
            if(w>138) w=138;
            if(l+w>203) l=m[i].x-w-4;
            if(l<10 || t<40 || t+8>h-82) continue;
            for(j=0;j<labels;j++) if(l<boxes[j][0]+boxes[j][2]+3 && l+w>boxes[j][0]-3 && t<boxes[j][1]+11 && t+8>boxes[j][1]-3) overlap=1;
            if(overlap) continue;
            boxes[labels][0]=l;boxes[labels][1]=t;boxes[labels][2]=w;labels++;
            rect(l-1,t-1,w+2,9,0);text(l,t,7,name);
        }
    }
    if(galaxy_project(game,frame,home,&x,&y,&d)) { line(x-4,y,x+4,y,14); line(x,y-4,x,y+4,14); }
    if(game->waypoint_set && galaxy_project(game,frame,galaxy_position(game->waypoint),&x,&y,&d)) {
        line(x-5,y,x,y-5,13); line(x,y-5,x+5,y,13); line(x+5,y,x,y+5,13); line(x,y+5,x-5,y,13);
    }
    if(galaxy_project(game,frame,galaxy_position(game->selected),&x,&y,&d)) {
        line(x-5,y-5,x-5,y+5,12); line(x+5,y-5,x+5,y+5,12);
        line(x-5,y-5,x+5,y-5,12); line(x-5,y+5,x+5,y+5,12);
    }
}
static void galaxy_text(void) {
    char s[110]; static System selected; int h=frame->h/draw_scale,c=catalog_index(game->selected),i;
    int64_t distance=galaxy_distance(game->system.sector,game->selected);
    { static Sector previous; static uint32_t seed; static int ready;
      if(!ready || seed!=game->seed || !same_sector(previous,game->selected)) {
          generate_system(game->seed,game->selected,&selected);
          previous=game->selected;seed=game->seed;ready=1;
      } }
    snprintf(s,sizeof(s),"%s %.31s%s",game->galaxy_searching?"FIND":"SEARCH /",game->galaxy_query,game->galaxy_searching?"-":"");
    text(10,25,13,s);
    if(game->galaxy_searching || game->galaxy_query[0]) {
        Sector results[GALAXY_SEARCH]; int count=galaxy_search(game,results,GALAXY_SEARCH),first=game->galaxy_result/6*6;
        for(i=0;i<6 && first+i<count;i++) {
            char name[24]; galaxy_name(game->seed,results[first+i],name);
            if(catalog_index(results[first+i])<0) snprintf(s,sizeof(s),"%.8s %ldLY",name,(long)(galaxy_distance(game->system.sector,results[first+i])/1000));
            else { snprintf(s,sizeof(s),"%.16s",name); }
            text(213,40+i*12,first+i==game->galaxy_result?12:15,s);
        }
        if(!count) text(212,40,14,"NO MATCHES");
        if(h>218) { snprintf(s,sizeof(s),"%d RESULTS",count); text(212,130,7,s); }
    } else {
        snprintf(s,sizeof(s),"%.17s",selected.name); text(212,40,12,s);
        snprintf(s,sizeof(s),"%ld.%03ld LY",(long)(distance/1000),(long)(distance%1000)); text(212,51,15,s);
        snprintf(s,sizeof(s),"%d STARS",selected.stars); text(212,62,13,s);
        text(212,73,stellar_colour(galaxy_type(game->seed,game->selected)),stellar_names[galaxy_type(game->seed,game->selected)]);
        snprintf(s,sizeof(s),"%d BODIES",selected.count); text(212,84,7,s);
        snprintf(s,sizeof(s),"%d PORTS",system_port_count(&selected)); text(212,95,7,s);
        snprintf(s,sizeof(s),"FUEL %d / %d",galaxy_jump_cost(game,game->selected),game->fuel); text(212,106,7,s);
        text(212,117,7,c>=0?"REAL CATALOG":"GENERATED");
        if(h>218) for(i=0;i<selected.stars && 130+i*10<h-72;i++) {
            snprintf(s,sizeof(s),"%d %.13s",i+1,c>=0?star_catalog[c].spectra[i]:stellar_names[selected.star[i].type]); text(212,130+i*10,15,s);
        }
    }
    if(game->galaxy_detail) {
        Vec p=galaxy_position(game->selected);
        rect(6,36,308,h-104,1);
        snprintf(s,sizeof(s),"%s / %s",selected.name,c>=0?"REAL STELLAR CATALOG":"GENERATED SYSTEM"); text(10,40,12,s);
        snprintf(s,sizeof(s),"RANGE %ld.%03ld LY / SOL %ld.%03ld LY",(long)(distance/1000),(long)(distance%1000),(long)(length(p)/1000),(long)(length(p)%1000)); text(10,51,15,s);
        snprintf(s,sizeof(s),"%d STARS / %d BODIES / %d PORTS",selected.stars,selected.count,system_port_count(&selected)); text(10,62,13,s);
        snprintf(s,sizeof(s),"%s / %s / %s",(const char *const[]){"FARMING","INDUSTRY","TECHNOLOGY","MINING"}[selected.economy],(const char *const[]){"COUNCIL","FEDERATION","CORPORATE","INDEPENDENT"}[selected.government],exploration_index(game,game->selected)>=0?"VISITED":"UNEXPLORED"); text(10,73,7,s);
        for(i=0;i<selected.stars;i++) {
            snprintf(s,sizeof(s),"%c %.8s %.12s",'A'+i,c>=0?star_catalog[c].spectra[i]:"",stellar_names[selected.star[i].type]); text(10+(i/3)*152,84+(i%3)*11,stellar_colour(selected.star[i].type),s);
        }
        if(h>220) {
            snprintf(s,sizeof(s),"XYZ 0.001 LY %ld / %ld / %ld",(long)p.x,(long)p.y,(long)p.z); text(10,121,7,s);
            text(10,132,7,c==0?"SOL: REAL BODIES / FICTIONAL PORTS":c>=0?"PLANETS/PORTS AND BINARY ORBITS GENERATED":"PROCEDURAL GALACTIC DISC");
        }
    } else if(game->ui_visible && !game->galaxy_searching) {
        Sector hover; int px=game->ui_x/draw_scale,py=game->ui_y/draw_scale;
        if(px>=6 && px<206 && py>=36 && py<h-80 && galaxy_pick(game,frame,px,py,&hover)) {
            char name[24]; galaxy_name(game->seed,hover,name);
            snprintf(s,sizeof(s),"%.22s",name); rect(10,39,138,10,0); text(12,40,13,s);
        }
    }
    if(game->galaxy_detail) snprintf(s,sizeof(s),"INFO TO RETURN TO MAP");
    else if(game->galaxy_extent>8000) snprintf(s,sizeof(s),"%ld LY / SAMPLED - ZOOM IN",(long)(game->galaxy_extent/1000));
    else snprintf(s,sizeof(s),"%ld LY / ALL LOCAL",(long)(game->galaxy_extent/1000));
    rect(8,h-81,196,11,0); text(10,h-79,7,s);
    snprintf(s,sizeof(s),"%.23s / %.23s",selected.name,galaxy_jump_status(game,game->selected)); text(8,h-31,12,s);
}
static void gui(void) {
    UiWidget v[UI_MAX_WIDGETS]; int i,n=ui_widgets(game,frame,v),s=ui_scale(frame);
    int h=frame->h/s,top=h-80,hover=-1;
    char buf[100];
    draw_scale=s;
    if(game->page==FLIGHT) {
        rect(0,top,320,80,16); line(0,top,319,top,20);
        bevel(0,top,100,64,0); bevel(99,top,121,43,0); bevel(220,top,100,45,0);
        cockpit_scan(top);
        rect(3,top+31,94,10,0);
        { int64_t speed=length(game->vel)/FP;
          speed_text(speed,buf,sizeof(buf)); text(5,top+33,13,buf); }
        for(i=0;i<4;i++) {
            int value=i==0?game->hull:i==1?game->shield:i==2?game->fuel:game->heat, x=223+i*23;
            int c=i==0?14:i==1?13:i==2?12:game->heat>=80?14:11;
            text(x,top+32,c,i==0?"HUL":i==1?"SHD":i==2?"FUE":"TMP");
            rect(x,top+40,21,3,0); rect(x,top+40,value*21/100,3,c);
        }
        snprintf(buf,sizeof(buf),"%s %dX",game->paused?"PAUSED":"TIME",game->effective_warp);
        text(104,top+39,12,buf);
    } else {
        static const char *titles[]={"FLIGHT","GALACTIC NAVIGATION","CONTRACT EXCHANGE","COMMODITY MARKET","SHIP CATALOGUE","CONTRACT LOG","SYSTEM NAVIGATION","FLIGHT MANUAL","SHIP EQUIPMENT","COMMANDER RECORD"};
        rect(0,0,320,h,1);
        if(game->page==LOG || game->page==HELP || (game->page==SYSTEM_MAP && game->system_display)) {
            /* Existing orbital projection and body data remain truthful at true scale. */
            int old_h=view_h; view_h=h; menus(); view_h=old_h;
            rect(0,h-60,320,60,1);
        }
        rect(0,0,320,20,16); line(0,19,319,19,20);
        text(8,7,13,titles[game->page]);
        if(game->page==MARKET) text(13,34,7,"ITEM        BUY SELL STOCK HOLD");
        if(game->page==BOARD) text(13,34,7,game->docked>=0?game->ports[game->docked].name:"DOCK TO ACCEPT CONTRACTS");
        if(game->page==EQUIPMENT) text(13,34,7,"EQUIPMENT          PRICE  FITTED");
        if(game->page==CAREER) {
            snprintf(buf,sizeof(buf),"%s / %d CR",ship_specs[game->ship].name,game->credits); text(12,28,15,buf);
            snprintf(buf,sizeof(buf),"CREW %d/%d / PASSENGERS %d/%d",game->crew,ship_specs[game->ship].crew,passengers_used(game),game->cabins*2); text(12,42,13,buf);
            for(i=0;i<FACTIONS;i++) {
                snprintf(buf,sizeof(buf),"%s / %s",faction_names[i],military_rank(game,i)); text(12,65+i*26,12,buf);
                snprintf(buf,sizeof(buf),"REP %d / FINE %d / MEDALS %d",game->reputation[i],game->wanted[i],game->medals[i]); text(12,76+i*26,7,buf);
            }
        }
        if(game->page==MAP) galaxy_map();
    }
    if (!game->ui_quit) hover=gui_buttons(v,n,s);
    if (!game->ui_quit && (game->page==MARKET || game->page==BOARD || game->page==EQUIPMENT || (game->page==SYSTEM_MAP && !game->system_display))) gui_rows();
    if (game->page==SHIPYARD) shipyard();
    if (game->page==MAP) galaxy_text();
    if(game->page!=FLIGHT && game->page!=MAP) {
        snprintf(buf,sizeof(buf),"%d CR / HOLD %d/%dT",game->credits,cargo_used(game),ship_capacity[game->ship]); text(8,h-28,12,buf);
        /* Status ribbon stays clear of both action buttons and navigation. */
        if(game->page==BOARD) {
            const Mission *m=&game->board[game->selection];
            snprintf(buf,sizeof(buf),"TO %ld/%ld P%d / %dT %dP / %ldH",(long)m->destination.x,(long)m->destination.z,m->port+1,m->quantity,m->passengers,(long)((m->deadline>game->tick?m->deadline-game->tick:0)/(HZ*3600)));
            text(8,h-62,7,buf);
        } else if(game->page==SYSTEM_MAP && !game->system_display) {
            snprintf(buf,sizeof(buf),"RANGE %ld KM / %s",(long)(length(sub(navigation_position(game),game->pos))/(FP*1000)),game->target<0?"BODY WAYPOINT":"DOCKING PORT");
            text(8,h-62,7,buf);
        }
        snprintf(buf,sizeof(buf),"%.50s",game->message); text(8,h-52,13,buf);
    } else if(game->page==FLIGHT) {
        draw_scale=1;
        { int64_t alt=altitude_at(game,game->pos);
          snprintf(buf,sizeof(buf),"ALT %ld %s / %s",(long)(alt>=1000000?alt/1000:alt),alt>=1000000?"KM":"M",game->drive_manual?(game->drive_active?(game->autopilot?"AUTO DRIVE":"DRIVE"):(game->autopilot?"AUTO DRIVE LOCK":"DRIVE LOCK")):(game->autopilot?"AUTO":game->drive_active?"BRAKING":"MANUAL")); text(7,view_h-23,13,buf); }
        snprintf(buf,sizeof(buf),"%.50s",hover>=0 && v[hover].label[0]?v[hover].label:game->message);
        text(7,view_h-10,12,buf);
        draw_scale=s;
    }
    if (game->ui_quit) {
        rect(36,h/2-50,248,104,16);
        bevel(36,h/2-50,248,104,0);
        text(70,h/2-39,13,"SAVE BEFORE QUITTING");
        text(50,h/2-26,game->ui_quit==2?14:7,
             game->ui_quit==2?"SAVE FAILED - RETRY OR CANCEL":"CHOOSE AN OPTION TO CONTINUE");
    }
    if (game->ui_quit) gui_buttons(v,n,s);
    if(game->ui_visible) {
        int x=game->ui_x/s,y=game->ui_y/s;
        line(x,y,x,y+9,0); line(x,y,x+7,y+7,0); line(x,y+9,x+3,y+6,0);
        line(x+1,y+1,x+1,y+7,15); line(x+1,y+1,x+5,y+5,15);
    }
    draw_scale=1;
}
void render_shutdown(void) {
    free(depth_buffer);
    depth_buffer = 0;
    depth_capacity = 0;
    terrain_surface = NULL;
    memset(globe_valid,0,sizeof(globe_valid));
    memset(surface_cache, 0, sizeof(surface_cache));
}
int render_init(Frame *f) {
    int i;
    size_t count;
    uint32_t *buffer;
    if (f->w <= 0 || f->h <= 56)
        return 0;
    count = (size_t)f->w * (f->h - 56);
    buffer = realloc(depth_buffer, count * sizeof(*buffer));
    if (!buffer)
        return 0;
    depth_buffer = buffer;
    depth_capacity = count;
    if (!tables_ready)
        plane_tables();
    static const uint32_t base[32] = {
        0x040711, 0x0b1729, 0x12273e, 0x20394b, 0x294758, 0x335b71, 0x4b7890, 0x81a7b1,
        0x254b40, 0x35684d, 0x658958, 0x70cd93, 0xf0bb65, 0x65d3d5, 0xd77d65, 0xe9e8d5,
        0x354353, 0x778897, 0xb8c6c9, 0x414f64, 0x9ab3c7, 0x543e4c, 0x93516a, 0xdf827d,
        0xefb3a0, 0x27376b, 0x384f87, 0x4f70aa, 0x536879, 0x657e8e, 0x78909e, 0xffffff};
    for (i = 0; i < 256; i++)
        f->palette[i] = base[i % 32];
    for (i = 0; i < 32; i++)
        f->palette[32 + i] =
            (uint32_t)((8 + i * 2) << 16) | ((uint32_t)(18 + i * 3) << 8) | (unsigned)(35 + i * 4);
    for (i = 0; i < 16; i++)
        f->palette[64 + i] =
            (uint32_t)((32 + i * 4) << 16) | ((uint32_t)(66 + i * 5) << 8) | (unsigned)(49 + i * 3);
    for (i = 0; i < 16; i++) {
        f->palette[80 + i] =
            ((uint32_t)(10 + i * 2) << 16) | ((uint32_t)(28 + i * 4) << 8) | (unsigned)(65 + i * 7);
        f->palette[96 + i] =
            ((uint32_t)(42 + i * 7) << 16) | ((uint32_t)(30 + i * 5) << 8) | (unsigned)(20 + i * 3);
        f->palette[112 + i] = ((uint32_t)(44 + i * 5) << 16) | ((uint32_t)(58 + i * 5) << 8) |
                              (unsigned)(72 + i * 5);
    }
    for (i = 0; i < 32; i++) {
        f->palette[128 + i] =
            ((uint32_t)(30 + i * 5) << 16) | ((uint32_t)(12 + i * 3) << 8) | (unsigned)(8 + i * 2);
        f->palette[160 + i] =
            ((uint32_t)(12 + i * 3) << 16) | ((uint32_t)(14 + i * 3) << 8) | (unsigned)(30 + i * 5);
        f->palette[192 + i] =
            ((uint32_t)(22 + i * 4) << 16) | ((uint32_t)(18 + i * 3) << 8) | (unsigned)(12 + i * 2);
    }
    /* Cloud ramp: night slate, shadowed blue-grey, sunlit white. */
    for (i = 0; i < 32; i++) {
        static const int key[4][4] = {{0, 16, 22, 36}, {12, 98, 112, 134}, {24, 196, 204, 214},
                                      {31, 255, 255, 250}};
        int k = i < 12 ? 0 : i < 24 ? 1 : 2, span = key[k + 1][0] - key[k][0], t = i - key[k][0];
        f->palette[224 + i] =
            (uint32_t)(key[k][1] + (key[k + 1][1] - key[k][1]) * t / span) << 16 |
            (uint32_t)(key[k][2] + (key[k + 1][2] - key[k][2]) * t / span) << 8 |
            (uint32_t)(key[k][3] + (key[k + 1][3] - key[k][3]) * t / span);
    }
    fog_tables(f);
    return 1;
}
/* Hyperspace sequence. Every colour comes from the 32 base entries, so the
   effect reads the same on ECS, AGA and RTG. Coordinates are frame pixels. */
#define HYPER_RINGS 14
#define HYPER_SEGMENTS 24
#define HYPER_DEPTH (HYPER_RINGS * 64)
static void disc(int cx, int cy, int r, int c) {
    int y;
    for (y = -r; y <= r; y++) {
        int half = (int)isqrt64((uint64_t)(r * r - y * y));
        if (cy + y >= 0 && cy + y < view_h) rect(cx - half, cy + y, half * 2 + 1, 1, c);
    }
}
/* Ordered dither stands in for a white fade on an indexed display. */
static void hyper_flash(int level) {
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int x, y;
    if (level <= 0) return;
    for (y = 0; y < view_h; y++) {
        uint8_t *row = frame->pixels + y * frame->w;
        const uint8_t *b = bayer + (y & 3) * 4;
        for (x = 0; x < frame->w; x++)
            if (b[x & 3] < level) row[x] = 31;
    }
}
/* Q8 extra length for star streaks: they stretch as the charge builds and
   snap back as the ship drops out into the new system. */
static int hyper_stretch(const Game *g) {
    int t = g->hyperspace - 1;
    if (!g->hyperspace) return 0;
    if (t < HYPER_CHARGE) return 1536 * t * t / (HYPER_CHARGE * HYPER_CHARGE);
    t -= HYPER_CHARGE + HYPER_TUNNEL;
    if (t < 0 || t >= HYPER_EXIT) return 0;
    return 1024 * (HYPER_EXIT - t) * (HYPER_EXIT - t) / (HYPER_EXIT * HYPER_EXIT);
}
static void hyper_title(const char *title, const char *detail, int blink) {
    int w = frame->w;
    if (blink) text(w / 2 - (int)strlen(title) * 3, 12, 15, title);
    if (detail) text(w / 2 - (int)strlen(detail) * 3, 22, 12, detail);
}
static void hyperspace_charge(int t) {
    int w = frame->w, cx = w / 2, cy = view_h / 2, i;
    int p = t * 256 / HYPER_CHARGE, r;
    /* Light accelerates outward from the bow once the field forms. */
    if (p > 32) for (i = 0; i < 96; i++) {
        uint32_t h = hash32((unsigned)i * 2654435761U + 17);
        int a = (int)(h & 1023), base = (int)((h >> 10) & 255) * w / 640 + w / 32;
        int run = (p - 32) * (p - 32) * w / (224 * 224) * 3 / 4, len = run / 2 + 2;
        int r0 = base + run, x0 = cx + cosi(a) * r0 / 16384, y0 = cy + sini(a) * r0 / 16384;
        line(x0, y0, cx + cosi(a) * (r0 + len) / 16384, cy + sini(a) * (r0 + len) / 16384,
             i % 3 == 0 ? 15 : i % 3 == 1 ? 13 : 20);
    }
    /* Shockwave rings ripple out faster as the charge nears release. */
    {
        int period = 16 - p / 24, wave = t % period;
        int rr = wave * w / (period * 2), j;
        for (j = 0; j < 24; j++) {
            int a = j * 1024 / 24, b = (j + 1) * 1024 / 24;
            line(cx + cosi(a) * rr / 16384, cy + sini(a) * rr / 16384,
                 cx + cosi(b) * rr / 16384, cy + sini(b) * rr / 16384, wave < period / 2 ? 13 : 27);
        }
    }
    r = w * p / (256 * 40) + (p > 176 ? (p - 176) * w / 640 : 0) + 1;
    r += sini(t * 96) * r / (16384 * 6);
    disc(cx, cy, r, 27); disc(cx, cy, r * 3 / 4, 13);
    disc(cx, cy, r / 2, 15); disc(cx, cy, r / 4, 31);
    hyper_title("HYPERSPACE CHARGING", NULL, 1);
    rect(cx - 50, 22, 100, 5, 0);
    rect(cx - 49, 23, p * 98 / 256, 3, p > 208 ? 31 : 13);
    if (p > 200) hyper_flash((p - 200) * 17 / 56);
}
/* The tunnel bends smoothly: offset grows with the square of depth, and the
   bend direction drifts slowly, so the throat wanders around the bow. */
static void tunnel_centre(int t, int z, int *x, int *y) {
    int bend = z * z / HYPER_DEPTH;
    *x = frame->w / 2 + sini(t * 6) * (frame->w / 6) / 16384 * bend / HYPER_DEPTH;
    *y = view_h / 2 + cosi(t * 4) * (view_h / 5) / 16384 * bend / HYPER_DEPTH;
}
static void hyperspace_tunnel(const Game *g, int t) {
    static const uint8_t ramp[10] = {31, 15, 13, 20, 7, 27, 6, 26, 25, 2};
    int w = frame->w, k, j, i, cx, cy, r;
    int vx[HYPER_RINGS][HYPER_SEGMENTS], vy[HYPER_RINGS][HYPER_SEGMENTS], colour[HYPER_RINGS];
    int travel = t < 16 ? t * t * 18 / 32 : (t - 8) * 18;
    char buf[40];
    rect(0, 0, w, view_h, 0);
    for (k = 0; k < HYPER_RINGS; k++) {
        int z = k * 64 + 64 - travel % 64, rot = t * 4 + z / 6, id = k + travel / 64;
        int depth = z * 10 / (HYPER_DEPTH + 1);
        tunnel_centre(t, z, &cx, &cy);
        r = z > 10 ? w * 24 / z : w * 3;
        colour[k] = id % 4 == 0 ? (depth < 4 ? 23 : 22) : ramp[depth];
        for (j = 0; j < HYPER_SEGMENTS; j++) {
            int a = rot + j * 1024 / HYPER_SEGMENTS;
            vx[k][j] = cx + cosi(a) * r / 16384;
            vy[k][j] = cy + sini(a) * r / 16384;
        }
    }
    {   /* Vanishing-point core, pulsing behind the farthest ring. */
        tunnel_centre(t, HYPER_DEPTH - 48, &cx, &cy);
        r = w / 30 + sini(t * 48) * (w / 120) / 16384;
        disc(cx, cy, r * 2, 25); disc(cx, cy, r * 3 / 2, 26); disc(cx, cy, r, 27);
        disc(cx, cy, r * 2 / 3, 13); disc(cx, cy, r / 3, 31);
    }
    for (k = HYPER_RINGS - 1; k >= 0; k--) {
        for (j = 0; j < HYPER_SEGMENTS; j++) {
            int n = (j + 1) % HYPER_SEGMENTS;
            line(vx[k][j], vy[k][j], vx[k][n], vy[k][n], colour[k]);
            if (k + 1 < HYPER_RINGS && j % 3 == 0)
                line(vx[k][j], vy[k][j], vx[k + 1][j], vy[k + 1][j], colour[k + 1]);
        }
    }
    /* Streaming light: streaks born at the core rush past the canopy. */
    for (i = 0; i < 48; i++) {
        uint32_t h = hash32((unsigned)i * 7919U + 3);
        int a = (int)(h & 1023), z = 16 + (int)(((h >> 12) % 880 + 880 - (unsigned)(travel * 3) % 880) % 880);
        int x0, y0, x1, y1, r0 = w * 24 / (z + 20), r1 = w * 24 / z;
        tunnel_centre(t, z + 20, &x0, &y0);
        tunnel_centre(t, z, &x1, &y1);
        line(x0 + cosi(a) * r0 / 16384, y0 + sini(a) * r0 / 16384,
             x1 + cosi(a) * r1 / 16384, y1 + sini(a) * r1 / 16384, z < 160 ? 31 : z < 400 ? 15 : 13);
    }
    snprintf(buf, sizeof(buf), "DESTINATION %.24s", g->system.name);
    hyper_title("HYPERSPACE", buf, (t / 6) % 4 != 3);
    if (t < 8) hyper_flash(16 - t * 2);
    if (t >= HYPER_TUNNEL - 6) hyper_flash((t - (HYPER_TUNNEL - 6)) * 17 / 6);
}
static void hyperspace_exit(int t) {
    if (t < HYPER_EXIT / 2) hyper_flash(16 - t * 32 / HYPER_EXIT);
}
/* Label every body in the field of view, including those too small to
   resolve as geometry. Leader lines keep crowded names tied to their body.
   Moons are named only once the ship is within a few moon-system radii of
   their host, so distant planets do not drown in satellite names. */
static void system_body_labels(void) {
    static int64_t reach[MAX_PLANETS];
    int i, used = 0, boxes[MAX_PLANETS + 6][3];
    const Planet *nearest = &game->system.planets[nearest_body(game, game->pos)];
    Vec ship = scale(game->pos, 1, FP);
    if (altitude_at(game, game->pos) < nearest->atmosphere + 20000LL) return;
    memset(reach, 0, sizeof(reach[0]) * (size_t)game->system.count);
    for (i = 0; i < game->system.count; i++) {
        const Planet *p = &game->system.planets[i];
        if (p->parent >= 0 && p->orbit * 3 > reach[p->parent]) reach[p->parent] = p->orbit * 3;
    }
    for (i = 0; i < game->system.count + game->system.stars; i++) {
        int star = i >= game->system.count, index = i - game->system.count;
        Vec p = camera(star ? game->system.star[index].center : game->system.planets[i].center);
        char name[32];
        int x, y, lx, ly, w, j = 0, attempt;
        if (p.z <= 0) continue;
        if (!star && game->system.planets[i].parent >= 0) {
            const Planet *host = &game->system.planets[game->system.planets[i].parent];
            if (length(sub(ship, host->center)) > reach[game->system.planets[i].parent] + host->radius)
                continue;
        }
        x = frame->w / 2 + screen_coord(p.x * (frame->w / 2) / p.z);
        y = view_h / 2 - screen_coord(p.y * (frame->w / 2) / p.z);
        if (x < 2 || x >= frame->w - 2 || y < 20 || y >= view_h - 27) continue;
        if (star) snprintf(name, sizeof(name), "%.23s %c", game->system.name, 'A' + index);
        else snprintf(name, sizeof(name), "%s", game->system.planets[i].name);
        w = (int)strlen(name) * 6;
        lx = x + 7;
        if (lx + w >= frame->w - 2) lx = frame->w - w - 2;
        if (lx < 2) lx = 2;
        ly = y + 6;
        for (attempt = 0; attempt < (view_h - 44) / 10; attempt++) {
            ly = 22 + (y - 16 + attempt * 10) % (view_h - 48);
            for (j = 0; j < used; j++)
                if (lx < boxes[j][0] + boxes[j][2] + 3 && lx + w + 3 > boxes[j][0] &&
                    abs(ly - boxes[j][1]) < 9) break;
            if (j == used) break;
        }
        if (j < used) continue; /* The viewport has no free name row here. */
        boxes[used][0] = lx; boxes[used][1] = ly; boxes[used++][2] = w;
        line(x - 2, y, x + 2, y, 7); line(x, y - 2, x, y + 2, 7);
        line(x + 3, y + 3, lx, ly + 3, 7);
        rect(lx - 1, ly - 1, w + 1, 9, 0);
        text(lx, ly, star ? 12 : 7, name);
    }
}
void render_game(const Game *g, Frame *f) {
    int i;
    int64_t alt = altitude_at(g, g->pos);
    frame = f;
    game = g;
    if (g->page != FLIGHT) {
        view_h=f->h;
        gui();
        return;
    }
    camera_position = g->pos;
    view_angles(g, &camera_yaw, &camera_pitch);
    camera_roll = g->camera_mode == 0 ? g->roll :
                  g->camera_mode == 1 ? (-g->roll & 1023) : 0;
    if (g->camera_mode == 2) {
        Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
        camera_position = add(sub(g->pos, scale(forward(g->yaw, g->pitch), 600 * FP, 16384)),
                              scale(up, 220 * FP, 16384));
        aim_angles(sub(g->pos, camera_position), &camera_yaw, &camera_pitch);
    }
    view_h = ui_view_height(g, f);
    nt = 0;
    {
        size_t count = (size_t)f->w * view_h;
        /* Allocation happens in render_init, before native OS takeover. */
        if (count > depth_capacity || !depth_buffer)
            return;
        memset(depth_buffer, 0, count * sizeof(*depth_buffer));
    }
    if (g->hyperspace > HYPER_CHARGE && g->hyperspace <= HYPER_CHARGE + HYPER_TUNNEL) {
        hyperspace_tunnel(g, g->hyperspace - HYPER_CHARGE - 1);
        gui();
        return;
    }
    {
        const Planet *p = &g->system.planets[g->body];
        Vec local = sub(scale(g->pos, 1, FP), p->center);
        int scatter = rayleigh_scattering(&g->system, p, local,
                                         forward(camera_yaw, camera_pitch));
        for (i = 0; i < view_h; i++) {
            int c = 0, intensity = scatter * (view_h + i) / (2 * view_h);
            if (intensity) {
                int base = p->climate == DENSE ? 128 : p->climate == HYDROGEN ? 192 : 32;
                c = f->enhanced ? base + intensity : 1 + intensity * 6 / 31;
            }
            rect(0, i, f->w, 1, c);
        }
    }
    if (alt > 6000) {
        int stretch = hyper_stretch(g);
        for (i = 0; i < 120; i++) {
            uint32_t h = hash32((unsigned)i + g->seed);
            Vec p = add(scale(g->pos, 1, FP),
                        scale(forward(h % 1024, (int)((h >> 16) % 512) - 256), 1000000, 16384));
            Vec v = camera(p);
            if (v.z > 100) {
                int x = f->w / 2 + (int)(v.x * (f->w / 2) / v.z),
                    y = view_h / 2 - (int)(v.y * (f->w / 2) / v.z);
                if (stretch)
                    line(x, y, f->w / 2 + (x - f->w / 2) * (256 + stretch) / 256,
                         view_h / 2 + (y - view_h / 2) * (256 + stretch) / 256, i % 5 ? 20 : 31);
                else if (y < view_h)
                    pixel(x, y, i % 5 ? 7 : 15);
            }
        }
    }
    if (g->page == FLIGHT) {
        world();
        weather_overlay();
        system_body_labels();
    }
    if (g->page == FLIGHT) {
        Vec p = camera(scale(navigation_position(g), 1, FP));
        const char *name = g->resource_target >= 0 ? (g->resource_target >= ASTEROIDS ? "MINING RIG" : "ASTEROID") : g->target < 0 ? g->system.planets[g->target_body].name : g->ports[g->target].name;
        char range[24];
        int x, y, label_x, label_y, label_w;
        target_range_text(length(sub(navigation_position(g), g->pos)) / FP, range, sizeof(range));
        label_w = (int)(strlen(name) > strlen(range) ? strlen(name) : strlen(range)) * 6;
        target_marker_position(p, 10, &x, &y);
        line(x - 8, y - 8, x + 8, y - 8, 13);
        line(x - 8, y + 8, x + 8, y + 8, 13);
        line(x - 8, y - 8, x - 8, y + 8, 13);
        line(x + 8, y - 8, x + 8, y + 8, 13);
        label_x = x - 30;
        if (label_x + label_w > f->w - 2) label_x = f->w - 2 - label_w;
        if (label_x < 2) label_x = 2;
        /* Name and range stack under the box, or above it near the console. */
        label_y = y + 12;
        if (label_y + 16 > view_h - 25) label_y = y - 28;
        text(label_x, label_y, 13, name);
        text(label_x, label_y + 9, 13, range);
        text(7, 8, 13,
             name);
        if (!g->camera_mode || g->camera_mode >= 3) {
            line(f->w / 2 - 9, view_h / 2, f->w / 2 - 3, view_h / 2, 15);
            line(f->w / 2 + 3, view_h / 2, f->w / 2 + 9, view_h / 2, 15);
            line(f->w / 2, view_h / 2 - 9, f->w / 2, view_h / 2 - 3, 15);
        }
        if (g->laser && (!g->camera_mode || g->camera_mode >= 3)) {
            line(20, view_h - 20, f->w / 2, view_h / 2, 12);
            line(f->w - 20, view_h - 20, f->w / 2, view_h / 2, 12);
        }
        sensors();
    }
    if (g->hyperspace && g->hyperspace <= HYPER_CHARGE)
        hyperspace_charge(g->hyperspace - 1);
    else if (g->hyperspace > HYPER_CHARGE + HYPER_TUNNEL)
        hyperspace_exit(g->hyperspace - HYPER_CHARGE - HYPER_TUNNEL - 1);
    gui();
}
/* Planes are contiguous and MSB-first, independent of host byte order. */
void c2p(const uint8_t *src, uint8_t *dst, int width, int height, int depth) {
    int y, x, b, p, stride = width / 8, plane = stride * height;
    if (width <= 0 || (width & 7) || height <= 0 || depth < 1 || depth > 8)
        return;
    if (!tables_ready)
        plane_tables();
    for (y = 0; y < height; y++) {
        if (platform_pump && y % 16 == 0)
            platform_pump();
        for (x = 0; x + 1 < stride; x += 2) {
            uint32_t lows[2] = {0, 0}, highs[2] = {0, 0};
            int k;
            for (k = 0; k < 2; k++) for (b = 0; b < 8; b++) {
                unsigned value = src[y * width + (x + k) * 8 + b];
                lows[k] |= plane_low[value] << (7 - b);
                if (depth > 4) highs[k] |= plane_high[value] << (7 - b);
            }
            for (p = 0; p < depth; p++) {
                uint32_t a = p < 4 ? lows[0] : highs[0], c = p < 4 ? lows[1] : highs[1];
                unsigned shift = (unsigned)(p & 3) * 8;
                uint16_t word = (uint16_t)(((a >> shift) & 255) << 8 | ((c >> shift) & 255));
#if defined(__mc68000__)
                /* Chip RAM is word addressed. memcpy also permits odd buffers
                   in the portable API without undefined aligned accesses. */
                memcpy(dst + p * plane + y * stride + x, &word, 2);
#else
                dst[p * plane + y * stride + x] = (uint8_t)(word >> 8);
                dst[p * plane + y * stride + x + 1] = (uint8_t)word;
#endif
            }
        }
        for (; x < stride; x++) {
            uint32_t low = 0, high = 0;
            const uint8_t *group = src + y * width + x * 8;
            for (b = 0; b < 8; b++) {
                unsigned pixel_value = group[b];
                low |= plane_low[pixel_value] << (7 - b);
                if (depth > 4)
                    high |= plane_high[pixel_value] << (7 - b);
            }
            for (p = 0; p < depth && p < 4; p++)
                dst[p * plane + y * stride + x] = (uint8_t)(low >> (8 * p));
            for (p = 4; p < depth; p++)
                dst[p * plane + y * stride + x] = (uint8_t)(high >> (8 * (p - 4)));
        }
    }
}
