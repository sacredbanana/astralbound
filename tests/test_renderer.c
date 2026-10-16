/* SPDX-License-Identifier: Unlicense
   Isolated raster regressions exercise intersecting surfaces directly. */
#include <assert.h>
#include "../src/render.c"
#include "../src/native_video.h"
static void warp_readout(void) {
    Game g;
    Frame f = {320, 200, 0, 0, {0}};
    uint8_t actual[114 * 7];
    int layout, paused, x, y, top;
    game_init(&g, 3);
    g.effective_warp = 256;
    g.warp = 4096;
    f.pixels = malloc(320 * 256); assert(f.pixels);
    for (layout = 0; layout < 2; layout++) {
        f.h = layout ? 256 : 200;
        top = f.h - 80;
        assert(render_init(&f));
        for (paused = 0; paused < 2; paused++) {
            g.paused = paused;
            render_game(&g, &f);
            for (y = 0; y < 7; y++) for (x = 0; x < 114; x++)
                actual[y * 114 + x] = f.pixels[(top + 39 + y) * 320 + 104 + x] == 12;
            rect(104, top + 39, 114, 7, 0);
            text(104, top + 39, 12, paused ? "PAUSED 256X" : "TIME 256X");
            for (y = 0; y < 7; y++) for (x = 0; x < 114; x++)
                assert(actual[y * 114 + x] == (f.pixels[(top + 39 + y) * 320 + 104 + x] == 12));
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS single actual warp multiplier in PAL/NTSC and paused HUD");
}
static void resource_visibility(void) {
    Game g;
    Frame f = {320, 200, 0, 0, {0}};
    int x, y, pixels = 0;
    game_init(&g, 0x53504143);
    g.docked = -1; g.target = -1; g.target_body = 0; g.resource_target = 0;
    g.pos = add(asteroid_position(&g, 0), vec(0, 0, -800 * FP));
    f.pixels = calloc(320 * 200, 1); assert(f.pixels && render_init(&f));
    render_game(&g, &f);
    for (y = 20; y < view_h-25; y++) for (x = 120; x < 200; x++)
        if (depth_buffer[y * 320 + x] && f.pixels[y * 320 + x] >= 6 &&
            f.pixels[y * 320 + x] <= 8) pixels++;
    assert(pixels > 300);
    free(f.pixels); render_shutdown();
    puts("PASS asteroid geometry visible in the astronomical Q8 camera frame");
}
static int surface_color(const Planet *p, int face, int u, int v) {
    return surface_cache[surface_lookup(p,face,u,v)].color;
}
static int ground_color(const Planet *p, Vec local) {
    return material_color(p,local,terrain_material(p,local));
}
static void hetha_target_bearing(void) {
    Game g;
    Frame f = {320, 200, 0, 0, {0}};
    Vec p;
    int pitch = 0, x, y;
    game_init(&g, 0x53504143);
    game_action(&g, 'l');
    g.target = -1;
    g.target_body = 2;
    aim_angles(sub(navigation_position(&g), g.pos), &g.yaw, &pitch);
    g.pitch = 0;
    f.pixels = calloc(f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    render_game(&g, &f);
    p = camera(scale(navigation_position(&g), 1, FP));
    assert(p.z > 0 && p.y < 0 && pitch < 0);
    x = f.w / 2 + (int)(p.x * (f.w / 2) / p.z);
    y = view_h / 2 - (int)(p.y * (f.w / 2) / p.z);
    assert(y > view_h / 2 && y < view_h - 25);
    assert(f.pixels[(y + 8) * f.w + x] == 13);
    free(f.pixels);
    render_shutdown();
    puts("PASS Hetha D target marker below level forward bearing");
}

static void persistent_target_markers(void) {
    static const Vec bearings[] = {
        {0,0,1000000}, {-2000000,0,1000000}, {2000000,0,1000000},
        {0,2000000,1000000}, {0,-2000000,1000000},
        {-2000000,2000000,1000000}, {2000000,-2000000,1000000},
        {-1000000,0,-1000000}, {1000000,0,-1000000},
        {0,1000000,-1000000}, {0,-1000000,-1000000}, {0,0,-1000000},
        {-1000000,0,0}, {1000000,0,1}
    };
    Game g;
    Frame f = {320,200,0,0,{0}};
    int layout, mode, b, x, y, sx, sy;
    f.pixels = malloc(640 * 480); assert(f.pixels);
    for (layout = 0; layout < 3; layout++) {
        f.w = layout == 2 ? 640 : 320;
        f.h = layout == 2 ? 480 : layout == 1 ? 256 : 200;
        assert(render_init(&f));
        game_init(&g, 0x53504143);
        g.docked = -1; g.target = 0; g.resource_target = -1;
        g.pos = scale(add(g.system.planets[g.body].center,
                         vec(0, g.system.planets[g.body].radius + 1000000, 0)), FP, 1);
        g.yaw = 73; g.pitch = 31; g.roll = 137; g.turret_yaw = 41; g.turret_pitch = -29;
        for (mode = 0; mode < 5; mode++) {
            g.camera_mode = mode;
            render_game(&g, &f);
            for (b = 0; b < (int)(sizeof(bearings) / sizeof(bearings[0])); b++) {
                Vec v = scale(bearings[b], 1, 1000), world_offset, actual;
                int cy = cosi(camera_yaw), syaw = sini(camera_yaw);
                int cp = cosi(camera_pitch), sp = sini(camera_pitch);
                int cr = cosi(camera_roll), sr = sini(camera_roll);
                v = vec((v.x * cr + v.y * sr) / 16384,
                        (v.y * cr - v.x * sr) / 16384, v.z);
                int64_t wy = (v.y * cp + v.z * sp) / 16384;
                int64_t wz = (v.z * cp - v.y * sp) / 16384;
                world_offset = vec((v.x * cy + wz * syaw) / 16384, wy,
                                   (wz * cy - v.x * syaw) / 16384);
                g.ports[0].pos = add(camera_position, scale(world_offset, FP, 1));
                g.npc[0] = (Npc){g.ports[0].pos, vec(0,0,0), 100, 1, 1, -1, 0};
                g.combat_target = 0;
                render_game(&g, &f);
                actual = camera(scale(g.ports[0].pos, 1, FP));
                target_marker_position(actual, 10, &x, &y);
                assert(x >= 10 && x <= f.w - 11 && y >= 10 && y <= view_h - 32);
                /* Check the rendered square and diamond after the complete HUD
                   pass, including edge cases and every supported camera. */
                assert(f.pixels[(y - 8) * f.w + x - 8] == 13);
                assert(f.pixels[(y + 8) * f.w + x + 8] == 13);
                target_marker_position(actual, 56, &sx, &sy);
                assert(f.pixels[(sy - 6) * f.w + sx] == 12);
                assert(f.pixels[(sy + 6) * f.w + sx] == 12);
            }
        }
        /* Exact bearings check edge choice without perspective rounding or
           camera transforms hiding a left/right or front/rear inversion. */
        for (b = 1; b < (int)(sizeof(bearings) / sizeof(bearings[0])); b++) {
            Vec v = bearings[b];
            target_marker_position(v, 10, &x, &y);
            assert(x == 10 || x == f.w - 11 || y == 10 || y == view_h - 32);
            if (v.x < 0) assert(x < f.w / 2);
            if (v.x > 0 || (!v.x && !v.y)) assert(x > f.w / 2);
            if (v.y > 0) assert(y < view_h / 2);
            if (v.y < 0) assert(y > view_h / 2);
        }
        target_marker_position(vec(100,50,1000), 10, &x, &y);
        assert(x == f.w / 2 + 100 * (f.w / 2) / 1000);
        assert(y == view_h / 2 - 50 * (f.w / 2) / 1000);
        target_marker_position(vec(0,0,0), 10, &x, &y);
        assert(x == f.w / 2 && y == view_h / 2);
    }
    free(f.pixels); render_shutdown();
    puts("PASS target markers persist at all viewport edges and behind all cameras in NTSC/PAL/RTG");
}

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
    for (page = MAP; page <= CAREER; page++) {
        g.page = page;
        assert(render_init(&full));
        render_game(&g, &full);
        assert(render_init(&small));
        render_game(&g, &small);
        for (y = 20; y < 138; y++)
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
        for (y = 20; y < 138; y++)
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
    assert(x == 191 && y == 109);
    g.yaw = 0;
    g.combat_target = -1;
    g.npc[0] = (Npc){add(g.pos, vec(0, 0, 10000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    cockpit_scan(120);
    assert(f.pixels[134 * 320 + 160] == 14); /* forward at 10 km */
    g.yaw = 256;
    cockpit_scan(120);
    assert(f.pixels[144 * 320 + 110] == 14); /* left after hull turns right */
    g.npc[0].pos = add(g.pos, vec(0, 0, 10001 * FP));
    cockpit_scan(120);
    assert(f.pixels[144 * 320 + 110] != 14); /* out of range is removed */
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
static int same_ground_material(int actual, int expected, int enhanced) {
    int base=enhanced ? expected/16*16 : expected>=28 ? 28 :
             expected>=25 ? 25 : expected>=16 ? 16 : 8;
    return actual>=base && actual<=base+(enhanced ? 14 : 2);
}
static void terrain_angles(void) {
    Game g;
    Frame f = {320, 256, 0, 0, {0}};
    const Planet *p;
    Vec local;
    int angle, i, enhanced;
    static const int offsets[5][2] = {{0,0},{2500,0},{-2500,0},{0,2500},{0,-2500}};
    game_init(&g, 0x53504143);
    p = &g.system.planets[0];
    local = sub(scale(g.ports[0].pos, 1, FP), p->center);
    g.pos = add(g.ports[0].pos, scale(g.ports[0].normal, (TERRAIN_MAX_HEIGHT+5260LL) * FP, 16384));
    g.docked = -1;
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    game = &g; frame = &f; view_h = 200; camera_position = g.pos;
    cache_globes();
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced;
        for (angle = 0; angle < 1024; angle += 32) {
            camera_yaw = angle; camera_pitch = -256;
            nt = 0;
            memset(f.pixels, 0, (size_t)f.w * f.h);
            memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
            planet_mesh(0);
            for (i = 0; i < nt; i++) raster(&triangles[i]);
            for (i = 0; i < 5; i++) {
                Vec point = terrain_vertex(p, local, vec(16384,0,0), vec(0,0,16384),
                                           offsets[i][0], offsets[i][1]);
                Vec v = camera(point);
                int x = f.w / 2 + (int)(v.x * (f.w / 2) / v.z);
                int y = view_h / 2 - (int)(v.y * (f.w / 2) / v.z);
                /* The same ground locations retain their material as the
                   camera rotates. A water corner must not paint adjacent land. */
                assert(x>=0 && x<f.w && y>=0 && y<view_h);
                assert(same_ground_material(f.pixels[y*f.w+x],
                       ground_color(p,sub(point,p->center)),enhanced));
            }
        }
    }
    /* A face crossing the near plane and all screen edges must be clipped
       geometrically, keeping its projected vertices inside the viewport. */
    camera_position = vec(0,0,0); camera_yaw = camera_pitch = 0; nt = 0;
    triangle(vec(-100000, -100000, -10), vec(100000,-100000,100),
             vec(0,100000,100), 8);
    assert(nt > 0);
    for (i = 0; i < nt; i++) {
        int vertex;
        for (vertex = 0; vertex < 3; vertex++) {
            assert(triangles[i].x[vertex] >= -2 && triangles[i].x[vertex] <= f.w + 2);
            assert(triangles[i].y[vertex] >= -2 && triangles[i].y[vertex] <= view_h + 2);
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS terrain materials across 32 viewing angles in ECS/AGA and frustum clipping");
}
static void terrain_flight_colors(void) {
    Game g;
    Frame f = {320,256,0,0,{0}};
    const Planet *p;
    Vec site;
    int enhanced, target, altitude, shift, i;
    static const int heights[] = {2000, 4000, 5260, 10000};
    static const int targets[] = {0, 4500, -4500};
    game_init(&g, 0x53504143);
    p = &g.system.planets[0];
    site = sub(scale(g.ports[0].pos, 1, FP), p->center);
    g.docked = -1;
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    game = &g; frame = &f; view_h = 200;
    cache_globes();
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced;
        for (target = 0; target < 3; target++) {
            Vec point = terrain_vertex(p, site, vec(16384,0,0), vec(0,0,16384), targets[target], 0);
            int expected = ground_color(p, sub(point, p->center));
            for (altitude = 0; altitude < 4; altitude++)
                for (shift = -3000; shift <= 3000; shift += 300) {
                    Vec local = sub(point, p->center);
                    Vec direction = add(local, vec(shift,0,shift/2));
                    Vec eye = add(p->center, radial(direction,
                        p->radius + terrain_height(p, direction) + heights[altitude]));
                    g.pos = scale(eye, FP, 1);
                    camera_position = g.pos;
                    aim_angles(sub(point, eye), &camera_yaw, &camera_pitch);
                    nt = 0;
                    memset(f.pixels, 0, (size_t)f.w * f.h);
                    memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
                    planet_mesh(0);
                    qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
                    for (i = 0; i < nt; i++) raster(&triangles[i]);
                    if (!same_ground_material(f.pixels[(view_h/2)*f.w + f.w/2],expected,enhanced))
                        fprintf(stderr,"enhanced %d target %d height %d shift %d color %d expected %d pitch %d yaw %d\n",
                                enhanced,targets[target],heights[altitude],shift,
                                f.pixels[(view_h/2)*f.w + f.w/2],expected,camera_pitch,camera_yaw);
                    assert(same_ground_material(f.pixels[(view_h/2)*f.w + f.w/2],expected,enhanced));
                }
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS fixed ground colours during lateral flight and altitude changes in ECS/AGA");
}
static void terrain_smooth_motion(void) {
    Game g;
    Frame f = {320,256,0,0,{0}};
    int enhanced, step, i, last, moved;
    game_init(&g, 0x53504143); g.docked = -1;
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    game = &g; frame = &f; view_h = 200; cache_globes();
    camera_yaw = 0; camera_pitch = -256;
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced; last = -1; moved = 0;
        for (step = 0; step <= 1000; step++) {
            int edge = -1;
            g.pos = add(g.ports[0].pos, vec((int64_t)step * FP, 5260LL * FP, 0));
            camera_position = g.pos; nt = 0;
            memset(f.pixels, 0, (size_t)f.w * f.h);
            memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
            planet_mesh(0);
            qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
            for (i = 0; i < nt; i++) raster(&triangles[i]);
            for (i = 0; i < f.w; i++) {
                int color = f.pixels[100 * f.w + i];
                if (enhanced ? color >= 80 && color <= 94 : color >= 25 && color <= 27)
                    edge = i;
            }
            assert(edge >= 0);
            if (last >= 0) {
                /* One metre projects to roughly 0.03 pixels here. The coast
                   moves monotonically; polygon rounding used to make it jump
                   two pixels at once or even shift in the opposite direction. */
                if (!(edge <= last && last - edge <= 1))
                    fprintf(stderr,"motion step %d mode %d edge %d previous %d\n",step,enhanced,edge,last);
                assert(edge <= last && last - edge <= 1);
                moved += last - edge;
            }
            last = edge;
        }
        assert(moved >= 25 && moved <= 35);
    }
    free(f.pixels); render_shutdown();
    puts("PASS smooth monotonic terrain boundaries over 1000 one-metre steps in ECS/AGA");
}
static void terrain_fixed_geometry(void) {
    Game g;
    Frame f = {320,256,0,0,{0}};
    const Planet *p;
    int step, camera_u, j, u, jump_u = 0, biggest = 0, previous = 0;
    int64_t last_height = 0;
    game_init(&g,0x53504143); p = &g.system.planets[0]; g.docked = -1;
    /* Check every fine/coarse seam vertex, including negative coordinates and
       both sides of a clipmap recenter. Midpoints lie on the coarse edge. */
    for (step = 256; step <= 1024; step *= 2)
        for (camera_u = -513; camera_u <= 1025; camera_u += 1) {
            int camera_v = 37;
            int edge_u = terrain_floor(camera_u,step*2)*step*2 - TERRAIN_GRID*step;
            int v0 = terrain_floor(camera_v,step*2)*step*2 - TERRAIN_GRID*step;
            for (j = 1; j < TERRAIN_GRID*2; j++) {
                int v = v0 + j*step, pv = terrain_floor(v,step*2)*step*2;
                Vec fine = terrain_grid_vertex(p,2,edge_u,v,step,camera_u,camera_v);
                Vec a = terrain_grid_vertex(p,2,edge_u,pv,step*2,camera_u,camera_v);
                Vec b = terrain_grid_vertex(p,2,edge_u,pv+step*2,step*2,camera_u,camera_v);
                Vec coarse = add(a,scale(sub(b,a),v-pv,step*2));
                assert(length(sub(fine,coarse)) <= 2);
            }
        }
    /* A nearby world vertex stays put as the camera crosses grid boundaries. */
    {
        Vec reference = terrain_grid_vertex(p,2,256,0,256,0,0);
        for (camera_u = -512; camera_u <= 1024; camera_u++)
            assert(length(sub(reference,terrain_grid_vertex(p,2,256,0,256,camera_u,0))) == 0);
    }
    /* Distant morphing must also avoid large Q8 weight increments. */
    {
        Vec last = terrain_grid_vertex(p,2,360448,0,65536,0,0);
        for (camera_u = 1; camera_u <= 1024; camera_u++) {
            Vec next = terrain_grid_vertex(p,2,360448,0,65536,camera_u,0);
            assert(length(sub(next,last)) <= 2); last = next;
        }
    }
    /* Locate the steepest quantized height sample near the landing site.
       Rendering must interpolate fixed samples rather than jumping to that
       newly sampled height whenever the camera crosses it. */
    for (u = 900; u <= 2200; u++) {
        int h = terrain_elevation(p,vec(u,p->radius,0));
        if (u > 900 && abs(h-previous) > biggest) { biggest=abs(h-previous); jump_u=u; }
        previous=h;
    }
    assert(biggest > 0);
    f.pixels = calloc((size_t)f.w*f.h,1); assert(f.pixels && render_init(&f));
    game=&g; frame=&f; view_h=200; cache_globes(); camera_yaw=0; camera_pitch=-256;
    for (u = jump_u-5; u <= jump_u+5; u++) {
        int i;
        int64_t height;
        g.pos=scale(add(p->center,vec(u,p->radius+5260LL,0)),FP,1);
        camera_position=g.pos; nt=0;
        memset(depth_buffer,0,depth_capacity*sizeof(*depth_buffer));
        planet_mesh(0); qsort(triangles,(size_t)nt,sizeof(Tri),sort_tri);
        for (i=0;i<nt;i++) raster(&triangles[i]);
        assert(depth_buffer[100*320+160] > 0);
        height = 5260 - 1073741824LL / depth_buffer[100*320+160];
        if (u > jump_u-5) assert(llabs(height-last_height) <= 3);
        last_height=height;
    }
    free(f.pixels); render_shutdown();
    puts("PASS fixed terrain vertices, recentering seams, continuous far LOD and steep ground motion");
}
static void close_ground_detail(void) {
    Game g; Frame f={320,256,0,0,{0}};
    const Planet *p; int mode,u,v,base,seen[256],count,changed,previous;
    int64_t last=0;
    game_init(&g,0x53504143); g.docked=-1; p=&g.system.planets[0];
    f.pixels=calloc(320*256,1); assert(f.pixels && render_init(&f));
    game=&g; frame=&f; view_h=200; cache_globes();
    for (mode=0;mode<2;mode++) {
        f.enhanced=mode; memset(seen,0,sizeof(seen)); changed=0;
        for (u=-700;u<=700;u+=4) for (v=-700;v<=700;v+=4) {
            int color=surface_detail(p,2,u,v,1073741824U/120,1,0);
            seen[color]=1; base=surface_color(p,2,u,v);
            changed+=color!=base;
            assert(color>=0 && color<(mode ? 128 : 32));
            /* Minified and distant texture converges exactly to the original
               material, without altering the preserved coast regression. */
            assert(surface_detail(p,2,u,v,1073741824U/2100,1,0)==base);
            assert(surface_detail(p,2,u,v,1073741824U/120,64,0)==base);
        }
        count=0; for (u=0;u<256;u++) count+=seen[u];
        if (count<(mode ? 6 : 2) || changed<=1000) fprintf(stderr,"detail mode %d colours %d changed %d\n",mode,count,changed);
        assert(count>=(mode ? 6 : 2) && changed>1000);
        /* Cache eviction/revisit and unrelated camera movement cannot move
           the albedo at an identical body-fixed surface hit. */
        previous=surface_detail(p,2,-317,241,1073741824U/120,1,0);
        for (u=0;u<12000;u+=256) (void)surface_detail(p,2,u,u*3,1073741824U/120,1,0);
        g.pos=add(g.pos,vec(513*FP,0,0));
        assert(surface_detail(p,2,-317,241,1073741824U/120,1,0)==previous);
        /* Actual low-altitude nadir flight crosses steep raw-noise samples,
           fine grid recenters, and the landing-pad blend. Collision follows
           visible geometry to within integer/raster rounding. */
        camera_yaw=0; camera_pitch=-256;
        for (u=900;u<=2200;u+=5) {
            int i,h=terrain_height(p,vec(u,p->radius,0));
            int64_t visible;
            g.pos=scale(add(p->center,vec(u,p->radius+h+120LL,0)),FP,1);
            camera_position=g.pos; nt=0;
            memset(depth_buffer,0,depth_capacity*sizeof(*depth_buffer));
            planet_mesh(0); qsort(triangles,(size_t)nt,sizeof(Tri),sort_tri);
            for(i=0;i<nt;i++) raster(&triangles[i]);
            assert(depth_buffer[100*320+160]>0);
            visible=h+120-1073741824LL/depth_buffer[100*320+160];
            assert(llabs(visible-h)<=3);
            if(u>900) assert(llabs((visible-last)-(h-previous))<=6);
            last=visible; previous=h;
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS close ECS/AGA albedo, stable cache revisits, minification and 120 m render/collision flight");
}
static void procedural_geology(void) {
    Game g; Frame f={320,256,1,0,{0}};
    Planet p; int type, u, v, lo, hi, distinct=0;
    uint32_t previous=0;
    game_init(&g,0x53504143); p=g.system.planets[0]; set_rotation(&p,0);
    assert(render_init(&f)); game=&g; frame=&f;
    for (type=0;type<BODY_TYPES;type++) {
        uint32_t fingerprint=2166136261U;
        int material=type==ICE ? 4 : type==OCEAN ? 2 : type==GAS_GIANT ? 5 : 3;
        p.type=(BodyType)type; lo=100; hi=-100;
        for (u=-2048;u<=2048;u+=32) for (v=-1024;v<=1024;v+=32) {
            Vec q=vec(u,p.radius,v);
            int a=terrain_albedo(&p,q,material);
            assert(a>=-10 && a<=10);
            assert(a==terrain_albedo(&p,scale(q,2,1),material));
            fingerprint=(fingerprint^(uint32_t)a)*16777619U;
            if (a<lo) lo=a;
            if (a>hi) hi=a;
        }
        if (material!=5) assert(hi-lo>=6);
        else assert(lo==0 && hi==0);
        if (type>0) distinct+=fingerprint!=previous;
        previous=fingerprint;
        /* Neighbouring cache tiles share their reflectance boundary exactly,
           even across negative coordinates; no 256 m texture rectangles. */
        for (u=-512;u<=512;u+=256) {
            int left[5], i;
            unsigned slot=surface_lookup(&p,2,u-1,11);
            surface_albedo(&p,slot);
            for(i=0;i<5;i++) left[i]=surface_cache[slot].albedo[i*5+4];
            slot=surface_lookup(&p,2,u,11);
            surface_albedo(&p,slot);
            for(i=0;i<5;i++) assert(left[i]==surface_cache[slot].albedo[i*5]);
        }
        /* The positive X/Y cube-face boundary is the identical physical
           direction, with no dependence on the chart used to reach it. */
        for (v=-p.radius;v<=p.radius;v+=p.radius/8) {
            Vec xface=vec(p.radius,p.radius-1,v), yface=vec(p.radius-1,p.radius,v);
            assert(abs(terrain_albedo(&p,xface,material)-terrain_albedo(&p,yface,material))<=1);
        }
    }
    assert(distinct==BODY_TYPES-1);
    /* Changing a planet's size/type must invalidate its metre-scale cache. */
    p.type=ICE; p.radius/=2;
    {
        unsigned slot=surface_lookup(&p,2,0,0);
        assert(!surface_cache[slot].albedo_ready);
        surface_albedo(&p,slot);
        assert(surface_cache[slot].albedo[0]==terrain_albedo(&p,vec(0,p.radius,0),4));
    }
    render_shutdown();
    puts("PASS seeded geological diversity, spherical sampling and shared tile boundaries");
}
static void rolled_camera(void) {
    Game g;
    Frame f = {320,200,0,0,{0}};
    Vec v, local, expected;
    int mode;
    game_init(&g, 3);
    g.yaw = g.pitch = 0; g.roll = 256;
    f.pixels = calloc(320 * 200, 1); assert(f.pixels && render_init(&f));
    for (mode = 0; mode < 5; mode++) {
        g.camera_mode = mode;
        render_game(&g, &f);
        assert(camera_roll == (mode == 0 ? 256 : mode == 1 ? 768 : 0));
        if (!mode) {
            v = camera(add(scale(g.pos, 1, FP), vec(1000,0,2000)));
            assert(v.x == 0 && v.y == 1000 && v.z == 2000);
            /* Terrain rays must invert the same rolled projection as meshes. */
            local = camera_to_local(&g.system.planets[g.body], scale(v, 16, 1));
            expected = body_rotate(&g.system.planets[g.body],
                sub(add(scale(g.pos, 1, FP / 16), vec(16000,0,32000)),
                    scale(g.system.planets[g.body].center, 16, 1)), 1);
            assert(length(sub(local, expected)) <= 2);
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS rolled front/rear cameras, stable turret/external cameras and inverse terrain rays");
}
int main(void) {
    persistent_target_markers();
    Frame f = {64, 120, 1, 0, {0}};
    Tri ground = {{0, 64, 0}, {0, 0, 64}, {1000, 1000, 1000}, 1000, 8, NULL, {0}, {0}, {0}, 0};
    Tri wall = {{0, 64, 0}, {0, 0, 64}, {400, 2000, 400}, 1200, 16, NULL, {0}, {0}, {0}, 0};
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
    resource_visibility();
    terrain_angles();
    terrain_flight_colors();
    terrain_smooth_motion();
    terrain_fixed_geometry();
    close_ground_detail();
    procedural_geology();
    environment_rendering();
    compact_display();
    orbital_and_radar();
    hetha_target_bearing();
    warp_readout();
    rolled_camera();
    puts("PASS depth, polar contrast, PAL/NTSC layout, compact menus and 200-line C2P");
    return 0;
}
