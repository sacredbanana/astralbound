/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "ui.h"
#include "services.h"
#include "galaxy.h"
#include "solar.h"
#include "fpu.h"
#include "intro.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "settings.h"
#include "launcher.h"
#include "performance.h"
#include "planet_view.h"
#include "planet_field.h"
#include "planet_contours.h"
#include "classic_raster.h"
#include "planet_ring.h"
/* Shared curve boundary crossings; each backend fills them independently. */
static ClRow cl_rows[1024];
static ClSpans cl_spans;
/* Classic renderer hooks (render_classic.inc): while a Classic frame is
   built, the shared scene code's triangles, hulls, streaks and beams go to
   its painter's draw list instead of the depth-buffered rasteriser. */
static int classic_mode, cl_active;
static int bezier_rings = 1;
void render_bezier(int enabled) { bezier_rings = enabled != 0; }
static int64_t cl_bias;
static int cl_emit;
/* Classic level of detail for the shared port generator: screen pixels
   per metre at the port being built (Q8), 0 when not drawing Classic. */
static int32_t cl_port_px;
static void cl_triangle_world(Vec a, Vec b, Vec c, int colour);
static void cl_ship(int ship, Vec origin, Vec right, Vec up, Vec front, Vec light);
static void cl_line_world(Vec a, Vec b, int colour, int thick);
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
static Tri *triangles;
static int triangle_capacity, triangle_limit;
/* Draw order: triangle indices sorted on a depth key, with ping-pong space. */
static int *triangle_order;
static uint32_t *triangle_keys;
static Graphics graphics = {0,3,3,2,2,3,2,2,3,3,3,3,1,1,1,1,3,1,0};
static int graphics_configured, scene_only, scaled_view_h, performance_camera, weather_status;
static uint8_t *scaled_pixels;
static size_t scaled_capacity;
static int nt;
static int scenery_reserve = 1000;
static const Planet *terrain_surface;
static int material_color(const Planet *p, Vec local, int material);
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
/* The title sequence directs a free camera; terrain, sky and stars are then
   resolved around that viewpoint instead of the commander's ship. */
static const Intro *cinema;
static Vec viewer(void) { return cinema || game->on_foot ? camera_position : game->pos; }
/* On foot the eye is a couple of metres off the ground: keep sub-metre
   camera translation and bring the near plane in from 8 m to 1 m. */
static int fine_camera, near_plane = 8;
/* A sub-viewport for inset scenes: the projection centre moves by proj_dx/dy
   from the frame centre and rasterisation is scissored to the view rectangle.
   All zero (the full frame) except while an inset is being drawn. */
static int proj_dx, proj_dy, view_left, view_top, view_right_inset;
static void (*platform_pump)(void);
#if defined(__mc68000__)
/* A longword that may alias bytes and sit on a word boundary (68000 rules). */
typedef uint32_t __attribute__((__may_alias__, __aligned__(2))) c2p_long;
#endif
static uint32_t plane_low[256], plane_high[256];
static int tables_ready;

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
static int64_t round_div(int64_t v, int64_t d) {
    return (v + (v < 0 ? -d / 2 : d / 2)) / d;
}
static int64_t floor_shift(int64_t v, int s) { return v >= 0 ? v >> s : ~(~v >> s); }
/* World-to-camera rotation (yaw, pitch, then roll) folded into one Q14
   matrix. It is rebuilt only when the camera turns, normally once a frame,
   so vertices pay nine multiplies and no sine lookups. */
static struct { int yaw, pitch, roll, valid; unsigned serial; int64_t m[3][3]; } view_matrix;
static void view_prepare(void) {
    int64_t sy = sini(camera_yaw), cy = cosi(camera_yaw), sp = sini(camera_pitch),
            cp = cosi(camera_pitch), sr = sini(camera_roll), cr = cosi(camera_roll);
    /* Q28 rows after yaw and pitch; the roll mixes the first two in Q42. */
    int64_t x[3] = {cy * 16384, 0, -sy * 16384}, y[3] = {-sp * sy, cp * 16384, -sp * cy},
            depth[3] = {cp * sy, sp * 16384, cp * cy};
    int i;
    for (i = 0; i < 3; i++) {
        view_matrix.m[0][i] = round_div(x[i] * cr - y[i] * sr, 1LL << 28);
        view_matrix.m[1][i] = round_div(y[i] * cr + x[i] * sr, 1LL << 28);
        view_matrix.m[2][i] = round_div(depth[i], 16384);
    }
    view_matrix.yaw = camera_yaw; view_matrix.pitch = camera_pitch; view_matrix.roll = camera_roll; view_matrix.valid = 1;
    view_matrix.serial++;
}
static void view_current(void) {
    if (!view_matrix.valid || view_matrix.yaw != camera_yaw || view_matrix.pitch != camera_pitch || view_matrix.roll != camera_roll)
        view_prepare();
}
/* An exact Q14 right/up/forward basis in place of the table angles, held
   (valid 2) until the angles change or the next frame begins. */
static void view_basis(const Vec axes[3]) {
    int i;
    for (i = 0; i < 3; i++) {
        view_matrix.m[i][0] = axes[i].x; view_matrix.m[i][1] = axes[i].y; view_matrix.m[i][2] = axes[i].z;
    }
    view_matrix.yaw = camera_yaw; view_matrix.pitch = camera_pitch; view_matrix.roll = camera_roll; view_matrix.valid = 2;
    view_matrix.serial++;
}
/* In the offset's unit. */
static Vec view_rotate(Vec r) {
    view_current();
    return vec((view_matrix.m[0][0] * r.x + view_matrix.m[0][1] * r.y + view_matrix.m[0][2] * r.z) / 16384,
               (view_matrix.m[1][0] * r.x + view_matrix.m[1][1] * r.y + view_matrix.m[1][2] * r.z) / 16384,
               (view_matrix.m[2][0] * r.x + view_matrix.m[2][1] * r.y + view_matrix.m[2][2] * r.z) / 16384);
}
/* Camera space in metres. On foot, nearby points rotate their exact Q8
   offset and round only at the end, so the eye moves by less than a metre. */
static Vec camera(Vec p) {
    Vec r = sub(p, scale(camera_position, 1, FP));
    if (fine_camera && llabs(r.x) < 1000000 && llabs(r.y) < 1000000 && llabs(r.z) < 1000000) {
        Vec q = view_rotate(sub(scale(p, FP, 1), camera_position));
        return vec(round_div(q.x, FP), round_div(q.y, FP), round_div(q.z, FP));
    }
    return view_rotate(r);
}
/* The same for a Q8 point, which keeps its place to the 256th of a metre. */
static Vec camera_q8(Vec p) {
    Vec r = sub(scale(p, 1, FP), scale(camera_position, 1, FP));
    if (fine_camera && llabs(r.x) < 1000000 && llabs(r.y) < 1000000 && llabs(r.z) < 1000000) {
        Vec q = view_rotate(sub(p, camera_position));
        return vec(round_div(q.x, FP), round_div(q.y, FP), round_div(q.z, FP));
    }
    return view_rotate(r);
}
/* Camera space in camera_unit steps per metre for polygons: 1/16 m on foot,
   where whole-metre vertices clipped at a 1 m near plane would bend the
   depth plane that ground texturing reconstructs each pixel from. */
static int camera_unit = 1;
static Vec camera_poly(Vec p) {
    Vec r;
    if (camera_unit == 1) return camera(p);
    r = sub(p, scale(camera_position, 1, FP));
    if (llabs(r.x) < 1000000 && llabs(r.y) < 1000000 && llabs(r.z) < 1000000) {
        Vec q = view_rotate(sub(scale(p, FP, 1), camera_position));
        int64_t d = FP / camera_unit;
        return vec(round_div(q.x, d), round_div(q.y, d), round_div(q.z, d));
    }
    return scale(view_rotate(r), camera_unit, 1);
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
/* Seeds 2^24/(256.5 + i) for the reciprocal of a depth normalised so its
   top bit is bit 31; i is the next eight bits. */
static uint16_t reciprocal_seed[256];
/* 1/z = y * 2^(shift - 63) for z > 0, y within a few parts in 2^32. Two
   Newton steps from the table: each squares the relative error. */
static uint32_t reciprocal(int64_t z, int *shift) {
    uint64_t u = (uint64_t)z;
    uint32_t m, y;
    int s = 0, step;
    if (!reciprocal_seed[0]) {
        int i;
        for (i = 0; i < 256; i++)
            reciprocal_seed[i] = (uint16_t)((2U * 33554432U / (513U + 2U * i) + 1) / 2);
    }
    /* Distant depths drop low bits: under one part in 2^31. */
    for (step = 16; step; step /= 2)
        if (u >> (32 + step)) { u >>= step; s -= step; }
    if (u >> 32) { u >>= 1; s--; }
    m = (uint32_t)u;
    for (step = 16; step; step /= 2)
        if (m < 1U << (32 - step)) { m <<= step; s += step; }
    /* Over every mantissa y stays below 2^32 and error >> 31 below 2^24, so
       a step is two 32x32->64 multiplies (one MULU.L each on 020+), with
       the signed correction floored exactly as a 64-bit shift would. */
    y = (uint32_t)reciprocal_seed[(m >> 23) & 255] << 16;
    for (step = 0; step < 2; step++) {
        int32_t error = (int32_t)floor_shift((int64_t)(0x8000000000000000ULL - (uint64_t)m * y), 31);
        uint64_t p = (uint64_t)y * (uint32_t)(error < 0 ? -error : error);
        y = error < 0 ? y - (uint32_t)((p - 1) >> 32) - 1 : y + (uint32_t)(p >> 32);
    }
    *shift = s;
    return y;
}
#ifndef ASTRALBOUND_FPU
/* v/z in Q28, sign kept. Clipping bounds |v| by about z; the clamp only
   guards the fixed-point range. */
static int32_t ratio(int64_t v, int64_t z, int shift, uint64_t y) {
    uint64_t a = (uint64_t)(v < 0 ? -v : v);
    int32_t q;
    if (a > (uint64_t)z * 7)
        a = (uint64_t)z * 7;
    a = shift >= 0 ? a << shift : a >> -shift;
    q = (int32_t)((uint64_t)(uint32_t)(a >> 3) * (uint32_t)y >> 32);
    return v < 0 ? -q : q;
}
/* Q28 ratio times scale, truncated toward zero like the division it
   replaces. The reciprocal errs low, so a 2^-16 nudge keeps exact quotients:
   a vertex clipped onto a side plane must still reach the screen edge. */
static int project(int32_t ratio, int scale) {
    int64_t v = ((int64_t)(ratio < 0 ? -ratio : ratio) * scale + 4096) >> 28;
    return ratio < 0 ? -(int)v : (int)v;
}
#endif
static void emit(Vec a, Vec b, Vec c, int color, int slope) {
    Tri *t;
    Vec v[3] = {a, b, c};
    int i;
    if (nt >= triangle_limit)
        return;
    t = &triangles[nt];
    /* Terrain needs the precise depth plane even with flat colours. Pixel
       shading is optional; stable terrain/structure occlusion is not. */
    t->surface = terrain_surface;
    t->slope = (int8_t)slope;
#ifdef ASTRALBOUND_FPU
    /* One reciprocal replaces six 64-bit divisions per vertex. Clipping
       guarantees z >= near_plane, so every projected quantity stays finite.
       Depths keep their metre scale whatever camera_unit the polygon uses. */
    for (i = 0; i < 3; i++) {
        double reciprocal = camera_unit / fpu_double(v[i].z);
        double px = fpu_double(v[i].x) * reciprocal / camera_unit, py = fpu_double(v[i].y) * reciprocal / camera_unit;
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
    /* One normalised reciprocal and multiplies replace up to six 64-bit
       divisions per vertex, as the FPU branch above does in floating point. */
    for (i = 0; i < 3; i++) {
        int shift;
        uint64_t y = reciprocal(v[i].z, &shift), depth = y * (uint64_t)camera_unit;
        int32_t px = ratio(v[i].x, v[i].z, shift, y), py = ratio(v[i].y, v[i].z, shift, y);
        t->inverse_z[i] = 33 - shift < 64 ? (uint32_t)(depth >> (33 - shift)) : 0;
        t->x[i] = screen_coord(frame->w / 2 + proj_dx + project(px, frame->w / 2));
        t->y[i] = screen_coord(view_h / 2 + proj_dy - project(py, frame->w / 2));
        if (t->surface) {
            t->surface_depth[i] = (int64_t)(shift <= 25 ? depth >> (25 - shift) : depth << (shift - 25));
            t->surface_x[i] = frame->w * 128 + project(px, frame->w * 128);
            t->surface_y[i] = (view_h / 2) * 256 - project(py, frame->w * 128);
        }
    }
#endif
    if ((t->x[0] < 0 && t->x[1] < 0 && t->x[2] < 0) ||
        (t->x[0] >= frame->w && t->x[1] >= frame->w && t->x[2] >= frame->w) ||
        (t->y[0] < 0 && t->y[1] < 0 && t->y[2] < 0) ||
        (t->y[0] >= view_h && t->y[1] >= view_h && t->y[2] >= view_h))
        return;
    t->z = (a.z + b.z + c.z) / (3 * camera_unit);
    t->color = (uint8_t)color;
    nt++;
}
/* Clip in camera space before projection. Clamping projected coordinates
   bends the reciprocal-depth plane of large nearby terrain faces. */
static int64_t clip_distance(Vec v, int plane) {
    switch (plane) {
    case 0: return v.z - near_plane * camera_unit;
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
/* Clip a camera-space triangle; a, b and c are its world vertices, used
   only to shade terrain. */
static void triangle_clip(Vec ca, Vec cb, Vec cc, Vec a, Vec b, Vec c, int color) {
    Vec vertices[2][10];
    int plane, i, count = 3, current = 0;
    vertices[0][0] = ca;
    vertices[0][1] = cb;
    vertices[0][2] = cc;
    for (plane = 0; plane < 5 && count >= 3; plane++) {
        Vec *in = vertices[current], *out = vertices[1 - current];
        int n = 0;
        for (i = 0; i < count; i++) {
            Vec p = in[i], q = in[(i + 1) % count];
            int64_t dp = clip_distance(p, plane), dq = clip_distance(q, plane);
            if (dp >= 0)
                out[n++] = p;
            /* Interpolate from the edge's lexicographically first end, so
               the triangle on the other side of a shared edge, which walks
               it the opposite way, clips it to the bit-identical vertex.
               Rounding from either end opened hairline cracks along
               clipped terrain edges. */
            if ((dp >= 0) != (dq >= 0)) {
                if (q.x < p.x || (q.x == p.x && (q.y < p.y || (q.y == p.y && q.z < p.z))))
                    out[n++] = add(q, scale(sub(p, q), dq, dq - dp));
                else
                    out[n++] = add(p, scale(sub(q, p), dp, dp - dq));
            }
        }
        count = n;
        current = 1 - current;
    }
    if (count >= 3) {
        int slope=terrain_surface && graphics.texture ? terrain_face_slope(terrain_surface,a,b,c) : 0;
        if(terrain_surface && !graphics.texture) {
            Vec local=sub(scale(add(a,add(b,c)),1,3),terrain_surface->center);
            color=material_color(terrain_surface,local,terrain_material(terrain_surface,local));
        }
        for (i = 1; i + 1 < count; i++)
            emit(vertices[current][0], vertices[current][i], vertices[current][i + 1], color, slope);
    }
}
static void triangle(Vec a, Vec b, Vec c, int color) {
    if (cl_active) { cl_triangle_world(a, b, c, color); return; }
    if (nt >= triangle_limit) return;
    triangle_clip(camera_poly(a), camera_poly(b), camera_poly(c), a, b, c, color);
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
    unsigned serial;
    int32_t spin_sin, spin_cos, radius;
    double m[3][3], t[3];
} ray;
/* Camera axis i in the world is row i of the view matrix, so rays and
   polygons share one rotation even when it is an exact on-foot basis. */
static void fpu_camera_to_world(const double in[3], double out[3]) {
    int i;
    view_current();
    for (i = 0; i < 3; i++)
        out[i] = (in[0] * (int)view_matrix.m[0][i] + in[1] * (int)view_matrix.m[1][i] +
                  in[2] * (int)view_matrix.m[2][i]) / 16384.0;
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
    ray.serial = view_matrix.serial;
    ray.spin_sin = p->spin_sin; ray.spin_cos = p->spin_cos; ray.radius = p->radius;
}
/* Q4 camera-space point to Q4 body-fixed planet coordinates. */
static void ray_local(const Planet *p, double x, double y, double z, double q[3]) {
    int i;
    view_current();
    if (ray.planet != p || ray.serial != view_matrix.serial || ray.spin_sin != p->spin_sin || ray.spin_cos != p->spin_cos ||
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
/* The same fold in fixed point: the transposed view matrix (camera to world)
   times the inverse body spin, Q30, with the body-fixed Q4 camera offset.
   The two screen columns are prescaled by 1/(w/2), so a pixel's ray costs
   one depth reciprocal and multiplies instead of nine 64-bit divisions. */
static struct {
    const Planet *planet;
    Vec camera, center;
    unsigned serial;
    int half;
    int32_t spin_sin, spin_cos;
    int32_t m[3][3], mx[3], my[3];
    int64_t t[3];
} ray;
static void ray_prepare(const Planet *p) {
    int64_t sn = -(int64_t)p->spin_sin, cs = p->spin_cos;
    Vec offset = body_rotate(p, sub(scale(camera_position, 1, FP / 16), scale(p->center, 16, 1)), 1);
    int i;
    view_current();
    if (!sn && !cs) cs = 1LL << 30;
    for (i = 0; i < 3; i++) {
        /* Column i is camera axis i (view row i) spun into the body frame. */
        int64_t x = view_matrix.m[i][0], y = view_matrix.m[i][1];
        ray.m[0][i] = (int32_t)round_div(x * cs - y * sn, 16384);
        ray.m[1][i] = (int32_t)round_div(x * sn + y * cs, 16384);
        ray.m[2][i] = (int32_t)(view_matrix.m[i][2] * 65536);
    }
    ray.half = frame->w / 2;
    for (i = 0; i < 3; i++) {
        ray.mx[i] = (int32_t)round_div(ray.m[i][0], ray.half);
        ray.my[i] = (int32_t)round_div(ray.m[i][1], ray.half);
    }
    ray.t[0] = offset.x; ray.t[1] = offset.y; ray.t[2] = offset.z;
    ray.planet = p; ray.camera = camera_position; ray.center = p->center;
    ray.serial = view_matrix.serial;
    ray.spin_sin = p->spin_sin; ray.spin_cos = p->spin_cos;
}
static void ray_current(const Planet *p) {
    view_current();
    if (ray.planet != p || ray.serial != view_matrix.serial || ray.spin_sin != p->spin_sin || ray.spin_cos != p->spin_cos ||
        ray.half != frame->w / 2 || ray.camera.x != camera_position.x ||
        ray.camera.y != camera_position.y || ray.camera.z != camera_position.z ||
        ray.center.x != p->center.x || ray.center.y != p->center.y || ray.center.z != p->center.z)
        ray_prepare(p);
}
/* Q4 camera-space point to Q4 body-fixed planet coordinates. */
static void ray_local(const Planet *p, Vec v, int64_t q[3]) {
    int i;
    ray_current(p);
    for (i = 0; i < 3; i++)
        q[i] = ray.t[i] + floor_shift(ray.m[i][0] * v.x + ray.m[i][1] * v.y + ray.m[i][2] * v.z + (1 << 29), 30);
}
/* |a| * y >> shift with a's sign; |a| below 2^32 keeps the product in range. */
static int64_t ray_scale(int64_t a, uint32_t y, int shift) {
    uint64_t m = (uint64_t)(uint32_t)(a < 0 ? -a : a) * y;
    m = shift < 64 ? m >> shift : 0;
    return a < 0 ? -(int64_t)m : (int64_t)m;
}
static void surface_ray_uv(const Planet *p, int x, int y, int64_t depth,
                           int *face, int *u, int *v) {
    int64_t q[3], dominant;
    int i, axis = 0, shift, step, gs = 0;
    uint32_t r;
    uint64_t g;
    ray_current(p);
    if (depth > 0) {
        /* Q4 depth 2^42/depth = r * 2^(shift - 21); with the Q30 direction
           the hit is direction * r >> (51 - shift). */
        int dx = x - ray.half, dy = view_h / 2 - y;
        r = reciprocal(depth, &shift);
        for (i = 0; i < 3; i++)
            q[i] = ray.t[i] + ray_scale((int64_t)ray.m[i][2] + ray.mx[i] * dx + ray.my[i] * dy, r, 51 - shift);
    } else ray_local(p, vec(0, 0, 1), q); /* the old z = 1 fallback: the camera */
    dominant = llabs(q[0]);
    if (llabs(q[1]) > dominant) { axis = 1; dominant = llabs(q[1]); }
    if (llabs(q[2]) > dominant) { axis = 2; dominant = llabs(q[2]); }
    *face = axis * 2 + (q[axis] < 0);
    if (!dominant) { *u = *v = 0; return; }
    /* q * radius / dominant, truncated exactly as surface_uv does, from one
       reciprocal: the other components normalise by dominant's shift (below
       2^32), multiply by radius * r cut to a 32-bit mantissa, and the
       estimate (low by at most one) is corrected from the remainder. */
    r = reciprocal(dominant, &shift);
    g = (uint64_t)(uint32_t)p->radius * r;
    for (step = 16; step; step /= 2)
        if (g >> (32 + step)) { g >>= step; gs += step; }
    if (g >> 32) { g >>= 1; gs++; }
    for (i = 0; i < 2; i++) {
        int64_t c = i == 0 ? (axis == 0 ? q[1] : q[0]) : (axis == 2 ? q[1] : q[2]);
        uint64_t a = (uint64_t)(c < 0 ? -c : c);
        uint32_t m;
        a = shift >= 0 ? a << shift : a >> -shift;
        m = (uint32_t)((uint64_t)(uint32_t)a * (uint32_t)g >> (63 - gs));
        if (shift >= 0) { /* exact operands: anything beyond 2^28 m is not terrain */
            uint32_t d = (uint32_t)(dominant << shift);
            uint64_t n = (uint64_t)(uint32_t)a * (uint32_t)p->radius, k = (uint64_t)m * d;
            if (k > n) m--;
            else if (n - k >= d) m++;
        }
        if (i == 0) *u = c < 0 ? -(int)m : (int)m;
        else *v = c < 0 ? -(int)m : (int)m;
    }
}
#endif
/* A triangle edge walked down the scanlines. x = x0 +/- floor(u*across/down)
   with u the distance from the start vertex, exactly the truncating division
   it replaces, but kept as quotient and remainder so a scanline one sample
   further on costs adds instead of a DIVS. */
typedef struct {
    int x, y, lo, hi, last, forward, negative, stepping;
    int32_t across, down, step_q, step_r, q, r;
} Edge;
static void edge_setup(Edge *e, int xi, int yi, int xj, int yj) {
    e->x = xi; e->y = yi;
    e->lo = yi < yj ? yi : yj; e->hi = yi < yj ? yj : yi;
    e->forward = yj > yi; e->negative = xj < xi;
    e->across = xj < xi ? xi - xj : xj - xi;
    e->down = e->hi - e->lo;
    e->stepping = 0; e->last = INT32_MIN;
}
/* Samples are scanlines, or Q8 rows for terrain (unit 256). */
static int edge_x(Edge *e, int sample, int unit) {
    if (e->last != INT32_MIN && sample - e->last == unit) {
        if (!e->stepping) {
            if (e->across < 0x7fffff) {
                int32_t n = unit * e->across;
                e->step_q = n / e->down;
                e->step_r = n - e->step_q * e->down;
            } else {
                int64_t n = (int64_t)unit * e->across;
                e->step_q = (int32_t)(n / e->down);
                e->step_r = (int32_t)(n - (int64_t)e->step_q * e->down);
            }
            e->stepping = 1;
        }
        if (e->forward) {
            e->q += e->step_q; e->r += e->step_r;
            if (e->r >= e->down) { e->r -= e->down; e->q++; }
        } else {
            e->q -= e->step_q; e->r -= e->step_r;
            if (e->r < 0) { e->r += e->down; e->q--; }
        }
    } else {
        int32_t u = e->forward ? sample - e->y : e->y - sample;
        /* Low-resolution facets normally fit exact 32-bit edge arithmetic;
           reserve wide division for long clipped spans. */
        if (e->across < 32768 && u < 32768) {
            e->q = u * e->across / e->down;
            e->r = u * e->across - e->q * e->down;
        } else {
            int64_t n = (int64_t)u * e->across;
            e->q = (int32_t)(n / e->down);
            e->r = (int32_t)(n - (int64_t)e->q * e->down);
        }
    }
    e->last = sample;
    return e->negative ? e->x - e->q : e->x + e->q;
}
/* The flat depth plane's whole part, (zx*dx + zy*dy) / area truncated, kept
   as an exact floor quotient and remainder. The next scanline's span start
   is usually a few pixels from the last, so it costs adds, not a 64-bit
   division; shared edges still see bit-identical depths and tie as before. */
typedef struct { int64_t zx, zy, area, q, r, xq, xr, yq, yr; int dx, dy, valid, stepping; } Plane;
static void floor_divide(int64_t n, int64_t d, int64_t *q, int64_t *r) {
    if (n >= INT32_MIN && n <= INT32_MAX && d <= INT32_MAX) {
        int32_t q32 = (int32_t)n / (int32_t)d;
        *q = q32; *r = (int32_t)n - q32 * (int32_t)d;
    } else {
        *q = n / d; *r = n - *q * d;
    }
    if (*r < 0) { *r += d; --*q; }
}
static void plane_setup(Plane *p, int64_t zx, int64_t zy, int64_t area) {
    p->zx = area < 0 ? -zx : zx; p->zy = area < 0 ? -zy : zy; p->area = area < 0 ? -area : area;
    p->q = p->r = p->xq = p->xr = p->yq = p->yr = 0;
    p->dx = p->dy = p->valid = p->stepping = 0;
}
static int64_t plane_at(Plane *p, int dx, int dy) {
    int step = dx - p->dx;
    if (p->valid && dy - p->dy == 1 && step >= -8 && step <= 8) {
        if (!p->stepping) {
            floor_divide(p->zx, p->area, &p->xq, &p->xr);
            floor_divide(p->zy, p->area, &p->yq, &p->yr);
            p->stepping = 1;
        }
        p->q += p->yq + step * p->xq; p->r += p->yr + step * p->xr;
        while (p->r >= p->area) { p->r -= p->area; p->q++; }
        while (p->r < 0) { p->r += p->area; p->q--; }
    } else
        floor_divide(p->zx * dx + p->zy * dy, p->area, &p->q, &p->r);
    p->dx = dx; p->dy = dy; p->valid = 1;
    return p->q + (p->q < 0 && p->r); /* toward zero, like the division */
}
static void raster(const Tri *t) {
    int y, miny = t->y[0], maxy = t->y[0], i, unit = t->surface ? 256 : 1;
    int64_t ax = t->x[1] - t->x[0], ay = t->y[1] - t->y[0];
    int64_t bx = t->x[2] - t->x[0], by = t->y[2] - t->y[0];
    int64_t az = (int64_t)t->inverse_z[1] - t->inverse_z[0];
    int64_t bz = (int64_t)t->inverse_z[2] - t->inverse_z[0];
    int64_t area = ax * by - bx * ay;
    int64_t zx = az * by - bz * ay, zy = ax * bz - bx * az;
    int64_t dz = 0, row = 0, row_step = 0;
    int64_t depth_x = 0, depth_y = 0;
    Plane plane;
    Edge edges[3];
    if (!area && !t->surface)
        return;
    plane_setup(&plane, zx, zy, area);
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
    } else
        dz = zx * 256 / area;
    for (i = 1; i < 3; i++) {
        if (t->y[i] < miny)
            miny = t->y[i];
        if (t->y[i] > maxy)
            maxy = t->y[i];
    }
    int terrain_y0=0,terrain_y1=0;
    if(t->surface) {
        terrain_y0=terrain_y1=t->surface_y[0];
        for(i=1;i<3;i++) {
            if(t->surface_y[i]<terrain_y0) terrain_y0=t->surface_y[i];
            if(t->surface_y[i]>terrain_y1) terrain_y1=t->surface_y[i];
        }
        miny=(terrain_y0-128+255)>>8; maxy=(terrain_y1+128)>>8;
    }
    if (miny < view_top)
        miny = view_top;
    if (maxy >= view_h)
        maxy = view_h - 1;
    for (i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        if (t->surface)
            edge_setup(&edges[i], t->surface_x[i], t->surface_y[i], t->surface_x[j], t->surface_y[j]);
        else
            edge_setup(&edges[i], t->x[i], t->y[i], t->x[j], t->y[j]);
    }
    /* The terrain plane's row term advances by addition. */
    if (t->surface) {
        row = depth_y * ((int64_t)miny * 256 - t->surface_y[0]);
        row_step = depth_y * 256;
    }
    for (y = miny; y <= maxy; y++, row += row_step) {
        int xs[3], n = 0, sample = y;
        if (t->surface) {
            sample = y * 256;
            if (sample < terrain_y0) sample = terrain_y0;
            if (sample >= terrain_y1) sample = terrain_y1 - 1;
        }
        for (i = 0; i < 3; i++)
            if (edges[i].lo <= sample && sample < edges[i].hi)
                xs[n++] = edge_x(&edges[i], sample, unit);
        if (n >= 2) {
            int l = xs[0] < xs[1] ? xs[0] : xs[1], r = xs[0] > xs[1] ? xs[0] : xs[1];
            int64_t z;
            /* Coverage must use the same subpixel projection as depth.
               Rounding a fine edge's midpoint independently of its parent
               edge otherwise opens black slivers at terrain LOD junctions. */
            /* Cover the pixel footprint, with a half-pixel guard for the
               metre-rounded world vertices shared by two terrain levels. */
            if(t->surface) { l=(l+127)>>8; r=(r+128)>>8; }
            if (l < view_left)
                l = view_left;
            /* Evaluate the triangle's depth plane at the actual pixel, not
               at a rounded scanline edge. This keeps shared faces coherent. */
            if (t->surface)
                z = t->surface_depth[0] + (depth_x * (l * 256 - t->surface_x[0]) + row) / 256;
            else z = ((int64_t)t->inverse_z[0] + plane_at(&plane, l - t->x[0], y - t->y[0])) * 256;
            if (r >= frame->w - view_right_inset)
                r = frame->w - view_right_inset - 1;
            if (r >= l) {
                int x;
                uint8_t *pixels = frame->pixels + (size_t)y * frame->w + l;
                uint32_t *depths = depth_buffer + (size_t)y * frame->w + l;
                int64_t end = z + dz * (r - l);
                /* Most distant polygons fit Q8 depth in 32 bits. Keep an
                   exact wide fallback for nearby/clipped geometry. */
                if (t->surface && graphics.texture) {
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
/* Ships are drawn in their own camera-space basis, as Frontier did: the
   origin takes the only 64-bit camera transform, then every model point is
   origin + x*R + y*U + z*F with 16-bit coefficients and 32-bit sums.
   Offsets are in sixteenths of a camera unit, the vertices' unit. */
typedef struct {
    Vec origin;          /* camera space, Q4 */
    int16_t axis[3][3];  /* [model axis][camera axis]: Q4 per model unit << shift */
    int shift, inside;   /* inside: the hull needs no clipping */
    int32_t light[3];    /* camera-space Q14 */
} ShipView;
static void ship_extent(int ship, int *x, int *y, int *front, int *back);
/* Sphere against the clip planes: -1 wholly outside one, 1 inside them all,
   0 when it straddles and its triangles must be clipped. Plane normals are
   rounded up, so either verdict is conservative. */
static int sphere_clip(Vec centre, int64_t radius) {
    int64_t half_w = frame->w / 2, top = view_h / 2, bottom = view_h - view_h / 2;
    int64_t norm[5];
    int plane, inside = 1;
    norm[0] = 1; norm[1] = norm[2] = 3; /* 3/2 > sqrt(2), applied below */
    norm[3] = (int64_t)isqrt64((uint64_t)(half_w * half_w + top * top)) + 1;
    norm[4] = (int64_t)isqrt64((uint64_t)(half_w * half_w + bottom * bottom)) + 1;
    for (plane = 0; plane < 5; plane++) {
        int64_t d = clip_distance(centre, plane), reach = radius * norm[plane];
        if (plane == 1 || plane == 2) reach /= 2;
        if (d < -reach) return -1;
        if (d < reach) inside = 0;
    }
    return inside;
}
typedef struct { int32_t off[3]; Vec cam; } ShipVertex;
#define MUL16(a, b) ((int32_t)(int16_t)(a) * (int16_t)(b))
static int64_t dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
/* A direction cut to under 2^14 per component, so 32-bit dot products of
   two such directions cannot overflow. */
static void direction14(int64_t x, int64_t y, int64_t z, int32_t *out) {
    uint64_t m = (uint64_t)(llabs(x) | llabs(y) | llabs(z));
    int s = 0;
    while (m >= 16384) { m >>= 1; s++; }
    out[0] = (int32_t)floor_shift(x, s);
    out[1] = (int32_t)floor_shift(y, s);
    out[2] = (int32_t)floor_shift(z, s);
}
static int32_t dot32(const int32_t *a, const int32_t *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
/* Model coordinates are 256ths of the spec extents; the origin is Q8, so a
   landed hull beside a walker does not jump between whole metres as the
   body turns. Returns 0 when the hull's bounding sphere lies wholly outside
   the view. */
static int ship_view(ShipView *v, int ship, Vec origin, Vec right, Vec up, Vec front, int unit, Vec light) {
    const ShipSpec *spec = &ship_specs[ship];
    const Vec axes[3] = {right, up, front};
    const int64_t size[3] = {spec->width, spec->height, spec->depth};
    int64_t p[3][3], largest = 0, reach = 0, radius;
    Vec d = sub(scale(origin, 1, FP), scale(camera_position, 1, FP));
    int a, c, t = 0, ex, ey, ez[2];
    /* Keep the camera's sub-unit position for the origin; otherwise a slowly
       moving chase camera shifts the whole hull in whole steps. */
    if (llabs(d.x) < (1LL << 30) && llabs(d.y) < (1LL << 30) && llabs(d.z) < (1LL << 30)) {
        Vec q = view_rotate(sub(origin, camera_position));
        v->origin = vec(round_div(q.x * camera_unit, 16), round_div(q.y * camera_unit, 16),
                        round_div(q.z * camera_unit, 16));
    } else
        v->origin = scale(view_rotate(d), 16LL * camera_unit, 1);
    /* Q22 camera axes times extents: offsets are coordinate * p >> 26. */
    for (a = 0; a < 3; a++) {
        Vec q = view_rotate(vec(axes[a].x * 256, axes[a].y * 256, axes[a].z * 256));
        int64_t k = size[a] * unit * camera_unit;
        p[a][0] = q.x * k; p[a][1] = q.y * k; p[a][2] = q.z * k;
        for (c = 0; c < 3; c++)
            if (llabs(p[a][c]) > largest) largest = llabs(p[a][c]);
    }
    /* Largest is below 2^38 for any spec, so the shift stays positive. */
    while ((largest >> t) >= 32767) t++;
    for (a = 0; a < 3; a++)
        for (c = 0; c < 3; c++)
            v->axis[a][c] = (int16_t)round_div(p[a][c], 1LL << t);
    v->shift = 26 - t;
    /* The sphere encloses the model's bounding box, measured with the same
       coefficients as the vertices, then widened for their rounding to
       whole camera units and the centre's. */
    ship_extent(ship, &ex, &ey, &ez[1], &ez[0]);
    for (c = 0; c < 8; c++) {
        int x = (c & 1 ? ex : -ex) * 16, y = (c & 2 ? ey : -ey) * 16, z = ez[c >> 2] * 16;
        int64_t corner = 0;
        for (a = 0; a < 3; a++) {
            int64_t o = MUL16(x, v->axis[0][a]) + MUL16(y, v->axis[1][a]) + MUL16(z, v->axis[2][a]);
            corner += o * o;
        }
        if (corner > reach) reach = corner;
    }
    radius = ((int64_t)isqrt64((uint64_t)reach) >> v->shift) / 16 + 4;
    v->inside = sphere_clip(vec(floor_shift(v->origin.x, 4), floor_shift(v->origin.y, 4),
                                floor_shift(v->origin.z, 4)), radius);
    if (v->inside < 0)
        return 0;
    light = view_rotate(light);
    v->light[0] = (int32_t)light.x; v->light[1] = (int32_t)light.y; v->light[2] = (int32_t)light.z;
    return 1;
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
   no hand-kept winding; back faces are dropped before clipping. Normals use
   the Q4 offsets, cut to 14-bit directions for 32-bit dot products. */
static void ship_face(const ShipView *v, const ShipVertex *a, const ShipVertex *b,
                      const ShipVertex *c, const int32_t *inside, int tone) {
    int64_t e1[3], e2[3], cross[3];
    int32_t n[3], mid[3], out[3], eye[3];
    int i, d, length;
    for (i = 0; i < 3; i++) {
        e1[i] = b->off[i] - a->off[i];
        e2[i] = c->off[i] - a->off[i];
        mid[i] = (a->off[i] + b->off[i] + c->off[i]) / 3;
    }
    cross[0] = e1[1] * e2[2] - e1[2] * e2[1];
    cross[1] = e1[2] * e2[0] - e1[0] * e2[2];
    cross[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (!cross[0] && !cross[1] && !cross[2])
        return;
    direction14(cross[0], cross[1], cross[2], n);
    direction14(mid[0] - inside[0], mid[1] - inside[1], mid[2] - inside[2], out);
    if (dot32(n, out) < 0)
        for (i = 0; i < 3; i++) n[i] = -n[i];
    /* The eye is the camera-space origin. */
    direction14(v->origin.x + mid[0], v->origin.y + mid[1], v->origin.z + mid[2], eye);
    if (dot32(n, eye) >= 0)
        return;
    length = (int)isqrt64((uint64_t)dot32(n, n));
    d = dot32(n, v->light) / length;
    tone = ship_colour(tone, d > 0 ? 3 + d * 12 / 16384 : 3 + d * 2 / 16384);
    if (v->inside)
        emit(a->cam, b->cam, c->cam, tone, 0);
    else
        triangle_clip(a->cam, b->cam, c->cam, a->cam, b->cam, c->cam, tone);
}
/* The x term is kept apart so a mirrored loft only flips its sign. */
static void ship_loft(const ShipView *v, const ShipLoft *l) {
    const int8_t (*profile)[2] = ship_profile[l->profile];
    int32_t across[4][9][3], along[4][9][3], axis[4][3], centre[3], inside[3];
    ShipVertex ring[4][8];
    int s, p, i, side, n = ship_profile_points[l->profile], last = l->count - 1;
    /* Point n of each station is its axis point. */
    for (s = 0; s < l->count; s++) {
        const ShipStation *st = &l->s[s];
        for (p = 0; p <= n; p++) {
            int x = l->x * 16 + (p < n ? profile[p][0] * st->w : 0);
            int y = st->y * 16 + (p < n ? profile[p][1] * st->h : 0), z = st->z * 16;
            for (i = 0; i < 3; i++) {
                across[s][p][i] = MUL16(x, v->axis[0][i]);
                along[s][p][i] = MUL16(y, v->axis[1][i]) + MUL16(z, v->axis[2][i]);
            }
        }
    }
    for (side = 1; side >= (l->flags & LOFT_MIRROR ? -1 : 1); side -= 2) {
        centre[0] = centre[1] = centre[2] = 0;
        for (s = 0; s < l->count; s++) {
            for (p = 0; p <= n; p++) {
                int32_t *off = p < n ? ring[s][p].off : axis[s];
                for (i = 0; i < 3; i++)
                    off[i] = (int32_t)floor_shift(side > 0 ? along[s][p][i] + across[s][p][i]
                                                           : along[s][p][i] - across[s][p][i], v->shift);
                if (p < n)
                    ring[s][p].cam = vec(floor_shift(v->origin.x + off[0] + 8, 4),
                                         floor_shift(v->origin.y + off[1] + 8, 4),
                                         floor_shift(v->origin.z + off[2] + 8, 4));
            }
            for (i = 0; i < 3; i++) centre[i] += axis[s][i];
        }
        for (i = 0; i < 3; i++) centre[i] /= l->count;
        for (s = 0; s < last; s++) {
            for (i = 0; i < 3; i++) inside[i] = (axis[s][i] + axis[s + 1][i]) / 2;
            for (p = 0; p < n; p++) {
                int q = (p + 1) % n;
                ship_face(v, &ring[s][p], &ring[s][q], &ring[s + 1][q], inside, l->tone);
                ship_face(v, &ring[s][p], &ring[s + 1][q], &ring[s + 1][p], inside, l->tone);
            }
        }
        for (p = 1; p + 1 < n; p++) {
            ship_face(v, &ring[0][0], &ring[0][p], &ring[0][p + 1], centre, l->tone);
            ship_face(v, &ring[last][0], &ring[last][p], &ring[last][p + 1], centre,
                      l->flags & LOFT_EXHAUST ? GLOW_TONE : l->tone);
        }
    }
}
/* Shared by the chase camera (unit 1, metres) and the shipyard preview. */
static void ship_mesh(int ship, Vec origin, Vec right, Vec up, Vec front, int unit, Vec light) {
    const Planet *surface = terrain_surface;
    if (cl_active) { cl_ship(ship, origin, right, up, front, light); return; }
    ShipView v;
    int i;
    if (!ship_view(&v, ship, origin, right, up, front, unit, light))
        return;
    terrain_surface = NULL;
    for (i = 0; i < (graphics.ships ? SHIP_LOFTS : 1); i++) {
        const ShipLoft *l = &ship_lofts[ship][i];
        if (l->count)
            ship_loft(&v, l);
    }
    terrain_surface = surface;
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
/* Order-preserving 30-bit depth key: exact metres below 2^24, then 24
   significant bits, ample for nearest-first drawing. */
static uint32_t depth_key(int64_t z) {
    uint64_t u = z > 0 ? (uint64_t)z : 0;
    uint32_t e = 0, step;
    if (u < (1U << 24))
        return (uint32_t)u;
    for (step = 32; step; step /= 2)
        if (u >> (24 + step)) { u >>= step; e += step; }
    return ((e + 1) << 24) | (uint32_t)(u >> 1);
}
/* Nearest first. A stable byte-wise radix sort of indices replaces a qsort
   that swapped whole triangles: four passes of adds, and none for bytes all
   keys share. Equal depths draw in reverse emission order, so ports, hulls
   and markers, emitted before terrain, win depth ties with the ground. */
static void draw_triangles(void) {
    int i, pass, *order = triangle_order, *spare = triangle_order + triangle_capacity;
    if (nt < 32) {
        qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
        for (i = 0; i < nt; i++) order[i] = i;
    } else {
        for (i = 0; i < nt; i++) {
            triangle_keys[i] = depth_key(triangles[i].z);
            order[i] = nt - 1 - i;
        }
        for (pass = 0; pass < 32; pass += 8) {
            int count[256] = {0}, start = 0, *swap;
            for (i = 0; i < nt; i++) count[triangle_keys[i] >> pass & 255]++;
            if (count[triangle_keys[0] >> pass & 255] == nt) continue;
            for (i = 0; i < 256; i++) { int n = count[i]; count[i] = start; start += n; }
            for (i = 0; i < nt; i++) spare[count[triangle_keys[order[i]] >> pass & 255]++] = order[i];
            swap = order; order = spare; spare = swap;
        }
    }
    for (i = 0; i < nt; i++) {
        if (platform_pump && i % 32 == 0)
            platform_pump();
        raster(&triangles[order[i]]);
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
static uint32_t surface_light_epoch, surface_light_key;
/* A tick alone cannot identify illumination: a restored/preview scene can
   move its sun or an eclipsing body without advancing time. Hash the light
   inputs once per terrain pass, never per pixel. Refresh colour and slope
   lazily, retaining the expensive material and geological sample tables. */
static uint32_t surface_light_vec(uint32_t key, Vec v) {
    key=hash32(key ^ (uint32_t)v.x ^ (uint32_t)((uint64_t)v.x>>32));
    key=hash32(key ^ (uint32_t)v.y ^ (uint32_t)((uint64_t)v.y>>32));
    return hash32(key ^ (uint32_t)v.z ^ (uint32_t)((uint64_t)v.z>>32));
}
static void surface_lighting_prepare(const Planet *p) {
    const System *system=&game->system;
    uint32_t key=hash32(game->tick/HZ ^ system->seed ^ (uint32_t)system->stars);
    int i;
    key=surface_light_vec(key,p->center);
    key=hash32(key ^ (uint32_t)p->spin_sin);
    key=hash32(key ^ (uint32_t)p->spin_cos);
    for(i=0;i<system->stars;i++) {
        key=surface_light_vec(key,system->star[i].center);
        key=hash32(key ^ (uint32_t)system->star[i].type);
    }
    for(i=0;i<system->count;i++) if(&system->planets[i]!=p) {
        key=surface_light_vec(key,system->planets[i].center);
        key=hash32(key ^ (uint32_t)system->planets[i].radius);
    }
    if(!surface_light_epoch || key!=surface_light_key) {
        surface_light_key=key;
        if(!++surface_light_epoch) {
            /* Reserve zero for uninitialised entries, including wraparound. */
            memset(surface_cache,0,sizeof(surface_cache));
            surface_light_epoch=1;
        }
    }
}

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
    surface_lighting_prepare(p);
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
    uint32_t epoch = surface_light_epoch;
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
    uint32_t epoch=surface_light_epoch;
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
    if (material == 5 || graphics.texture == 1) return color;
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
    if (graphics.texture >= 3 && material == 0 && footprint < 16) {
        uint32_t h = hash32(grain ^ (unsigned)(u & 192)*173U ^ (unsigned)(v & 192)*7919U);
        int wave=(v+(u & 63)*(u & 63)/128+(int)(h & 15)) & 63;
        if (wave<8) variation += 3*(16-footprint)/16;
    }
    /* Fine 16 m inclusions supply optic flow in the last few hundred metres. */
    if (graphics.texture >= 3 && material != 0 && depth > 1073741824U/500 && footprint < 4) {
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
    if (graphics.texture >= 3) surface_slope(p,slot,face,tu,tv);
    {
        int slope=graphics.texture >= 3 ? surface_cache[slot].slope : 0;
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
/* A flat globe facet is coloured by one ground sample, packed as material
   (bits 0-2) and regional albedo + 16 (bits 3-7). */
static uint8_t ground_sample(const Planet *fixed, Vec direction) {
    int material = terrain_material(fixed, direction);
    return (uint8_t)(material | (terrain_regional_albedo(fixed, direction, material) + 16) << 3);
}
static const int ground_ecs[] = {25,8,8,16,28,21};
static const int ground_aga[] = {80,64,64,96,112,96};
/* shade: material_color's illumination above the material base, 0..14. */
static int ground_shade(int shade, uint8_t sample) {
    int material = sample & 7;
    shade += ((sample >> 3) - 16)*shade/14;
    if (shade<0) shade=0;
    if (shade>14) shade=14;
    return frame->enhanced ? ground_aga[material]+shade : ground_ecs[material]+shade/6;
}
/* Geometry clipmaps use a fixed body-space lattice. Camera motion chooses
   visible cells, but never relocates the terrain samples. Outer vertices
   continuously converge on the next level's triangles before cells retire. */
#define TERRAIN_GRID 8
/* The small window uses centred snapping and a later morph band, so its
   child perimeter meets an unmorphed parent even as the camera recentres. */
#define TERRAIN_LOW_GRID 6
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
static Vec face_direction(const Planet *p, int face, int u, int v) {
    int sign = face & 1 ? -p->radius : p->radius;
    return face / 2 == 0 ? vec(sign,u,v) : face / 2 == 1 ? vec(u,sign,v) : vec(u,v,sign);
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
        Vec direction = face_direction(p, face, u, v);
        set_rotation(&fixed, 0);
        int height = terrain_elevation(&fixed, direction);
        terrain_vertices[slot].point = radial(direction, p->radius + (height > 0 ? height : 0));
        terrain_vertices[slot].u = u; terrain_vertices[slot].v = v;
        terrain_vertices[slot].face = face; terrain_vertices[slot].valid = 1;
    }
    return terrain_vertices[slot].point;
}
static Vec terrain_grid_point(const Planet *p, int face, int u, int v, int step,
                              int camera_u, int camera_v) {
    Vec point = terrain_fixed_vertex(p, face, u, v);
    int distance = abs(u - camera_u), dv = abs(v - camera_v);
    int parent_step = step * 2, blend;
    if (dv > distance) distance = dv;
    int grid = graphics.terrain <= 1 ? TERRAIN_LOW_GRID : TERRAIN_GRID;
    int band = grid == TERRAIN_LOW_GRID ? grid - 2 : grid - 3;
    blend = (int)((int64_t)(distance - band * step) * 65536 / step);
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
    return point;
}
static Vec terrain_grid_vertex(const Planet *p, int face, int u, int v, int step,
                               int camera_u, int camera_v) {
    return add(p->center, body_rotate(p, terrain_grid_point(p, face, u, v, step, camera_u, camera_v), 0));
}
/* A clipmap vertex in the world (whole metres, for shading) and in camera
   space. Rounded afresh each tick as the body turns, whole-metre vertices
   shook the ground by up to a metre around a walker's 2 m eye, so on foot
   the camera-space point comes from the exact body-fixed offset to the eye. */
typedef struct { Vec world, eye; } TerrainVertex;
static struct { const Planet *p; Vec eye; } terrain_eye;
static TerrainVertex clip_vertex(const Planet *p, Vec point) {
    TerrainVertex t;
    t.world = add(p->center, body_rotate(p, point, 0));
    if (terrain_eye.p == p) {
        Vec r = sub(scale(point, FP, 1), terrain_eye.eye);
        if (llabs(r.x) < 1000000LL * FP && llabs(r.y) < 1000000LL * FP && llabs(r.z) < 1000000LL * FP) {
            Vec q = view_rotate(body_rotate(p, r, 0));
            int64_t d = FP / camera_unit;
            t.eye = vec(round_div(q.x, d), round_div(q.y, d), round_div(q.z, d));
            return t;
        }
    }
    t.eye = camera_poly(t.world);
    return t;
}
static void terrain_triangle(TerrainVertex a, TerrainVertex b, TerrainVertex c, int color) {
    if (cl_active) { cl_triangle_world(a.world, b.world, c.world, color); return; }
    if (nt >= triangle_limit) return;
    triangle_clip(a.eye, b.eye, c.eye, a.world, b.world, c.world, color);
}
/* Above the detail altitude the same lattice continues as flat facets, each
   coloured by a ground sample at its centroid, so coasts hold still while
   the ship moves. Each level's 16x16 window slides over its own lattice:
   indexing cells by lattice coordinate modulo 16 never evicts a visible one. */
#define PATCH_LEVELS 20
static struct {
    int u, v;
    uint8_t face, valid, sample[2];
} patch_cells[PATCH_LEVELS][TERRAIN_GRID * 2][TERRAIN_GRID * 2];
static uint32_t patch_seed;
static int patch_radius, patch_type;
static const uint8_t *patch_samples(const Planet *p, int face, int u, int v, int step) {
    int level = 0, x = (u / step) & (TERRAIN_GRID * 2 - 1), y = (v / step) & (TERRAIN_GRID * 2 - 1);
    static uint8_t uncached[2];
    uint8_t *sample;
    while ((TERRAIN_FINE_STEP << level) < step) level++;
    if (patch_seed != p->seed || patch_radius != p->radius || patch_type != (int)p->type) {
        memset(patch_cells, 0, sizeof(patch_cells));
        patch_seed = p->seed; patch_radius = p->radius; patch_type = (int)p->type;
    }
    sample = level < PATCH_LEVELS ? patch_cells[level][y][x].sample : uncached;
    if (level >= PATCH_LEVELS || !patch_cells[level][y][x].valid ||
        patch_cells[level][y][x].u != u || patch_cells[level][y][x].v != v ||
        patch_cells[level][y][x].face != face) {
        Planet fixed = *p;
        set_rotation(&fixed, 0);
        /* Centroids of the (a,b,c) and (a,c,d) triangles drawn below. */
        sample[0] = ground_sample(&fixed, face_direction(p, face, u + step * 2 / 3, v + step / 3));
        sample[1] = ground_sample(&fixed, face_direction(p, face, u + step / 3, v + step * 2 / 3));
        if (level < PATCH_LEVELS) {
            patch_cells[level][y][x].u = u; patch_cells[level][y][x].v = v;
            patch_cells[level][y][x].face = (uint8_t)face; patch_cells[level][y][x].valid = 1;
        }
    }
    return sample;
}
/* A sphere point P is in view from C only while C.P exceeds R^2. */
static int patch_visible(const Planet *p, Vec local, Vec a, Vec b, Vec c, Vec d) {
    int64_t horizon = (int64_t)p->radius * p->radius;
    Vec corner[4];
    int i;
    corner[0] = a; corner[1] = b; corner[2] = c; corner[3] = d;
    for (i = 0; i < 4; i++) {
        Vec q = sub(corner[i], p->center);
        if (q.x * local.x + q.y * local.y + q.z * local.z > horizon) return 1;
    }
    return 0;
}
static void terrain_clipmap(const Planet *p, Vec local, int step, int span) {
    int grid = graphics.terrain <= 1 ? TERRAIN_LOW_GRID : TERRAIN_GRID;
    Vec fixed = body_rotate(p, local, 1);
    int64_t direction[3] = {fixed.x, fixed.y, fixed.z};
    int face, camera_u, camera_v, x, y, first = step, flat = !terrain_surface;
    int inner_u0 = 0, inner_v0 = 0, inner_u1 = 0, inner_v1 = 0;
    TerrainVertex rows[2][TERRAIN_GRID * 2 + 1];
    surface_uv(p, direction, &face, &camera_u, &camera_v);
    terrain_eye.p = fine_camera && game->on_foot && p == &game->system.planets[game->body] ? p : NULL;
    if (terrain_eye.p)
        terrain_eye.eye = body_rotate(p, sub(camera_position, scale(p->center, FP, 1)), 1);
    if (flat) {
        /* span arrives as the tangent distance D to the horizon. The face
           projection stretches it radially away from the face centre: a
           camera rho out sees the far horizon D(R^2+rho^2)/(R^2-rho D) away. */
        int64_t r2 = (int64_t)p->radius * p->radius;
        int64_t rho = (int64_t)isqrt64((uint64_t)((int64_t)camera_u * camera_u +
                                                  (int64_t)camera_v * camera_v));
        int64_t slack = r2 - rho * span;
        if (slack < r2 / 16) span = p->radius * 8;
        else span = (int)(span * ((r2 + rho * rho) / (slack >> 16)) >> 16);
    }
    for (;; step *= 2) {
        if(nt >= triangle_limit) break;
        int snap = grid == TERRAIN_LOW_GRID ? step : 0;
        int u0 = terrain_floor(camera_u + snap, step * 2) * step * 2 - grid * step;
        int v0 = terrain_floor(camera_v + snap, step * 2) * step * 2 - grid * step;
        int u1 = u0 + grid * 2 * step;
        int v1 = v0 + grid * 2 * step;
        for (y = 0; y <= grid * 2; y++) {
            int v = v0 + y * step;
            if (platform_pump) platform_pump();
            for (x = 0; x <= grid * 2; x++) {
                int u = u0 + x * step;
                if (step != first && u > inner_u0 && u < inner_u1 &&
                    v > inner_v0 && v < inner_v1) continue;
                rows[y % 2][x] = clip_vertex(p, terrain_grid_point(p,face,u,v,step,camera_u,camera_v));
            }
            if (y) for (x = 0; x < grid * 2; x++) {
                int u = u0 + x * step;
                if (step != first && u >= inner_u0 && u + step <= inner_u1 &&
                    v - step >= inner_v0 && v <= inner_v1) continue;
                TerrainVertex a = rows[(y - 1) % 2][x], b = rows[(y - 1) % 2][x + 1];
                TerrainVertex c = rows[y % 2][x + 1], d = rows[y % 2][x];
                if (flat) {
                    const uint8_t *sample;
                    int shade;
                    if (!patch_visible(p, local, a.world, b.world, c.world, d.world)) continue;
                    sample = patch_samples(p, face, u, v - step, step);
                    /* One illumination at the cell centre serves both halves. */
                    shade = surface_light(&game->system, p, sub(scale(add(a.world,c.world),1,2),p->center)) * 14 / 16384;
                    if (!frame->enhanced) shade = shade / 6 * 6;
                    terrain_triangle(a,b,c,ground_shade(shade,sample[0]));
                    terrain_triangle(a,c,d,ground_shade(shade,sample[1]));
                } else {
                    terrain_triangle(a,b,c,0); terrain_triangle(a,c,d,0);
                }
            }
        }
        /* Recentering can leave a level two steps short on one side. */
        if (flat && step * (grid - 2) >= span) break;
        if (step * grid >= span) {
            /* Only the distant outer perimeter descends into the fallback
               globe. Adjacent clipmap levels share their morphed edges. */
            for (y = 0; y < 4; y++) for (x = 0; x < grid * 2; x++) {
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
#define PATCH_ALTITUDE 200000
static void planet_mesh(int body) {
    const Planet *p = &game->system.planets[body];
    Vec cam_local = sub(scale(viewer(), 1, FP), p->center);
    int radius = p->radius;
    static const int steps[] = {16384,4096,1024,256,128};
    int fine_step=steps[graphics.terrain];
    int64_t distance = length(cam_local), altitude = distance - radius;
    /* Below PATCH_ALTITUDE a coarse clipmap adds what the analytic sphere
       cannot: relief on the horizon and the kilometre-scale inlets, ranges
       and forests that planet_field only blends. Its facets are about an
       eighth of the altitude, so above this they would be coarser than the
       sphere's own per-pixel coastlines. */
    int patch = body == game->body && altitude >= TERRAIN_DETAIL_ALTITUDE &&
                altitude < PATCH_ALTITUDE && p->type != GAS_GIANT;
    terrain_surface = body == game->body && altitude < TERRAIN_DETAIL_ALTITUDE ? p : NULL;
    if (terrain_surface && fine_step > TERRAIN_FINE_STEP) {
        /* Keep the physical 256 m lattice around low flight and cities.
           Low reduces the window and distant detail, rather than stretching
           kilometre-wide facets across the camera and landing pads. */
        fine_step = TERRAIN_FINE_STEP;
        while (fine_step < altitude / 8 && fine_step < steps[graphics.terrain]) fine_step *= 2;
    }
    if (terrain_surface) surface_lod_prepare(p, altitude);
    {
        /* The sphere itself is drawn analytically (planet_disc); only the
           relief near the ship is geometry. */
        Vec c = camera(p->center);
        if (c.z + radius < 8 || (!terrain_surface && !patch)) {
            terrain_surface = NULL;
            return;
        }
    }
    if (body == game->body && altitude < TERRAIN_DETAIL_ALTITUDE)
        terrain_clipmap(p, cam_local, fine_step,
            (int)isqrt64((uint64_t)(2LL * p->radius * (TERRAIN_DETAIL_ALTITUDE + TERRAIN_MAX_HEIGHT))) * 2);
    else if (patch) {
        /* Facets of about an eighth of the altitude beneath the ship, out
           to the horizon. */
        int step = fine_step;
        while (step < altitude / 8) step *= 2;
        terrain_clipmap(p, cam_local, step,
            (int)isqrt64((uint64_t)(2LL * radius * altitude + altitude * altitude)));
    }
    terrain_surface = NULL;
}
/* Decorative forests use the same terrain sampler as flight collision.
   Only nearby land has geometry; the globe material carries distant forests. */
static void forests(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(viewer(), 1, FP), p->center), up = unit(local, 16384);
    Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), front;
    int x, y, span=graphics.trees == 1 ? 2 : graphics.trees == 3 ? 8 : 4;
    if(!graphics.trees) return;
    if (altitude_at(game, viewer()) > 3000 || p->type != OCEAN)
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
        for (y = -span; y <= span; y++)
            for (x = -span; x <= span; x++) {
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
    int i, band, segments=graphics.rings ? 4 << graphics.rings : 0;
    for (band = 0; band < 3; band++) {
        int64_t inner = (int64_t)p->radius * (130 + band * 25) / 100;
        int64_t outer = (int64_t)p->radius * (150 + band * 25) / 100;
        for (i = 0; i < segments; i++) {
            Vec a = vec(sini(i * 1024 / segments), cosi(i * 1024 / segments), 0);
            Vec b = vec(sini((i + 1) * 1024 / segments), cosi((i + 1) * 1024 / segments), 0);
            Vec c = add(p->center, scale(b, outer, 16384));
            Vec d = add(p->center, scale(a, outer, 16384));
            a = add(p->center, scale(a, inner, 16384));
            b = add(p->center, scale(b, inner, 16384));
            triangle(a, b, c, 17 + band);
            triangle(a, c, d, 17 + band);
        }
    }
}
#include "render_rings.inc"
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
    static const int ranges[]={1,2,3,5,8};
    int cell = cloud_spacing(p), rings = ranges[graphics.cloud_range], face, ring, du, dv, k;
    int64_t alt = length(local) - p->radius, range = (int64_t)cell * rings;
    if (!graphics.clouds || !p->atmosphere || alt > (int64_t)cell * 10) return;
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
                    if (nt > triangle_limit - scenery_reserve) return; /* leave room for nearby ports and ships */
                    c = cloud_at(p, face, u + du, v + dv, game->tick);
                    if (!c) continue;
                    base = body_rotate(p, c->base, 0);
                    rel = sub(local, base);
                    dist = length(rel);
                    fade = (int)((range - dist) * 256 / (cell * 2));
                    if (fade <= 16) continue;
                    if (fade > 256) fade = 256;
                    lod = dist < cell * 2 ? 0 : dist < cell * 7 / 2 ? 1 : 2;
                    if(graphics.clouds == 1) lod=2;
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
                    for (k = 0; k < c->puffs && k < (graphics.clouds == 3 ? 8 : 6 - lod * 2); k++)
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
        weather_status |= 1;
    }
    if (rain) {
        for (i = 0; i < (frame->enhanced ? 48 : 24) * (cinema ? 2 : 1); i++) {
            uint32_t h = hash32(p->seed + (unsigned)i * 7919U);
            x = (int)(h % (unsigned)frame->w);
            /* The title sequence freezes world time; its rain runs on real time. */
            y = (int)(((h >> 16) + (cinema ? game->ui_clock : game->tick) * (rain == 1 ? 9U : 2U)) % (unsigned)view_h);
            rect(x, y, rain == 1 ? 1 : 2, rain == 1 ? 7 : 2, lit ? 7 : 8);
        }
        weather_status |= rain == 1 ? 2 : 4;
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
    ship_mesh(ship, n->pos, right, up, front, 1, unit(sub(game->system.star[0].center, p), 16384));
}
/* Depth-tested billboard particles use the existing indexed palette on ECS,
   AGA and RTG. All random directions are seeded once per event. */
typedef struct { Vec p; int radius, age, fire; uint32_t seed; } DestructionSprite;
static DestructionSprite destruction_sprites[MAX_SMOKE + MAX_BLASTS * 15 + MAX_NPC + 1];
static int sprite_count;
static void cl_sprite(const DestructionSprite *s);
static void destruction_sprite(Vec pos, int body, int radius, int age, int fire, uint32_t seed) {
    DestructionSprite *s;
    if (sprite_count >= (int)(sizeof(destruction_sprites) / sizeof(*destruction_sprites))) return;
    s = &destruction_sprites[sprite_count++];
    s->p = camera(scale(destruction_position(game, pos, body), 1, FP));
    s->radius = radius; s->age = age; s->fire = fire; s->seed = seed;
}
static int sprite_sort(const void *a, const void *b) {
    const DestructionSprite *sa = a, *sb = b;
    int64_t az = sa->p.z, bz = sb->p.z;
    /* Emissive fire lights the smoke instead of vanishing inside dark puffs. */
    if (sa->fire != sb->fire) return sa->fire ? 1 : -1;
    return az > bz ? -1 : az < bz;
}
static void destruction_disc(const DestructionSprite *s) {
    int cx, cy, r, x, y, left, right, top, bottom, fade;
    uint32_t depth;
    if (s->p.z <= 4 || s->p.z > 200000) return;
    cx = screen_coord(frame->w / 2 + s->p.x * (frame->w / 2) / s->p.z);
    cy = screen_coord(view_h / 2 - s->p.y * (frame->w / 2) / s->p.z);
    r = (int)((int64_t)s->radius * (frame->w / 2) / s->p.z);
    if (r < 1) r = 1;
    if (r > frame->w) r = frame->w;
    left = cx - r < 0 ? 0 : cx - r; right = cx + r >= frame->w ? frame->w - 1 : cx + r;
    top = cy - r < 0 ? 0 : cy - r; bottom = cy + r >= view_h ? view_h - 1 : cy + r;
    depth = (uint32_t)(1073741824LL / s->p.z);
    fade = s->fire ? 0 : s->age * 12 / SMOKE_TICKS;
    for (y = top; y <= bottom; y++) for (x = left; x <= right; x++) {
        int dx = x - cx, dy = y - cy, d = (dx * dx + dy * dy) * 256 / (r * r);
        int grain = ((x / 3 + (int)(s->seed & 31)) ^ (y / 3 + (int)(s->seed >> 8 & 31))) & 15;
        int c, stipple = (x & 3) * 4 + (y & 3);
        if (d > 235 + grain * 2 || depth < depth_buffer[y * frame->w + x]) continue;
        if (s->fire) {
            int heat = d + grain * 3 + s->age * 2;
            c = heat < 40 ? 31 : heat < 90 ? 15 : heat < 160 ? 12 :
                heat < 230 ? 14 : heat < 300 ? 23 : 22;
            if (d > 215 && ((x + y) & 1)) continue;
        } else {
            if (stipple < fade || (d > 180 && ((x + y) & 1))) continue;
            c = d < 70 ? 19 : d < 170 ? 16 : 3;
            if (grain < 4) c = 4;
        }
        frame->pixels[y * frame->w + x] = (uint8_t)c;
    }
}
static void destruction_streak(Vec a, Vec b, int color) {
    Vec ca, cb;
    if (cl_active) { cl_line_world(scale(a, 1, FP), scale(b, 1, FP), color, 0); return; }
    ca = camera(scale(a, 1, FP)); cb = camera(scale(b, 1, FP));
    int x, y, xx, yy, count, i;
    uint32_t depth;
    if (ca.z < 8 || cb.z < 8 || ca.z > 200000) return;
    x = screen_coord(frame->w / 2 + ca.x * (frame->w / 2) / ca.z);
    y = screen_coord(view_h / 2 - ca.y * (frame->w / 2) / ca.z);
    xx = screen_coord(frame->w / 2 + cb.x * (frame->w / 2) / cb.z);
    yy = screen_coord(view_h / 2 - cb.y * (frame->w / 2) / cb.z);
    count = abs(xx - x) > abs(yy - y) ? abs(xx - x) : abs(yy - y);
    if (count < 1) count = 1;
    if (count > frame->w * 2) return;
    depth = (uint32_t)(1073741824LL / ca.z);
    for (i = 0; i <= count; i++) {
        int px = x + (xx - x) * i / count, py = y + (yy - y) * i / count;
        if (px >= 0 && px < frame->w && py >= 0 && py < view_h &&
            depth >= depth_buffer[py * frame->w + px]) pixel(px, py, color);
    }
}
static Vec blast_fragment(const Blast *b, int index, int age) {
    uint32_t h = hash32(b->seed + (unsigned)index * 7919U);
    Vec d = forward(h & 1023, (int)(h >> 16 & 511) - 256);
    Vec p = add(b->pos, scale(d, (int64_t)(350 + h % 650) * FP * age, 16384 * HZ));
    if (b->body >= 0) {
        const Planet *planet = &game->system.planets[b->body];
        p = sub(p, scale(unit(b->pos, 16384), (int64_t)planet->gravity * FP * age * age,
                        2 * HZ * HZ * 16384));
    }
    return destruction_position(game, p, b->body);
}
static void destruction_geometry(void) {
    int i, j;
    for (i = 0; i <= MAX_NPC; i++) {
        const Wreck *w = &game->wrecks[i];
        Vec p, q, front, right, up, r;
        if (w->state != 1) continue;
        q = destruction_position(game, w->pos, w->body);
        p = scale(q, 1, FP);
        front = forward(w->yaw, w->pitch); r = vec(cosi(w->yaw), 0, -sini(w->yaw));
        up = unit(cross(front, r), 16384);
        right = sub(scale(r, cosi(w->roll), 16384), scale(up, sini(w->roll), 16384));
        up = add(scale(r, sini(w->roll), 16384), scale(up, cosi(w->roll), 16384));
        ship_mesh(w->ship, q, right, up, front, 1, unit(sub(game->system.star[0].center, p), 16384));
    }
    for (i = 0; i < MAX_BLASTS; i++) {
        const Blast *b = &game->blasts[i];
        if (!b->active || b->age > 4 * HZ) continue;
        for (j = 0; j < (graphics.particles+1)*6; j++) {
            Vec p = scale(blast_fragment(b, j, b->age), 1, FP);
            Vec d = forward((int)(b->seed + j * 131U + b->age * 11U) & 1023, j * 37 % 320 - 160);
            Vec r = unit(cross(d, vec(0, 16384, 0)), 6 + j % 9);
            d = scale(d, 10 + j % 13, 16384);
            triangle(add(p, d), add(p, r), sub(p, r), b->age < HZ ? 12 : j % 2 ? 17 : 19);
        }
    }
}
static void destruction_effects(void) {
    int i, j;
    sprite_count = 0;
    for (i = 0; i < MAX_SMOKE; i += 4-graphics.particles) {
        const Smoke *s = &game->smoke[i];
        if (s->active) destruction_sprite(s->pos, s->body, s->size + s->age / 2,
                                         s->age, s->age < 5, s->seed);
    }
    for (i = 0; i <= MAX_NPC; i++) {
        const Wreck *w = &game->wrecks[i];
        if (w->state == 1) {
            Vec tail = forward(w->yaw, w->pitch);
            if (w->body >= 0) tail = body_rotate(&game->system.planets[w->body], tail, 1);
            tail = sub(w->pos, scale(tail, ship_specs[w->ship].depth * FP / 2, 16384));
            destruction_sprite(tail, w->body,
                8 + (int)(hash32((unsigned)w->age / 2) % 9), 12, 1, (unsigned)i);
        }
    }
    for (i = 0; i < MAX_BLASTS; i++) {
        const Blast *b = &game->blasts[i];
        Vec center;
        int t = b->age;
        if (!b->active) continue;
        center = destruction_position(game, b->pos, b->body);
        if (t < HZ * 5 / 2) {
            for (j = 0; j < (graphics.particles+1)*14/4; j++) {
                uint32_t h = hash32(b->seed + (unsigned)j * 137U);
                Vec dir = forward(h & 1023, (int)(h >> 16 & 511) - 256);
                int delay = j % 4 * 3, age = t - delay, radius;
                Vec p;
                if (age < 0) continue;
                radius = b->size * (12 + age) / (24 + age / 2);
                if (age > HZ) radius = radius * (HZ * 5 / 2 - age) / (HZ * 3 / 2);
                if (radius < 2) continue;
                p = add(b->pos, scale(dir, (int64_t)b->size * FP * age, 16384 * HZ));
                if (b->body >= 0) p = add(p, scale(unit(b->pos, 16384), (int64_t)age * 20 * FP, HZ * 16384));
                destruction_sprite(p, b->body, radius, age, 1, h);
            }
        }
        if (t < HZ / 3) destruction_sprite(b->pos, b->body,
            b->size * (HZ / 3 - t) / (HZ / 3) + 10, 0, 1, b->seed);
        /* Expanding 3D shock ring, ground-aligned on planets. */
        if (t < HZ * 3 / 2) {
            Vec up = b->body < 0 ? vec(0, 16384, 0) :
                body_rotate(&game->system.planets[b->body], unit(b->pos, 16384), 0);
            Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), front, previous = center;
            int radius = b->size / 2 + t * 12;
            if (length(right) < 100) right = vec(16384, 0, 0);
            front = unit(cross(right, up), 16384);
            for (j = 0; j <= 40; j++) {
                Vec d = add(scale(right, cosi(j * 1024 / 40), 16384), scale(front, sini(j * 1024 / 40), 16384));
                Vec p = add(center, scale(d, radius * FP, 16384));
                if (j) destruction_streak(previous, p, t < HZ / 3 ? 15 : t < HZ ? 12 : 14);
                previous = p;
            }
        }
        if (t < 3 * HZ) for (j = 0; j < (graphics.particles+1)*6; j++) {
            Vec head = blast_fragment(b, j, t), tail = blast_fragment(b, j, t > 3 ? t - 3 : 0);
            destruction_streak(tail, head, t < HZ / 2 ? 31 : t < HZ ? 12 : 14);
        }
    }
    if (cl_active) { for (i = 0; i < sprite_count; i++) cl_sprite(&destruction_sprites[i]); return; }
    qsort(destruction_sprites, (size_t)sprite_count, sizeof(*destruction_sprites), sprite_sort);
    for (i = 0; i < sprite_count; i++) destruction_disc(&destruction_sprites[i]);
}

#include "render_planet.inc"
#include "render_atmosphere.inc"
#include "render_aurora.inc"

#include "render_weapons.inc"

#include "render_ports.inc"

static void npc_hulls(void) {
    int i;
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
}
static void commander_hull(void) {
    Vec p = scale(game->pos, 1, FP), f = forward(game->yaw, game->pitch);
    Vec right = vec(cosi(game->yaw), 0, -sini(game->yaw)), up = unit(cross(f, right), 16384);
    Vec level_right = right;
    right = sub(scale(right, cosi(game->roll), 16384), scale(up, sini(game->roll), 16384));
    up = add(scale(level_right, sini(game->roll), 16384), scale(up, cosi(game->roll), 16384));
    /* On the ground the exact body-fixed axes, not table angles stepping a
       third of a degree as the body turns, keep the hull still on its slope. */
    if (game->docked != -1 && length(game->ground_up)) {
        const Planet *b = &game->system.planets[game->body];
        f = body_rotate(b, game->ground_front, 0);
        up = body_rotate(b, game->ground_up, 0);
        right = cross(up, f);
    }
    ship_mesh(game->ship, game->pos, right, up, f, 1, unit(sub(game->system.star[0].center, p), 16384));
}
/* Ground sites near a low or landed ship: the outcrop, wreck or beacon
   that a commander on foot walks to. */
static void ground_markers(void) {
    GroundSite sites[GROUND_SITES];
    Vec eye = scale(camera_position, 1, FP);
    int i, n;
    if (game->docked >= 0 && !game->on_foot) return;
    if (altitude_at(game, viewer()) > 3000) return;
    n = ground_sites(game, viewer(), sites, GROUND_SITES);
    for (i = 0; i < n; i++) {
        Vec base = scale(site_position(game, &sites[i]), 1, FP);
        Vec up = unit(sub(base, game->system.planets[sites[i].body].center), 16384);
        if (length(sub(base, eye)) > 2500) continue;
        if (sites[i].kind == SITE_OUTCROP) {
            box(base, up, 4, 3, 6);
            box(add(base, scale(up, 3, 16384)), up, 2, 2, 7);
        } else if (sites[i].kind == SITE_WRECKAGE) {
            box(base, up, 9, 2, 22);
            box(add(base, scale(up, 2, 16384)), up, 3, 4, 21);
        } else {
            box(base, up, 1, 7, 23);
            box(add(base, scale(up, 7, 16384)), up, 1, 1, 13);
        }
    }
}
/* Ships, ports, rocks, pods and wrecks: the scene both renderers share.
   The Classic renderer receives the same geometry through its hooks. */
static void world_objects(void) {
    int i;
    /* Directed shots are composed around their hulls: emit them before
       terrain and port scenery can exhaust the triangle budget. */
    if (cinema) {
        npc_hulls();
        if (cinema->hero && !game->dead) commander_hull();
        if (cinema->escort) {
            const IntroHull *h = &cinema->ally;
            Vec p = scale(h->pos, 1, FP);
            ship_mesh(h->ship, h->pos, cross(h->up, h->front), h->up, h->front, 1,
                      unit(sub(game->system.star[0].center, p), 16384));
        }
    }
    scenery_reserve = triangle_capacity / 4;
    for (i = 0; i < port_count(game); i++)
        if (game->ports[i].surface &&
            length(sub(game->ports[i].pos, camera_position)) < 6000LL * FP)
            scenery_reserve = triangle_capacity / 2;
    port_light_count = 0;
    for (i = 0; i < port_count(game); i++) {
        /* Classic: buildings draw after the ground they stand on, and each
           port's later geometry (windows, markings) after its walls. */
        cl_bias = 8 * 16; cl_emit = 0;
        port_geometry(i);
    }
    cl_bias = 0;
    if (!cinema) npc_hulls();
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
    if (!cinema && (game->camera_mode == 2 || game->on_foot) && !game->dead) commander_hull();
    if (!cinema) { cl_bias = 8 * 16; ground_markers(); cl_bias = 0; }
    destruction_geometry();
}
static void world(void) {
    int b;
    world_objects();
    /* Fill the remaining geometry capacity with scenery. Ports, contacts,
       weapons and wrecks must survive an aggressively reduced triangle cap. */
    stellar_discs();
    for (b = 0; b < game->system.count; b++) {
        planet_mesh(b);
        if (game->system.planets[b].rings && !bezier_rings)
            ring_mesh(&game->system.planets[b]);
    }
    forests();
    cloud_deck();
    draw_triangles();
    for (b = 0; b < game->system.count; b++) {
        const Planet *p = &game->system.planets[b];
        int64_t altitude = length(sub(scale(viewer(), 1, FP), p->center)) - p->radius;
        planet_disc(b, b == game->body && altitude < PATCH_ALTITUDE && p->type != GAS_GIANT);
    }
    if (bezier_rings && graphics.rings) {
        nt = 0;
        for (b = 0; b < game->system.count; b++) {
            const Planet *p = &game->system.planets[b];
            if (p->rings && !ring_curves(p)) ring_mesh(p);
        }
        if (nt) draw_triangles(); /* clipped close-up fallback only */
    }
    for (b = 0; b < game->system.count; b++) atmosphere_halo(&game->system.planets[b], 0);
    if (graphics.aurorae) for (b = 0; b < game->system.count; b++)
        aurora_body(&game->system.planets[b], 0);
    if(graphics.lights) port_draw_lights();
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
    case HELP:
        panel("ASTRALBOUND / FLIGHT MANUAL");
        {
            const char *help[] = {
                "ARROWS ROLL PITCH / W S SET SPEED",  "CTRL FIRE / E ASSIST / A DRIVE",
                "L LAUNCH / Y PORTS / T PORT / Q BODY", "D DOCK OR LAND / K EXIT / F SERVICE",
                "[ ] TIME TO 4096X / SPACE PAUSE",          "G MAP / DRAG ROTATE / +/- ZOOM",
                "S SERVICES / B BOARD / M MARKET",  "J CONTRACT LOG / V FLIGHT OR CAMERA",
                "F5 SAVE / F9 LOAD / ESC QUIT DIALOG",         "R CONTACT / Z ORBITS / I BODY DATA",
                "0 EQUIP / 9 CREW / , MISSILE . DECOY", "BACKSLASH ROCK / O MINE N CARGO / / POD",
                "ON FOOT: WASD WALK / ARROWS LOOK / E USE"};
            for (i = 0; i < 13; i++)
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
    ship_mesh(ship, scale(centre, FP, 1), right, vec(0, 16384, 0), f, 16, unit(vec(-7000, 12000, -9000), 16384));
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
#include "render_services.inc"
/* On foot the scope plots the ship and ground sites within 500 m, ahead up. */
static void ground_scan(int top) {
    int cx=160,cy=top+24,i,n;
    GroundSite sites[GROUND_SITES];
    Vec me=walker_position(game), up=unit(sub(scale(me,1,FP),game->system.planets[game->body].center),16384);
    Vec fwd=forward(camera_yaw,camera_pitch), right;
    fwd=unit(sub(fwd,scale(up,dot(fwd,up),16384LL*16384)),16384);
    right=cross(up,fwd);
    line(106,cy,214,cy,3); line(cx,top+11,cx,top+36,3);
    n=ground_sites(game,me,sites,GROUND_SITES);
    for(i=0;i<=n;i++) {
        Vec r=scale(sub(i<n?site_position(game,&sites[i]):game->pos,me),1,FP);
        int64_t x=dot(r,right)/16384, y=dot(r,fwd)/16384;
        if(llabs(x)>500 || llabs(y)>500) continue;
        rect(cx+(int)(x*53/500)-1,cy-(int)(y*12/500)-1,3,3,i<n?12:15);
    }
    rect(cx-1,cy-1,3,3,13); text(124,top+4,7,"SURFACE 500M");
}
static void cockpit_scan(int top) {
    int cx=160,cy=top+24,i,a,px=0,py=0;
    rect(102,top+3,115,36,0);
    if(game->on_foot) { ground_scan(top); return; }
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
    if (game->death_started && game->page == FLIGHT) {
        rect(0,h-32,320,32,1); line(0,h-32,319,h-32,14);
        text(8,h-26,14,game->wrecks[0].state == 1 ? "HULL FAILURE" : "SHIP DESTROYED");
        text(8,h-13,7,game->wrecks[0].state == 1 ? "FALLING - IMPACT IMMINENT" : "R TO RESTART");
    } else if(game->page==FLIGHT) {
        rect(0,top,320,80,16); line(0,top,319,top,20);
        bevel(0,top,100,64,0); bevel(99,top,121,43,0); bevel(220,top,100,45,0);
        cockpit_scan(top);
        rect(3,top+31,94,10,0);
        if(game->on_foot) text(5,top+33,13,"ON FOOT");
        else { int64_t speed=length(game->vel)/FP;
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
        static const char *titles[]={"FLIGHT","GALACTIC NAVIGATION","CONTRACT EXCHANGE","COMMODITY MARKET","SHIP CATALOGUE","CONTRACT LOG","SYSTEM NAVIGATION","FLIGHT MANUAL","SHIP EQUIPMENT","COMMANDER RECORD","STATION SERVICES"};
        rect(0,0,320,h,1);
        if(game->page==HELP || (game->page==SYSTEM_MAP && game->system_display)) {
            /* Existing orbital projection and body data remain truthful at true scale. */
            int old_h=view_h; view_h=h; menus(); view_h=old_h;
            rect(0,h-60,320,60,1);
        }
        rect(0,0,320,20,16); line(0,19,319,19,20);
        if(game->page==MARKET || game->page==SHIPYARD || game->page==EQUIPMENT || game->page==CAREER) {
            int service=game->page==MARKET?1:game->page==SHIPYARD?2:game->page==EQUIPMENT?3:5;
            uint32_t id=service_identity(game,service);
            if(game->docked>=0) {
                contact_portrait(4,2,16,16,id); contact_name(id,buf,sizeof(buf));
                text(24,2,15,buf); text(24,11,13,service_roles[service]);
            } else text(8,7,13,titles[game->page]);
        } else text(8,7,13,titles[game->page]);
        if(game->page==MARKET) text(13,34,7,"ITEM        BUY SELL STOCK HOLD");

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
    if (!game->ui_quit && (game->page==MARKET || game->page==EQUIPMENT || (game->page==SYSTEM_MAP && !game->system_display))) gui_rows();
    if (game->page==SERVICES) service_directory();
    if (game->page==BOARD || game->page==LOG) mission_screen();
    if (game->page==SHIPYARD) shipyard();
    if (game->page==MAP) galaxy_text();
    if(game->page!=FLIGHT && game->page!=MAP) {
        snprintf(buf,sizeof(buf),"%d CR / HOLD %d/%dT",game->credits,cargo_used(game),ship_capacity[game->ship]); text(8,h-28,12,buf);
        /* Status ribbon stays clear of both action buttons and navigation. */
        if(game->page==SYSTEM_MAP && !game->system_display) {
            snprintf(buf,sizeof(buf),"RANGE %ld KM / %s",(long)(length(sub(navigation_position(game),game->pos))/(FP*1000)),game->target<0?"BODY WAYPOINT":"DOCKING PORT");
            text(8,h-62,7,buf);
        }
        snprintf(buf,sizeof(buf),"%.50s",game->message); text(8,h-52,13,buf);
    } else if(game->page==FLIGHT && !game->death_started) {
        draw_scale=1;
        if(game->on_foot) {
          snprintf(buf,sizeof(buf),"SHIP %ld M / RANGE %d M",(long)walker_ship_distance(game),WALK_RANGE); text(7,view_h-23,13,buf);
        } else { int64_t alt=altitude_at(game,game->pos);
          snprintf(buf,sizeof(buf),"ALT %ld %s / %s",(long)(alt>=1000000?alt/1000:alt),alt>=1000000?"KM":"M",game->drive_manual?(game->drive_active?(game->autopilot?"AUTO DRIVE":"DRIVE"):(game->autopilot?"AUTO DRIVE LOCK":"DRIVE LOCK")):(game->landing?"LANDING":game->autopilot?"AUTO":game->drive_active?"BRAKING":"MANUAL")); text(7,view_h-23,13,buf); }
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
    free(triangles); triangles=NULL; triangle_capacity=triangle_limit=0;
    free(triangle_order); triangle_order=NULL;
    free(triangle_keys); triangle_keys=NULL;
    free(scaled_pixels); scaled_pixels=NULL; scaled_capacity=0;
    free(depth_buffer);
    depth_buffer = 0;
    depth_capacity = 0;
    terrain_surface = NULL;
    memset(surface_cache, 0, sizeof(surface_cache));
}
void render_palette(Frame *f) {
    int i;
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
}
int render_init(Frame *f) {
    size_t count;
    uint32_t *buffer;
    if (f->w <= 0 || f->h <= 56)
        return 0;
    count = (size_t)f->w * f->h;
    buffer = realloc(depth_buffer, count * sizeof(*buffer));
    if (!buffer)
        return 0;
    depth_buffer = buffer;
    depth_capacity = count;
    {
        size_t capacity=(size_t)512 << graphics.triangles;
        Tri *list=realloc(triangles,capacity*sizeof(Tri));
        int *order;
        uint32_t *keys;
        uint8_t *pixels;
        if(!list) return 0;
        triangles=list;
        order=realloc(triangle_order,capacity*2*sizeof(*order));
        if(!order) return 0;
        triangle_order=order;
        keys=realloc(triangle_keys,capacity*sizeof(*keys));
        if(!keys) return 0;
        triangle_keys=keys; triangle_limit=triangle_capacity=(int)capacity;
        pixels=realloc(scaled_pixels,count);
        if(!pixels) return 0;
        scaled_pixels=pixels; scaled_capacity=count;
    }
    if (!tables_ready)
        plane_tables();
    render_palette(f);
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
    if(performance_camera) return;
    int w = frame->w;
    if (blink) text(w / 2 - (int)strlen(title) * 3, 12, 15, title);
    if (detail) text(w / 2 - (int)strlen(detail) * 3, 22, 12, detail);
}
static void hyperspace_charge_labels(int t) {
    int cx=frame->w/2,p=t*256/HYPER_CHARGE;
    hyper_title("HYPERSPACE CHARGING",NULL,1);
    rect(cx-50,22,100,5,0);
    rect(cx-49,23,p*98/256,3,p>208 ? 31 : 13);
}
static void hyperspace_tunnel_labels(const Game *g,int t) {
    char buf[40];
    snprintf(buf,sizeof(buf),"DESTINATION %.24s",g->system.name);
    hyper_title("HYPERSPACE",buf,(t/6)%4!=3);
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
    if(!scene_only) hyperspace_charge_labels(t);
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
    if(!scene_only) hyperspace_tunnel_labels(g,t);
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
#define SKY_COLUMNS 40
#define SKY_ROWS 128
static struct {
    int scatter, opacity, glow, band, daylight;
    Vec up, sun; /* world Q14 */
} sky_state;
static uint16_t sky_ray[SKY_ROWS][SKY_COLUMNS];
static int sky_ray_w, sky_ray_h;
static int sky_intensity(int y) {
    return sky_state.scatter * (view_h + y) / (2 * view_h);
}
/* Strongest at and below the horizon towards the star, a faint ring all round. */
static int sky_glow(int elevation, int sun_cosine) {
    int v, a;
    if (!sky_state.glow || elevation >= sky_state.band) return 0;
    v = elevation <= 0 ? 256 : 256 * (sky_state.band - elevation) / sky_state.band;
    a = (sun_cosine + 16384) >> 7;
    a = 32 + 224 * a * a / 65536;
    return sky_state.glow * (v * v >> 8) * a >> 16;
}
/* Diffuse daylight 0..31 that drowns stars wherever the view looks: it rises
   from about 11 degrees below the horizon, an eclipse cuts it back to the
   twilight level and thinner air scatters proportionally less of it. */
static int sky_daylight(const Planet *p, Vec local) {
    int i, elevation = -16384, density = atmosphere_density(p, length(local) - p->radius), light;
    for (i = 0; i < game->system.stars; i++) {
        const Star *s = &game->system.star[i];
        int e;
        if (s->type == ST_BLACK_HOLE) continue;
        e = (int)(dot(sky_state.up, unit(sub(s->center, add(p->center, local)), 16384)) / 16384);
        if (e > elevation) elevation = e;
    }
    if (elevation > 0) elevation = surface_light(&game->system, p, local);
    if (!density || elevation <= -3200) return 0;
    light = (elevation + 3200) * 32 / 8000;
    if (light > 31) light = 31;
    return density >= 32 ? light : light * density / 32;
}
static int sky_row(int tint, int y) {
    int intensity = sky_intensity(y);
    return !intensity ? 0 : frame->enhanced ? sky_tint[tint].day + intensity : ecs_sky[tint][1 + intensity * 6 / 31];
}
static void sky(void) {
    const Planet *p = &game->system.planets[game->body];
    Planet haze_air;
    const Planet *air = p;
    Vec local = sub(scale(viewer(), 1, FP), p->center), cu, cs, eye;
    int tint = atmosphere_sky_tint(p, length(local) - p->radius), w = frame->w, x, y, bw = w / SKY_COLUMNS, bh = bw / 2, cx, cy;
    int columns = bw ? (w + bw - 1) / bw : 0, rows = bh ? (view_h + bh - 1) / bh : 0;
    /* Dense organic smog (Titan and generated N2/CH4 worlds) has a large
       optical depth even though its weather category is named FROZEN.
       Keep this optical column separate from the flight-density model. */
    if (atmosphere_haze(p) == HAZE_ORGANIC) {
        haze_air = *p; haze_air.climate = DENSE; air = &haze_air;
    }
    sky_state.scatter = rayleigh_scattering(&game->system, air, local, forward(camera_yaw, camera_pitch));
    sky_state.opacity = sky_opacity(air, length(local) - p->radius);
    sky_state.up = unit(local, 16384);
    sky_state.glow = twilight(&game->system, air, local, &sky_state.sun);
    sky_state.band = sky_tint[tint].band;
    sky_state.daylight = sky_daylight(air, local);
    if (!graphics.sky || bw < 4 || bh > 16 || columns > SKY_COLUMNS || rows > SKY_ROWS) sky_state.glow = 0;
    if (!sky_state.glow) {
        for (y = 0; y < view_h; y++)
            memset(frame->pixels + y * w,sky_row(tint,y),(size_t)w);
        return;
    }
    /* Blocks bound the cost on 68k: one elevation and sun angle each from the
       camera-space ray, with ray lengths cached per frame size. */
    if (sky_ray_w != w || sky_ray_h != view_h) {
        for (cy = 0; cy < rows; cy++)
            for (cx = 0; cx < columns; cx++) {
                int64_t rx = cx * bw + bw / 2 - w / 2, ry = view_h / 2 - (cy * bh + bh / 2), rz = w / 2;
                sky_ray[cy][cx] = (uint16_t)isqrt64((uint64_t)(rx * rx + ry * ry + rz * rz));
            }
        sky_ray_w = w;
        sky_ray_h = view_h;
    }
    eye = scale(camera_position, 1, FP);
    cu = camera(add(eye, sky_state.up));
    cs = camera(add(eye, sky_state.sun));
    /* Q14 units and screen offsets both fit 16 bits: native muls.w on 68000. */
    int16_t ux = (int16_t)cu.x, uy = (int16_t)cu.y, sx = (int16_t)cs.x, sy = (int16_t)cs.y;
    int up_z = w / 2 * (int)cu.z, sun_z = w / 2 * (int)cs.z;
    for (cy = 0; cy < rows; cy++) {
        int16_t ry = (int16_t)(view_h / 2 - (cy * bh + bh / 2));
        int up_y = ry * uy + up_z, sun_y = ry * sy + sun_z, top = cy * bh;
        uint8_t base[16];
        for (y = top; y < top + bh && y < view_h; y++) base[y-top]=(uint8_t)sky_row(tint,y);
        for (cx = 0; cx < columns; cx++) {
            int16_t rx = (int16_t)(cx * bw + bw / 2 - w / 2);
            int len = sky_ray[cy][cx], left = cx * bw;
            int elevation = (rx * ux + up_y) / len, sun_cosine = (rx * sx + sun_y) / len;
            int glow = sky_glow(elevation, sun_cosine), mix = glow * 17 >> 8;
            int n = w - left < bw ? w - left : bw;
            int colour = frame->enhanced
                             ? sky_tint[tint].glow + sky_tint[tint].steps * (4 + glow * 27 / 256) / 32
                             : ecs_glow[sky_tint[tint].warm][glow * 4 / 257];
            for (y = top; y < top + bh && y < view_h; y++) {
                uint8_t pattern[4], *out = frame->pixels + y * w + left;
                for (x = 0; x < 4; x++)
                    pattern[x] = bayer4[(y & 3) * 4 + ((left + x) & 3)] < mix ? (uint8_t)colour : base[y - top];
                for (x = 0; x + 4 <= n; x += 4) memcpy(out + x, pattern, 4);
                for (; x < n; x++) out[x] = pattern[x & 3];
            }
        }
    }
}
/* A star shows where the sky is darker than it is: the bright fifth survive
   early twilight, and the air column thickens towards the horizon. */
static int star_visible(Vec d, int y, int bright) {
    int elevation = (int)(dot(d, sky_state.up) / 16384), glare = sky_intensity(y);
    if (sky_state.opacity) {
        if (elevation <= 0) return 0;
        if (sky_state.opacity * 16384 / (elevation < 2048 ? 2048 : elevation) > (bright ? 640 : 256))
            return 0;
    }
    if (glare < sky_state.daylight) glare = sky_state.daylight;
    glare += sky_glow(elevation, (int)(dot(d, sky_state.sun) / 16384)) / 16;
    return glare < (bright ? 6 : 2);
}
static const char *suit_status(const Planet *p) {
    return p->climate == TEMPERATE ? "BREATHABLE AIR" :
           p->climate == VACUUM ? "VACUUM - SUIT SEALED" : "TOXIC - SUIT SEALED";
}
/* Walking: markers for the parked ship and the nearest ground site. */
static void foot_hud(void) {
    GroundSite site;
    int64_t metres;
    char s[48];
    int x, y;
    target_marker_position(camera_q8(game->pos), 30, &x, &y);
    line(x - 6, y - 4, x + 6, y - 4, 15); line(x - 6, y + 4, x + 6, y + 4, 15);
    line(x - 6, y - 4, x - 6, y + 4, 15); line(x + 6, y - 4, x + 6, y + 4, 15);
    snprintf(s, sizeof(s), "SHIP %ld M", (long)walker_ship_distance(game));
    text(x - 24 < 2 ? 2 : x - 24, y + 8, 15, s);
    if (nearest_site(game, &site, &metres)) {
        target_marker_position(camera_q8(site_position(game, &site)), 30, &x, &y);
        line(x - 6, y, x, y - 6, 12); line(x, y - 6, x + 6, y, 12);
        line(x + 6, y, x, y + 6, 12); line(x, y + 6, x - 6, y, 12);
        snprintf(s, sizeof(s), "%s %ld M", site_names[site.kind], (long)metres);
        x -= (int)strlen(s) * 3;
        if (x + (int)strlen(s) * 6 > frame->w - 2) x = frame->w - 2 - (int)strlen(s) * 6;
        text(x < 2 ? 2 : x, y + 9, 12, s);
    }
    text(7, 8, 13, game->system.planets[game->body].name);
    text(7, 20, 7, suit_status(&game->system.planets[game->body]));
    if (nearest_site(game, &site, &metres) && metres <= SITE_REACH) {
        snprintf(s, sizeof(s), "E USE %s", site_names[site.kind]);
        text(7, 32, 12, s);
    }
}
/* Flight labels and instruments belong to output pixels, independent of the
   3D render scale. Camera attitude/position survives the low-resolution pass. */
static void flight_overlays(const Game *g, Frame *f) {
    if (g->hyperspace > HYPER_CHARGE && g->hyperspace <= HYPER_CHARGE + HYPER_TUNNEL) return;
    if (graphics.labels && !g->dead) system_body_labels();
    if (weather_status & 1) text(8,32,15,"IN CLOUD");
    if (weather_status & 6) text(8,44,15,weather_status & 2 ? "RAIN" : "SNOW");
    if (g->page == FLIGHT && !g->dead && g->on_foot) {
        foot_hud();
    } else if (g->page == FLIGHT && !g->dead) {
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
        sensors();
    }
}
#include "render_classic.inc"
#include "render_classic_planet.inc"
static void render_game_full(const Game *g, Frame *f) {
    int i;
    frame = f;
    game = g;
    weather_status = 0;
    if (view_matrix.valid == 2) view_matrix.valid = 0;
    if (g->page != FLIGHT) {
        view_h=f->h;
        gui();
        return;
    }
    camera_position = g->pos;
    view_angles(g, &camera_yaw, &camera_pitch);
    camera_roll = g->camera_mode == 0 ? g->roll :
                  g->camera_mode == 1 ? (-g->roll & 1023) : 0;
    fine_camera = 0; near_plane = 8; camera_unit = 1;
    if (cinema) {
        camera_position = cinema->eye;
        camera_yaw = cinema->yaw; camera_pitch = cinema->pitch; camera_roll = cinema->roll;
    } else if (g->death_started) {
        const Wreck *w = &g->wrecks[0];
        Vec up = w->body < 0 ? vec(0, 16384, 0) :
            unit(sub(g->pos, scale(g->system.planets[w->body].center, FP, 1)), 16384);
        Vec side = vec(cosi(g->death_yaw + 110), 0, -sini(g->death_yaw + 110));
        Vec tangent = sub(side, scale(up, dot(side, up), 16384LL * 16384));
        int distance = 220 + ship_specs[w->ship].width * 2;
        int lift = 110;
        if (w->state == 2) {
            distance += w->age < 2 * HZ ? w->age * 7 : HZ * 14;
            lift += w->age < HZ ? w->age * 3 : HZ * 3;
        }
        if (length(tangent) < 2048) tangent = cross(up, vec(0, 0, 16384));
        if (length(tangent) < 2048) tangent = cross(up, vec(16384, 0, 0));
        tangent = unit(tangent, 16384);
        camera_position = add(g->pos, add(scale(tangent, distance * FP, 16384), scale(up, lift * FP, 16384)));
        /* A hillside must never bury the death camera. */
        if (w->body >= 0) {
            const Planet *p = &g->system.planets[w->body];
            Vec local = sub(camera_position, scale(p->center, FP, 1));
            int64_t floor = ((int64_t)p->radius + terrain_height(p, scale(local, 1, FP)) + 70) * FP;
            if (length(local) < floor) camera_position = add(scale(p->center, FP, 1), radial(local, floor));
        }
        aim_angles(sub(g->pos, camera_position), &camera_yaw, &camera_pitch);
        camera_roll = 0;
    } else if (g->on_foot) {
        Vec axes[3];
        ground_view(g, &camera_position, axes, &camera_yaw, &camera_pitch, &camera_roll);
        view_basis(axes);
        fine_camera = 1; near_plane = 1; camera_unit = 16;
    } else if (g->camera_mode == 2) {
        Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
        camera_position = add(sub(g->pos, scale(forward(g->yaw, g->pitch), 600 * FP, 16384)),
                              scale(up, 220 * FP, 16384));
        aim_angles(sub(g->pos, camera_position), &camera_yaw, &camera_pitch);
    }
    view_h = cinema ? f->h : scene_only ? scaled_view_h : ui_view_height(g, f);
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
        if (!cinema && !scene_only) gui();
        return;
    }
    sky();
    {
        int stretch = hyper_stretch(g);
        for (i = 0; i < (graphics.stars == 4 ? 480 : graphics.stars == 3 ? 120 : graphics.stars == 2 ? 60 : graphics.stars ? 30 : 0); i++) {
            uint32_t h = hash32((unsigned)i + g->seed);
            Vec d = forward(h % 1024, (int)((h >> 16) % 512) - 256);
            Vec v = camera(add(scale(viewer(), 1, FP), scale(d, 1000000, 16384)));
            if (v.z > 100) {
                int x = f->w / 2 + (int)(v.x * (f->w / 2) / v.z),
                    y = view_h / 2 - (int)(v.y * (f->w / 2) / v.z);
                if (!star_visible(d, y, i % 5 == 0))
                    continue;
                if (stretch)
                    line(x, y, f->w / 2 + (x - f->w / 2) * (256 + stretch) / 256,
                         view_h / 2 + (y - view_h / 2) * (256 + stretch) / 256, i % 5 ? 20 : 31);
                else if (y < view_h)
                    pixel(x, y, i % 5 ? 7 : 15);
            }
        }
    }
    if (g->page == FLIGHT) {
        if (classic_mode) {
            classic_world();
            if (graphics.weather) cl_weather();
        } else {
            world();
            if(graphics.weather) weather_overlay();
            destruction_effects();
            if(!cinema || performance_camera) weapon_effects();
        }
    }
    if (!cinema && !scene_only) flight_overlays(g,f);
    if (g->hyperspace && g->hyperspace <= HYPER_CHARGE)
        hyperspace_charge(g->hyperspace - 1);
    else if (g->hyperspace > HYPER_CHARGE + HYPER_TUNNEL)
        hyperspace_exit(g->hyperspace - HYPER_CHARGE - HYPER_TUNNEL - 1);
    if (!cinema && !scene_only) gui();
}
/* Scale only the 3D view; the console and menus retain output resolution. */
static void enlarge(const Frame *source, Frame *destination, int height) {
    int x,y,k,factor=graphics.scale+1;
    for(y=0;y<height;y+=factor) {
        uint8_t *out=destination->pixels+(size_t)y*destination->w;
        const uint8_t *in=source->pixels+(size_t)(y/factor)*source->w;
        /* Doubling, the common case: on the big-endian 68k two source
           pixels a, b become one longword store a a b b. */
        if(factor==2 && !(destination->w&3) && !((uintptr_t)out&1)) {
            int n=destination->w/4;
#if defined(__mc68000__)
            c2p_long *o=(c2p_long *)out;
            while(n--) { uint32_t a=in[0], b=in[1]; in+=2; *o++=(a*0x0101u)<<16|b*0x0101u; }
#else
            uint8_t *o=out;
            while(n--) { o[0]=o[1]=in[0]; o[2]=o[3]=in[1]; o+=4; in+=2; }
#endif
        } else for(x=0;x<destination->w;x+=factor) {
            uint8_t c=*in++;
            for(k=0;k<factor && x+k<destination->w;k++) out[x+k]=c;
        }
        for(k=1;k<factor && y+k<height;k++) memcpy(out+(size_t)k*destination->w,out,(size_t)destination->w);
        if(platform_pump) platform_pump();
    }
}
void render_graphics(const Graphics *g) {
    graphics=*g; graphics.scale=0; graphics_configured=1;
    memset(surface_cache,0,sizeof(surface_cache)); lod_altitude=-1;
}
void render_game(const Game *g, Frame *f) {
    int factor=graphics.scale+1, height=ui_view_height(g,f);
    if(factor>1 && g->page==FLIGHT && !cinema && scaled_capacity>=(size_t)f->w*f->h) {
        Frame low=*f;
        low.w=(f->w+factor-1)/factor; low.h=(height+factor-1)/factor;
        low.pixels=scaled_pixels;
        scene_only=1; scaled_view_h=low.h;
        render_game_full(g,&low);
        scene_only=0;
        enlarge(&low,f,height);
        frame=f; game=g; view_h=height; draw_scale=1;
        flight_overlays(g,f);
        if(g->hyperspace && g->hyperspace<=HYPER_CHARGE) hyperspace_charge_labels(g->hyperspace-1);
        else if(g->hyperspace>HYPER_CHARGE && g->hyperspace<=HYPER_CHARGE+HYPER_TUNNEL)
            hyperspace_tunnel_labels(g,g->hyperspace-HYPER_CHARGE-1);
        gui();
    } else render_game_full(g,f);
}
/* Planes are contiguous and MSB-first, independent of host byte order. */
static void c2p_bytes(const uint8_t *src, uint8_t *dst, int width, int height, int depth) {
    int y, x, b, p, stride = width / 8, plane = stride * height;
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

/* The merge C2P of 1990s Amiga demos and games: 32 chunky pixels in eight
   longwords become eight plane longwords through five exchange passes, each
   swapping one register-index bit with one bit-position bit (shifts 16, 8,
   4, 2, 1). About 15 cycles a pixel on a 68030, against roughly 170 for
   per-pixel tables. Group r of four pixels loads into register 7 - r; plane
   p ends in register 4 p1 + 2 p0 + p2. */
#if defined(__mc68000__)
#define C2P_LOAD(s) (*(const c2p_long *)(s))
#define C2P_STORE(d, v) (*(c2p_long *)(d) = (v))
#else
#define C2P_LOAD(s) ((uint32_t)(s)[0] << 24 | (uint32_t)(s)[1] << 16 | (uint32_t)(s)[2] << 8 | (s)[3])
#define C2P_STORE(d, v) ((d)[0] = (uint8_t)((v) >> 24), (d)[1] = (uint8_t)((v) >> 16), \
                         (d)[2] = (uint8_t)((v) >> 8), (d)[3] = (uint8_t)(v))
#endif
#define C2P_MERGE(a, b, mask, shift) \
    do { uint32_t t = (((a) >> (shift)) ^ (b)) & (mask); (b) ^= t; (a) ^= t << (shift); } while (0)
void c2p(const uint8_t *src, uint8_t *dst, int width, int height, int depth) {
    static const uint8_t slot[8] = {0, 2, 4, 6, 1, 3, 5, 7};
    int y, x, p, stride = width / 8, plane = stride * height;
    if (width <= 0 || (width & 7) || height <= 0 || depth < 1 || depth > 8)
        return;
    if ((width & 31) || (((uintptr_t)src | (uintptr_t)dst) & 1)) {
        c2p_bytes(src, dst, width, height, depth);
        return;
    }
    for (y = 0; y < height; y++) {
        const uint8_t *in = src + (size_t)y * width;
        uint8_t *out = dst + (size_t)y * stride;
        if (platform_pump && y % 16 == 0)
            platform_pump();
        for (x = 0; x < width; x += 32, in += 32, out += 4) {
            uint32_t r[8];
            r[7] = C2P_LOAD(in); r[6] = C2P_LOAD(in + 4); r[5] = C2P_LOAD(in + 8); r[4] = C2P_LOAD(in + 12);
            r[3] = C2P_LOAD(in + 16); r[2] = C2P_LOAD(in + 20); r[1] = C2P_LOAD(in + 24); r[0] = C2P_LOAD(in + 28);
            C2P_MERGE(r[0], r[4], 0x0000ffffU, 16); C2P_MERGE(r[1], r[5], 0x0000ffffU, 16);
            C2P_MERGE(r[2], r[6], 0x0000ffffU, 16); C2P_MERGE(r[3], r[7], 0x0000ffffU, 16);
            C2P_MERGE(r[0], r[2], 0x00ff00ffU, 8); C2P_MERGE(r[1], r[3], 0x00ff00ffU, 8);
            C2P_MERGE(r[4], r[6], 0x00ff00ffU, 8); C2P_MERGE(r[5], r[7], 0x00ff00ffU, 8);
            C2P_MERGE(r[0], r[1], 0x0f0f0f0fU, 4); C2P_MERGE(r[2], r[3], 0x0f0f0f0fU, 4);
            C2P_MERGE(r[4], r[5], 0x0f0f0f0fU, 4); C2P_MERGE(r[6], r[7], 0x0f0f0f0fU, 4);
            C2P_MERGE(r[0], r[4], 0x33333333U, 2); C2P_MERGE(r[1], r[5], 0x33333333U, 2);
            C2P_MERGE(r[2], r[6], 0x33333333U, 2); C2P_MERGE(r[3], r[7], 0x33333333U, 2);
            C2P_MERGE(r[0], r[2], 0x55555555U, 1); C2P_MERGE(r[1], r[3], 0x55555555U, 1);
            C2P_MERGE(r[4], r[6], 0x55555555U, 1); C2P_MERGE(r[5], r[7], 0x55555555U, 1);
            for (p = 0; p < depth; p++) C2P_STORE(out + (size_t)p * plane, r[slot[p]]);
        }
    }
}

#include "render_intro.inc"

/* Launch UI is deliberately a flat 320x200 page, with no animated backdrop. */
void render_launcher(const Launcher *m, Frame *f) {
    int i, rows=launcher_rows(m); char name[64],value[48],buf[96];
    frame=f; draw_scale=ui_scale(f); view_h=f->h;
    memset(f->pixels,0,(size_t)f->w*f->h);
    text(8,7,15,"ASTRALBOUND");
    text(8,21,13,m->page==1 ? "GRAPHICS OPTIONS" : m->page==2 ? "PERFORMANCE RESULTS" : "LAUNCH OPTIONS");
    if(m->page==1) snprintf(buf,sizeof(buf),"PAGE %d OF %d - CHANGES SELECT CUSTOM",m->group+1,GRAPHICS_MENU_PAGES);
    else if(m->page==2) snprintf(buf,sizeof(buf),"%.14s %s %dX%d %s",video_names[m->latest.settings.mode],quality_names[m->latest.settings.preset],m->latest.width,m->latest.height,m->latest.complete ? "DONE" : "PARTIAL");
    else snprintf(buf,sizeof(buf),"CONFIGURE - TEST - START");
    text(8,34,7,buf);
    for(i=0;i<rows;i++) {
        int enabled=launcher_graphics_available(m,i);
        launcher_row(m,i,name,sizeof(name),value,sizeof(value));
        /* LAUNCHER_ROW_TOP/PITCH keeps every page clear of the message. */
        if(i==m->selected) rect(6,LAUNCHER_ROW_TOP-1+i*LAUNCHER_ROW_PITCH,308,LAUNCHER_ROW_PITCH,3);
        text(9,LAUNCHER_ROW_TOP+1+i*LAUNCHER_ROW_PITCH,enabled ? (i==m->selected ? 15 : 7) : 6,name);
        if(value[0]) text(310-(int)strlen(value)*6,LAUNCHER_ROW_TOP+1+i*LAUNCHER_ROW_PITCH,enabled ? 13 : 6,value);
    }
    if(m->page==1 && m->selected<launcher_graphics_rows(m)) {
        const char *hint=launcher_graphics_hint(m);
        /* Wrap once at a word boundary for the compact native menu. */
        int n=(int)strlen(hint),cut=n>49 ? 49 : n;
        while(cut>0 && cut<n && hint[cut]!=' ') cut--;
        snprintf(buf,sizeof(buf),"%.*s",cut,hint); text(8,158,12,buf);
        if(cut<n) text(8,168,12,hint+cut+1);
    }
    if(m->page==2) {
        text(8,86,13,"SCENE                FPS    PREV    MAX MS");
        for(i=m->group*6;i<PERFORMANCE_SCENES && i<m->group*6+6;i++) {
            const PerformanceResult *r=&m->latest.result[i],*old=&m->previous.result[i];
            unsigned fps=r->total_us ? (unsigned)((uint64_t)r->frames*10000000/r->total_us) : 0;
            unsigned prev=old->total_us ? (unsigned)((uint64_t)old->frames*10000000/old->total_us) : 0;
            if(!r->frames) snprintf(buf,sizeof(buf),"%-20.20s --",performance_names[i]);
            else snprintf(buf,sizeof(buf),"%-20.20s %3u.%u %3u.%u %5lu",performance_names[i],fps/10,fps%10,prev/10,prev%10,(unsigned long)(r->slowest_us/1000));
            text(8,99+(i%6)*11,7,buf);
        }
        text(8,170,12,"FPS INCLUDES OUTPUT. PREV IS LAST RUN.");
        if(m->previous.width) { snprintf(buf,sizeof(buf),"PREV %.14s %s %dX%d",video_names[m->previous.settings.mode],quality_names[m->previous.settings.preset],m->previous.width,m->previous.height); text(8,180,7,buf); }
    }
    if(m->message[0]) { rect(6,175,308,10,0); text(8,176,12,m->message); }
    text(8,188,7,m->page ? "ARROWS CHANGE - RETURN SELECT - ESC BACK" : "ARROWS CHANGE - RETURN SELECT - ESC QUIT");
    draw_scale=1;
}
void render_performance(const Performance *p, Frame *f) {
    char buf[96]; const PerformanceResult *r=&p->report.result[p->scene];
    unsigned fps=r->total_us ? (unsigned)((uint64_t)r->frames*10000000/r->total_us) : 0;
    int scale=ui_scale(f),width=f->w/scale;
    const char *name=performance_names[p->scene];
    frame=f; draw_scale=scale;
    rect(0,0,width,32,1);
    /* A double-size, high-contrast name stays clear of the flight horizon.
       All statistics sit on a separate line, on every display backend. */
    draw_scale=scale*2;
    text((f->w/draw_scale-(int)strlen(name)*6)/2,2,15,name);
    draw_scale=scale;
    snprintf(buf,sizeof(buf),"%d/%d %u.%u FPS R%luMS OUT%luMS ESC",p->scene+1,PERFORMANCE_SCENES,fps/10,fps%10,
             (unsigned long)(r->frames ? r->render_us/r->frames/1000 : 0),
             (unsigned long)(r->frames ? r->output_us/r->frames/1000 : 0)); text(6,22,13,buf);
    rect(0,30,width,2,3);
    rect(0,30,(int)((r->total_us>PERFORMANCE_MIN_US ? PERFORMANCE_MIN_US : r->total_us)*width/PERFORMANCE_MIN_US),2,13);
    draw_scale=1;
}

void render_performance_scene(const Game *g, Frame *f) {
    if(g->page==FLIGHT) {
        /* Reuse the full-frame cinematic camera without scripted actors,
           captions, letterboxing or a cockpit. It also suppresses world,
           target and weather labels at every internal graphics resolution. */
        Intro view={0};
        view.eye=g->pos; view.yaw=g->yaw; view.pitch=g->pitch; view.roll=g->roll;
        performance_camera=1;
        render_intro(&view,g,f);
        performance_camera=0;
    } else render_game(g,f);
}
