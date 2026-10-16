/* SPDX-License-Identifier: Unlicense */
/* Nearby NPC hulls oriented along their velocity, plus one far tetrahedron. */
#include "space.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void output(const Game *g, Frame *f, const char *path) {
    int i;
    FILE *out;
    render_game(g, f);
    out = fopen(path, "wb"); if (!out) exit(1);
    fprintf(out, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t p = f->palette[f->pixels[i]];
        fputc((p >> 16) & 255, out); fputc((p >> 8) & 255, out); fputc(p & 255, out);
    }
    fclose(out);
}
/* Offsets and velocities are camera-relative metres: x right, y up, z ahead. */
static Vec view(const Game *g, Vec v) {
    Vec f = forward(g->yaw, 0), r = vec(cosi(g->yaw), 0, -sini(g->yaw));
    return add(scale(r, v.x, 16384), add(vec(0, v.y, 0), scale(f, v.z, 16384)));
}
static void npc(Game *g, int i, int ship, Vec offset, Vec velocity) {
    g->npc[i].active = 1; g->npc[i].hull = 60; g->npc[i].mission = -1;
    g->npc[i].pos = add(g->pos, scale(view(g, offset), FP, 1));
    g->npc[i].vel = scale(view(g, velocity), FP, 1);
    g->npc_role[i] = CIVILIAN; g->npc_ship[i] = ship;
}
int main(void) {
    Game g;
    Frame f = {320, 200, 0, 0, {0}};
    int mode;
    char name[120];
    game_init(&g, 0x53504143);
    f.pixels = malloc(320 * 200); if (!f.pixels || !render_init(&f)) return 1;
    g.page = FLIGHT; g.docked = -1; g.target = -1; g.target_body = 0; g.resource_target = -1;
    g.pos = add(asteroid_position(&g, 0), vec(0, 0, -3000 * FP)); g.yaw = 512; g.pitch = g.roll = 0;
    memset(g.npc, 0, sizeof(g.npc));
    npc(&g, 0, 3, vec(-300, -110, 650), vec(90, 0, 60));   /* KESTREL below, quartering right and away */
    npc(&g, 1, 2, vec(380, -60, 1300), vec(-20, 15, -90)); /* HEARTH closing, climbing, veering left */
    npc(&g, 2, 1, vec(0, 170, 900), vec(0, 0, 0));         /* LOOM holding the fixed level heading */
    npc(&g, 3, 5, vec(330, 120, 6000), vec(0, 0, 80));     /* WAYFARER far away: tetrahedron */
    for (mode = 0; mode < 2; mode++) {
        f.enhanced = mode;
        snprintf(name, sizeof(name), "build/evidence/npc-%s.ppm", mode ? "aga" : "ecs");
        output(&g, &f, name);
    }
    free(f.pixels); render_shutdown(); return 0;
}
