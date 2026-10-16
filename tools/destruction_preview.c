/* SPDX-License-Identifier: Unlicense; reproducible destruction visual evidence. */
#include "astralbound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
static void output(Frame *f, const char *name) {
    char path[160];
    FILE *out;
    int i;
    render_game(&g, f);
    snprintf(path, sizeof(path), "build/evidence/destruction-%s-%s.ppm", name,
             f->w == 640 ? "rtg" : f->enhanced ? "aga" : "ecs");
    out = fopen(path, "wb"); if (!out) exit(1);
    fprintf(out, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc(c >> 16, out); fputc(c >> 8, out); fputc(c, out);
    }
    fclose(out);
}
int main(void) {
    Frame f = {320,200,0,NULL,{0}};
    int mode, i;
    for (mode = 0; mode < 3; mode++) {
        f.w = mode == 2 ? 640 : 320; f.h = mode == 2 ? 480 : 200; f.enhanced = mode != 0;
        f.pixels = malloc((size_t)f.w * f.h); if (!f.pixels || !render_init(&f)) return 1;
        game_init(&g, 0x53504143);
        g.docked = g.frame_body = -1; g.pos = vec(0, AU_METRES * FP, 0);
        g.yaw = 80; g.ship = 3;
        destruction_player(&g, -1);
        game_frame(&g, (Controls){0}, 12); output(&f, "space");
        game_frame(&g, (Controls){0}, HZ * 2); output(&f, "debris");
        game_init(&g, 0x53504143);
        g.docked = -1; g.frame_body = g.body = 0; g.ship = 3; g.yaw = 320;
        g.pos = add(g.ports[0].pos, scale(g.ports[0].normal, 1400 * FP, 16384));
        g.vel = surface_velocity(&g.system.planets[0], sub(scale(g.pos, 1, FP), g.system.planets[0].center));
        g.vel = add(g.vel, scale(forward(g.yaw, 0), 160 * FP, 16384));
        destruction_player(&g, -1);
        game_frame(&g, (Controls){0}, HZ * 4); output(&f, "falling");
        for (i = 0; i < HZ * 120 && g.wrecks[0].state == 1; i++) game_frame(&g, (Controls){0}, 1);
        if (g.wrecks[0].state != 2) return 2;
        game_frame(&g, (Controls){0}, 12); output(&f, "impact");
        free(f.pixels); render_shutdown();
    }
    return 0;
}
