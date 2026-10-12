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
static int quick;
static void test_universe(void) {
    System a, b, c;
    int i;
    for (i = -100; i < 100; i++) {
        generate_system(123, (Sector){i, 0, -i}, &a);
        generate_system(456, (Sector){2, 3, 4}, &c);
        generate_system(123, (Sector){i, 0, -i}, &b);
        assert(!memcmp(&a, &b, sizeof(a)));
        assert(a.count == 0 || (a.count >= 4 && a.count <= MAX_PLANETS));
        if (a.count)
            assert(a.planets[0].radius >= 2500000);
    }
    generate_system(0x53504143, (Sector){0, 0, 0}, &a);
    assert(!strcmp(a.name, "HETHA") && a.seed == 1851271993U);
    assert(a.star[0].radius == 696000000);
    assert(a.planets[0].orbit == AU_METRES / 3);
    assert(a.planets[0].type == OCEAN && a.planets[0].parent == -1);
    assert(a.count == 5 && a.planets[0].radius == 8202371 && a.planets[1].radius == 5998702);
    {
        Vec p = body_position(&a, 1, 3600 * HZ);
        assert(p.x == -83807737285LL && p.y == 1132230113LL && p.z == -92285203750LL);
    }
    puts("PASS deterministic generation, terrain and cross-CPU golden values");
}
static void test_world(void) {
    Game g, h;
    System s;
    Controls c = {0};
    int i, j, empty = 0, binaries = 0, moons = 0, rings = 0;
    Vec previous;
    for (i = 1; i <= 40; i++) {
        generate_system((unsigned)i, (Sector){1000, 0, 1000}, &s);
        empty += system_port_count(&s) == 0;
        binaries += s.stars == 2;
        for (j = 0; j < s.count; j++) {
            const Planet *p = &s.planets[j];
            Vec a = body_position(&s, j, 0), b = body_position(&s, j, HZ * 3600);
            assert(length(sub(a, b)) > 1000);
            assert(p->radius >= 300000 && p->orbit > p->radius);
            if (p->parent >= 0) {
                int64_t orbital_radius = length(sub(a, body_position(&s, p->parent, 0)));
                assert(llabs(orbital_radius - p->orbit) < p->orbit / 10000);
                moons++;
            }
            rings += p->rings;
            if (p->atmosphere) {
                assert(atmosphere_density(p, 0) > atmosphere_density(p, p->atmosphere / 2));
                assert(!atmosphere_density(p, p->atmosphere));
            }
        }
    }
    assert(empty > 20 && binaries && moons && rings);
    assert(galaxy_density((Sector){0, 0, 0}) > galaxy_density((Sector){10000, 2000, 0}));
    assert(galaxy_density((Sector){40000, 0, 0}) == 1);
    game_init(&g, 123);
    previous = g.pos;
    for (i = 0; i < 50; i++)
        game_tick(&g, c);
    assert(g.docked == 0 && g.pos.x != previous.x);
    assert(!memcmp(&g.pos, &g.ports[0].pos, sizeof(Vec)));
    /* Clearance queues cannot grant an occupied station, then release in
       simulated time. The annulus is solid while its axial aperture is open. */
    g.tick = 103 * HZ;
    update_world(&g);
    g.clearance = -1;
    assert(!request_clearance(&g, 1));
    assert(g.ports[1].busy_until > g.tick);
    g.tick = g.ports[1].busy_until;
    update_world(&g);
    assert(request_clearance(&g, 1));
    {
        Vec axis = g.ports[1].normal;
        Vec tangent = unit(vec(axis.y, -axis.x, 0), 16384);
        Vec center = g.ports[1].pos;
        Vec movement = scale(port_relative_velocity(&g, 1), 1, HZ);
        assert(!station_impact(&g, sub(add(center, scale(axis, 1000 * FP, 16384)), movement),
                               sub(center, scale(axis, 1000 * FP, 16384)), 1));
        center = add(center, scale(tangent, 160 * FP, 16384));
        assert(station_impact(&g, sub(add(center, scale(axis, 1000 * FP, 16384)), movement),
                              sub(center, scale(axis, 1000 * FP, 16384)), 1));
    }
    game_init(&g, 123);
    game_action(&g, 'l');
    g.autopilot = 1;
    g.fuel = 0;
    game_tick(&g, c);
    assert(!g.autopilot && safe_warp(&g) == 1);
    game_init(&g, 123);
    game_action(&g, 'l');
    g.autopilot = 1;
    g.system.planets[0].gravity = 80;
    game_tick(&g, c);
    assert(!g.autopilot && safe_warp(&g) == 1);
    /* A transition to the system frame preserves inertial velocity. */
    game_init(&g, 123);
    game_action(&g, 'l');
    g.assist = 0;
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].influence + 1000000) * FP, 0));
    previous = add(g.vel, body_velocity(&g.system, 0, g.tick + 1));
    game_tick(&g, c);
    assert(g.frame_body == -1);
    assert(length(sub(g.vel, previous)) < FP);
    /* Journals and orbital/approach state survive the portable format. */
    game_init(&g, 123);
    for (i = 1000; i < 1100; i++) {
        generate_system(g.seed, (Sector){i, 0, 1000}, &s);
        if (s.count && !system_port_count(&s))
            break;
    }
    assert(i < 1100);
    setup_system(&g, s.sector);
    g.tick = 3600 * HZ;
    update_world(&g);
    g.docked = -1;
    g.target = -1;
    g.target_body = 0;
    g.pos = navigation_position(&g);
    g.vel = vec(0, 0, 0);
    record_exploration(&g);
    assert(g.explored_count == 2);
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(!memcmp(g.explored, h.explored, sizeof(g.explored)));
    assert(!memcmp(&g.pos, &h.pos, sizeof(Vec)) && g.target_body == h.target_body);
    assert(!memcmp(&g.system.planets[0].center, &h.system.planets[0].center, sizeof(Vec)));
    game_action(&h, 't'); /* No ports must preserve a usable body target. */
    if (!h.nports)
        assert(h.target_body == 0);
    remove(TEST_SAVE);
    remove(TEST_SAVE ".bak");
    puts("PASS orbits, hierarchy, density, atmosphere, queues, walls, authority, frames and "
         "exploration saves");
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
    for (i = 0; i < 500000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    printf("autopilot ticks=%lu dock=%d hull=%d altitude=%ld speed=%ld\n", (unsigned long)g.tick,
           g.docked, g.hull, (long)altitude_at(&g, g.pos), (long)(length(g.vel) / FP));
    assert(!g.dead);
    assert(g.docked == 1);
    game_action(&g, 'l');
    g.target = 0;
    g.autopilot = 1;
    g.warp = 256;
    for (i = 0; i < 500000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    printf("return tick%lu hull%d alt%ld speed%ld dst%ld fuel%d msg%s\n", (unsigned long)g.tick,
           g.hull, (long)altitude_at(&g, g.pos), (long)(length(g.vel) / FP),
           (long)(length(sub(g.pos, g.ports[0].pos)) / FP), g.fuel, g.message);
    assert(!g.dead && g.docked == 0);
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
    g.fuel_burn = 12345;
    assert(save_game(&g, TEST_SAVE));
    g.credits += 17;
    assert(save_game(&g, TEST_SAVE));
    game_init(&h, 0);
    assert(load_game(&h, TEST_SAVE));
    assert(g.seed == h.seed && g.credits == h.credits && g.contracts[0].id == h.contracts[0].id &&
           g.pos.y == h.pos.y && g.fuel_burn == h.fuel_burn);
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
    assert(render_init(&f));
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
        g.pos = add(
            scale(g.system.planets[0].center, FP, 1),
            vec(i * 100LL * FP, (g.system.planets[0].radius + 20LL + i * 3000) * FP, -500 * FP));
        g.yaw = i * 51;
        g.pitch = i * 20 - 190;
        g.page = i % 8;
        g.camera_mode = i % 3;
        render_game(&g, &f);
    }
    free(f.pixels);
    render_shutdown();
    puts("PASS planar conversion and varied renderer views");
}
static void test_routes(void) {
    Game g;
    Controls c = {0};
    int seed, t, i, routes = 0;
    unsigned types = 0, port_types = 0;
    /* End-to-end terminal routes use generated surface/orbital ports on every
       inhabited body, including moons and gas giants. The full launch/return
       route above exercises the long atmospheric leg. */
    for (seed = 1; seed <= 8; seed++) {
        game_init(&g, (unsigned)seed);
        for (t = 0; t < g.nports; t++) {
            int body = g.ports[t].body;
            if (quick && (port_types & (1U << g.system.planets[body].type)))
                continue;
            port_types |= 1U << g.system.planets[body].type;
            g.docked = -1;
            g.body = g.frame_body = body;
            g.pos = add(g.ports[t].pos,
                        scale(g.ports[t].normal, (g.ports[t].surface ? 1000 : 3000) * FP, 16384));
            g.vel = port_relative_velocity(&g, t);
            g.target = t;
            g.target_body = -1;
            g.autopilot = 1;
            g.dock_stage = 0;
            g.clearance = -1;
            g.warp = 256;
            g.fuel = g.hull = 100;
            g.dead = 0;
            for (i = 0; i < 20000 && !g.dead && g.docked < 0; i++) {
                game_frame(&g, c, 1);
                if (quick && i % 1000 == 0)
                    printf("quick port %d tick %lu distance %ld stage %d\n", t,
                           (unsigned long)g.tick, (long)(length(sub(g.pos, g.ports[t].pos)) / FP),
                           g.dock_stage);
            }
            if (g.dead || g.docked != t)
                printf("route seed%d body%d type%d port%d dock%d stage%d dst%ld\n", seed, body,
                       g.system.planets[body].type, t, g.docked, g.dock_stage,
                       (long)(length(sub(g.pos, g.ports[t].pos)) / FP));
            assert(!g.dead && g.docked == t && g.hull == 100);
            routes++;
        }
    }
    for (seed = 1; seed <= 20; seed++) {
        game_init(&g, (unsigned)seed);
        for (t = 0; t < g.system.count; t++) {
            if (quick && (types & (1U << g.system.planets[t].type)))
                continue;
            g.docked = -1;
            g.body = g.frame_body = t;
            g.target = -1;
            g.target_body = t;
            g.pos = add(navigation_position(&g), vec(0, 1000 * FP, 0));
            g.vel = vec(0, 0, 0);
            g.autopilot = 1;
            g.fuel = g.hull = 100;
            g.dead = 0;
            g.warp = 256;
            for (i = 0; i < 5000 && !g.dead && g.autopilot; i++)
                game_frame(&g, c, 1);
            assert(!g.dead && !g.autopilot);
            assert(length(sub(g.pos, navigation_position(&g))) < 100 * FP);
            types |= 1U << g.system.planets[t].type;
        }
    }
    assert(types == (1U << BODY_TYPES) - 1);
    printf("PASS %d generated port arrivals and all six body types at 256x\n", routes);
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
            for (i = 0; i < 200000 && g.contracts[slot].state == ACTIVE && !g.dead; i++)
                game_frame(&g, c, 1);
        } else {
            g.pos = add(scale(g.system.planets[0].center, FP, 1),
                        vec(0,
                            (g.system.planets[0].radius +
                             (type == AIR_PATROL || type == SURVEY ? 2000LL : 18000LL)) *
                                FP,
                            0));
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
    g.pos = add(g.ports[1].pos, scale(g.ports[1].normal, 3000 * FP, 16384));
    g.vel = port_relative_velocity(&g, 1);
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
    h.pos = add(scale(h.system.planets[0].center, FP, 1),
                vec(0, (int64_t)(h.system.planets[0].radius + 500) * FP, 0));
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
int main(int argc, char **argv) {
    quick = argc > 1 && !strcmp(argv[1], "--quick");
    setvbuf(stdout, NULL, _IONBF, 0);
    puts(quick ? "SPACE QUICK SMOKE SUITE" : "SPACE FULL CORE SUITE");
    test_universe();
    test_world();
    test_collision();
    test_economy();
    if (!quick)
        test_auto();
    test_ai();
    test_save();
    test_render();
    test_routes();
    if (!quick)
        test_missions();
    test_safety();
    puts(quick ? "All Space quick smoke tests passed." : "All Space tests passed.");
    return 0;
}
