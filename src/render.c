/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define TRI_MAX 4096
typedef struct {
    int x[3], y[3], z;
    uint8_t color;
} Tri;
static Tri triangles[TRI_MAX];
static int nt;
static Frame *frame;
static const Game *game;
static int view_h;
static Vec camera_position;
static int camera_yaw, camera_pitch;
static void (*platform_pump)(void);
static uint32_t plane_low[256], plane_high[256];
static int tables_ready;
static Vec globe_vertices[MAX_PLANETS][17][33];
static uint8_t globe_colors[MAX_PLANETS][17][33];
static uint32_t globe_seed;
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
    for (i = 0; i < 3; i++) {
        t->x[i] = screen_coord(frame->w / 2 + v[i].x * (frame->w / 2) / v[i].z);
        t->y[i] = screen_coord(view_h / 2 - v[i].y * (frame->w / 2) / v[i].z);
    }
    if ((t->x[0] < 0 && t->x[1] < 0 && t->x[2] < 0) ||
        (t->x[0] >= frame->w && t->x[1] >= frame->w && t->x[2] >= frame->w) ||
        (t->y[0] < 0 && t->y[1] < 0 && t->y[2] < 0) ||
        (t->y[0] >= view_h && t->y[1] >= view_h && t->y[2] >= view_h))
        return;
    t->z = (int)((a.z + b.z + c.z) / 3);
    t->color = (uint8_t)color;
    nt++;
}
static void triangle(Vec a, Vec b, Vec c, int color) {
    Vec in[3], out[4];
    int i, n = 0;
    in[0] = camera(a);
    in[1] = camera(b);
    in[2] = camera(c);
    for (i = 0; i < 3; i++) {
        Vec p = in[i], q = in[(i + 1) % 3];
        if (p.z >= 8)
            out[n++] = p;
        if ((p.z >= 8) != (q.z >= 8)) {
            Vec d = sub(q, p);
            out[n++] = add(p, scale(d, 8 - p.z, d.z));
        }
    }
    if (n >= 3)
        emit(out[0], out[1], out[2], color);
    if (n == 4)
        emit(out[0], out[2], out[3], color);
}
static int sort_tri(const void *a, const void *b) {
    const Tri *x = a, *y = b;
    return x->z < y->z ? 1 : x->z > y->z ? -1 : 0;
}
static void raster(const Tri *t) {
    int y, miny = t->y[0], maxy = t->y[0], i;
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
            if ((t->y[i] <= y && t->y[j] > y) || (t->y[j] <= y && t->y[i] > y))
                xs[n++] = t->x[i] +
                          (int)((int64_t)(y - t->y[i]) * (t->x[j] - t->x[i]) / (t->y[j] - t->y[i]));
        }
        if (n >= 2) {
            int l = xs[0] < xs[1] ? xs[0] : xs[1], r = xs[0] > xs[1] ? xs[0] : xs[1];
            if (l < 0)
                l = 0;
            if (r >= frame->w)
                r = frame->w - 1;
            if (r >= l)
                memset(frame->pixels + y * frame->w + l, t->color, (size_t)(r - l + 1));
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
static int ground_color(const Planet *planet, Vec local) {
    int h = terrain_height(planet, local);
    Vec normal = unit(local, 16384);
    int shade =
        (int)(((int)normal.x * -6000 + (int)normal.y * 12000 + (int)normal.z * -7000) / 16384);
    shade = (shade + 16000) / 2400;
    if (shade < 0)
        shade = 0;
    if (shade > 15)
        shade = 15;
    if (!frame->enhanced)
        return h < 5 ? 25 + shade / 4 : h > 700 ? 16 + shade / 6 : 8 + shade / 6;
    return (h < 5 ? 80 : h > 800 ? 112 : h > 500 ? 96 : 64) + shade;
}
static void cache_globes(void) {
    int b, x, y;
    if (globe_ready && globe_seed == game->system.seed && globe_enhanced == frame->enhanced)
        return;
    for (b = 0; b < game->system.count; b++) {
        const Planet *p = &game->system.planets[b];
        for (y = 0; y <= 16; y++) {
            if (platform_pump)
                platform_pump();
            for (x = 0; x <= 32; x++) {
                Vec dir = forward(x * 32, y * 32 - 256);
                Vec local = scale(dir, p->radius + terrain_height(p, dir), 16384);
                globe_vertices[b][y][x] = add(p->center, local);
                globe_colors[b][y][x] = (uint8_t)ground_color(p, local);
            }
        }
    }
    globe_seed = game->system.seed;
    globe_enhanced = frame->enhanced;
    globe_ready = 1;
}
static void planet_mesh(int body) {
    const Planet *p = &game->system.planets[body];
    Vec cam_local = sub(scale(game->pos, 1, FP), p->center);
    int radius = p->radius, x, y, lons = frame->enhanced ? 32 : 16, lats = lons / 2;
    int64_t distance = length(cam_local), altitude = distance - radius;
    if (body != game->body || altitude > 3000) {
        Vec c = camera(p->center);
        if (c.z + radius < 8)
            return;
        if (distance > radius * 80LL) {
            lons = 8;
            lats = 4;
        }
        {
            int step = 32 / lons;
            for (y = 0; y < lats; y++)
                for (x = 0; x < lons; x++) {
                    int row = y * step, column = x * step;
                    Vec a = globe_vertices[body][row][column],
                        b = globe_vertices[body][row][column + step];
                    Vec c2 = globe_vertices[body][row + step][column + step],
                        d = globe_vertices[body][row + step][column];
                    int color = globe_colors[body][row][column];
                    triangle(a, b, c2, color);
                    triangle(a, c2, d, color);
                }
        }
    }
    if (body == game->body && altitude < 12000) {
        Vec up = unit(cam_local, 16384), right = unit(cross(up, vec(0, 0, 16384)), 16384), front,
            rows[2][21];
        int span = (int)isqrt64((uint64_t)(2LL * radius * (altitude > 20 ? altitude : 20))) * 2,
            step;
        if (span < 4000)
            span = 4000;
        if (!length(right))
            right = vec(16384, 0, 0);
        front = cross(right, up);
        step = span / 10;
        for (y = 0; y <= 20; y++) {
            if (platform_pump)
                platform_pump();
            for (x = 0; x <= 20; x++) {
                Vec v = add(scale(up, radius, 16384), add(scale(right, (x - 10) * step, 16384),
                                                          scale(front, (y - 10) * step, 16384)));
                rows[y % 2][x] = add(p->center, unit(v, radius + terrain_height(p, v) + 2));
            }
            if (y)
                for (x = 0; x < 20; x++) {
                    Vec a = rows[(y - 1) % 2][x], b = rows[(y - 1) % 2][x + 1],
                        c2 = rows[y % 2][x + 1], d = rows[y % 2][x];
                    int color = ground_color(p, sub(a, p->center));
                    triangle(a, b, c2, color);
                    triangle(a, c2, d, color);
                }
        }
    }
}
static void world(void) {
    int i, x, b;
    cache_globes();
    for (b = 0; b < game->system.count; b++)
        planet_mesh(b);
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
            for (x = 0; x < 12; x++) {
                uint32_t h = hash32(game->system.seed + (unsigned)x * 173 + (unsigned)i);
                int ox = ((int)(h % 7) - 3) * 160, oz = ((int)((h >> 8) % 7) - 3) * 160;
                Vec offset, v;
                if (abs(ox) < 150 && abs(oz) < 150)
                    ox += 350;
                offset = add(scale(right, ox, 16384), scale(front, oz, 16384));
                v = add(local, offset);
                v = unit(v, body->radius + terrain_height(body, v));
                box(add(body->center, v), up, 25 + h % 30, 40 + h % 140, 16);
            }
            box(add(body->center, unit(local, body->radius)), up, 65, 4, 13);
        } else {
            for (x = 0; x < 8; x++) {
                Vec d = add(scale(right, cosi(x * 128) * 160 / 16384, 16384),
                            scale(up, sini(x * 128) * 160 / 16384, 16384));
                box(add(p, d), up, 35, 55, 16);
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
            rect(x, y, 3, 3, i == 4 ? 12 : 13);
            text(x - 30, y + 12, i == 4 ? 12 : 7, sys.name);
        }
        snprintf(s, sizeof(s), "%s  SECTOR %ld / %ld", sel.name, (long)game->selected.x,
                 (long)game->selected.z);
        text(12, 29, 15, s);
        text(12, 173, 7, "ARROWS SELECT / X JUMP / V FLIGHT");
        break;
    }
    case SYSTEM_MAP: {
        int first = game->target / 8 * 8;
        panel("SYSTEM / FLIGHT DESTINATIONS");
        for (i = first; i < first + 8 && i < port_count(game); i++) {
            snprintf(s, sizeof(s), "%2d %s", i + 1, game->ports[i].name);
            text(12, 32 + (i - first) * 16, i == game->target ? 12 : 15, s);
        }
        text(12, 169, 7, "UP DOWN TARGET / V FLIGHT / P AUTO");
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
        panel("PORT / COMMODITIES");
        text(12, 28, 7, "ITEM          BUY  STOCK  HOLD");
        for (i = 0; i < GOODS; i++) {
            snprintf(s, sizeof(s), "%d %-11s %4d %4d %4d", i + 1, goods_names[i], price(game, i),
                     game->stock[i], game->cargo[i]);
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
            const char *help[] = {"ARROWS STEER / W S CHANGE SET SPEED",
                                  "CTRL FIRE / E TOGGLE FLIGHT ASSIST",
                                  "L LAUNCH / Y PORTS / T TARGET / P AUTO",
                                  "K LANDING GEAR / F SERVICE AT PORT",
                                  "[ ] TIME RATE / SPACE PAUSE",
                                  "G GALAXY / ARROWS SELECT / X JUMP",
                                  "B CONTRACTS / M MARKET / U SHIPYARD",
                                  "J CONTRACT LOG / V FLIGHT OR CAMERA",
                                  "F5 SAVE / F9 LOAD / ESC EXIT",
                                  "PATROLS: AIR 600-6000 M / ORBIT 13 KM",
                                  "SURVEY: 500-6000 M BELOW 200 M/S",
                                  "JUMP: ABOVE 12 KM / NO NEARBY TRAFFIC"};
            for (i = 0; i < 12; i++)
                text(12, 30 + i * 12, i < 9 ? 15 : 7, help[i]);
        }
        break;
    default:
        break;
    }
}
void render_init(Frame *f) {
    int i;
    if (!tables_ready)
        plane_tables();
    static const uint32_t base[32] = {
        0x040711, 0x0b1729, 0x12273e, 0x20394b, 0x294758, 0x335b71, 0x4b7890, 0x81a7b1,
        0x254b40, 0x35684d, 0x658958, 0x70cd93, 0xf0bb65, 0x65d3d5, 0xd77d65, 0xe9e8d5,
        0x354353, 0x778897, 0xb8c6c9, 0x414f64, 0x9ab3c7, 0x543e4c, 0x93516a, 0xdf827d,
        0xefb3a0, 0x27376b, 0x384f87, 0x4f70aa, 0x7c9bc4, 0xaac1cf, 0xc3d4d5, 0xffffff};
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
        f->palette[112 + i] = ((uint32_t)(90 + i * 10) << 16) | ((uint32_t)(102 + i * 9) << 8) |
                              (unsigned)(112 + i * 8);
    }
}
void render_game(const Game *g, Frame *f) {
    int i;
    int64_t alt = altitude_at(g, g->pos);
    frame = f;
    game = g;
    camera_position = g->pos;
    camera_yaw = g->yaw;
    camera_pitch = g->pitch;
    if (g->camera_mode == 1) {
        camera_yaw = (g->yaw + 512) & 1023;
        camera_pitch = -g->pitch;
    }
    if (g->camera_mode == 2) {
        Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
        camera_position = add(sub(g->pos, scale(forward(g->yaw, g->pitch), 600 * FP, 16384)),
                              scale(up, 220 * FP, 16384));
        aim_angles(sub(g->pos, camera_position), &camera_yaw, &camera_pitch);
    }
    view_h = f->h - 56;
    nt = 0;
    for (i = 0; i < view_h; i++) {
        int c = 0;
        if (alt < g->system.planets[g->body].atmosphere)
            c = f->enhanced ? 32 + i * 25 / view_h : 25 + i * 5 / view_h;
        rect(0, i, f->w, 1, c);
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
    if (g->page == FLIGHT)
        world();
    if (g->page == FLIGHT) {
        Vec p = camera(scale(g->ports[g->target].pos, 1, FP));
        if (p.z > 8) {
            int x = f->w / 2 + (int)(p.x * (f->w / 2) / p.z),
                y = view_h / 2 - (int)(p.y * (f->w / 2) / p.z);
            if (x > 10 && x < f->w - 10 && y > 10 && y < view_h - 25) {
                line(x - 8, y - 8, x + 8, y - 8, 13);
                line(x - 8, y + 8, x + 8, y + 8, 13);
                line(x - 8, y - 8, x - 8, y + 8, 13);
                line(x + 8, y - 8, x + 8, y + 8, 13);
                text(x - 30, y + 12, 13, g->ports[g->target].name);
            }
        }
        text(7, 8, 13, g->ports[g->target].name);
        if (!g->camera_mode) {
            line(f->w / 2 - 9, view_h / 2, f->w / 2 - 3, view_h / 2, 15);
            line(f->w / 2 + 3, view_h / 2, f->w / 2 + 9, view_h / 2, 15);
            line(f->w / 2, view_h / 2 - 9, f->w / 2, view_h / 2 - 3, 15);
        }
        if (g->laser && !g->camera_mode) {
            line(20, view_h - 20, f->w / 2, view_h / 2, 12);
            line(f->w - 20, view_h - 20, f->w / 2, view_h / 2, 12);
        }
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
        for (x = 0; x < stride; x++) {
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
