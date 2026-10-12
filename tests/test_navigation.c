/* SPDX-License-Identifier: Unlicense; extended astronomical-route regression. */
#include "space.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    Game g;
    Controls c = {0};
    int i, system_frame = 0;
    game_init(&g, 1);
    g.docked = -1;
    g.target = -1;
    g.target_body = 1;
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (int64_t)(g.system.planets[0].radius + 300000) * FP, 0));
    g.vel = vec(0, 0, 0);
    g.autopilot = 1;
    g.warp = MAX_WARP;
    assert(length(sub(g.pos, navigation_position(&g))) / FP > AU_METRES / 3);
    for (i = 0; i < 150000 && g.autopilot && !g.dead; i++) {
        game_frame(&g, c, 1);
        system_frame |= g.frame_body == -1;
    }
    printf("interplanetary: %lu ticks, frame %d, fuel %d, arrival %ld m\n", (unsigned long)g.tick,
           g.frame_body, g.fuel, (long)(length(sub(g.pos, navigation_position(&g))) / FP));
    assert(!g.dead && !g.autopilot && g.fuel > 0);
    assert(system_frame && g.frame_body == 1 && g.body == 1);
    assert(length(sub(g.pos, navigation_position(&g))) < 100 * FP);
    puts("PASS full AU-scale departure, system-frame cruise and moving-body arrival");
    return 0;
}
