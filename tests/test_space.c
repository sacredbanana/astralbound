/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __AMIGAOS3__
unsigned long __stack = 131072;
#define TEST_SAVE "space-test.sav"
#else
#define TEST_SAVE "build/test.sav"
#endif
static void test_universe(void) {
    System a, b, c;
    int i;
    for (i = -100; i < 100; i++) {
        generate_system(123, (Sector){i, 0, -i}, &a);
        generate_system(456, (Sector){2, 3, 4}, &c);
        generate_system(123, (Sector){i, 0, -i}, &b);
        assert(!memcmp(&a, &b, sizeof(a)));
        assert(a.count >= 2 && a.count <= 6);
        assert(a.planets[0].radius >= 60000);
    }
    generate_system(0x53504143, (Sector){0, 0, 0}, &a);
    assert(!strcmp(a.name, "HETHA") && a.seed == 1851271993U && a.count == 4);
    assert(a.planets[0].radius == 70964 && a.planets[1].radius == 84212);
    assert(a.planets[1].center.x == -2534545 && a.planets[1].center.z == -1604919);
    assert(terrain_height(&a.planets[0], vec(5000, 9000, 3000)) == 367);
    puts("PASS deterministic generation, terrain and cross-CPU golden values");
}
static void test_collision(void) {
    assert(swept_sphere(vec(-10000, 0, 0), vec(10000, 0, 0), vec(0, 0, 0), 30));
    assert(!swept_sphere(vec(-10000, 31, 0), vec(10000, 31, 0), vec(0, 0, 0), 30));
    assert(swept_sphere(vec(0, 0, 0), vec(0, 0, 0), vec(0, 0, 0), 30));
    puts("PASS swept collisions include fast crossings and stationary overlap");
}
static void test_economy(void) {
    Game g;
    int credits;
    game_init(&g, 1);
    credits = g.credits;
    assert(trade(&g, 0, 1));
    assert(trade(&g, 0, 0));
    assert(g.credits <= credits);
    assert(accept_mission(&g, 1));
    assert(!accept_mission(&g, 1));
    g.docked = -1;
    assert(!trade(&g, 0, 1));
    assert(!accept_mission(&g, 2));
    puts("PASS trading, reserved cargo and duplicate-contract rejection");
}
static void test_auto(void) {
    Game g;
    int i;
    Controls c = {0};
    game_init(&g, 123);
    game_action(&g, 'l');
    game_action(&g, 'p');
    g.warp = 256;
    for (i = 0; i < 120000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    printf("autopilot ticks=%lu dock=%d hull=%d altitude=%ld speed=%ld\n", (unsigned long)g.tick,
           g.docked, g.hull, (long)(length(g.pos) / FP - g.system.planets[0].radius),
           (long)(length(g.vel) / FP));
    assert(!g.dead);
    assert(g.docked == 1);
    game_action(&g, 'l');
    g.target = 0;
    g.autopilot = 1;
    g.warp = 256;
    for (i = 0; i < 120000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    assert(!g.dead);
    assert(g.docked == 0);
    puts("PASS 256x request: launch, orbital arrival and surface return");
}
static void test_ai(void) {
    Vec p = vec(0, 0, 3000 * FP), v = vec(0, 0, -500 * FP);
    int i;
    int64_t nearest = length(p);
    for (i = 0; i < 15 * HZ; i++) {
        Vec a = avoidance(scale(p, -1, 1), scale(v, -1, 1), 50 * FP, 300 * FP);
        v = add(v, scale(a, 1, HZ));
        p = add(p, scale(v, 1, HZ));
        if (length(p) < nearest)
            nearest = length(p);
    }
    printf("head-on closest approach: %ld m\n", (long)(nearest / FP));
    assert(nearest > 100 * FP);
    puts("PASS head-on AI breaks away");
}
static void test_save(void) {
    Game g, h, before;
    FILE *f;
    game_init(&g, 998);
    accept_mission(&g, 0);
    assert(save_game(&g, TEST_SAVE));
    g.credits += 17;
    assert(save_game(&g, TEST_SAVE));
    game_init(&h, 0);
    assert(load_game(&h, TEST_SAVE));
    assert(g.seed == h.seed && g.credits == h.credits && g.contracts[0].id == h.contracts[0].id &&
           g.pos.y == h.pos.y);
    before = h;
    f = fopen(TEST_SAVE, "r+b");
    assert(f);
    fseek(f, 80, SEEK_SET);
    fputc(255, f);
    fclose(f);
    assert(!load_game(&h, TEST_SAVE));
    assert(!memcmp(&h, &before, sizeof(h)));
    remove(TEST_SAVE);
    remove(TEST_SAVE ".bak");
    puts("PASS portable save roundtrip and corruption leaves live game unchanged");
}
static void test_render(void) {
    Game g;
    Frame f = {320, 256, 1, 0, {0}};
    uint8_t src[320 * 3], dst[320 * 3];
    int i, p, b;
    game_init(&g, 8);
    f.pixels = malloc(320 * 256);
    assert(f.pixels);
    render_init(&f);
    for (i = 0; i < 320 * 3; i++)
        src[i] = (uint8_t)hash32((unsigned)i);
    {
        int depth;
        for (depth = 1; depth <= 8; depth++) {
            c2p(src, dst, 320, 3, depth);
            for (i = 0; i < 320 * 3; i++) {
                unsigned n = 0;
                for (p = 0; p < depth; p++) {
                    b = (dst[p * 120 + i / 8] >> (7 - i % 8)) & 1;
                    n |= (unsigned)b << p;
                }
                assert(n == (src[i] & ((1 << depth) - 1)));
            }
        }
    }
    for (i = 0; i < 20; i++) {
        g.pos = vec(i * 100LL * FP, (g.system.planets[0].radius + 20LL + i * 3000) * FP, -500 * FP);
        g.yaw = i * 51;
        g.pitch = i * 20 - 190;
        g.page = i % 8;
        g.camera_mode = i % 3;
        render_game(&g, &f);
    }
    free(f.pixels);
    puts("PASS planar conversion and varied renderer views");
}
static void test_routes(void) {
    Game g;
    Controls c = {0};
    int seed, t, i;
    for (seed = 1; seed <= 3; seed++)
        for (t = 2; t < 6; t++) {
            game_init(&g, (unsigned)seed);
            game_action(&g, 'l');
            g.target = t;
            g.autopilot = 1;
            g.warp = 256;
            for (i = 0; i < 20000 && !g.dead && g.docked < 0; i++)
                game_frame(&g, c, 1);
            assert(!g.dead && g.docked == t);
            assert(g.hull == 100);
        }
    puts("PASS 12 surface, around-planet and interplanetary routes at 256x");
}
static void test_missions(void) {
    Game g;
    Controls c = {0};
    int type, i, old, slot;
    for (type = 0; type < MISSION_TYPES; type++) {
        game_init(&g, 37);
        assert(accept_mission(&g, type));
        slot = 0;
        old = g.credits;
        game_action(&g, 'l');
        if (type == COURIER || type == FREIGHT) {
            g.pos = g.ports[1].pos;
            assert(jump(&g, g.contracts[slot].destination));
            g.target = g.contracts[slot].port;
            g.autopilot = 1;
            g.warp = 256;
            for (i = 0; i < 20000 && g.contracts[slot].state == ACTIVE && !g.dead; i++)
                game_frame(&g, c, 1);
        } else {
            g.pos = vec(0,
                        (g.system.planets[0].radius +
                         (type == AIR_PATROL || type == SURVEY ? 2000LL : 18000LL)) *
                            FP,
                        0);
            g.vel = vec(0, 0, 0);
            c.fire = type != SURVEY;
            for (i = 0; i < 30 * HZ && g.contracts[slot].state == ACTIVE && !g.dead; i++)
                game_tick(&g, c);
        }
        if (g.contracts[slot].state != COMPLETE)
            printf("mission %d state%d dead%d dock%d progress%d\n", type, g.contracts[slot].state,
                   g.dead, g.docked, g.contracts[slot].progress);
        assert(g.contracts[slot].state == COMPLETE);
        assert(g.credits > old);
        old = g.credits;
        game_tick(&g, c);
        assert(g.credits == old);
    }
    puts("PASS all six contract types complete through flight/combat and pay once");
}
static void test_safety(void) {
    Game g, h;
    Controls c = {0};
    int i;
    game_init(&g, 3);
    game_action(&g, 'l');
    g.pos = g.ports[1].pos;
    g.vel = vec(0, 0, 0);
    g.autopilot = 1;
    g.target = 3;
    h = g;
    g.warp = 1;
    h.warp = 256;
    for (i = 0; i < 300; i++)
        game_frame(&g, c, 1);
    while (h.tick < g.tick)
        game_tick(&h, c);
    assert(g.pos.x == h.pos.x && g.pos.y == h.pos.y && g.pos.z == h.pos.z);
    assert(g.vel.x == h.vel.x && g.vel.y == h.vel.y && g.vel.z == h.vel.z);
    h.pos = vec(0, (h.system.planets[0].radius + 500) * FP, 0);
    h.warp = 256;
    assert(safe_warp(&h) == 1);
    h.npc[0].active = 1;
    h.npc[0].pos = add(h.pos, vec(0, 0, 1000 * FP));
    assert(safe_warp(&h) == 1);
    h.pos = vec(0, 100000000000LL, 0);
    h.npc[0].pos = vec(0, 0, 0);
    h.npc[0].vel = vec(10000000, 0, 0);
    game_tick(&h, c);
    puts("PASS compression preserves tick trajectory and hazards clamp time");
}
int main(void) {
    test_universe();
    test_collision();
    test_economy();
    test_auto();
    test_ai();
    test_save();
    test_render();
    test_routes();
    test_missions();
    test_safety();
    puts("All Space tests passed.");
    return 0;
}
