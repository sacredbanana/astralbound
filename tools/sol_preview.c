/* SPDX-License-Identifier: Unlicense; Solar System appearance sheets.
   For each named body, a full-face view centred on a body-fixed latitude and
   east longitude, lit from behind the camera, in Enhanced and Classic at
   AGA and ECS: one 1280x256 PPM row per body, for checking the measured
   maps against their sources.

     sol_preview OUT.ppm BODY[:LAT:LON[:RADII]] ...

   RADII is the camera's centre distance in sixteenths of a radius (48). */
#include "performance.h"
#include "solar.h"
#include "galaxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
static int64_t dot14(Vec a, Vec b) { return (a.x * b.x + a.y * b.y + a.z * b.z) / 16384; }
static int level_roll(int yaw, int pitch, Vec up) {
    Vec right0 = vec(cosi(yaw), 0, -sini(yaw)), up0;
    int roll, unused;
    up0 = vec(-(int64_t)sini(pitch) * sini(yaw) / 16384, cosi(pitch),
              -(int64_t)sini(pitch) * cosi(yaw) / 16384);
    aim_angles(vec(dot14(up, right0), 0, dot14(up, up0)), &roll, &unused);
    return roll;
}
/* Degrees to the game's 1024-step table angles. */
static int angle(double degrees) { return (int)(degrees * 1024 / 360 + (degrees < 0 ? -0.5 : 0.5)); }
static int place(const char *spec) {
    char name[32];
    double lat = 0, lon = 0, radii = 48;
    int body, la, lo;
    Planet *p;
    Vec fixed, dir, offset;
    if (sscanf(spec, "%31[^:]:%lf:%lf:%lf", name, &lat, &lon, &radii) < 1) return -1;
    game_init(&g, 1);
    setup_system(&g, catalog_sector(0));
    update_world(&g);
    g.docked = -1; g.page = FLIGHT; g.ui_visible = 0; g.target = -1;
    memset(g.npc, 0, sizeof(g.npc));
    body = solar_body_index(&g.system, name);
    if (body < 0) { fprintf(stderr, "no body %s\n", name); exit(1); }
    g.body = g.target_body = g.frame_body = body;
    p = &g.system.planets[body];
    la = angle(lat); lo = angle(lon);
    /* Body north is -z (see body_map.h). */
    fixed = vec((int64_t)cosi(la) * cosi(lo) / 16384, (int64_t)cosi(la) * sini(lo) / 16384, -sini(la));
    dir = unit(body_rotate(p, fixed, 0), 16384);
    /* The sun just above the camera's shoulder: a full, readable face. */
    g.system.stars = 1;
    g.system.star[0].center = add(p->center, scale(unit(add(dir, vec(0, 0, 2000)), 16384), AU_METRES, 16384));
    offset = scale(dir, (int64_t)p->radius * (int64_t)radii, 16384 * 16LL);
    g.pos = scale(add(p->center, offset), FP, 1);
    aim_angles(scale(offset, -1, 1), &g.yaw, &g.pitch);
    g.roll = level_roll(g.yaw, g.pitch, body_rotate(p, vec(0, 0, -16384), 0));
    g.nports = 0;
    return body;
}
int main(int argc, char **argv) {
    static const struct { int renderer, enhanced; } columns[4] = {
        {RENDERER_ENHANCED, 1}, {RENDERER_ENHANCED, 0}, {RENDERER_CLASSIC, 1}, {RENDERER_CLASSIC, 0}};
    FILE *out;
    int row, col, x, y, rows = argc - 2;
    unsigned char *sheet;
    if (argc < 3) { fprintf(stderr, "usage: %s OUT.ppm BODY[:LAT:LON[:RADII]] ...\n", argv[0]); return 2; }
    sheet = calloc((size_t)1280 * 256 * rows, 3);
    if (!sheet) return 1;
    for (row = 0; row < rows; row++)
        for (col = 0; col < 4; col++) {
            Settings s;
            Frame f = {320, 256, columns[col].enhanced, NULL, {0}};
            settings_defaults(&s, 0); settings_preset(&s, QUALITY_HIGH);
            render_graphics(&s.gfx); render_bezier(s.bezier); render_renderer(columns[col].renderer);
            f.pixels = malloc((size_t)f.w * f.h);
            if (!f.pixels || !render_init(&f)) return 1;
            place(argv[row + 2]);
            render_performance_scene(&g, &f);
            for (y = 0; y < 256; y++)
                for (x = 0; x < 320; x++) {
                    uint32_t c = f.palette[f.pixels[y * 320 + x]];
                    unsigned char *o = sheet + (((size_t)row * 256 + y) * 1280 + col * 320 + x) * 3;
                    o[0] = (unsigned char)(c >> 16); o[1] = (unsigned char)(c >> 8); o[2] = (unsigned char)c;
                }
            free(f.pixels); render_shutdown();
        }
    out = fopen(argv[1], "wb");
    if (!out) { perror(argv[1]); return 1; }
    fprintf(out, "P6\n1280 %d\n255\n", 256 * rows);
    fwrite(sheet, 3, (size_t)1280 * 256 * rows, out);
    fclose(out);
    free(sheet);
    return 0;
}
