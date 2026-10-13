/* SPDX-License-Identifier: Unlicense
   Isolated raster regressions exercise intersecting surfaces directly. */
#include <assert.h>
#include "../src/render.c"
#include "../src/native_video.h"

static void compact_display(void) {
    static Game g;
    Frame small = {320, 200, 1, 0, {0}}, full = {320, 256, 1, 0, {0}};
    uint8_t *planar;
    int page, depth, x, y, b;
    NativeVideo pal = native_video(1, 0), ntsc = native_video(0, 0), low = native_video(1, 1);
    assert(pal.start == 44 && pal.stop == 300 && pal.height == 256 && pal.rate == 50);
    assert(ntsc.start == 44 && ntsc.stop == 244 && ntsc.height == 200 && ntsc.rate == 60);
    assert(low.start == 72 && low.stop == 272 && low.height == 200 && low.rate == 50);
    game_init(&g, 0x53504143);
    small.pixels = malloc(320 * 200);
    full.pixels = malloc(320 * 256);
    planar = malloc(40 * 200 * 8 + 16);
    assert(small.pixels && full.pixels && planar);
    /* Compare every menu's text area with the established 256-line layout,
       including the last contract/commodity row and the bottom instructions. */
    for (page = MAP; page <= HELP; page++) {
        g.page = page;
        assert(render_init(&full));
        render_game(&g, &full);
        assert(render_init(&small));
        render_game(&g, &small);
        for (y = 12; y < 181; y++)
            assert(!memcmp(small.pixels + y * 320 + 12, full.pixels + y * 320 + 12, 296));
    }
    /* New system panels must remain fully legible in the 200-line layout. */
    g.page = SYSTEM_MAP;
    for (page = 1; page <= 2; page++) {
        g.system_display = page;
        assert(render_init(&small));
        render_game(&g, &small);
        assert(render_init(&full));
        render_game(&g, &full);
        for (y = 12; y < 181; y++)
            assert(!memcmp(small.pixels + y * 320 + 12, full.pixels + y * 320 + 12, 296));
    }
    g.map_local = 1; g.system_display = 1;
    render_game(&g, &full);
    g.page = FLIGHT;
    assert(render_init(&small));
    g.npc[0] = (Npc){add(g.pos, vec(0, 0, 1000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    g.combat_target = 0;
    for (page = 0; page < 5; page++) {
        g.camera_mode = page;
        render_game(&g, &small);
    }
    for (depth = 5; depth <= 8; depth += 3) {
        int size = 40 * 200 * depth;
        for (x = 0; x < 320 * 200; x++)
            small.pixels[x] = (uint8_t)((x * 13 + x / 320) & ((1 << depth) - 1));
        memset(planar, 0xa5, 40 * 200 * 8 + 16);
        c2p(small.pixels, planar + 8, 320, 200, depth);
        for (x = 0; x < 8; x++)
            assert(planar[x] == 0xa5 && planar[8 + size + x] == 0xa5);
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x++) {
                int pixel = 0;
                for (b = 0; b < depth; b++)
                    pixel |= ((planar[8 + b * 8000 + y * 40 + x / 8] >> (7 - (x & 7))) & 1) << b;
                assert(pixel == small.pixels[y * 320 + x]);
            }
    }
    free(planar);
    free(small.pixels);
    free(full.pixels);
    render_shutdown();
}

static void orbital_and_radar(void) {
    static Game g;
    Frame f = {320, 200, 0, 0, {0}};
    int seed, body, display, x, y;
    assert(render_init(&f));
    f.pixels = calloc(320 * 200, 1);
    assert(f.pixels);
    for (seed = 1; seed <= 8; seed++) {
        game_init(&g, (unsigned)seed);
        g.page = SYSTEM_MAP;
        g.target = -1;
        for (body = 0; body < g.system.count; body++) {
            g.target_body = body;
            for (display = 1; display <= 2; display++) {
                g.system_display = display;
                g.map_local = 1;
                render_game(&g, &f);
            }
        }
        g.system_display = 1;
        g.map_local = 0;
        render_game(&g, &f);
    }
    frame = &f; game = &g; view_h = 144;
    /* AU-sized origins must cancel before projection; a local displacement
       of half the map radius lands exactly halfway to the edge. */
    map_point(add(g.pos, vec(500, 0, -500)), g.pos, 1000, &x, &y);
    assert(x == 191 && y == 127);
    g.yaw = 0;
    g.combat_target = -1;
    g.npc[0] = (Npc){add(g.pos, vec(0, 0, 10000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    sensors();
    assert(f.pixels[25 * 320 + 274] == 14); /* forward at 10 km */
    g.yaw = 256;
    sensors();
    assert(f.pixels[43 * 320 + 242] == 14); /* left after hull turns right */
    g.npc[0].pos = add(g.pos, vec(0, 0, 10001 * FP));
    sensors();
    assert(f.pixels[43 * 320 + 242] != 14); /* out of range is removed */
    free(f.pixels);
    render_shutdown();
}

static void environment_rendering(void) {
    Game g;
    Frame f = {320,256,1,0,{0}};
    int a, found = 0;
    Vec local;
    game_init(&g,37);
    f.pixels = calloc((size_t)f.w*f.h,1); assert(f.pixels && render_init(&f));
    game=&g; frame=&f; view_h=200;
    g.docked=-1;
    for (a=0;a<1024;a+=8) {
        const Planet *p=&g.system.planets[0];
        local=radial(forward(a,0),p->radius+200);
        if (terrain_material(p,local)!=2) continue;
        g.pos=scale(add(p->center,local),FP,1); camera_position=g.pos;
        aim_angles(vec(-local.y,local.x,0),&camera_yaw,&camera_pitch);
        nt=0; forests(); found |= nt>0;
    }
    assert(found); /* geometry survives full-radius placement and terrain depth */
    for (a=0;a<1024;a+=8) {
        const Planet *p=&g.system.planets[0];
        local=radial(forward(a,0),p->radius+1800);
        if (cloud_density(p,local,g.tick)<100) continue;
        g.pos=scale(add(p->center,local),FP,1);
        memset(f.pixels,0,(size_t)f.w*f.h); weather_overlay();
        assert(f.pixels[100*320+160] || f.pixels[100*320+161] || f.pixels[102*320+160]);
        g.pos=scale(add(p->center,radial(local,p->radius+4000)),FP,1);
        memset(f.pixels,0,(size_t)f.w*f.h); weather_overlay();
        assert(!f.pixels[100*320+160]); break;
    }
    assert(a<1024);
    free(f.pixels); render_shutdown();
    puts("PASS procedural flora geometry and visible cloud entry/exit fog");
}
int main(void) {
    Frame f = {64, 120, 1, 0, {0}};
    Tri ground = {{0, 64, 0}, {0, 0, 64}, {1000, 1000, 1000}, 1000, 8};
    Tri wall = {{0, 64, 0}, {0, 0, 64}, {400, 2000, 400}, 1200, 16};
    uint8_t reference[64 * 64];
    int order;
    assert(render_init(&f));
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels);
    frame = &f;
    view_h = 64;
    for (order = 0; order < 2; order++) {
        memset(f.pixels, 0, (size_t)f.w * f.h);
        memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
        raster(order ? &wall : &ground);
        raster(order ? &ground : &wall);
        /* A large ground triangle passes in front of one end of the wall
           and behind the other. Average-distance sorting cannot do this. */
        assert(f.pixels[8 * 64 + 8] == 8);
        assert(f.pixels[8 * 64 + 40] == 16);
        if (!order)
            memcpy(reference, f.pixels, sizeof(reference));
        else {
            int x, y;
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++)
                    /* At x=24 the two surfaces have exactly equal depth. */
                    if (x != 24)
                        assert(reference[y * 64 + x] == f.pixels[y * 64 + x]);
        }
    }
    /* Clipped spans must evaluate depth at the clipped pixel position. */
    wall.x[0] = wall.x[2] = -32;
    wall.x[1] = 32;
    memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
    raster(&wall);
    assert(depth_buffer[8 * 64 + 8] == 1400);
    /* ECS polar ice stays visibly darker than the atmospheric horizon. */
    assert(f.palette[30] != f.palette[7]);
    assert(((f.palette[30] >> 8) & 255) + 20 < ((f.palette[7] >> 8) & 255));
    free(f.pixels);
    render_shutdown();
    assert(!render_init(&(Frame){64, 56, 0, 0, {0}}));
    environment_rendering();
    compact_display();
    orbital_and_radar();
    puts("PASS depth, polar contrast, PAL/NTSC layout, compact menus and 200-line C2P");
    return 0;
}
