/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "galaxy.h"
#include "flight_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __AMIGAOS3__
unsigned long __stack = 1048576;
#define TEST_SAVE "astralbound-test.sav"
#else
#define TEST_SAVE "build/test.sav"
#endif
static int quick;
static void test_hetha_departure(void) {
    Game g;
    Controls c = {0};
    int i, yaw = 0, pitch = 0;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    g.target = -1;
    g.target_body = 2;
    assert(!strcmp(g.system.planets[2].name, "HETHA D"));
    aim_angles(sub(navigation_position(&g), g.pos), &yaw, &pitch);
    assert(pitch < 0 && pitch > -4);
    g.autopilot = 1;
    /* The original route levelled off at about 2 km and 450 m/s. Run fixed
       ticks so this also covers the uncompressed atmospheric departure. */
    for (i = 0; i < 12000 && !g.dead &&
         altitude_at(&g, g.pos) < g.system.planets[0].atmosphere + 100000; i++)
        game_tick(&g, c);
    assert(!g.dead && g.autopilot && g.hull == 100 && g.fuel > 0);
    assert(altitude_at(&g, g.pos) >= g.system.planets[0].atmosphere + 100000);
    assert(length(g.vel) > 600 * FP);
    puts("PASS Hetha D downward bearing and atmospheric departure");
}
/* FPU builds take a square-root shortcut; it must match the integer loop. */
static uint64_t reference_isqrt(uint64_t n) {
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > n)
        b >>= 2;
    for (; b; b >>= 2) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
    }
    return r;
}
static void test_isqrt(void) {
    uint64_t n, k;
    int i, shift;
    for (n = 0; n < 70000; n++)
        assert(isqrt64(n) == reference_isqrt(n));
    for (shift = 0; shift < 32; shift++)
        for (i = -2; i <= 2; i++) {
            k = ((uint64_t)1 << shift) + (uint64_t)(int64_t)i;
            if (k > 0xffffffffU) continue;
            for (n = k * k - (k ? 1 : 0); n <= k * k + 1; n++)
                assert(isqrt64(n) == reference_isqrt(n));
        }
    for (k = 0xfffffff0U; k <= 0xffffffffU; k++) {
        assert(isqrt64(k * k) == k && isqrt64(k * k - 1) == k - 1);
        if (k < 0xffffffffU) assert(isqrt64(k * k + 2 * k) == k);
    }
    assert(isqrt64(UINT64_MAX) == 0xffffffffU);
    for (i = 0, n = 0x9e3779b97f4a7c15ULL; i < 200000; i++) {
        n = n * 6364136223846793005ULL + 1442695040888963407ULL;
        assert(isqrt64(n >> (i % 64)) == reference_isqrt(n >> (i % 64)));
    }
    puts("PASS integer square roots match the reference on every CPU build");
}
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
static void test_landforms(void) {
    Planet p = {0};
    int type, u, v, seed;
    p.radius=3000000; p.parent=-1; set_rotation(&p,0);
    for (type=0;type<BODY_TYPES;type++) {
        int lo=TERRAIN_MAX_HEIGHT, hi=-TERRAIN_MAX_HEIGHT, slopes=0;
        p.type=(BodyType)type;
        for (seed=1;seed<=3;seed++) {
            p.seed=(uint32_t)seed;
            for (v=-65536;v<=65536;v+=4096) for (u=8192;u<=139264;u+=4096) {
                Vec q=vec(u,p.radius,v);
                int h=terrain_elevation(&p,q), next=terrain_elevation(&p,add(q,vec(256,0,0)));
                assert(h<=TERRAIN_MAX_HEIGHT);
                assert(abs(h-terrain_elevation(&p,scale(q,2,1)))<10);
                assert(abs(h-next)<600); /* No cell/face cliffs or spikes. */
                if (h<lo) lo=h;
                if (h>hi) hi=h;
                slopes+=abs(h-next)>10;
            }
            assert(terrain_elevation(&p,vec(0,p.radius,0))==0);
            assert(terrain_elevation(&p,forward(120,180))==0);
        }
        printf("terrain %s: %d..%d m, %d resolved slopes\n",body_names[type],lo,hi,slopes);
        if (type==GAS_GIANT) assert(lo==0 && hi==0);
        else {
            assert(hi-lo>1000 && hi>1400 && slopes>10);
            if (type!=OCEAN) assert(lo>=0);
        }
    }
    /* Flight safety must see a mountain above the old 1/2 km shortcuts. */
    {
        Game g; Planet *body; Vec q=vec(0,0,0), from,to; int h=0;
        game_init(&g,1); body=&g.system.planets[0]; body->type=ROCKY;
        set_rotation(body,0);
        for (u=8192;u<200000 && h<2400;u+=256) {
            q=vec(u,body->radius,0); h=terrain_height(body,q);
        }
        assert(h>=2400);
        from=scale(add(body->center,radial(q,body->radius+h+300)),FP,1);
        to=scale(add(body->center,radial(q,body->radius+h-100)),FP,1);
        assert(terrain_impact(&g,from,to,0));
        assert(!terrain_impact(&g,from,from,0));
        assert(llabs(altitude_at(&g,from)-300)<5);
    }
    /* Older saves may place a commander below newly generated mountains. */
    {
        Game g,h; Planet *body; Vec q; int height;
        game_init(&g,1); body=&g.system.planets[1];
        q=vec(122000,body->radius,-10000); height=terrain_height(body,q);
        assert(height>0);
        g.docked=-1; g.body=g.frame_body=1;
        g.pos=scale(add(body->center,radial(q,body->radius+height-300)),FP,1);
        assert(save_game(&g,TEST_SAVE) && load_game(&h,TEST_SAVE));
        assert(llabs(altitude_at(&h,h.pos)-50)<5);
        g.docked=-2;
        assert(save_game(&g,TEST_SAVE) && load_game(&h,TEST_SAVE));
        assert(llabs(altitude_at(&h,h.pos)-12)<5);
        remove(TEST_SAVE); remove(TEST_SAVE ".bak");
    }
    puts("PASS type-specific metre-scale landforms, pads, bounded slopes and tall-peak flight safety");
}
static void test_world(void) {
    Game g, h;
    System s;
    Controls c = {0};
    int i, j, empty = 0, binaries = 0, moons = 0, rings = 0;
    Vec previous;
    for (i = 1; i <= 40; i++) {
        Sector sector;
        assert(galaxy_system_at((unsigned)i,1000000U+(unsigned)i,&sector));
        generate_system((unsigned)i, sector, &s);
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
    assert(galaxy_density((Sector){40000, 0, 0}) == 0);
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
        Sector sector;
        assert(galaxy_system_at(g.seed,1000000U+(unsigned)i,&sector));
        generate_system(g.seed, sector, &s);
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
static void test_markets(void) {
    Game g, h, before;
    int normal, quote, other, i;
    Sector home = {0, 0, 0};
    game_init(&g, 1);
    g.credits = 100000;
    normal = market_stock(&g, 5);
    quote = price(&g, 5);
    assert(g.market_count == 0);
    assert(trade(&g, 5, 1));
    assert(market_stock(&g, 5) == normal - 1);
    assert(g.credits == 100000 - quote);
    g.docked = 1;
    other = market_stock(&g, 5);
    assert(trade(&g, 5, 1));
    assert(market_stock(&g, 5) == other - 1);
    g.docked = 0;
    assert(market_stock(&g, 5) == normal - 1 && g.market_count == 2);
    setup_system(&g, (Sector){1, 0, 1});
    setup_system(&g, home);
    g.docked = 0;
    assert(market_stock(&g, 5) == normal - 1);
    for (i = 0; i < 10 && market_stock(&g, 5); i++)
        assert(trade(&g, 5, 1));
    assert(price(&g, 5) > quote);
    assert(save_game(&g, TEST_SAVE));
    assert(load_game(&h, TEST_SAVE));
    assert(h.market_count == g.market_count && market_stock(&h, 5) == market_stock(&g, 5));
    assert(price(&h, 5) == price(&g, 5));
    g.tick += MARKET_INTERVAL - 1;
    assert(market_stock(&g, 5) == normal - 11);
    g.tick++;
    assert(market_stock(&g, 5) == normal - 10);
    /* Trade materializes recovery for every commodity and preserves sub-hour time. */
    assert(trade(&g, 0, 1));
    assert(market_stock(&g, 5) == normal - 10);
    g.tick += MARKET_INTERVAL * 100;
    assert(market_stock(&g, 5) == normal && price(&g, 5) == quote);
    g.markets[0].stock[5] = 0;
    g.markets[0].last_tick = g.tick;
    before = g;
    assert(!trade(&g, 5, 1));
    assert(!memcmp(&g, &before, sizeof(g)));
    quote = sell_price(&g, 5);
    assert(trade(&g, 5, 0));
    assert(g.credits == before.credits + quote && market_stock(&g, 5) == 1);
    g.credits = 0;
    before = g;
    assert(!trade(&g, 0, 1));
    assert(!trade(&g, -1, 1) && !trade(&g, GOODS, 0));
    assert(price(&g, GOODS) == 0 && market_stock(&g, -1) == 0);
    assert(!memcmp(&g, &before, sizeof(g)));
    g.credits = 100000000;
    before = g;
    assert(!trade(&g, 5, 0) && !memcmp(&g, &before, sizeof(g)));
    g.credits = 100000;
    g.markets[0].stock[5] = normal + 3;
    g.tick += MARKET_INTERVAL;
    assert(market_stock(&g, 5) == normal + 2);
    g.markets[0].last_tick = g.tick + 1;
    assert(!save_game(&g, TEST_SAVE));
    g.markets[0].last_tick = g.tick;
    g.markets[1] = g.markets[0];
    assert(!save_game(&g, TEST_SAVE));
    game_init(&g, 1);
    for (i = 0; i < MAX_MARKETS; i++) {
        g.markets[i].sector = (Sector){i + 1, 0, 0};
        g.markets[i].port = 0;
    }
    g.market_count = MAX_MARKETS;
    for (i = 1; i < MAX_EXPLORED; i++) {
        g.explored[i].sector = (Sector){i, 0, 0};
        g.explored[i].visited = 1;
    }
    g.explored_count = MAX_EXPLORED;
    before = g;
    assert(!trade(&g, 0, 1));
    assert(!memcmp(&g, &before, sizeof(g)));
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(h.market_count == MAX_MARKETS && h.explored_count == MAX_EXPLORED);
    for (i = 0; i < 32; i++) {
        int good;
        game_init(&g, (uint32_t)i);
        g.credits = 100000;
        g.system.government = 1;
        for (good = 0; good < GOODS; good++) {
            int credits = g.credits, stock = market_stock(&g, good);
            assert(trade(&g, good, 1) && trade(&g, good, 0));
            assert(g.credits <= credits && market_stock(&g, good) == stock);
        }
    }
    remove(TEST_SAVE);
    remove(TEST_SAVE ".bak");
    puts("PASS independent persistent markets, scarcity, replenishment and atomic refusals");
}
static void put_word(unsigned char *p, uint32_t n) {
    p[0] = (unsigned char)(n >> 24);
    p[1] = (unsigned char)(n >> 16);
    p[2] = (unsigned char)(n >> 8);
    p[3] = (unsigned char)n;
}
static void test_legacy_market_save(void) {
    Game g, h, before;
    unsigned char data[16384];
    size_t len, i;
    uint32_t sum = 2166136261U;
    int stock;
    FILE *f;
    game_init(&g, 17);
    assert(trade(&g, 0, 1));
    stock = market_stock(&g, 0);
    assert(save_game(&g, TEST_SAVE));
    f = fopen(TEST_SAVE, "rb");
    assert(f);
    len = fread(data, 1, sizeof(data), f);
    fclose(f);
    /* Version 3 ends before the market ledger; its common prefix includes stock. */
    for (i = 16; i + 4 < len; i += 4) if (data[i] == 0x43 && data[i+1] == 0x41 &&
        data[i+2] == 0x52 && data[i+3] == 0x36) { len = i; break; }
    len -= 4 + 52 * g.market_count;
    put_word(data + 4, GENERATOR_VERSION);
    put_word(data + 8, (uint32_t)(len - 16));
    for (i = 16; i < len; i++)
        sum = (sum ^ data[i]) * 16777619U;
    put_word(data + 12, sum);
    f = fopen(TEST_SAVE, "wb");
    assert(f && fwrite(data, 1, len, f) == len);
    fclose(f);
    assert(load_game(&h, TEST_SAVE));
    assert(h.market_count == 1 && market_stock(&h, 0) == stock);
    assert(h.credits == g.credits && h.cargo[0] == g.cargo[0]);
    /* A valid checksum still cannot admit malformed ledger state. */
    assert(save_game(&h, TEST_SAVE));
    f = fopen(TEST_SAVE, "rb");
    assert(f);
    len = fread(data, 1, sizeof(data), f);
    fclose(f);
    for (i = 16; i + 4 < len; i += 4) if (data[i] == 0x43 && data[i+1] == 0x41 &&
        data[i+2] == 0x52 && data[i+3] == 0x36) break;
    if (i < 56 || i >= len) abort();
    put_word(data + i - 56, MAX_MARKETS + 1);
    sum = 2166136261U;
    for (i = 16; i < len; i++)
        sum = (sum ^ data[i]) * 16777619U;
    put_word(data + 12, sum);
    f = fopen(TEST_SAVE, "wb");
    assert(f && fwrite(data, 1, len, f) == len);
    fclose(f);
    before = h;
    assert(!load_game(&h, TEST_SAVE) && !memcmp(&h, &before, sizeof(h)));
    remove(TEST_SAVE);
    remove(TEST_SAVE ".bak");
    puts("PASS version 3 commander migration and checksummed malformed market rejection");
}
static void test_auto(void) {
    Game g;
    int i;
    Controls c = {0};
    game_init(&g, 123);
    game_action(&g, 'l');
    game_action(&g, 'p');
    g.warp = MAX_WARP;
    for (i = 0; i < 500000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    printf("autopilot ticks=%lu dock=%d hull=%d altitude=%ld speed=%ld\n", (unsigned long)g.tick,
           g.docked, g.hull, (long)altitude_at(&g, g.pos), (long)(length(g.vel) / FP));
    assert(!g.dead);
    assert(g.docked == 1);
    game_action(&g, 'l');
    g.target = 0;
    g.autopilot = 1;
    g.warp = MAX_WARP;
    for (i = 0; i < 500000 && g.docked < 0 && !g.dead; i++)
        game_frame(&g, c, 1);
    printf("return tick%lu hull%d alt%ld speed%ld dst%ld fuel%d msg%s\n", (unsigned long)g.tick,
           g.hull, (long)altitude_at(&g, g.pos), (long)(length(g.vel) / FP),
           (long)(length(sub(g.pos, g.ports[0].pos)) / FP), g.fuel, g.message);
    assert(!g.dead && g.docked == 0);
    puts("PASS 4096x request: launch, orbital arrival and surface return");
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
            g.warp = MAX_WARP;
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
            g.warp = MAX_WARP;
            for (i = 0; i < 5000 && !g.dead && g.autopilot; i++)
                game_frame(&g, c, 1);
            assert(!g.dead && !g.autopilot);
            assert(length(sub(g.pos, navigation_position(&g))) < 100 * FP);
            types |= 1U << g.system.planets[t].type;
        }
    }
    assert(types == (1U << BODY_TYPES) - 1);
    printf("PASS %d generated port arrivals and all six body types at 4096x\n", routes);
}
static void test_missions(void) {
    Game g;
    Controls c = {0};
    int type, i, old, slot;
    for (type = 0; type <= BOUNTY; type++) {
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
            game_action(&g, 'a'); /* Star arrivals require an interplanetary leg. */
            g.warp = MAX_WARP;
            for (i = 0; i < 200000 && g.contracts[slot].state == ACTIVE && !g.dead; i++)
                game_frame(&g, c, 1);
        } else {
            g.pos = add(scale(g.system.planets[0].center, FP, 1),
                        vec(0,
                            (g.system.planets[0].radius +
                             (type == AIR_PATROL || type == SURVEY ? 2000LL : 18000LL)) *
                                FP,
                            0));
            g.vel = surface_velocity(&g.system.planets[0],
                sub(scale(g.pos, 1, FP), g.system.planets[0].center));
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
static void test_atmosphere_speed_limit(void) {
    Game g;
    Controls c = {0};
    int assist, i;
    for (assist = 0; assist <= 1; assist++) {
        Planet *p;
        game_init(&g, 1);
        g.docked = -1; g.frame_body = g.body = 0;
        g.assist = assist; g.fuel = 0; g.tick = HZ - 1;
        memset(g.npc, 0, sizeof(g.npc));
        p = &g.system.planets[0];
        assert(p->atmosphere > 18000);
        g.pos = add(scale(p->center, FP, 1), vec(0, (p->radius + 18000LL) * FP, 0));
        g.vel = add(surface_velocity(p, sub(scale(g.pos, 1, FP), p->center)),
                    vec(0, 0, 1000000LL * FP));
        for (i = 0; i < HZ; i++) {
            Vec air;
            game_tick(&g, c);
            air = surface_velocity(p, sub(scale(g.pos, 1, FP), p->center));
            assert(length(sub(g.vel, air)) <= (ATMOSPHERE_SPEED + 1) * FP);
            assert(g.hull == 100 && g.heat == 0 && !g.dead);
        }
        /* An incoming vacuum trajectory is limited before it crosses the
           whole atmosphere or hits the surface in one tick. */
        g.pos = add(scale(p->center, FP, 1),
                    vec(0, ((int64_t)p->radius + p->atmosphere + 1000) * FP, 0));
        g.vel = vec(0, -2000000LL * FP, 0);
        game_tick(&g, c);
        assert(altitude_at(&g, g.pos) > p->atmosphere);
        assert(length(sub(g.vel, surface_velocity(p,
            sub(scale(g.pos, 1, FP), p->center)))) < (ATMOSPHERE_SPEED + 1) * FP);
        assert(g.hull == 100 && g.heat == 0 && !g.dead);
    }
    puts("PASS atmospheric speed cap, rotating air, vacuum entry and no overspeed damage");
}
static void test_entry_brake(void) {
    static const int64_t descents[] = {2000000, 1200, 300, 20};
    Game g;
    int thin, d, push, i;
    for (thin = 0; thin <= 2; thin++)
        for (d = 0; d < 4; d++)
            for (push = 0; push <= 1; push++) {
                Planet *p;
                Controls c = {0};
                int64_t interface, lowest = INT64_MAX;
                int armed = 0;
                game_init(&g, 1);
                g.docked = -1; g.frame_body = g.body = 0;
                g.assist = 0; g.tick = HZ - 1;
                memset(g.npc, 0, sizeof(g.npc));
                p = &g.system.planets[0];
                /* A thin frozen shell, then no air at all. */
                if (thin) { p->climate = FROZEN; p->atmosphere = thin == 1 ? 8000 : 0; }
                interface = p->atmosphere > TERRAIN_MAX_HEIGHT + 2 * ENTRY_BRAKE_FLOOR
                                ? p->atmosphere : TERRAIN_MAX_HEIGHT + 2 * ENTRY_BRAKE_FLOOR;
                g.pos = add(scale(p->center, FP, 1),
                            vec(0, ((int64_t)p->radius + interface + 500) * FP, 0));
                g.vel = vec(900 * FP, -descents[d] * FP, 0);
                if (push) {
                    /* Full manual thrust held straight down through the brake. */
                    aim_angles(vec(0, -1, 0), &g.yaw, &g.pitch);
                    c.thrust = 1;
                }
                for (i = 0; i < HZ * 600 && (!armed || g.entry_brake); i++) {
                    int64_t sea;
                    game_tick(&g, c);
                    armed |= g.entry_brake != 0;
                    sea = altitude_at(&g, g.pos);
                    if (sea < lowest) lowest = sea;
                    assert(!g.dead && g.hull == 100);
                }
                assert(armed && lowest >= ENTRY_BRAKE_FLOOR);
                if (getenv("BRAKE_DEBUG")) printf("brake air %d descent %lld push %d: lowest %lld m%s\n", thin, (long long)descents[d],
                                                  push, (long long)lowest, g.entry_brake ? ", still armed" : "");
                /* Drag may hold an unpowered hull high in thick air, the
                   brake still armed beneath it; a powered dive is stopped
                   at the floor itself, not high in the sky. */
                if (g.entry_brake) {
                    assert(!push);
                    continue;
                }
                assert(lowest < ENTRY_BRAKE_FLOOR + 500);
                assert(altitude_at(&g, g.pos) >= ENTRY_BRAKE_FLOOR);
                assert(g.assist && strstr(g.message, "DESCENT ARRESTED"));
                /* Released into an assisted hover rather than a free fall. */
                c.thrust = 0;
                for (i = 0; i < HZ * 5; i++) game_tick(&g, c);
                assert(altitude_at(&g, g.pos) >= ENTRY_BRAKE_FLOOR - 200);
            }
    /* Autopilot descents to surface pads are flown by guidance instead. */
    game_init(&g, 1);
    g.docked = -1; g.frame_body = g.body = 0; g.autopilot = 1; g.entry_brake = 1;
    {
        Controls c = {0};
        game_tick(&g, c);
    }
    assert(!g.entry_brake);
    puts("PASS emergency brake stops every descent 2000 m above the ground, with or without air");
}
static void test_thirty_c(void) {
    Game g, h;
    Controls c = {0};
    int i;
    assert(SYSTEM_DRIVE_SPEED == 30LL * LIGHT_SPEED);
    /* Braking from 30c still needs about 27 AU of room. */
    assert(muldiv(SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_ACCEL) > 20 * AU_METRES);
    assert(isqrt_product(SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_SPEED) <= (uint64_t)SYSTEM_DRIVE_SPEED);
    assert(isqrt_product(SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_SPEED) > (uint64_t)SYSTEM_DRIVE_SPEED - 16);
    game_init(&g, 1);
    g.docked = g.frame_body = -1;
    g.pos = vec(0, AU_METRES * 1000 * FP, 0);
    g.vel = vec(0, 0, (SYSTEM_DRIVE_SPEED - 1000000) * FP);
    game_action(&g, 'a');
    for (i = 0; i < 16 * HZ; i++) game_tick(&g, c);
    assert(!g.dead && g.drive_manual && g.drive_active);
    assert(llabs(length(g.vel) / FP - SYSTEM_DRIVE_SPEED) < 32);
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(!memcmp(&g.vel, &h.vel, sizeof(Vec)));
    game_action(&g, 'a');
    for (i = 0; i < HZ; i++) game_tick(&g, c);
    assert(length(g.vel) / FP < SYSTEM_DRIVE_SPEED - 3000000);
    /* At 30c, the AP's squared braking envelope exceeds signed 64 bits. */
    assert(length(guidance(vec(0,0,0), vec(0,0,0),
        vec(0,0,1000 * AU_METRES * FP), SYSTEM_DRIVE_ACCEL * FP,
        SYSTEM_DRIVE_SPEED * FP)) > 0);
    remove(TEST_SAVE); remove(TEST_SAVE ".bak");
    puts("PASS 30c manual ceiling, braking, saved velocity and guidance envelope");
}
static void test_system_drive(void) {
    Game g, h;
    Controls c = {0};
    int i;
    assert(length(avoidance(vec(1000000 * FP, 0, 0),
        vec(-SYSTEM_DRIVE_SPEED * FP, 0, 0), 50 * FP, 300 * FP)) > 0);
    game_init(&g, 1);
    game_action(&g, 'a');
    assert(!g.drive_manual); /* Docked lockout. */
    g.docked = -1;
    g.frame_body = -1;
    g.pos = vec(0, AU_METRES * FP, 0);
    g.vel = vec(0, 0, 0);
    game_action(&g, 'a');
    assert(g.drive_manual && g.throttle == 4000 && g.assist);
    for (i = 0; i < 5 * HZ; i++) game_tick(&g, c);
    assert(g.drive_active && length(g.vel) > 10000000LL * FP);
    assert(g.fuel >= 99 && !g.dead && safe_warp(&g) == 1);
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(!h.drive_manual && !h.drive_active && !memcmp(&g.vel, &h.vel, sizeof(Vec)));
    game_tick(&h, c);
    assert(h.drive_active); /* Restored momentum retains drive braking. */
    game_action(&g, 'a');
    for (i = 0; i < 25 * HZ; i++) game_tick(&g, c);
    assert(!g.drive_manual && !g.drive_active && length(g.vel) < 8 * FP);
    /* Port and nearby-traffic locks refuse the drive, or disarm it. */
    game_init(&g, 1);
    game_action(&g, 'l');
    assert(!drive_available(&g));
    game_action(&g, 'a');
    game_tick(&g, c);
    assert(!g.drive_manual && !g.drive_active && !g.dead);
    g.frame_body = -1;
    g.pos = vec(0, AU_METRES * FP, 0);
    g.vel = vec(0, 0, 0);
    game_action(&g, 'a');
    assert(g.drive_manual && g.throttle == 4000);
    g.npc[0].active = 1;
    g.npc[0].mission = -1;
    g.npc[0].pos = add(g.pos, vec(0, 0, 1000 * FP));
    g.npc[0].vel = vec(0, 0, 0);
    game_tick(&g, c);
    assert(!g.drive_manual && !g.throttle && !g.drive_active && length(g.vel) < 100 * FP);
    assert(!strcmp(g.message, "SYSTEM DRIVE DISENGAGED - EXCLUSION ZONE"));
    /* Running dry mid-cruise disarms the drive and zeroes the set speed. */
    game_init(&g, 1);
    g.docked = g.frame_body = -1;
    g.pos = vec(0, AU_METRES * FP, 0);
    g.vel = vec(0, 0, 0);
    game_action(&g, 'a');
    for (i = 0; i < HZ; i++) game_tick(&g, c);
    assert(g.drive_manual && g.drive_active);
    g.fuel = 0;
    game_tick(&g, c);
    assert(!g.drive_manual && !g.throttle && !strcmp(g.message, "SYSTEM DRIVE DISENGAGED - NO FUEL"));
    remove(TEST_SAVE);
    remove(TEST_SAVE ".bak");
    puts("PASS System Drive acceleration, braking, cruise saves and mass/traffic locks");
}
static void test_independent_drive_autopilot(void) {
    Game g;
    Controls c = {0};
    int ap, drive;
    /* Exercise every switch combination through the real controls and a
       physics tick, so AP cannot implicitly acquire cruise thrust. */
    for (ap = 0; ap <= 1; ap++) for (drive = 0; drive <= 1; drive++) {
        game_init(&g, 1);
        g.docked = g.frame_body = g.target = -1;
        g.target_body = 1;
        g.pos = vec(0, AU_METRES * FP, 0);
        g.vel = vec(0, 0, 0);
        g.autopilot = ap;
        g.drive_manual = drive;
        g.throttle = 2000;
        game_action(&g, 'p');
        assert(g.autopilot == !ap && g.drive_manual == drive && g.throttle == 2000);
        game_tick(&g, c);
        assert(g.autopilot == !ap && g.drive_manual == drive && g.drive_active == drive);
        assert(drive ? length(g.vel) > 10000 * FP : length(g.vel) < 100 * FP);
        game_action(&g, 'a');
        assert(g.autopilot == !ap && g.drive_manual == !drive);
        game_tick(&g, c);
        assert(g.autopilot == !ap && g.drive_manual == !drive && !g.dead);
    }
    /* A body waypoint lies inside its exclusion zone: the drive refuses to
       arm there, and arrival stops commanded motion without it. */
    game_init(&g, 1);
    g.docked = g.target = -1;
    g.target_body = g.body = g.frame_body = 0;
    g.pos = navigation_position(&g);
    g.vel = vec(0, 0, 0);
    game_action(&g, 'a');
    assert(!g.drive_manual && !strcmp(g.message, "SYSTEM DRIVE UNAVAILABLE - EXCLUSION ZONE"));
    game_action(&g, 'p');
    game_tick(&g, c);
    assert(!g.autopilot && !g.drive_manual && !g.throttle && !g.dead);
    game_tick(&g, c);
    assert(length(g.vel) < 100 * FP);
    puts("PASS independent autopilot/drive switches, thrust selection and drive-free arrival");
}
static int hyperspace_ready(Game *g) {
    int i;
    game_init(g, 1);
    g->docked = g->frame_body = -1;
    g->pos = vec(0, AU_METRES * FP, 0);
    g->vel = vec(0, 0, 0);
    for (i = 0; i < 9; i++) {
        g->selected = (Sector){i % 3 - 1, 0, i / 3 - 1};
        if (!jump_refusal(g, g->selected)) return 1;
    }
    return 0;
}
static void assert_host_star_ahead(const Game *g) {
    Vec to_star = sub(scale(g->system.star[0].center, FP, 1), g->pos);
    assert(length(sub(forward(g->yaw, g->pitch), unit(to_star, 16384))) <= 2);
    assert(g->roll == 0);
}
static void test_jump_heading(void) {
    Game g;
    int i, arrivals = 0;
    for (i = 0; i < 9; i++) {
        assert(hyperspace_ready(&g));
        g.selected = (Sector){i % 3 - 1, 0, i / 3 - 1};
        if (jump_refusal(&g, g.selected)) continue;
        g.yaw = (i * 113 + 37) & 1023;
        g.pitch = i * 51 - 204;
        g.roll = (i * 127 + 83) & 1023;
        g.roll_fraction = 123; g.pitch_fraction = -234;
        assert(jump(&g, g.selected));
        assert_host_star_ahead(&g);
        assert(!g.roll_fraction && !g.pitch_fraction);
        arrivals++;
    }
    assert(arrivals > 1);
    puts("PASS direct jumps face the host star from varied departure attitudes");
}
static void test_hyperspace(void) {
    Game g;
    Controls c = {0};
    uint32_t seed, tick;
    int i, fuel;
    /* X charges in the departure system, jumps at the tunnel boundary, holds
       the simulation in the tunnel and arrives with controls restored. */
    assert(hyperspace_ready(&g));
    seed = g.system.seed; fuel = g.fuel;
    g.yaw = 379; g.pitch = 91; g.roll = 237;
    g.page = MAP; g.warp = 64;
    game_action(&g, 'x');
    assert(g.hyperspace == 1 && g.page == FLIGHT && g.warp == 1 && g.system.seed == seed);
    game_action(&g, 'g');
    game_action(&g, 'a');
    assert(g.page == FLIGHT && !g.drive_manual); /* the sequence owns the ship */
    for (i = 0; i < HYPER_CHARGE - 1; i++) game_frame(&g, c, 1);
    assert(g.hyperspace == HYPER_CHARGE && g.system.seed == seed && g.fuel == fuel);
    game_frame(&g, c, 1);
    assert(g.system.seed != seed && g.fuel < fuel && g.hyperspace == HYPER_CHARGE + 1);
    assert(!strncmp(g.message, "HYPERSPACE TRANSIT", 18));
    assert(g.frame_body == -1 && g.body == nearest_body(&g, g.pos));
    assert(llabs(length(sub(scale(g.pos, 1, FP), g.system.star[0].center)) -
                 g.system.star[0].radius - 1100000000LL) < 16);
    assert(drive_available(&g));
    assert_host_star_ahead(&g);
    tick = g.tick;
    for (i = 0; i < HYPER_TUNNEL - 1; i++) game_frame(&g, c, 1);
    assert(g.tick == tick);
    game_frame(&g, c, 1);
    assert(g.tick > tick && !strncmp(g.message, "JUMP COMPLETE", 13));
    for (i = 0; i < HYPER_EXIT; i++) game_frame(&g, c, 1);
    assert(!g.hyperspace);
    assert_host_star_ahead(&g);
    game_action(&g, 'g');
    assert(g.page == MAP);
    /* Traffic arriving during the charge aborts at release, at no cost. */
    assert(hyperspace_ready(&g));
    seed = g.system.seed; fuel = g.fuel;
    game_action(&g, 'x');
    g.npc[0].active = 1;
    g.npc[0].mission = -1;
    g.npc[0].pos = add(g.pos, vec(0, 0, 1000 * FP));
    g.npc[0].vel = vec(0, 0, 0);
    for (i = 0; i < HYPER_CHARGE + 1; i++) game_frame(&g, c, 1);
    assert(!g.hyperspace && g.system.seed == seed && g.fuel == fuel);
    assert(!strcmp(g.message, "JUMP ABORTED - TRAFFIC NEARBY"));
    /* Refusals name their reason and never start the charge. */
    game_init(&g, 1);
    g.selected = (Sector){1, 0, 0};
    game_action(&g, 'x');
    assert(!g.hyperspace && !strcmp(g.message, "JUMP DENIED - DOCKED"));
    puts("PASS hyperspace charge, tunnel hold, arrival, traffic abort and refusal reasons");
}
static void test_safety(void) {
    Game g, h;
    Controls c = {0};
    game_init(&g, 3);
    game_action(&g, 'l');
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + 300000LL) * FP, 0));
    g.vel = vec(0, 0, 0);
    g.autopilot = 1;
    g.target = 3;
    h = g;
    g.warp = 1;
    h.warp = MAX_WARP;
    game_frame(&h, c, 1);
    assert(h.tick > g.tick + 1 && h.tick - g.tick <= MAX_FRAME_STEPS);
    assert(h.effective_warp == (int)(h.tick - g.tick));
    while (g.tick < h.tick)
        game_frame(&g, c, 1);
    assert(g.pos.x == h.pos.x && g.pos.y == h.pos.y && g.pos.z == h.pos.z);
    assert(g.vel.x == h.vel.x && g.vel.y == h.vel.y && g.vel.z == h.vel.z);
    h.pos = add(scale(h.system.planets[0].center, FP, 1),
                vec(0, (int64_t)(h.system.planets[0].radius + 500) * FP, 0));
    h.warp = MAX_WARP;
    assert(safe_warp(&h) == MAX_WARP);
    h.npc[0].active = 1;
    h.npc[0].pos = add(h.pos, vec(0, 0, 1000 * FP));
    assert(safe_warp(&h) == MAX_WARP);
    h.pos = vec(0, 100000000000LL, 0);
    h.npc[0].pos = vec(0, 0, 0);
    h.npc[0].vel = vec(10000000, 0, 0);
    game_tick(&h, c);
    puts("PASS compression preserves tick trajectory without hazard locks");
}
static void test_unrestricted_warp(void) {
    Game g, reference;
    Controls c = {0};
    int situation, i;
    /* Each former local lock must actually advance the simulation, with
       exactly the same result as running the corresponding ordinary ticks. */
    for (situation = 0; situation < 8; situation++) {
        game_init(&g, 3);
        if (situation) game_action(&g, 'l');
        if (situation == 1) g.docked = -2;
        if (situation == 2) g.autopilot = 1;
        if (situation == 3 || situation == 4) {
            g.npc[0] = (Npc){add(g.pos, vec(0, 0, 1000 * FP)), g.vel, 100, 1, 1, -1, 0};
        }
        if (situation == 4) {
            g.projectiles[0].active = 1;
            g.projectiles[0].pos = g.pos;
            g.projectiles[0].vel = g.vel;
            g.projectiles[0].target = 0;
            g.projectiles[0].ttl = HZ * 20;
        }
        if (situation == 5) c.fire = 1;
        if (situation == 6) g.pos = add(asteroid_position(&g, 0), vec(0, 0, 1000 * FP));
        if (situation == 7) g.pos = add(g.ports[1].pos, scale(g.ports[1].normal, 3000 * FP, 16384));
        g.warp = 64;
        assert(safe_warp(&g) == 64);
        reference = g;
        game_frame(&g, c, 1);
        for (i = 0; i < 64; i++) game_tick(&reference, c);
        assert(g.tick == 64 && !g.dead && g.effective_warp == 64);
        reference.effective_warp = g.effective_warp;
        reference.ui_clock = g.ui_clock; /* frame-level presentation clock */
        assert(!memcmp(&g, &reference, sizeof(g)));
        c.fire = 0;
    }
    g.pos = scale(g.system.star[0].center, FP, 1);
    g.warp = MAX_WARP;
    assert(safe_warp(&g) == MAX_WARP);
    g.laser = 1;
    assert(safe_warp(&g) == MAX_WARP);
    g.paused = 1;
    i = g.tick;
    game_frame(&g, c, 1);
    assert(g.tick == (uint32_t)i);
    g.paused = 0; g.dead = 1;
    game_frame(&g, c, 1);
    assert(g.tick == (uint32_t)i);
    game_init(&g, 3);
    game_action(&g, 'l');
    g.warp = 64;
    game_action(&g, 'p'); assert(g.autopilot && g.warp == 64);
    game_action(&g, 'p'); assert(!g.autopilot && g.warp == 64);
    g.frame_body = -1; g.pos = vec(0, AU_METRES * FP, 0); g.vel = vec(0, 0, 0);
    game_action(&g, 'a'); assert(g.drive_manual && g.warp == 64);
    game_action(&g, 'a'); assert(!g.drive_manual && g.warp == 64);
    puts("PASS manual, docked, landed, approach, combat and hazard warp with exact local physics");
}
static void test_presentation(void) {
    Game g, h;
    Controls c = {1, 1, 0, 0, 0, 0, 0, 0, 0};
    int i, yaw, pitch;
    game_init(&g, 3);
    assert(combat_target(&g) == -1);
    g.npc[2] = (Npc){add(g.pos, vec(0, 0, 2000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    g.npc[5] = (Npc){add(g.pos, vec(0, 0, 11000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    game_action(&g, 'r');
    assert(combat_target(&g) == 2 && g.target == 1);
    g.npc[2].active = 0;
    assert(combat_target(&g) == -1);
    game_action(&g, 'r');
    assert(g.combat_target == -1);
    game_action(&g, 'z');
    assert(g.page == SYSTEM_MAP && g.system_display == 1);
    game_action(&g, 'c');
    assert(g.map_local);
    game_action(&g, 'i');
    assert(g.system_display == 2);
    game_action(&g, 'y');
    assert(g.system_display == 0);
    game_action(&g, 'v');
    for (i = 1; i <= 5; i++) {
        game_action(&g, 'v');
        assert(g.camera_mode == i % 5);
    }
    g.camera_mode = 3;
    g.turret_yaw = 512;
    g.turret_pitch = 0;
    yaw = g.yaw; pitch = g.pitch;
    game_tick(&g, c);
    assert(g.yaw == yaw && g.pitch == pitch);
    assert(g.turret_yaw == 515 && g.turret_pitch == 3);
    view_angles(&g, &yaw, &pitch);
    assert(yaw == 515 && pitch == 3);
    assert(save_game(&g, TEST_SAVE));
    assert(load_game(&h, TEST_SAVE) && h.camera_mode == 3);
    assert(combat_target(&h) == -1);
    remove(TEST_SAVE);
    /* A shot behind the hull must hit only when the turret points backwards. */
    game_action(&g, 'l');
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + 20000LL) * FP, 0));
    g.vel = vec(0, 0, 0); g.yaw = 0; g.pitch = 0; g.assist = 0;
    g.turret_yaw = 512; g.turret_pitch = 0; g.tick = 5;
    g.npc[2] = (Npc){add(g.pos, vec(0, 0, -2000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    h = g; h.camera_mode = 0;
    game_tick(&g, (Controls){0, 0, 0, 1, 0, 0, 0, 0, 0});
    game_tick(&h, (Controls){0, 0, 0, 1, 0, 0, 0, 0, 0});
    assert(g.npc[2].hull == 80 && h.npc[2].hull == 100);
    assert(g.pos.x == h.pos.x && g.pos.y == h.pos.y && g.pos.z == h.pos.z);
    puts("PASS independent turret aim/fire, contact range, map controls and save compatibility");
}
static void test_flight_input(void) {
    MouseStick m = {0};
    Controls c = {0};
    Game g, h;
    int i;
    /* Every switch combination, including diagonals, uses XOR quadrature. */
    for (i = 0; i < 16; i++) {
        int right = i & 1, left = (i >> 1) & 1;
        int down = (i >> 2) & 1, up = (i >> 3) & 1;
        unsigned joy = (unsigned)((right << 1) | (left << 9) |
                                  (down ^ right) | ((up ^ left) << 8));
        assert(joystick_roll(joy) == right - left);
        assert(joystick_pitch(joy) == down - up);
    }
    /* On foot the stick walks and turns; fire held looks and sidesteps,
       and only a tap of fire alone uses what is in reach. */
    {
        FootStick s = {1, 1};
        assert(!foot_stick(&s, &c, 0, 0, 0)); /* already down on stepping out */
        assert(!foot_stick(&s, &c, 1, -1, 0) && c.thrust == 1 && c.roll == 1 && !c.strafe && !c.pitch);
        c = (Controls){0};
        assert(!foot_stick(&s, &c, 0, 0, 1) && !foot_stick(&s, &c, 0, 0, 1));
        assert(foot_stick(&s, &c, 0, 0, 0) && !c.thrust && !c.roll);
        assert(!foot_stick(&s, &c, 1, 1, 1) && c.strafe == 1 && c.pitch == 1 && !c.thrust && !c.roll);
        assert(!foot_stick(&s, &c, 0, 0, 1) && !foot_stick(&s, &c, 0, 0, 0));
        c = (Controls){0};
    }
    mouse_stick(&m, 8, -8, 1);
    flight_input(&c, &m, 0, 0, 0);
    assert(!c.pitch && !c.roll);
    mouse_stick(&m, 100, 100, 1);
    flight_input(&c, &m, 1, 1, 1);
    assert(c.pitch == 1 && c.roll == 1 && c.fire);
    mouse_stick(&m, 0, 0, 0);
    assert(!m.x && !m.y);
    c = (Controls){0};
    mouse_stick(&m, 0, -64, 1);
    flight_input(&c, &m, 0, 0, 0);
    assert(c.pitch == -1 && !c.roll);
    mouse_stick(&m, 0, 0, 0);
    /* Roll alone leaves the nose fixed; banked pitch follows the hull axes. */
    game_init(&g, 3);
    g.docked = -1; g.assist = 0; g.pos = add(g.pos, vec(0, 100000 * FP, 0));
    g.yaw = g.pitch = 0;
    game_tick(&g, (Controls){.roll = 1});
    assert(g.roll == 3 && !g.yaw && !g.pitch);
    game_tick(&g, (Controls){.roll = -1});
    assert(!g.roll && !g.yaw && !g.pitch);
    game_tick(&g, (Controls){.roll = -1});
    assert(g.roll == 1021 && !g.yaw && !g.pitch);
    g.roll = 256;
    game_tick(&g, (Controls){.pitch = 1});
    assert(g.yaw == 3 && !g.pitch && g.roll == 256);
    g.yaw = g.pitch = 0; g.roll = 768;
    game_tick(&g, (Controls){.pitch = 1});
    assert(g.yaw == 1021 && !g.pitch && g.roll == 768);
    g.yaw = g.pitch = 0; g.roll = 512;
    game_tick(&g, (Controls){.pitch = 1});
    assert(!g.yaw && g.pitch == -3 && g.roll == 512);
    /* Crossing a pole keeps the flight direction continuous. */
    g.yaw = 0; g.pitch = 255; g.roll = 0;
    game_tick(&g, (Controls){.pitch = 1});
    assert(g.yaw == 512 && abs(g.pitch - 254) <= 1 && g.roll == 512);
    g.roll = 137;
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(h.roll == 137 && h.yaw == g.yaw && h.pitch == g.pitch);
    remove(TEST_SAVE);
    g.paused = 1; h = g;
    game_tick(&g, (Controls){.roll = 1, .pitch = 1});
    assert(g.roll == h.roll && g.yaw == h.yaw && g.pitch == h.pitch);
    g.paused = 0; g.camera_mode = 3; g.turret_yaw = g.turret_pitch = 0; h = g;
    game_tick(&g, (Controls){.roll = 1, .pitch = 1});
    assert(g.roll == h.roll && g.yaw == h.yaw && g.pitch == h.pitch);
    assert(g.turret_yaw == 3 && g.turret_pitch == 3);
    puts("PASS roll-only steering, banked pitch, pole crossing, pause, turret isolation and bank save");
    game_init(&g, 3);
    for (i = 0; i < 10; i++) game_action(&g, ']');
    assert(g.warp == MAX_WARP);
    assert(save_game(&g, TEST_SAVE));
    assert(load_game(&h, TEST_SAVE) && h.warp == MAX_WARP);
    remove(TEST_SAVE);
    for (i = 0; i < 6; i++) game_action(&g, '[');
    assert(g.warp == 1);
    puts("PASS mouse deadzone/recenter, joystick quadrature, 4096x controls/save");
}
static void test_environment(void) {
    Game g, h;
    Planet fixed;
    Vec local, turned, before, v;
    int a, found_rain = 0, found_snow = 0, found_cloud = 0;
    game_init(&g, 37);
    fixed = g.system.planets[0];
    {
        Planet slow = fixed;
        slow.parent=0; slow.period=2000000;
        set_rotation(&slow,slow.period*HZ/2);
        assert(length(sub(body_rotate(&slow,vec(1000000,0,0),0),vec(-1000000,0,0))) < 4);
        set_rotation(&slow,slow.period*HZ);
        assert(length(sub(body_rotate(&slow,vec(1000000,0,0),0),vec(1000000,0,0))) < 4);
    }
    local = radial(forward(140, 30), fixed.radius + 1800);
    set_rotation(&g.system.planets[0], rotation_period(&fixed) * HZ / 4);
    turned = body_rotate(&g.system.planets[0], local, 0);
    assert(length(sub(local, turned)) > fixed.radius / 2);
    assert(length(sub(body_rotate(&g.system.planets[0], turned, 1), local)) < 4);
    assert(abs(terrain_elevation(&fixed, local) - terrain_elevation(&g.system.planets[0], turned)) < 2);
    assert(terrain_material(&fixed, local) == terrain_material(&g.system.planets[0], turned));
    g.tick = rotation_period(&fixed) * HZ / 4; update_world(&g);
    before = g.ports[0].pos; v = g.ports[0].velocity;
    g.tick++; update_world(&g);
    assert(length(sub(scale(sub(g.ports[0].pos, before), HZ, 1), v)) < 100 * FP);
    g.docked = -2; g.frame_body = g.body = 0;
    turned=radial(turned,fixed.radius+terrain_height(&g.system.planets[0],turned)+12);
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                scale(turned, FP, 1));
    before = body_rotate(&g.system.planets[0], sub(scale(g.pos, 1, FP), g.system.planets[0].center), 1);
    game_tick(&g, (Controls){0});
    local = body_rotate(&g.system.planets[0], sub(scale(g.pos, 1, FP), g.system.planets[0].center), 1);
    assert(length(sub(local, before)) < 4);
    assert(save_game(&g, TEST_SAVE) && load_game(&h, TEST_SAVE));
    assert(length(sub(g.pos,h.pos)) < 4);
    { /* Version 4 ground commander has an unrotated body-relative position. */
        unsigned char data[24576];
        size_t len, k;
        uint32_t sum = 2166136261U;
        FILE *old = fopen(TEST_SAVE, "rb");
        assert(old); len = fread(data, 1, sizeof(data), old); fclose(old);
        for (k = 16; k + 4 < len; k += 4) if (data[k] == 0x43 && data[k+1] == 0x41 &&
            data[k+2] == 0x52 && data[k+3] == 0x36) { len = k; break; }
        put_word(data + 4, 4); put_word(data + 8, (uint32_t)(len - 16));
        for (k = 16; k < len; k++) sum = (sum ^ data[k]) * 16777619U;
        put_word(data + 12, sum);
        old = fopen(TEST_SAVE, "wb"); assert(old && fwrite(data, 1, len, old) == len); fclose(old);
        assert(load_game(&h,TEST_SAVE));
        assert(length(sub(g.pos,h.pos)) > fixed.radius * FP / 2);
        assert(altitude_at(&h,h.pos) >= 10 && altitude_at(&h,h.pos) <= 14);
    }
    remove(TEST_SAVE);
    /* Isolate a star and test both hemispheres and an eclipse. */
    g.system.count = 1; g.system.stars = 1;
    g.system.star[0].center = add(g.system.planets[0].center, vec(1000000000,0,0));
    local = vec(fixed.radius + 100,0,0);
    assert(surface_light(&g.system,&g.system.planets[0],local) > 16000);
    assert(!surface_light(&g.system,&g.system.planets[0],scale(local,-1,1)));
    assert(rayleigh_scattering(&g.system,&g.system.planets[0],local,vec(0,1,0)) > 0);
    assert(!rayleigh_scattering(&g.system,&g.system.planets[0],scale(local,-1,1),vec(0,1,0)));
    { /* A low star lights the horizon band; cloud decks hide the stars. */
        Planet air = g.system.planets[0];
        Vec sun = vec(0,0,0);
        air.climate = TEMPERATE; air.atmosphere = 80000;
        assert(!twilight(&g.system,&air,local,&sun));
        assert(twilight(&g.system,&air,vec(0,air.radius + 100,0),&sun) > 200 && sun.x > 16000);
        assert(!twilight(&g.system,&air,scale(local,-1,1),&sun));
        assert(!twilight(&g.system,&air,vec(0,air.radius + 90000,0),&sun));
        assert(sky_opacity(&air,100) > 0 && sky_opacity(&air,100) < 256);
        air.climate = DENSE;
        assert(sky_opacity(&air,100) > 640 && !sky_opacity(&air,air.atmosphere));
    }
    g.system.count = 2; g.system.planets[1].center = add(g.system.planets[0].center,vec(100000000,0,0));
    g.system.planets[1].radius = 1000000;
    assert(!surface_light(&g.system,&g.system.planets[0],local));
    for (a = 0; a < 1024; a += 8) {
        local = radial(forward(a,0), fixed.radius + 1800);
        found_cloud |= cloud_density(&fixed,local,0) > 0;
        assert(!cloud_density(&fixed,radial(local,fixed.radius + 4000),0));
        assert(!cloud_density(&fixed,radial(local,fixed.radius + 200),0));
        local = radial(local,fixed.radius + 200);
        found_rain |= precipitation(&fixed,local,0) == 1;
        fixed.climate = FROZEN;
        found_snow |= precipitation(&fixed,local,0) == 2;
        fixed.climate = TEMPERATE;
    }
    assert(found_cloud && found_rain && found_snow);
    fixed.atmosphere = 0;
    assert(!cloud_density(&fixed,local,0) && !precipitation(&fixed,local,0));
    game_init(&g,37);
    for (a=0; a<g.system.count && g.system.planets[a].type!=MOON; a++) {}
    assert(a<g.system.count);
    g.body=g.frame_body=a; g.docked=-1; g.target=-1; g.target_body=a;
    g.assist=0; g.throttle=0;
    local=vec(0,g.system.planets[a].radius+13,0);
    g.pos=scale(add(g.system.planets[a].center,local),FP,1);
    g.vel=surface_velocity(&g.system.planets[a],local);
    for (a=0; a<4*HZ && g.docked==-1 && !g.dead; a++) game_tick(&g,(Controls){0});
    assert(g.docked==-2 && !g.dead && g.system.planets[g.body].type==MOON);
    before=g.vel; game_action(&g,'l');
    assert(g.docked==-1 && length(sub(g.vel,before)) >= 19*FP &&
           length(sub(g.vel,before)) <= 21*FP);
    game_init(&g,37);
    for (a=0; a<1024; a+=8) {
        local=radial(forward(a,0),g.system.planets[0].radius+1800);
        if (cloud_density(&g.system.planets[0],local,0)>100 &&
            terrain_height(&g.system.planets[0],local)<500) break;
    }
    assert(a<1024);
    local=radial(local,g.system.planets[0].radius+600);
    g.pos=scale(add(g.system.planets[0].center,local),FP,1);
    g.vel=add(surface_velocity(&g.system.planets[0],local),unit(local,400*FP));
    g.docked=-1; g.assist=0; g.throttle=0;
    found_cloud=0;
    for (a=0; a<10*HZ && !g.dead; a++) {
        game_tick(&g,(Controls){0});
        local=sub(scale(g.pos,1,FP),g.system.planets[0].center);
        found_cloud |= cloud_density(&g.system.planets[0],local,g.tick)>100;
    }
    assert(!g.dead && found_cloud && !cloud_density(&g.system.planets[0],local,g.tick));
    puts("PASS body-fixed terrain, rotating ports/ground saves, day/night/eclipse, scattering, twilight, star extinction, cloud entry/exit, rain/snow");
}
int main(int argc, char **argv) {
    quick = argc > 1 && !strcmp(argv[1], "--quick");
    setvbuf(stdout, NULL, _IONBF, 0);
    puts(quick ? "ASTRALBOUND QUICK SMOKE SUITE" : "ASTRALBOUND FULL CORE SUITE");
    test_isqrt();
    test_universe();
    test_landforms();
    test_environment();
    test_world();
    test_hetha_departure();
    test_collision();
    test_economy();
    test_markets();
    test_legacy_market_save();
    if (!quick)
        test_auto();
    test_ai();
    test_save();
    test_render();
    test_presentation();
    test_routes();
    if (!quick)
        test_missions();
    test_atmosphere_speed_limit();
    test_entry_brake();
    test_thirty_c();
    test_system_drive();
    test_independent_drive_autopilot();
    test_jump_heading();
    test_hyperspace();
    test_safety();
    test_unrestricted_warp();
    test_flight_input();
    puts(quick ? "All Astralbound quick smoke tests passed." : "All Astralbound tests passed.");
    return 0;
}
