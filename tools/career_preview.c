/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <stdio.h>
#include <stdlib.h>
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
int main(void) {
    Game g;
    Frame f = {320, 200, 0, 0, {0}};
    int page, mode;
    char name[120];
    game_init(&g, 0x53504143);
    g.credits = 20000; g.crew = 1; g.cabins = 2;
    g.reputation[system_faction(&g)] = 12; g.board_page = 1; refresh_board(&g);
    f.pixels = malloc(320 * 200); if (!f.pixels || !render_init(&f)) return 1;
    for (mode = 0; mode < 2; mode++) {
        f.enhanced = mode;
        for (page = BOARD; page <= CAREER; page++) {
            if (page != BOARD && page != SHIPYARD && page != EQUIPMENT && page != CAREER) continue;
            g.page = page;
            snprintf(name, sizeof(name), "build/evidence/careers-%s-page%d.ppm", mode ? "aga" : "ecs", page);
            output(&g, &f, name);
        }
    }
    g.page = FLIGHT; g.docked = -1; g.target = -1; g.target_body = 0; g.resource_target = 0;
    g.pos = add(asteroid_position(&g, 0), vec(0, 0, -800 * FP)); g.yaw = g.pitch = 0;
    output(&g, &f, "build/evidence/careers-asteroid.ppm");
    free(f.pixels); render_shutdown(); return 0;
}
