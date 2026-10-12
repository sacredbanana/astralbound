/* SPDX-License-Identifier: Unlicense; host-side renderer workload benchmark. */
#include "space.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(void) {
    Game g;
    Frame f = {320, 256, 1, 0, {0}};
    int i, j;
    clock_t start;
    uint32_t hash = 2166136261U;
    game_init(&g, 0x53504143);
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + g.system.planets[0].atmosphere + 35000LL) * FP,
                    -60000LL * FP));
    g.docked = -1;
    g.pitch = -70;
    f.pixels = malloc(320 * 256);
    if (!f.pixels)
        return 1;
    if (!render_init(&f))
        return 1;
    render_game(&g, &f);
    start = clock();
    for (i = 0; i < 200; i++)
        render_game(&g, &f);
    for (j = 0; j < 320 * 256; j++)
        hash = (hash ^ f.pixels[j]) * 16777619U;
    printf("orbit: %.3f ms/frame, indexed frame hash %08lx\n",
           (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / 200, (unsigned long)hash);
    free(f.pixels);
    render_shutdown();
    return 0;
}
