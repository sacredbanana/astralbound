/* SPDX-License-Identifier: Unlicense; LAND in place, then walk outside. */
#include "astralbound.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
static int save_frame(const Frame *f, const char *name, int mode) {
    char path[120];
    FILE *out;
    int i;
    snprintf(path, sizeof(path), "build/evidence/ground-%d-%s.ppm", mode, name);
    out = fopen(path, "wb");
    if (!out) return 0;
    fprintf(out, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc(c >> 16, out); fputc(c >> 8, out); fputc(c, out);
    }
    fclose(out);
    return 1;
}
/* Hover over dry, gentle ground on the start world, away from its ports. */
static int place_over_land(int body, int height) {
    const Planet *p = &g.system.planets[body];
    int a, b;
    for (a = 0; a < 1024; a += 16)
        for (b = -200; b <= 200; b += 40) {
            Vec local = radial(forward(a, b), p->radius);
            int ground = terrain_height(p, local);
            local = radial(local, (int64_t)p->radius + ground + height);
            g.docked = -1; g.body = g.frame_body = body;
            g.pos = add(scale(p->center, FP, 1), scale(local, FP, 1));
            g.vel = surface_velocity(p, local);
            if (park_mode(&g, NULL) == PARK_LAND && !land_refusal(&g) && ground > 50) return 1;
        }
    return 0;
}
int main(void) {
    Frame f = {320, 200, 0, NULL, {0}};
    Settings s;
    int mode, i, body;
    for (mode = 0; mode < 2; mode++) {
        f.w = mode ? 640 : 320; f.h = mode ? 480 : 200; f.enhanced = mode;
        settings_defaults(&s, 1); settings_preset(&s, mode ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&s.gfx);
        f.pixels = malloc((size_t)f.w * f.h);
        if (!f.pixels || !render_init(&f)) return 1;
        game_init(&g, 0x53504143);
        body = g.ports[g.docked].body;
        game_action(&g, 'l');
        g.tick = HZ * 3600 * 5; /* daylight over the start world */
        update_world(&g);
        memset(g.npc, 0, sizeof(g.npc));
        if (!place_over_land(body, 600)) return 2;
        g.page = FLIGHT;
        game_action(&g, 'd');
        render_game(&g, &f);
        if (!save_frame(&f, "landing", mode)) return 1;
        for (i = 0; i < 600 * HZ && g.docked == -1 && !g.dead; i++) game_tick(&g, (Controls){0});
        if (g.docked != -2) { printf("no touchdown: %s\n", g.message); return 3; }
        g.camera_mode = 2;
        render_game(&g, &f);
        if (!save_frame(&f, "landed", mode)) return 1;
        g.camera_mode = 0;
        if (!disembark(&g)) return 4;
        render_game(&g, &f);
        if (!save_frame(&f, "outside", mode)) return 1;
        /* Turn away and walk toward the nearest site. */
        {
            GroundSite site;
            int64_t metres;
            if (nearest_site(&g, &site, &metres)) {
                Vec to = body_rotate(&g.system.planets[g.body], sub(site_position(&g, &site), walker_position(&g)), 1);
                Vec up = unit(g.walker, 16384), east = vec(-up.y, up.x, 0), north;
                east = unit(east, 16384);
                north = vec((up.y * east.z - up.z * east.y) / 16384, (up.z * east.x - up.x * east.z) / 16384,
                            (up.x * east.y - up.y * east.x) / 16384);
                g.walk_yaw = attitude_angle((to.x * east.x + to.y * east.y + to.z * east.z) / 16384,
                                            (to.x * north.x + to.y * north.y + to.z * north.z) / 16384);
                g.walk_pitch = -40;
                for (i = 0; i < 1200 * HZ; i++) {
                    if (nearest_site(&g, &site, &metres) && metres < 20) break;
                    game_tick(&g, (Controls){.thrust = 1});
                }
                printf("mode %d: walked to %s at %lld m (%s)\n", mode, site_names[site.kind],
                       (long long)metres, g.message);
            }
        }
        render_game(&g, &f);
        if (!save_frame(&f, "site", mode)) return 1;
        g.walk_yaw = (g.walk_yaw + 512) & 1023; g.walk_pitch = 0;
        render_game(&g, &f);
        if (!save_frame(&f, "lookback", mode)) return 1;
        free(f.pixels); render_shutdown();
    }
    return 0;
}
