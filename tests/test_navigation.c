/* SPDX-License-Identifier: Unlicense; extended astronomical-route regression. */
#include "astralbound.h"
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
    /* Batched cruise uses mean velocity and one-second drive steps, so it may
       turn over from acceleration to braking up to a step late. Bound the
       difference by the peak speed reachable in the run (a*duration) held
       for that one-second step. */
    assert(length(sub(g.pos, h.pos)) < SYSTEM_DRIVE_ACCEL * 128LL * FP);
    /* With a 30c ceiling this route follows the changing gravity-well cap
       rather than a constant ceiling. One-second cruise samples can differ
       by up to half a second of drive acceleration at the end of the run. */
    assert(length(sub(g.vel, h.vel)) < SYSTEM_DRIVE_ACCEL * FP / 2);
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
static void superluminal_cruise(void) {
    Game g;
    Controls c = {0};
    int64_t before;
    game_init(&g, 1);
    g.docked = g.frame_body = g.target = -1;
    g.target_body = 1;
    g.pos = vec(0, 1000 * AU_METRES * FP, 0);
    g.vel = radial(sub(navigation_position(&g), g.pos), SYSTEM_DRIVE_SPEED * FP);
    g.autopilot = 1;
    game_action(&g, 'a');
    g.warp = MAX_WARP;
    before = length(sub(navigation_position(&g), g.pos));
    game_frame(&g, c, 1);
    assert(g.tick == MAX_WARP && g.drive_active && !g.dead && g.fuel > 0);
    assert(length(sub(navigation_position(&g), g.pos)) < before);
    assert(length(g.vel) > 20 * LIGHT_SPEED * FP && length(g.vel) < SYSTEM_DRIVE_SPEED * FP);
    puts("PASS superluminal cruise integration and braking at 30c with 4096x warp");
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
/* The relay moves at 8 km/s. A 2.2 km/s planet detour used to wait for it to
   lap the ship, taking over two hours even on the starter launch route. */
static void orbital_relay_route(int ship, int loaded, int drive) {
    Game g;
    Controls c = {0};
    int i, port, armed = 0, drive_used = 0;
    int budget = drive ? 50000 : loaded ? 190000 : 90000;
    int64_t peak = 0, orbital_speed;
    game_init(&g, 0x53504143);
    g.ship = ship;
    g.crew = ship_specs[ship].crew;
    if (loaded) g.cargo[0] = ship_specs[ship].capacity;
    game_action(&g, 'l');
    port = find_port(&g, "B RELAY");
    g.target = port;
    game_action(&g, 'p');
    orbital_speed = length(port_relative_velocity(&g, port));
    assert(orbital_speed > 8000LL * FP);
    for (i = 0; i < budget && g.autopilot && !g.dead; i++) {
        if (drive && !armed && drive_available(&g)) {
            game_action(&g, 'a');
            armed = g.drive_manual;
        }
        game_tick(&g, c);
        if (length(g.vel) > peak) peak = length(g.vel);
        drive_used |= g.drive_active;
        if (!drive) assert(!g.drive_manual && !g.drive_active);
    }
    printf("Hetha B Relay ship %d%s%s: %d ticks, peak %lld m/s, hull %d, fuel %d\n",
           ship, loaded ? " loaded" : "", drive ? " drive" : "", i,
           (long long)(peak / FP), g.hull, g.fuel);
    fflush(stdout);
    assert(g.docked == port && !g.autopilot && g.hull == 100 && g.fuel > 0);
    assert(peak > orbital_speed && g.clearance == port);
    assert(length(sub(g.vel, port_relative_velocity(&g, port))) < 8 * FP);
    assert(!drive || (armed && drive_used));
}
/* The point above a surface pad is the destination's own approach, not a
   detour: an armed drive must carry the cruise instead of ordinary thrust. */
static void surface_port_drive(void) {
    Game g;
    Controls c = {0};
    int frames, drive = 0, armed = 0;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    g.target = find_port(&g, "D REACH");
    g.target_body = -1;
    g.autopilot = 1;
    game_action(&g, 'a');
    assert(!g.drive_manual); /* refused inside the departure port's zone */
    g.warp = MAX_WARP;
    for (frames = 0; frames < 20000 && g.autopilot && !g.dead; frames++) {
        /* Arm once, as soon as the DRIVE button becomes available. */
        if (!armed && drive_available(&g)) { game_action(&g, 'a'); armed = g.drive_manual; }
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
/* Cleared at the gate above a pad, the ship levels its nose and wings with the
   pad and gravity brings it down onto its gear. It used to dive nose-first at
   the pad and snap down from up to 35 m. */
static int64_t along_axis(Vec a, Vec n) {
    return (a.x * n.x + a.y * n.y + a.z * n.z) / 16384;
}
static void level_pad_touchdown(const char *name) {
    Game g;
    Controls c = {0};
    int i, port, steepest = 0;
    int64_t height = 0, sink = 0;
    Vec n, nose, right, up;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    port = find_port(&g, name);
    n = g.ports[port].normal;
    g.frame_body = g.body = g.ports[port].body;
    g.pos = add(g.ports[port].pos, scale(n, 2000 * FP, 16384));
    g.vel = port_relative_velocity(&g, port);
    g.target = port;
    g.target_body = -1;
    assert(request_clearance(&g, port));
    g.dock_stage = 1;
    g.pitch = -250; /* nose down, as the approach leaves it */
    g.autopilot = 1;
    for (i = 0; i < 300 * HZ && g.autopilot && !g.dead; i++) {
        Vec off = sub(g.pos, g.ports[port].pos);
        n = g.ports[port].normal;
        height = along_axis(off, n);
        sink = -along_axis(sub(g.vel, port_relative_velocity(&g, port)), n);
        if (height < 300 * FP) {
            int64_t dip = llabs(along_axis(forward(g.yaw, g.pitch), n));
            if (dip > steepest) steepest = (int)dip;
        }
        game_tick(&g, c);
    }
    height -= sink / HZ; /* where the final tick carried the ship */
    n = g.ports[port].normal;
    nose = forward(g.yaw, g.pitch);
    right = vec(cosi(g.yaw), 0, -sini(g.yaw));
    up = vec(-(int64_t)sini(g.pitch) * sini(g.yaw) / 16384, cosi(g.pitch),
             -(int64_t)sini(g.pitch) * cosi(g.yaw) / 16384);
    up = add(scale(right, sini(g.roll), 16384), scale(up, cosi(g.roll), 16384));
    printf("%s settle: %d ticks, touchdown %lld cm at %lld cm/s, nose %lld, up %lld\n", name, i,
           (long long)(height * 100 / FP), (long long)(sink * 100 / FP),
           (long long)along_axis(nose, n), (long long)along_axis(up, n));
    assert(g.docked == port && g.hull == 100);
    assert(height < 1 * FP && sink > 0 && sink < 4 * FP);
    assert(steepest < 600 && llabs(along_axis(nose, n)) < 600 && along_axis(up, n) > 16200);
}
int main(void) {
    Game g;
    Controls c = {0};
    int i, system_frame = 0;
    level_pad_touchdown("B HAVEN");
    level_pad_touchdown("D REACH");
    remote_cruise();
    superluminal_cruise();
    moving_target_approach();
    orbital_relay_route(0, 0, 0);
    orbital_relay_route(5, 1, 0);
    orbital_relay_route(0, 0, 1);
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
