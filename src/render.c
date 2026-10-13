/* SPDX-License-Identifier: Unlicense */
#include "space.h"
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
} Tri;
static Tri triangles[TRI_MAX];
static int nt;
static const Planet *terrain_surface;
static int surface_color(const Planet *p, int face, int u, int v);
/* Reciprocal depth is linear in screen space. Keep the buffer in ordinary
   RAM; only the final indexed image is copied to Amiga display memory. */
static uint32_t *depth_buffer;
static size_t depth_capacity;
static Frame *frame;
static const Game *game;
static int view_h;
static Vec camera_position;
static int camera_yaw, camera_pitch;
/* Q4 coordinates retain fractional ray hits between fixed mesh vertices. */
static Vec camera_to_local(const Planet *p, Vec v) {
    int sy = sini(camera_yaw), cy = cosi(camera_yaw);
    int sp = sini(camera_pitch), cp = cosi(camera_pitch);
    int64_t y = (v.y * cp + v.z * sp) / 16384;
    int64_t z = (v.z * cp - v.y * sp) / 16384;
    Vec world = vec((v.x * cy + z * sy) / 16384, y, (z * cy - v.x * sy) / 16384);
    return body_rotate(p, add(world, sub(scale(camera_position, 1, FP / 16), scale(p->center, 16, 1))), 1);
}

static void (*platform_pump)(void);
static uint32_t plane_low[256], plane_high[256];
static int tables_ready;
static Vec globe_vertices[MAX_PLANETS][17][33];

static uint32_t globe_seed, globe_hour;
static int globe_ready, globe_enhanced;

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
    if (x >= 0 && y >= 0 && x < frame->w && y < frame->h)
        frame->pixels[y * frame->w + x] = (uint8_t)c;
}
static void rect(int x, int y, int w, int h, int c) {
    int yy;
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
    return vec(x, (r.y * cp - z * sp) / 16384, (r.y * sp + z * cp) / 16384);
}
static int screen_coord(int64_t value) {
    return value < -32760 ? -32760 : value > 32760 ? 32760 : (int)value;
}
static void emit(Vec a, Vec b, Vec c, int color) {
    Tri *t;
    Vec v[3] = {a, b, c};
    int i;
    if (nt >= TRI_MAX)
        return;
    t = &triangles[nt];
    t->surface = terrain_surface;
    for (i = 0; i < 3; i++) {
        t->inverse_z[i] = (uint32_t)(1073741824LL / v[i].z);
        t->x[i] = screen_coord(frame->w / 2 + v[i].x * (frame->w / 2) / v[i].z);
        t->y[i] = screen_coord(view_h / 2 - v[i].y * (frame->w / 2) / v[i].z);
        if (t->surface) {
            t->surface_depth[i] = 274877906944LL / v[i].z;
            t->surface_x[i] = frame->w * 128 + (int)(v[i].x * (frame->w * 128) / v[i].z);
            t->surface_y[i] = (view_h / 2) * 256 - (int)(v[i].y * (frame->w * 128) / v[i].z);
        }
    }
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
    for (i = 1; i + 1 < count; i++)
        emit(vertices[current][0], vertices[current][i], vertices[current][i + 1], color);
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
static void surface_ray_uv(const Planet *p, int x, int y, int64_t depth,
                           int *face, int *u, int *v) {
    int64_t z = depth > 0 ? 4398046511104LL / depth : 1;
    Vec point = vec((x - frame->w / 2) * z / (frame->w / 2),
                    (view_h / 2 - y) * z / (frame->w / 2), z);
    Vec local = camera_to_local(p, point);
    int64_t value[3] = {local.x, local.y, local.z};
    surface_uv(p, value, face, u, v);
}
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
    if (miny < 0)
        miny = 0;
    if (maxy >= view_h)
        maxy = view_h - 1;
    for (y = miny; y <= maxy; y++) {
        int xs[3], n = 0, j;
        for (i = 0; i < 3; i++) {
            j = (i + 1) % 3;
            if ((t->y[i] <= y && t->y[j] > y) || (t->y[j] <= y && t->y[i] > y)) {
                xs[n] = t->x[i] +
                        (int)((int64_t)(y - t->y[i]) * (t->x[j] - t->x[i]) / (t->y[j] - t->y[i]));
                n++;
            }
        }
        if (n >= 2) {
            int l = xs[0] < xs[1] ? xs[0] : xs[1], r = xs[0] > xs[1] ? xs[0] : xs[1];
            int64_t z;
            if (l < 0)
                l = 0;
            /* Evaluate the triangle's depth plane at the actual pixel, not
               at a rounded scanline edge. This keeps shared faces coherent. */
            z = ((int64_t)t->inverse_z[0] +
                (zx * (l - t->x[0]) + zy * (y - t->y[0])) / area) * 256;
            if (t->surface)
                z = t->surface_depth[0] +
                    (depth_x * (l * 256 - t->surface_x[0]) +
                     depth_y * (y * 256 - t->surface_y[0])) / 256;
            if (r >= frame->w)
                r = frame->w - 1;
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
                        int visible = 0;
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
                        for (x = block; x <= stop; x++, pixels++, depths++, z += dz) {
                            int face = face0, u = u0, v = v0;
                            if (z < 0 || (uint32_t)(z >> 8) < *depths) continue;
                            if (face0 != face1) {
                                surface_ray_uv(t->surface, x, y, z, &face, &u, &v);
                            } else if (stop > block) {
                                u += (int)((int64_t)(u1 - u0) * (x - block) / (stop - block));
                                v += (int)((int64_t)(v1 - v0) * (x - block) / (stop - block));
                            }
                            *depths = (uint32_t)(z >> 8);
                            *pixels = (uint8_t)surface_color(t->surface, face, u, v);
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
static struct {
    int u, v, face, valid, material, color;
    uint32_t epoch;
} surface_cache[SURFACE_CACHE_SIZE];
static uint32_t surface_seed, surface_system;
static int surface_enhanced;
static int surface_color(const Planet *p, int face, int u, int v) {
    unsigned slot;
    int tile_u = u >= 0 ? u / SURFACE_TILE : (u - SURFACE_TILE + 1) / SURFACE_TILE;
    int tile_v = v >= 0 ? v / SURFACE_TILE : (v - SURFACE_TILE + 1) / SURFACE_TILE;
    uint32_t epoch = game->tick / HZ;
    Vec fixed, local;
    if (surface_seed != p->seed || surface_system != game->system.seed ||
        surface_enhanced != frame->enhanced) {
        memset(surface_cache, 0, sizeof(surface_cache));
        surface_seed = p->seed; surface_system = game->system.seed;
        surface_enhanced = frame->enhanced;
    }
    slot = hash32((unsigned)tile_u ^ hash32((unsigned)tile_v) ^ (unsigned)face * 7919U) &
           (SURFACE_CACHE_SIZE - 1);
    if (surface_cache[slot].valid && surface_cache[slot].u == tile_u &&
        surface_cache[slot].v == tile_v && surface_cache[slot].face == face &&
        surface_cache[slot].epoch == epoch)
        return surface_cache[slot].color;
    u = tile_u * SURFACE_TILE + SURFACE_TILE / 2;
    v = tile_v * SURFACE_TILE + SURFACE_TILE / 2;
    {
        int sign = face & 1 ? -p->radius : p->radius;
        fixed = face / 2 == 0 ? vec(sign,u,v) : face / 2 == 1 ? vec(u,sign,v) : vec(u,v,sign);
    }
    local = body_rotate(p, radial(fixed, p->radius), 0);
    if (!surface_cache[slot].valid || surface_cache[slot].u != tile_u ||
        surface_cache[slot].v != tile_v || surface_cache[slot].face != face)
        surface_cache[slot].material = terrain_material(p, local);
    surface_cache[slot].u = tile_u; surface_cache[slot].v = tile_v;
    surface_cache[slot].face = face; surface_cache[slot].valid = 1;
    surface_cache[slot].epoch = epoch;
    surface_cache[slot].color = material_color(p, local, surface_cache[slot].material);
    return surface_cache[slot].color;
}
static uint8_t globe_material[MAX_PLANETS][17][33];
static void cache_globes(void) {
    int b, x, y;
    if (globe_ready && globe_seed == game->system.seed && globe_enhanced == frame->enhanced &&
        globe_hour == 0)
        return;
    for (b = 0; b < game->system.count; b++) {
        Planet fixed = game->system.planets[b];
        const Planet *p = &fixed;
        set_rotation(&fixed, 0);
        for (y = 0; y <= 16; y++) {
            if (platform_pump)
                platform_pump();
            for (x = 0; x <= 32; x++) {
                Vec dir = forward(x * 32, y * 32 - 256);
                Vec local = radial(dir, p->radius + terrain_height(p, dir));
                globe_vertices[b][y][x] = local;
                globe_material[b][y][x] = (uint8_t)terrain_material(p, local);

            }
        }
    }
    globe_seed = game->system.seed;
    globe_hour = 0;
    globe_enhanced = frame->enhanced;
    globe_ready = 1;
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
        terrain_vertices[slot].point = radial(direction,
            p->radius + terrain_height(&fixed, direction));
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
    int span = (int)isqrt64((uint64_t)(2LL * p->radius * 12000)) * 2;
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
    int radius = p->radius, x, y, lons = frame->enhanced ? 32 : 16, lats = lons / 2;
    int64_t distance = length(cam_local), altitude = distance - radius;
    terrain_surface = body == game->body && altitude < 12000 ? p : NULL;
    {
        Vec c = camera(p->center);
        if (c.z + radius < 8) {
            terrain_surface = NULL;
            return;
        }
        if (distance > radius * 80LL) {
            lons = 8;
            lats = 4;
        }
        {
            int step = 32 / lons;
            for (y = 0; y < lats; y++)
                for (x = 0; x < lons; x++) {
                    int row = y * step, column = x * step;
                    Vec a = add(p->center, body_rotate(p, globe_vertices[body][row][column], 0)),
                        b = add(p->center, body_rotate(p, globe_vertices[body][row][column + step], 0));
                    Vec c2 = add(p->center, body_rotate(p, globe_vertices[body][row + step][column + step], 0)),
                        d = add(p->center, body_rotate(p, globe_vertices[body][row + step][column], 0));
                    int color = material_color(p, sub(a, p->center), globe_material[body][row][column]);
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
    if (body == game->body && altitude < 12000)
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
            triangle(center, a, b, i ? 14 : 12);
        }
    }
}
/* Sparse curved cloud deck in body coordinates: depth-tested, permeable and
   sampled by the identical density field used for cockpit fog. */
static void cloud_deck(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(game->pos, 1, FP), p->center), up = unit(local, 16384);
    Vec right = unit(cross(up, vec(0, 0, 16384)), 16384), front;
    int x, y, level = p->climate == HYDROGEN ? 18000 : 1800;
    if (!p->atmosphere || length(local) - p->radius > 40000) return;
    if (!length(right)) right = vec(16384, 0, 0);
    front = cross(right, up);
    for (y = -4; y < 4; y++) for (x = -4; x < 4; x++) {
        Vec a = radial(add(local, add(scale(right, x * 2400, 16384),
                                     scale(front, y * 2400, 16384))), p->radius + level);
        Vec b = radial(add(a, scale(right, 2200, 16384)), p->radius + level);
        Vec c = radial(add(b, scale(front, 2200, 16384)), p->radius + level);
        Vec d = radial(add(a, scale(front, 2200, 16384)), p->radius + level);
        int light = surface_light(&game->system, p, a);
        if (cloud_density(p, a, game->tick) < 70) continue;
        triangle(add(p->center,a), add(p->center,b), add(p->center,c), light ? 7 : 1);
        triangle(add(p->center,a), add(p->center,c), add(p->center,d), light ? 7 : 1);
    }
}
static void weather_overlay(void) {
    const Planet *p = &game->system.planets[game->body];
    Vec local = sub(scale(game->pos, 1, FP), p->center);
    int density = cloud_density(p, local, game->tick), rain = precipitation(p, local, game->tick);
    int x, y, i, lit = surface_light(&game->system, p, local) > 0;
    if (density) {
        for (y = 0; y < view_h; y += 2) {
            if (platform_pump && !(y % 16)) platform_pump();
            for (x = 0; x < frame->w; x += 2)
                if ((int)(hash32((unsigned)(x / 2 + y / 2 * frame->w)) & 255) < density * 3 / 4)
                    rect(x, y, 2, 2, lit ? 7 : 1);
        }
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
            Vec a = add(p, vec(0, 0, -45)), b2 = add(p, vec(-55, 0, 25)),
                c = add(p, vec(55, 0, 25)), d = add(p, vec(0, 15, 15));
            triangle(a, b2, d, 23);
            triangle(a, d, c, 24);
            triangle(b2, c, d, 22);
            triangle(a, c, b2, 21);
        }
    if (game->camera_mode == 2) {
        int wide = game->ship == 0 ? 35 : game->ship == 1 ? 60 : 110;
        Vec p = scale(game->pos, 1, FP), f = forward(game->yaw, game->pitch);
        Vec right = vec(cosi(game->yaw), 0, -sini(game->yaw)), up = unit(cross(f, right), 16384);
        Vec tip = add(p, scale(f, wide * 2, 16384)), left = add(p, scale(right, -wide, 16384));
        Vec r = add(p, scale(right, wide, 16384)),
            roof = add(sub(p, scale(f, wide / 2, 16384)), scale(up, wide / 3, 16384));
        triangle(tip, left, roof, 19);
        triangle(tip, roof, r, 20);
        triangle(left, r, roof, 17);
        triangle(tip, r, left, 16);
    }
    qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
    for (i = 0; i < nt; i++) {
        if (platform_pump && i % 32 == 0)
            platform_pump();
        raster(&triangles[i]);
    }
}
static void hud(void) {
    char s[100];
    int w = frame->w, h = view_h, i;
    rect(0, h, w, frame->h - h, 1);
    rect(0, h, w, 1, 13);
    text(7, h + 6, 13, "SPACE");
    snprintf(s, sizeof(s), "%s / %s", game->system.name,
             game->docked >= 0 ? "DOCKED"
             : game->autopilot ? "AUTO"
                               : "MANUAL");
    text(49, h + 6, 15, s);
    snprintf(s, sizeof(s), "ALT %ld M  V %ld M/S", (long)(altitude_at(game, game->pos)),
             (long)(length(game->vel) / FP));
    text(7, h + 18, 15, s);
    snprintf(s, sizeof(s), "%d CR  %dX/%dX", game->credits, game->effective_warp, game->warp);
    text(7, h + 30, 12, s);
    for (i = 0; i < 3; i++) {
        int value = i == 0 ? game->hull : i == 1 ? game->shield : game->fuel;
        int xx = w - 96;
        text(xx, h + 6 + i * 12, 15, i == 0 ? "H" : i == 1 ? "S" : "F");
        rect(xx + 10, h + 6 + i * 12, 78, 6, 3);
        rect(xx + 10, h + 6 + i * 12, value * 78 / 100, 6, i == 0 ? 11 : i == 1 ? 13 : 12);
    }
    text(7, h + 44, 7, "V FLIGHT  G MAP  B PORT  H HELP");
    text(7, view_h - 10, 12, game->message);
    if (game->paused)
        text(w / 2 - 18, 40, 12, "PAUSED");
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
    Vec r = scale(sub(p, origin), 62, extent);
    *x = screen_coord(frame->w / 2 + r.x);
    *y = screen_coord(96 - r.z);
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
    if (game->map_local) {
        parent = game->system.planets[b].parent;
        if (parent < 0)
            parent = b;
        origin = game->system.planets[parent].center;
        extent = (int64_t)game->system.planets[parent].radius * 4;
        for (i = 0; i < game->system.count; i++)
            if (game->system.planets[i].parent == parent && game->system.planets[i].orbit > extent)
                extent = game->system.planets[i].orbit;
        for (i = 0; i < game->nports; i++)
            if (game->ports[i].body == parent) {
                int64_t d = length(sub(scale(game->ports[i].pos, 1, FP), origin));
                if (d > extent)
                    extent = d;
            }
    } else {
        for (i = 0; i < game->system.count; i++) {
            const Planet *p = &game->system.planets[i];
            int64_t d = p->orbit + (p->parent >= 0 ? game->system.planets[p->parent].orbit : 0);
            if (d > extent)
                extent = d;
        }
    }
    extent = extent * 11 / 10;
    panel(game->map_local ? "LOCAL ORBITS / TOP DOWN" : "SYSTEM ORBITS / TOP DOWN");
    for (i = 0; i < game->system.count; i++) {
        const Planet *p = &game->system.planets[i];
        Vec center = p->parent >= 0 ? game->system.planets[p->parent].center : vec(0, 0, 0);
        if (game->map_local && i != parent && p->parent != parent)
            continue;
        if (!game->map_local || i != parent)
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
    text(12, 174, 7, "C SCALE / Q T SELECT / I DATA / Y LIST");
}
static void system_data(void) {
    static const char *climates[] = {"VACUUM", "TEMPERATE", "THIN", "DENSE", "FROZEN", "HYDROGEN"};
    static const char *economies[] = {"AGRICULTURE", "INDUSTRY", "TECHNOLOGY", "EXTRACTION"};
    static const char *governments[] = {"COUNCIL", "FEDERATION", "CORPORATE", "INDEPENDENT"};
    const Planet *p = &game->system.planets[selected_body()];
    int j = exploration_index(game, game->system.sector), b = selected_body(), ports = 0, i;
    char s[96];
    panel("SYSTEM / BODY DATA");
    snprintf(s, sizeof(s), "%s / %d STARS / %d BODIES", game->system.name,
             game->system.stars, game->system.count);
    text(12, 30, 15, s);
    snprintf(s, sizeof(s), "%s / %s", economies[game->system.economy],
             governments[game->system.government]);
    text(12, 42, 7, s);
    snprintf(s, sizeof(s), "%s / %s", p->name, body_names[p->type]);
    text(12, 60, 12, s);
    snprintf(s, sizeof(s), "RADIUS %ld KM / GRAVITY %d M/S2", (long)(p->radius / 1000), (int)p->gravity);
    text(12, 72, 15, s);
    snprintf(s, sizeof(s), "%s / ATMOSPHERE %ld KM", climates[p->climate], (long)(p->atmosphere / 1000));
    text(12, 84, 15, s);
    snprintf(s, sizeof(s), "PARENT %s / %s", p->parent >= 0 ? game->system.planets[p->parent].name : "STARS",
             p->rings ? "RINGS" : "NO RINGS");
    text(12, 96, 15, s);
    snprintf(s, sizeof(s), "ORBIT %ld %s", (long)(p->orbit / (p->parent >= 0 ? 1000 : AU_METRES / 100)),
             p->parent >= 0 ? "KM" : "0.01 AU");
    text(12, 108, 15, s);
    snprintf(s, sizeof(s), "PERIOD %lu H / INCLINATION %d DEG", (unsigned long)(p->period / 3600),
             p->inclination * 360 / ANGLE);
    text(12, 120, 15, s);
    for (i = 0; i < game->nports; i++)
        ports += game->ports[i].body == b;
    snprintf(s, sizeof(s), "%d PORTS / %s", ports,
             j >= 0 && (game->explored[j].surveyed & (1U << b)) ? "SURVEYED" : "UNSURVEYED");
    text(12, 132, 13, s);
    snprintf(s, sizeof(s), "NAV RANGE %ld KM", (long)(length(sub(navigation_position(game), game->pos)) / (FP * 1000)));
    text(12, 150, 15, s);
    text(12, 174, 7, "Q T SELECT / UP DOWN / Z ORBITS / V FLY");
}
/* Radar follows the hull heading even when a turret or rear camera is used.
   Horizontal distance uses X/Z; a short stem shows signed vertical separation. */
static void sensors(void) {
    int i, selected = combat_target(game), cx = frame->w - 46, cy = 43;
    char s[80];
    rect(cx - 40, 8, 80, 61, 1);
    text(cx - 36, 11, 7, "RADAR 10 KM");
    line(cx - 35, cy, cx + 35, cy, 3);
    line(cx, cy - 18, cx, cy + 18, 3);
    rect(cx - 1, cy - 1, 3, 3, 15);
    for (i = 0; i < MAX_NPC; i++) {
        const Npc *n = &game->npc[i];
        Vec r;
        int x, y, stem, color;
        if (!n->active || length(sub(n->pos, game->pos)) > 10000LL * FP)
            continue;
        r = scale(sub(n->pos, game->pos), 1, FP);
        x = (int)((r.x * cosi(game->yaw) - r.z * sini(game->yaw)) / 16384 * 32 / 10000);
        y = (int)((r.x * sini(game->yaw) + r.z * cosi(game->yaw)) / 16384 * 18 / 10000);
        stem = (int)(r.y * 8 / 10000);
        color = i == selected ? 12 : n->hostile ? 14 : 11;
        line(cx + x, cy - y, cx + x, cy - y - stem, color);
        rect(cx + x - 1, cy - y - stem - 1, 3, 3, color);
    }
    text(7, 20, 7, game->camera_mode == 0 ? "FRONT" : game->camera_mode == 1 ? "REAR" :
         game->camera_mode == 2 ? "EXTERNAL" : game->camera_mode == 3 ? "DORSAL TURRET" : "VENTRAL TURRET");
    if (selected >= 0) {
        const Npc *n = &game->npc[selected];
        Vec rel = sub(n->pos, game->pos), rv = sub(n->vel, game->vel), dir = unit(rel, 16384);
        Vec p = camera(scale(n->pos, 1, FP));
        int64_t closing = -(rv.x * dir.x + rv.y * dir.y + rv.z * dir.z) / (16384 * FP);
        snprintf(s, sizeof(s), "R %s %d / HULL %d", n->hostile ? "HOSTILE" : "CONTACT", selected + 1, n->hull);
        text(7, 32, 12, s);
        snprintf(s, sizeof(s), "%ld M / CLOSING %ld M/S", (long)(length(rel) / FP), (long)closing);
        text(7, 44, 12, s);
        if (p.z > 8) {
            int x = screen_coord(frame->w / 2 + p.x * (frame->w / 2) / p.z);
            int y = screen_coord(view_h / 2 - p.y * (frame->w / 2) / p.z);
            if (x > 8 && x < frame->w - 8 && y > 56 && y < view_h - 20) {
                line(x - 6, y, x, y - 6, 12);
                line(x, y - 6, x + 6, y, 12);
                line(x + 6, y, x, y + 6, 12);
                line(x, y + 6, x - 6, y, 12);
            }
        }
    } else
        text(7, 32, 7, "R SELECT RADAR CONTACT");
}
static void menus(void) {
    char s[110];
    int i;
    switch (game->page) {
    case MAP: {
        System sel;
        panel("GALACTIC NAVIGATION");
        generate_system(game->seed, game->selected, &sel);
        for (i = 0; i < 9; i++) {
            Sector ss = game->selected;
            System sys;
            ss.x += i % 3 - 1;
            ss.z += i / 3 - 1;
            generate_system(game->seed, ss, &sys);
            int x = 55 + (i % 3) * 100, y = 60 + (i / 3) * 35;
            line(x - 40, y + 8, x + 40, y + 8, 3);
            if (sys.count)
                rect(x, y, 3, 3, i == 4 ? 12 : exploration_index(game, ss) >= 0 ? 11 : 13);
            text(x - 30, y + 12, i == 4 ? 12 : 7, sys.count ? sys.name : "EMPTY");
        }
        snprintf(s, sizeof(s), "%s  SECTOR %ld / %ld", sel.name, (long)game->selected.x,
                 (long)game->selected.z);
        text(12, 29, 15, s);
        snprintf(s, sizeof(s), "DENSITY %d / PORTS %d / %s", sel.density, system_port_count(&sel),
                 !sel.count                                     ? "EMPTY"
                 : exploration_index(game, game->selected) >= 0 ? "VISITED"
                                                                : "UNEXPLORED");
        text(12, 157, 13, s);
        text(12, 173, 7, "ARROWS SELECT / X JUMP / V FLIGHT");
        break;
    }
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
                         journal >= 0 && (game->explored[journal].surveyed & (1U << i)) ? "SCAN"
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
            snprintf(s, sizeof(s), "%ld/%ld PORT %d - %d T", (long)m->destination.x,
                     (long)m->destination.z, m->port + 1, m->quantity);
            text(12, 158, 13, s);
        }
        text(12, 174, 7, "1-8 SELECT / ENTER ACCEPT / J LOG");
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
        break;
    case SHIPYARD:
        panel("PORT / SHIPYARD");
        snprintf(s, sizeof(s), "%s / %d TONNE HOLD",
                 game->ship == 0   ? "CINDER"
                 : game->ship == 1 ? "LOOM"
                                   : "HEARTH",
                 ship_capacity[game->ship]);
        text(12, 35, 15, s);
        text(12, 62, 15, "F REFUEL AND REPAIR - 100 CR");
        text(12, 86, 15, "O LASER UPGRADE - 2000 CR");
        snprintf(s, sizeof(s), "N LARGER HULL - %d CR", 8000 * (game->ship + 1));
        text(12, 110, 15, s);
        text(12, 144, 7, "L TO REQUEST LAUNCH");
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
            text(12, 30 + i * 18, 13, s);
            snprintf(s, sizeof(s), "  %ld/%ld P%d DUE %ld S", (long)m->destination.x,
                     (long)m->destination.z, m->port + 1,
                     (long)(m->deadline > game->tick ? (m->deadline - game->tick) / HZ : 0));
            text(12, 39 + i * 18, 7, s);
        }
        break;
    case HELP:
        panel("SPACE / FLIGHT MANUAL");
        {
            const char *help[] = {
                "ARROWS STEER / W S CHANGE SET SPEED",  "CTRL FIRE / E TOGGLE FLIGHT ASSIST",
                "L LAUNCH / Y PORTS / T PORT / Q BODY", "K GEAR / D CLEARANCE / F SERVICE",
                "[ ] TIME TO 4096X / SPACE PAUSE",          "G GALAXY / ARROWS SELECT / X JUMP",
                "B CONTRACTS / M MARKET / U SHIPYARD",  "J CONTRACT LOG / V FLIGHT OR CAMERA",
                "F5 SAVE / F9 LOAD / ESC EXIT",         "R CONTACT / Z ORBITS / I BODY DATA",
                "RMOUSE/JOY STEER / LMOUSE FIRE",     "JUMP: ABOVE 12 KM / NO NEARBY TRAFFIC"};
            for (i = 0; i < 12; i++)
                text(12, 30 + i * 12, i < 9 ? 15 : 7, help[i]);
        }
        break;
    default:
        break;
    }
}
void render_shutdown(void) {
    free(depth_buffer);
    depth_buffer = 0;
    depth_capacity = 0;
    terrain_surface = NULL;
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
    return 1;
}
void render_game(const Game *g, Frame *f) {
    int i;
    int64_t alt = altitude_at(g, g->pos);
    frame = f;
    game = g;
    /* Keep all eight rows and menu instructions readable at 200 lines.
       Compact menus use the whole screen; flight retains its cockpit. */
    if (f->h < 256 && g->page != FLIGHT) {
        char status[110];
        view_h = f->h;
        memset(f->pixels, 0, (size_t)f->w * f->h);
        menus();
        snprintf(status, sizeof(status), "%ld CR  %s", (long)g->credits, g->message);
        text(7, f->h - 12, 12, status);
        return;
    }
    camera_position = g->pos;
    view_angles(g, &camera_yaw, &camera_pitch);
    if (g->camera_mode == 2) {
        Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
        camera_position = add(sub(g->pos, scale(forward(g->yaw, g->pitch), 600 * FP, 16384)),
                              scale(up, 220 * FP, 16384));
        aim_angles(sub(g->pos, camera_position), &camera_yaw, &camera_pitch);
    }
    view_h = f->h - 56;
    nt = 0;
    {
        size_t count = (size_t)f->w * view_h;
        /* Allocation happens in render_init, before native OS takeover. */
        if (count > depth_capacity || !depth_buffer)
            return;
        memset(depth_buffer, 0, count * sizeof(*depth_buffer));
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
        for (i = 0; i < 120; i++) {
            uint32_t h = hash32((unsigned)i + g->seed);
            Vec p = add(scale(g->pos, 1, FP),
                        scale(forward(h % 1024, (int)((h >> 16) % 512) - 256), 1000000, 16384));
            Vec v = camera(p);
            if (v.z > 100) {
                int x = f->w / 2 + (int)(v.x * (f->w / 2) / v.z),
                    y = view_h / 2 - (int)(v.y * (f->w / 2) / v.z);
                if (y < view_h)
                    pixel(x, y, i % 5 ? 7 : 15);
            }
        }
    }
    if (g->page == FLIGHT) {
        world();
        weather_overlay();
    }
    if (g->page == FLIGHT) {
        Vec p = camera(scale(navigation_position(g), 1, FP));
        if (p.z > 8) {
            int x = f->w / 2 + (int)(p.x * (f->w / 2) / p.z),
                y = view_h / 2 - (int)(p.y * (f->w / 2) / p.z);
            if (x > 10 && x < f->w - 10 && y > 10 && y < view_h - 25) {
                line(x - 8, y - 8, x + 8, y - 8, 13);
                line(x - 8, y + 8, x + 8, y + 8, 13);
                line(x - 8, y - 8, x - 8, y + 8, 13);
                line(x + 8, y - 8, x + 8, y + 8, 13);
                text(x - 30, y + 12, 13,
                     g->target < 0 ? g->system.planets[g->target_body].name
                                   : g->ports[g->target].name);
            }
        }
        text(7, 8, 13,
             g->target < 0 ? g->system.planets[g->target_body].name : g->ports[g->target].name);
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
    } else
        menus();
    hud();
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
