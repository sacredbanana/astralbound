/* SPDX-License-Identifier: Unlicense; extended astronomical-route regression. */
#include "space.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
static void remote_cruise(void) {
    Game g, h, initial;
    Controls c = {0};
    clock_t start;
    double fast, reference;
    int i;
    game_init(&g, 1);
    g.docked = -1;
    g.target = -1;
    g.target_body = 1;
    g.frame_body = -1;
    g.pos = scale(add(scale(g.system.planets[0].center, FP, 1),
                      scale(g.system.planets[1].center, FP, 1)), 1, 2);
    g.vel = g.system.planets[1].cruise_velocity;
    g.autopilot = 1;
    game_action(&g, 'a');
    g.warp = MAX_WARP;
    g.heat = 50;
    g.shield = 20;
    initial = h = g;
    start = clock();
    game_frame(&g, c, 1);
    fast = (double)(clock() - start) / CLOCKS_PER_SEC;
    start = clock();
    for (i = 0; i < MAX_WARP; i++)
        game_tick(&h, c);
    reference = (double)(clock() - start) / CLOCKS_PER_SEC;
    assert(g.tick == MAX_WARP && g.tick == h.tick);
    assert(!g.dead && g.frame_body == -1 && g.fuel == h.fuel);
    assert(g.heat == h.heat && g.shield == h.shield);
    printf("drive cruise comparison: position %lld m, velocity %lld m/s\n",
           (long long)(length(sub(g.pos, h.pos)) / FP),
           (long long)(length(sub(g.vel, h.vel)) / FP));
    fflush(stdout);
    /* Batched cruise uses mean velocity; fixed ticks use end velocity. Bound
       their accumulated integration difference by a*duration/(2*HZ). */
    assert(length(sub(g.pos, h.pos)) < SYSTEM_DRIVE_ACCEL * 128LL * FP / (2 * HZ));
    assert(length(sub(g.vel, h.vel)) < 100 * FP);
    printf("remote 4096x: %.3f ms cruise, %.3f ms fixed ticks, error %ld m\n",
           fast * 1000, reference * 1000, (long)(length(sub(g.pos, h.pos)) / FP));
    /* Input, NPCs and patrol events must retain exact fixed-tick behavior. */
    for (i = 0; i < 3; i++) {
        int j;
        g = initial;
        c.fire = i == 0;
        if (i == 1) {
            g.npc[0].active = 1;
            g.npc[0].pos = add(g.pos, vec(1000000LL * FP, 0, 0));
            g.npc[0].vel = g.vel;
            g.npc[0].mission = -1;
        }
        if (i == 2) {
            g.contracts[0].state = ACTIVE;
            g.contracts[0].type = ORBIT_PATROL;
            g.contracts[0].destination = (Sector){99, 99, 99};
            g.contracts[0].deadline = 100000;
        }
        h = g;
        game_frame(&g, c, 1);
        for (j = 0; j < (int)g.tick; j++)
            game_tick(&h, c);
        assert(memcmp(&g.pos, &h.pos, sizeof(Vec)) == 0);
        assert(memcmp(&g.vel, &h.vel, sizeof(Vec)) == 0);
    }
    c.fire = 0;
    g = initial;
    g.contracts[0].state = ACTIVE;
    g.contracts[0].type = COURIER;
    g.contracts[0].deadline = 100;
    game_frame(&g, c, 1);
    assert(g.contracts[0].state == FAILED && g.tick > 100 && g.tick <= MAX_WARP);
    g = initial;
    g.paused = 1;
    game_frame(&g, c, 1);
    assert(g.tick == 0 && memcmp(&g.pos, &initial.pos, sizeof(Vec)) == 0);
    g = initial;
    g.npc[0].active = 1;
    g.npc[0].mission = -1;
    g.npc[0].pos = add(g.pos, vec(1000000LL * FP, 0, 0));
    game_frame(&g, c, 8);
    assert(g.tick == MAX_FRAME_STEPS && g.effective_warp == MAX_FRAME_STEPS / 8);
    puts("PASS bounded remote cruise, elapsed timers/deadlines and fixed-tick fallbacks");
}
static void moving_target_approach(void) {
    Game g;
    Controls c = {0};
    Vec motion;
    int i;
    game_init(&g, 1);
    g.docked = -1;
    g.target = -1;
    g.target_body = 1;
    /* A compact influence sphere keeps this approach in the system frame
       through the 10,000 km speed boundary. The destination's generated
       orbital motion is much faster than the local approach speed cap. */
    g.system.planets[1].influence = 7000000;
    g.frame_body = -1;
    motion = navigation_velocity(&g);
    assert(length(motion) > 4000 * FP);
    g.pos = sub(navigation_position(&g), unit(motion, 8000000 * FP));
    g.vel = vec(0, 0, 0);
    g.autopilot = 1;
    for (i = 0; i < 240000 && g.autopilot && !g.dead; i++)
        game_tick(&g, c);
    printf("moving approach: %lu ticks, fuel %d, arrival %ld m\n", (unsigned long)g.tick,
           g.fuel, (long)(length(sub(g.pos, navigation_position(&g))) / FP));
    assert(!g.dead && !g.autopilot && g.fuel > 0);
    assert(g.frame_body == 1 && length(sub(g.pos, navigation_position(&g))) < 100 * FP);
}
static void hetha_d_route(void) {
    Game g;
    Controls c = {0};
    int i, escaped = 0;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    g.target = -1;
    g.target_body = 2;
    g.autopilot = 1;
    game_action(&g, 'a');
    g.warp = MAX_WARP;
    for (i = 0; i < 150000 && g.autopilot && !g.dead; i++) {
        game_frame(&g, c, 1);
        escaped |= g.frame_body == -1;
    }
    printf("Hetha D launch/arrival: %lu ticks, fuel %d, arrival %ld m\n",
           (unsigned long)g.tick, g.fuel,
           (long)(length(sub(g.pos, navigation_position(&g))) / FP));
    assert(escaped && !g.dead && !g.autopilot && g.fuel > 0);
    assert(g.body == 2 && g.frame_body == 2);
    assert(length(sub(g.pos, navigation_position(&g))) < 100 * FP);
}
static int find_port(const Game *g, const char *name) {
    int i;
    for (i = 0; i < g->nports; i++)
        if (strstr(g->ports[i].name, name)) return i;
    assert(0);
    return -1;
}
/* The point above a surface pad is the destination's own approach, not a
   detour: an armed drive must carry the cruise instead of ordinary thrust. */
static void surface_port_drive(void) {
    Game g;
    Controls c = {0};
    int frames, drive = 0;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    g.target = find_port(&g, "D REACH");
    g.target_body = -1;
    g.autopilot = 1;
    game_action(&g, 'a');
    g.warp = MAX_WARP;
    for (frames = 0; frames < 20000 && g.autopilot && !g.dead; frames++) {
        game_frame(&g, c, 1);
        drive |= g.drive_active;
    }
    printf("Hetha D Reach with drive: %d frames, %lu ticks, hull %d, fuel %d\n", frames,
           (unsigned long)g.tick, g.hull, g.fuel);
    assert(drive && g.docked == g.target && g.hull == 100 && frames < 2000);
}
/* Deep in the air with the pad below the horizon. Climbing toward orbit
   restored the line of sight to the overhead point within seconds, and the
   two waypoints then alternated, swinging the nose by tens of degrees. */
static void atmospheric_pad_approach(void) {
    Game g;
    Controls c = {0};
    int i, port, last_yaw, last_pitch, swings = 0;
    const Planet *p;
    Vec center, up, side;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    port = find_port(&g, "D REACH");
    p = &g.system.planets[g.ports[port].body];
    center = scale(p->center, FP, 1);
    up = unit(sub(g.ports[port].pos, center), 16384);
    side = unit(vec(up.z, 0, -up.x), 16384);
    /* 26 km above ground ten degrees around from the pad, as reported. */
    g.pos = add(center, radial(add(scale(up, cosi(28), 16384), scale(side, sini(28), 16384)),
                               (int64_t)(p->radius + 26000) * FP));
    g.frame_body = g.body = g.ports[port].body;
    g.vel = surface_velocity(p, scale(sub(g.pos, center), 1, FP));
    g.target = port;
    g.target_body = -1;
    g.autopilot = 1;
    for (i = 0; i < 6 * HZ; i++)
        game_tick(&g, c); /* initial alignment */
    last_yaw = g.yaw;
    last_pitch = g.pitch;
    for (i = 0; i < 400000 && g.autopilot && !g.dead &&
                length(sub(g.pos, g.ports[port].pos)) > 12000 * FP; i++) {
        int yaw, pitch;
        game_tick(&g, c);
        yaw = ((g.yaw - last_yaw + 1536) & 1023) - 512;
        pitch = g.pitch - last_pitch;
        swings += abs(yaw) >= 6 || abs(pitch) >= 6;
        last_yaw = g.yaw;
        last_pitch = g.pitch;
    }
    while (g.autopilot && !g.dead && i++ < 400000)
        game_tick(&g, c);
    printf("atmospheric pad approach: %d ticks, %d steering swings, hull %d\n", i, swings, g.hull);
    assert(g.docked == port && g.hull == 100 && swings < 20);
}
int main(void) {
    Game g;
    Controls c = {0};
    int i, system_frame = 0;
    remote_cruise();
    moving_target_approach();
    hetha_d_route();
    surface_port_drive();
    atmospheric_pad_approach();
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
    game_action(&g, 'a');
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
