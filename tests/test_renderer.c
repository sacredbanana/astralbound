/* SPDX-License-Identifier: Unlicense
   Isolated raster regressions exercise intersecting surfaces directly. */
#include <assert.h>
#include "../src/render.c"
#include "../src/native_video.h"
static void weapon_visibility(void) {
    static Game g;
    Frame f={320,200,0,NULL,{0}};
    Graphics saved=graphics;
    Settings settings;
    uint8_t *first=malloc(640*480);
    int mode,quality,i,count,different;
    f.pixels=malloc(640*480); assert(f.pixels && first);
    for(mode=0;mode<3;mode++) for(quality=0;quality<2;quality++) {
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        settings_defaults(&settings,1); settings_preset(&settings,quality ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&settings.gfx); assert(render_init(&f));
        game_init(&g,0x53504143); g.docked=g.frame_body=-1;
        g.pos=vec(0,AU_METRES*FP,0); g.yaw=g.pitch=g.roll=0;
        render_game(&g,&f); frame=&f; game=&g; view_h=f.h-32*ui_scale(&f);
        memset(f.pixels,0,(size_t)f.w*f.h);
        for(i=0;i<f.w*view_h;i++) depth_buffer[i]=1073741824U/100;
        weapon_beam(add(g.pos,vec(-300*FP,100*FP,600*FP)),add(g.pos,vec(300*FP,100*FP,1200*FP)),11,1);
        for(i=0;i<f.w*f.h;i++) assert(!f.pixels[i]); /* hidden by terrain/hulls */
        memset(depth_buffer,0,depth_capacity*sizeof(*depth_buffer));
        weapon_bolt(&g,add(g.pos,vec(-400*FP,130*FP,800*FP)),add(g.pos,vec(400*FP,130*FP,1800*FP)),vec(0,0,0),11);
        weapon_effects(); count=0;
        for(i=0;i<f.w*view_h;i++) count+=f.pixels[i]!=0;
        assert(count>5); memcpy(first,f.pixels,(size_t)f.w*f.h);
        weapons_tick(&g); memset(f.pixels,0,(size_t)f.w*f.h); weapon_effects(); different=0;
        for(i=0;i<f.w*view_h;i++) different+=first[i]!=f.pixels[i];
        assert(different>5); /* a moving streak, not a fixed cockpit overlay */
        weapons_reset(&g); memset(f.pixels,0,(size_t)f.w*f.h);
        g.projectiles[0]=(Missile){add(g.pos,vec(150*FP,80*FP,500*FP)),vec(800*FP,0,400*FP),1,0,HZ*20};
        weapon_effects(); count=0;
        for(i=0;i<f.w*view_h;i++) count+=f.pixels[i]!=0;
        assert(count>2); /* missiles retain a visible engine at minimum detail */
        memset(f.pixels,0,(size_t)f.w*f.h);
        /* Clip a long beam and a crossing of the camera near plane. */
        weapon_beam(add(g.pos,vec(-100000*FP,0,100*FP)),add(g.pos,vec(100000*FP,0,100*FP)),12,1);
        weapon_beam(add(g.pos,vec(30*FP,10*FP,-100*FP)),add(g.pos,vec(30*FP,10*FP,1000*FP)),13,1);
        count=0;
        for(i=0;i<f.w*view_h;i++) { assert(f.pixels[i]<32); count+=f.pixels[i]!=0; }
        assert(count>=f.w);
        for(i=f.w*view_h;i<f.w*f.h;i++) assert(!f.pixels[i]);
        memset(f.pixels,0,(size_t)f.w*f.h);
        weapon_beam(add(g.pos,vec(0,0,-500*FP)),add(g.pos,vec(0,0,-100*FP)),12,1);
        for(i=0;i<f.w*f.h;i++) assert(!f.pixels[i]);
    }
    free(first); free(f.pixels); render_shutdown(); render_graphics(&saved);
    puts("PASS moving laser bolts and missile exhaust, depth occlusion, near/screen clipping and HUD bounds in ECS/AGA/RTG Low/High");
}
static void destruction_visibility(void) {
    static Game g;
    Frame f = {320,200,0,NULL,{0}};
    int mode, i, colored;
    DestructionSprite sprite = { {0,0,1000}, 100, 0, 1, 1 };
    f.pixels = malloc(640 * 480); assert(f.pixels);
    for (mode = 0; mode < 3; mode++) {
        f.w = mode == 2 ? 640 : 320; f.h = mode == 2 ? 480 : mode == 1 ? 256 : 200;
        f.enhanced = mode != 0;
        assert(render_init(&f)); frame = &f;
        view_h = f.h - 32 * ui_scale(&f);
        memset(f.pixels, 0, (size_t)f.w * f.h);
        for (i = 0; i < f.w * view_h; i++) depth_buffer[i] = 1073741824U / 500;
        destruction_disc(&sprite);
        for (i = 0; i < f.w * f.h; i++) assert(!f.pixels[i]);
        memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
        destruction_disc(&sprite); colored = 0;
        for (i = 0; i < f.w * view_h; i++) colored += f.pixels[i] != 0;
        assert(colored > 100);
        for (i = f.w * view_h; i < f.w * f.h; i++) assert(!f.pixels[i]);
        game_init(&g, 0x53504143);
        g.docked = g.frame_body = -1; g.pos = vec(0, AU_METRES * FP, 0);
        destruction_player(&g, -1); game_frame(&g, (Controls){0}, 12);
        render_game(&g, &f);
        assert(view_h == f.h - 32 * ui_scale(&f));
        { Vec focus = camera(scale(g.pos, 1, FP));
          assert(focus.z > 100 && llabs(focus.x) < focus.z / 100 && llabs(focus.y) < focus.z / 100); }
        colored = 0;
        for (i = 0; i < f.w * view_h; i++) {
            int c = f.pixels[i];
            if (!f.enhanced) assert(c < 32);
            colored += c == 12 || c == 15 || c == 31;
        }
        assert(colored > 100);
    }
    free(f.pixels); render_shutdown();
    puts("PASS destruction particles hidden by terrain depth, cinematic framing and ECS/AGA/RTG viewport bounds");
}
static void speed_readout(void) {
    static const int64_t speeds[] = {600, 100000, LIGHT_SPEED / 100 - 1,
        LIGHT_SPEED / 100, LIGHT_SPEED / 2, LIGHT_SPEED, SYSTEM_DRIVE_SPEED};
    static const char *expected[] = {"V 600 M/S", "V 100 KM/S", "V 2997 KM/S",
        "V 0.01 C", "V 0.50 C", "V 1.00 C", "V 30.00 C"};
    char buf[32];
    int i;
    for (i = 0; i < (int)(sizeof(speeds) / sizeof(speeds[0])); i++) {
        speed_text(speeds[i], buf, sizeof(buf));
        assert(!strcmp(buf, expected[i]));
    }
    puts("PASS speed readout transitions from m/s and km/s through 0.01c to 30c");
}
static void body_labels(void) {
    Game g;
    Frame f = {320,200,0,0,{0}};
    int layout, i, x, y, count;
    f.pixels = malloc(640 * 480); assert(f.pixels);
    for (layout = 0; layout < 3; layout++) {
        f.w = layout == 2 ? 640 : 320; f.h = layout == 2 ? 480 : layout == 1 ? 256 : 200;
        assert(render_init(&f));
        game_init(&g, 1);
        g.docked = g.frame_body = -1;
        g.pos = vec(0,0,0); g.yaw = g.pitch = g.roll = 0;
        g.system.count = 3; g.system.stars = 1;
        g.system.star[0].center = vec(-1000000000,0,2000000000);
        for (i = 0; i < 3; i++) {
            g.system.planets[i].center = vec((i - 1LL) * 500000000,0,2000000000);
            snprintf(g.system.planets[i].name,24,"BODY %d",i);
        }
        g.body = 0; g.target = -1; g.target_body = 0;
        render_game(&g, &f);
        /* Clear geometry, then inspect the overlay using the same camera. */
        memset(f.pixels, 0, (size_t)f.w * f.h);
        system_body_labels();
        count = 0;
        for (y = 20; y < view_h - 20; y++) for (x = 0; x < f.w; x++)
            if (f.pixels[y * f.w + x] == 7) count++;
        assert(count > 150); /* All three unselected/selected body names. */
        count = 0;
        for (y = 20; y < view_h - 20; y++) for (x = 0; x < f.w; x++)
            if (f.pixels[y * f.w + x] == 12) count++;
        assert(count > 30); /* Primary star name. */
        /* A rearward body must not produce a mirrored forward label. */
        for (i = 0; i < 3; i++) g.system.planets[i].center.z *= -1;
        g.system.star[0].center.z *= -1;
        memset(f.pixels, 0, (size_t)f.w * f.h);
        system_body_labels();
        for (i = 0; i < f.w * f.h; i++) assert(!f.pixels[i]);
    }
    free(f.pixels); render_shutdown();
    puts("PASS space body and star labels in PAL/NTSC/RTG, without mirrored rear labels");
}
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
    return surface_cache[surface_lookup_level(p,face,u,v,0)].color;
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
    for (page = MAP; page <= SERVICES; page++) {
        g.page = page;
        assert(render_init(&full));
        render_game(&g, &full);
        assert(render_init(&small));
        render_game(&g, &small);
        /* The shipyard preview and its stat bars grow with the screen; the
           hull list and purchase status beside them keep one layout. */
        /* Galaxy viewport responds to height; separate galaxy tests exercise
           picking and controls in every layout. Other menus retain fixed rows. */
        for (y = 20; page != MAP && y < 138; y++)
            assert(!memcmp(small.pixels + y * 320 + 12, full.pixels + y * 320 + 12,
                           page == SHIPYARD ? 106 : 296));
    }
    /* Every mission story/orders/terms pane fits the same 200-line content area.
       Compare the rendered content with PAL so extra height cannot hide text. */
    for(page=BOARD;page<=LOG;page+=LOG-BOARD) {
        int type,tab;
        g.page=page; g.selection=0; g.mission_detail=1;
        for(type=0;type<MISSION_TYPES;type++) for(tab=0;tab<3;tab++) {
            Mission *m=page==BOARD?&g.board[0]:&g.contracts[0];
            *m=g.board[1]; m->type=(MissionType)type; m->state=page==BOARD?OFFERED:ACTIVE;
            g.mission_tab=tab;
            assert(render_init(&small)); render_game(&g,&small);
            assert(render_init(&full)); render_game(&g,&full);
            for(y=34;y<136;y++) assert(!memcmp(small.pixels+y*320,full.pixels+y*320,320));
        }
    }
    g.mission_detail=0;
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
        g.pos=scale(add(p->center,local),FP,1); camera_position=g.pos;
        memset(depth_buffer,0,(size_t)f.w*view_h*sizeof(*depth_buffer));
        memset(f.pixels,0,(size_t)f.w*f.h); weather_overlay();
        assert(f.pixels[100*320+160] || f.pixels[100*320+161] || f.pixels[102*320+160]);
        g.pos=scale(add(p->center,radial(local,p->radius+4000)),FP,1); camera_position=g.pos;
        memset(f.pixels,0,(size_t)f.w*f.h); weather_overlay();
        assert(!f.pixels[100*320+160]); break;
    }
    assert(a<1024);
    free(f.pixels); render_shutdown();
    puts("PASS procedural flora geometry and visible cloud entry/exit fog");
}
/* A planet as world() draws it: the analytic sphere (immediately, with
   backdrop depth near the ground) plus the terrain triangles it queues. */
static void planet_scene(int body) {
    const Planet *p = &game->system.planets[body];
    int64_t altitude = length(sub(scale(viewer(), 1, FP), p->center)) - p->radius;
    planet_disc(body, body == game->body && altitude < PATCH_ALTITUDE && p->type != GAS_GIANT);
    planet_mesh(body);
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
   
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced;
        for (angle = 0; angle < 1024; angle += 32) {
            camera_yaw = angle; camera_pitch = -256;
            nt = 0;
            memset(f.pixels, 0, (size_t)f.w * f.h);
            memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
            planet_scene(0);
            for (i = 0; i < nt; i++) raster(&triangles[i]);
            for (i = 0; i < 5; i++) {
                Vec point = terrain_vertex(p, local, vec(16384,0,0), vec(0,0,16384),
                                           offsets[i][0], offsets[i][1]);
                Vec v = camera(point);
                int x = f.w / 2 + (int)(v.x * (f.w / 2) / v.z);
                int y = view_h / 2 - (int)(v.y * (f.w / 2) / v.z);
                /* The same ground locations retain their material as the
                   camera rotates. The grass pad must not paint the rock around it. */
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
                    planet_scene(0);
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
/* From orbit down to the detail clipmap, the ground beneath a point keeps
   the material it has on landing. REACH lies 300 km out to sea, while one
   globe facet corner touched a distant coast and painted it green. */
static void orbital_ground_materials(void) {
    Game g;
    Frame f = {320,256,0,0,{0}};
    const Planet *p;
    Vec sites[2];
    int enhanced, site, altitude, i;
    static const int altitudes[] = {25000, 120000, 970000};
    game_init(&g, 0x53504143);
    p = &g.system.planets[0];
    g.docked = -1; g.body = 0;
    /* Each 1 km pad is smaller than a facet: REACH's is an island, HAVEN's
       a grass clearing among rock. Sample the open sea and land around them. */
    sites[0] = radial(add(sub(scale(g.ports[2].pos, 1, FP), p->center), vec(20000,0,0)), p->radius);
    sites[1] = radial(add(sub(scale(g.ports[0].pos, 1, FP), p->center), vec(50000,0,0)), p->radius);
    assert(terrain_material(p, sites[0]) == 0 && terrain_material(p, sites[1]) != 0);
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    game = &g; frame = &f; view_h = 200;
   
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced;
       
        for (site = 0; site < 2; site++)
            for (altitude = 0; altitude < 3; altitude++) {
                Vec ground = add(p->center, sites[site]);
                Vec eye = add(ground, add(radial(sites[site], altitudes[altitude]),
                                          vec(altitudes[altitude] / 3, 0, 0)));
                int expected = ground_color(p, sites[site]);
                g.pos = scale(eye, FP, 1);
                camera_position = g.pos;
                aim_angles(sub(ground, eye), &camera_yaw, &camera_pitch);
                nt = 0;
                memset(f.pixels, 0, (size_t)f.w * f.h);
                memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
                planet_scene(0);
                qsort(triangles, (size_t)nt, sizeof(Tri), sort_tri);
                for (i = 0; i < nt; i++) raster(&triangles[i]);
                if (!same_ground_material(f.pixels[(view_h/2)*f.w + f.w/2], expected, enhanced))
                    fprintf(stderr, "enhanced %d site %d altitude %d color %d expected %d\n",
                            enhanced, site, altitudes[altitude],
                            f.pixels[(view_h/2)*f.w + f.w/2], expected);
                assert(same_ground_material(f.pixels[(view_h/2)*f.w + f.w/2], expected, enhanced));
            }
    }
    free(f.pixels); render_shutdown();
    puts("PASS orbital sea and land beneath ports match the landed ground in ECS/AGA");
}
static void terrain_smooth_motion(void) {
    Game g;
    Frame f = {320,256,0,0,{0}};
    int enhanced, step, i, last, moved;
    const Planet *p;
    int64_t shore, z = -1061000, ground;
    game_init(&g, 0x53504143); g.docked = -1;
    p = &g.system.planets[0];
    /* HAVEN stands 1000 km inland. South of it a 6 km arm of the sea meets
       land along the screen-centre row (fixed z); walk east to that shore. */
    for (shore = -38000;; shore++) {
        int64_t r = p->radius;
        ground = (int64_t)isqrt64((uint64_t)(r * r - shore * shore - z * z));
        if (terrain_material(p, vec(shore, ground, z))) break;
        assert(shore < -30000);
    }
    assert(terrain_material(p, vec(shore - 1, ground, z)) == 0);
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels && render_init(&f));
    game = &g; frame = &f; view_h = 200;
    camera_yaw = 0; camera_pitch = -256;
    for (enhanced = 0; enhanced < 2; enhanced++) {
        f.enhanced = enhanced; last = -1; moved = 0;
        for (step = 0; step <= 1000; step++) {
            int edge = -1;
            g.pos = scale(add(p->center, vec(shore - 1000 + step, ground + 5260, z)), FP, 1);
            camera_position = g.pos; nt = 0;
            memset(f.pixels, 0, (size_t)f.w * f.h);
            memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
            planet_scene(0);
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
    game=&g; frame=&f; view_h=200; camera_yaw=0; camera_pitch=-256;
    for (u = jump_u-5; u <= jump_u+5; u++) {
        int i;
        int64_t height;
        g.pos=scale(add(p->center,vec(u,p->radius+5260LL,0)),FP,1);
        camera_position=g.pos; nt=0;
        memset(depth_buffer,0,depth_capacity*sizeof(*depth_buffer));
        planet_scene(0); qsort(triangles,(size_t)nt,sizeof(Tri),sort_tri);
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
    game=&g; frame=&f; view_h=200;
    for (mode=0;mode<2;mode++) {
        f.enhanced=mode; memset(seen,0,sizeof(seen)); changed=0;
        /* Straddle HAVEN's level grass pad and the natural ground east of it. */
        for (u=300;u<=1700;u+=4) for (v=-700;v<=700;v+=4) {
            int color=surface_detail(p,2,u,v,1073741824U/120,1,0);
            seen[color]=1; base=surface_color(p,2,u,v);
            changed+=color!=base;
            assert(color>=0 && color<(mode ? 128 : 32));
            /* Minified and distant texture converges exactly to the original
               material. */
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
            planet_scene(0); qsort(triangles,(size_t)nt,sizeof(Tri),sort_tri);
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
            unsigned slot=surface_lookup_level(&p,2,u-1,11,0);
            surface_albedo(&p,slot);
            for(i=0;i<5;i++) left[i]=surface_cache[slot].albedo[i*5+4];
            slot=surface_lookup_level(&p,2,u,11,0);
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
        unsigned slot=surface_lookup_level(&p,2,0,0,0);
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
#ifdef ASTRALBOUND_FPU
            {
                double q[3];
                ray_local(&g.system.planets[g.body], v.x * 16.0, v.y * 16.0, v.z * 16.0, q);
                local = vec(fpu_int64(q[0]), fpu_int64(q[1]), fpu_int64(q[2]));
            }
#else
            {
                int64_t q[3];
                ray_local(&g.system.planets[g.body], scale(v, 16, 1), q);
                local = vec(q[0], q[1], q[2]);
            }
#endif
            expected = body_rotate(&g.system.planets[g.body],
                sub(add(scale(g.pos, 1, FP / 16), vec(16000,0,32000)),
                    scale(g.system.planets[g.body].center, 16, 1)), 1);
            assert(length(sub(local, expected)) <= 2);
        }
    }
    free(f.pixels); render_shutdown();
    puts("PASS rolled front/rear cameras, stable turret/external cameras and inverse terrain rays");
}
/* Observer 3 km above open ground, looking along a meridian at view_pitch;
   the star sits at sun_pitch ahead of the observer, or behind it. */
static void sky_view(Game *g, Frame *f, int climate, int sun_pitch, int behind, int view_pitch) {
    static const int32_t air[6] = {0, 80000, 20000, 150000, 8000, 400000};
    Vec up = unit(vec(0, 16000, 3000), 16384), north = unit(vec(0, -3000, 16000), 16384), view, sun;
    Planet *p;
    game_init(g, 0x53504143);
    p = &g->system.planets[0];
    p->climate = (AtmosphereType)climate;
    p->atmosphere = air[climate];
    g->docked = -1; g->system.count = 1; g->system.stars = 1; g->body = 0;
    g->pos = add(scale(p->center, FP, 1),
                 scale(up, (p->radius + terrain_height(p, scale(up, p->radius, 16384)) + 3000LL) * FP, 16384));
    view = add(scale(north, cosi(view_pitch), 16384), scale(up, sini(view_pitch), 16384));
    aim_angles(view, &g->yaw, &g->pitch);
    sun = add(scale(north, behind ? -cosi(sun_pitch) : cosi(sun_pitch), 16384), scale(up, sini(sun_pitch), 16384));
    g->system.star[0].center = add(scale(g->pos, 1, FP), scale(sun, 150000000000LL, 16384));
    g->system.star[0].type = ST_G;
    render_game(g, f);
}
/* Open-sky pixels (no geometry) within [lo, hi], or isolated star points. */
static int sky_pixels(const Frame *f, int lo, int hi, int stars) {
    int x, y, n = 0;
    for (y = 45; y < view_h - 30; y++)
        for (x = 1; x < f->w - 1; x++) {
            int c = f->pixels[y * f->w + x];
            if (depth_buffer[y * f->w + x]) continue;
            if (stars ? (c == 7 || c == 15) && f->pixels[y * f->w + x - 1] != c &&
                            f->pixels[y * f->w + x + 1] != c && f->pixels[(y - 1) * f->w + x] != c &&
                            f->pixels[(y + 1) * f->w + x] != c
                      : c >= lo && c <= hi)
                n++;
        }
    return n;
}
static void night_and_twilight_skies(void) {
    static Game g;
    Frame f = {320, 256, 1, 0, {0}};
    static const uint8_t ecs_warm[4] = {21, 22, 14, 12};
    int i, warm = 0;
    f.pixels = calloc(320 * 256, 1); assert(f.pixels && render_init(&f));
    /* Stars come out at night through clear air and on airless ground, never
       under a noon sky or the cloud deck of a DENSE world. */
    sky_view(&g, &f, TEMPERATE, -200, 0, 200); assert(sky_pixels(&f, 0, 0, 1) >= 3);
    sky_view(&g, &f, VACUUM, 100, 1, 200); assert(sky_pixels(&f, 0, 0, 1) >= 3);
    sky_view(&g, &f, TEMPERATE, 100, 1, 200); assert(!sky_pixels(&f, 0, 0, 1));
    sky_view(&g, &f, DENSE, -200, 0, 200); assert(!sky_pixels(&f, 0, 0, 1));
    sky_view(&g, &f, HYDROGEN, -200, 0, 200); assert(!sky_pixels(&f, 0, 0, 1));
    /* Sunset colour follows composition: N2/O2 burns orange-red, thin CO2 dust
       glows blue round the sun, and both bands sit towards the sun. */
    sky_view(&g, &f, TEMPERATE, 3, 0, 0);
    assert(sky_pixels(&f, 128, 159, 0) > 1000);
    sky_view(&g, &f, TEMPERATE, 3, 1, 0);
    assert(sky_pixels(&f, 128, 159, 0) < 1000);
    sky_view(&g, &f, THIN, 3, 0, 0);
    assert(sky_pixels(&f, 160, 191, 0) > 1000 && !sky_pixels(&f, 128, 159, 0));
    sky_view(&g, &f, TEMPERATE, 100, 0, 0);
    assert(!sky_pixels(&f, 128, 159, 0));
    f.enhanced = 0; assert(render_init(&f));
    sky_view(&g, &f, TEMPERATE, 3, 0, 0);
    for (i = 0; i < 4; i++) warm += sky_pixels(&f, ecs_warm[i], ecs_warm[i], 0);
    assert(warm > 1000);
    free(f.pixels); render_shutdown();
    puts("PASS stars by night and in vacuum, hidden by day and cloud; composition-tinted sunsets");
}
/* Scenery lights must obey scene depth, including hull backs and terrain,
   while the station's actual central approach remains visibly open. */
/* Title sequence frames: letterbox bars stay black outside captions, every
   act draws a picture in all layouts, and the cinematic camera never leaks
   into ordinary flight rendering. */
static void classic_intro_sun_occlusion(void) {
    static Game g;
    static Intro in;
    static const int seconds[] = {4, 8, 12};
    Frame f = {320, 256, 0, NULL, {0}};
    Graphics saved = graphics;
    Settings s;
    uint8_t *reference = malloc(640 * 480);
    int mode, quality, t, effect, visible[2] = {0, 0};
    f.pixels = malloc(640 * 480);
    assert(f.pixels && reference);
    render_renderer(RENDERER_CLASSIC);
    for (mode = 0; mode < 4; mode++) for (quality = 0; quality < 2; quality++) {
        f.w = mode == 3 ? 640 : 320; f.h = mode == 3 ? 480 : mode == 2 ? 200 : 256;
        f.enhanced = mode != 0;
        settings_defaults(&s, 0); settings_preset(&s, quality ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&s.gfx); assert(render_init(&f));
        intro_start(&in, &g, 0x53504143);
        for (t = 0; t < 3; t++) {
            int glows, beams, bar = (f.h - f.w * 100 / 185) / 2;
            intro_frame(&in, &g, seconds[t] * HZ - in.tick);
            glows = in.glows; beams = in.beams;
            assert(glows && beams);
            in.glows = in.beams = 0;
            render_intro(&in, &g, &f);
            memcpy(reference, f.pixels, (size_t)f.w * f.h);
            /* Test the solar bloom and streak separately: neither may paint
               over the foreground planet, and both must emerge outside it. */
            for (effect = 0; effect < 2; effect++) {
                int b, x, y, interior = 0, wrong = 0;
                in.glows = effect == 0 ? glows : 0;
                in.beams = effect == 1 ? beams : 0;
                render_intro(&in, &g, &f);
                for (b = 0; b < cl_body_count && cl_body_info[b].body != 0; b++) {}
                assert(b < cl_body_count);
                for (y = 0; y < f.h - 2 * bar; y++) {
                    int lo, hi;
                    if (!planet_row_pixels(&cl_body_info[b].view, y, &lo, &hi)) continue;
                    /* Keep clear of the subpixel silhouette and its halo. */
                    for (x = lo + 2; x <= hi - 2; x++) {
                        int at = (y + bar) * f.w + x;
                        interior++; wrong += reference[at] != f.pixels[at];
                    }
                }
                if (wrong) fprintf(stderr, "Classic sunrise mode %d quality %d at %ds effect %d: %d planet pixels overwritten\n",
                                   mode, quality, seconds[t], effect, wrong);
                assert(interior > 100 && !wrong);
                for (x = 0; x < f.w * f.h; x++) visible[effect] += reference[x] != f.pixels[x];
            }
            in.glows = glows; in.beams = beams;
        }
        render_shutdown();
    }
    assert(visible[0] > 100 && visible[1] > 100);
    render_renderer(RENDERER_ENHANCED);
    render_graphics(&saved);
    free(reference); free(f.pixels);
    puts("PASS Classic sunrise bloom and streak hidden by the planet and visible beyond its limb in ECS/AGA/NTSC/RTG Low/High");
}
static void intro_frames(void) {
    static Game g, plain;
    static Intro in;
    static const int seconds[] = {6, 18, 24, 31, 40, 48, 54, 61, 70, 86, 97, 103, 112, 132, 140, 146, 160, 172, 190};
    Frame f = {320, 256, 1, NULL, {0}};
    uint8_t *reference;
    int mode, i;
    f.pixels = malloc(640 * 480); reference = malloc(640 * 480);
    assert(f.pixels && reference);
    for (mode = 0; mode < 4; mode++) {
        f.w = mode == 3 ? 640 : 320; f.h = mode == 3 ? 480 : mode == 2 ? 200 : 256;
        f.enhanced = mode != 0;
        assert(render_init(&f));
        intro_start(&in, &g, 0x53504143);
        for (i = 0; i < (int)(sizeof(seconds) / sizeof(*seconds)); i++) {
            int x, y, colours = 0, seen[256] = {0}, bar = (f.h - f.w * 100 / 185) / 2;
            intro_frame(&in, &g, seconds[i] * HZ - in.tick);
            memset(f.pixels, 77, (size_t)f.w * f.h);
            render_intro(&in, &g, &f);
            assert(!cinema);
            for (y = 0; y < f.h; y++) for (x = 0; x < f.w; x++) {
                int c = f.pixels[y * f.w + x];
                if (!seen[c]) { seen[c] = 1; colours++; }
                if (!in.title && y < bar) assert(c == 0); /* captions use only the lower bar */
            }
            assert(colours >= 4);
        }
        render_shutdown();
    }
    /* Ordinary flight renders identically before and after a title frame. */
    f.w = 320; f.h = 256; f.enhanced = 1;
    assert(render_init(&f));
    game_init(&plain, 0x53504143);
    render_game(&plain, &f); memcpy(reference, f.pixels, (size_t)f.w * f.h);
    intro_start(&in, &g, 0x53504143); intro_frame(&in, &g, 40 * HZ);
    render_intro(&in, &g, &f);
    render_game(&plain, &f);
    assert(!memcmp(reference, f.pixels, (size_t)f.w * f.h));
    render_shutdown();
    free(f.pixels); free(reference);
    puts("PASS title sequence frames on ECS/AGA/NTSC/RTG, black letterbox and isolated cinematic camera");
}
static void port_scenery(void) {
    static Game g, before;
    Frame f={320,200,0,NULL,{0}};
    PortFrame s={ {0,0,1200}, {16384,0,0}, {0,0,-16384}, {0,16384,0} };
    Port p={0};
    int layout, variant, x, y, n;
    uint32_t hashes[3];
    f.pixels=malloc(640*480); assert(f.pixels);
    game_init(&g,0x53504143);
    before=g;
    for(layout=0;layout<3;layout++) {
        f.w=layout==2 ? 640 : 320;
        f.h=layout==2 ? 480 : layout==1 ? 256 : 200;
        f.enhanced=layout!=0;
        assert(render_init(&f));
        frame=&f; game=&g; view_h=f.h-32*ui_scale(&f);
        camera_position=vec(0,0,0); camera_yaw=camera_pitch=camera_roll=0;
        draw_scale=1;
        for(variant=0;variant<3;variant++) {
            uint32_t h=2166136261U;
            memset(f.pixels,0,(size_t)f.w*f.h);
            memset(depth_buffer,0,depth_capacity*sizeof(*depth_buffer));
            nt=port_light_count=0;
            port_orbital(&p,s,(unsigned)variant,1,1);
            assert(nt>200 && nt<1000 && port_light_count<PORT_LIGHT_MAX);
            draw_triangles(); port_draw_lights();
            /* The 120 m docking bore is empty, in every design and layout. */
            for(y=-4;y<=4;y++) for(x=-4;x<=4;x++)
                assert(!f.pixels[(view_h/2+y)*f.w+f.w/2+x]);
            n=0;
            for(y=0;y<f.h;y++) for(x=0;x<f.w;x++) {
                int c=f.pixels[y*f.w+x];
                assert(c<32);
                if(y>=view_h) assert(!c);
                n+=c!=0;
                h=(h^(unsigned)c)*16777619U;
            }
            assert(n>800);
            hashes[variant]=h;
        }
        assert(hashes[0]!=hashes[1] && hashes[1]!=hashes[2] && hashes[0]!=hashes[2]);
        /* A beacon in front, hidden behind a nearer occluder, behind the
           camera, then outside the viewport. No overlay can leak into HUD. */
        for(n=0;n<5;n++) {
            s.p=vec(n==3 ? 100000 : 0,n==4 ? -10000 : 0,n==2 ? -100 : 1000);
            memset(f.pixels,0,(size_t)f.w*f.h);
            for(x=0;x<f.w*f.h;x++) depth_buffer[x]=n==1 ? 1073741824U/500 : 0;
            port_light_count=0;
            port_lamp(&s,0,0,0,14,-1); port_draw_lights();
            y=0; for(x=0;x<f.w*f.h;x++) y+=f.pixels[x]!=0;
            assert(n==0 ? y>0 : y==0);
        }
        s.p=vec(0,0,1200);
        /* A clear light changes only on the presentation clock. */
        port_light_count=0; g.ui_clock=0;
        port_lamp(&s,0,0,0,14,0); assert(port_light_count==1);
        g.ui_clock=4; port_light_count=0;
        port_lamp(&s,0,0,0,14,0); assert(port_light_count==0);
        g.ui_clock=8; port_lamp(&s,0,0,0,14,0); assert(port_light_count==1);
        g.ui_clock=16; port_light_count=0;
        port_lamp(&s,0,0,0,14,0); assert(port_light_count==0);
        g.ui_clock=0;
        /* The richer surface view fits the reserved scenery budget. */
        camera_position=add(g.ports[0].pos,scale(g.ports[0].normal,1500LL*FP,16384));
        aim_angles(sub(g.ports[0].pos,camera_position),&camera_yaw,&camera_pitch);
        nt=port_light_count=0;
        port_geometry(0);
        assert(nt>400 && nt<2000);
        assert(port_light_count<PORT_LIGHT_MAX);
    }
    assert(!memcmp(&g,&before,sizeof(g))); /* rendering cannot affect saves/physics */
    free(f.pixels); render_shutdown();
    puts("PASS varied port structures, open docking bore, scenery budget, beacon timing and depth in ECS/AGA/RTG");
}
static void low_city_terrain(void) {
    static Game g;
    Frame f={320,200,0,NULL,{0}};
    Settings s; Graphics saved=graphics;
    uint32_t *flat=malloc(640*480*sizeof(*flat));
    int mode,detail,x,y;
    f.pixels=malloc(640*480); assert(f.pixels && flat);
    for(mode=0;mode<3;mode++) for(detail=0;detail<2;detail++) {
        const Planet *p; Vec front,right,up;
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        settings_defaults(&s,1); s.gfx.scale=0; s.gfx.terrain=detail;
        render_graphics(&s.gfx); assert(render_init(&f));
        performance_scene(&g,4); render_performance_scene(&g,&f);
        {
            int holes=0;
            for(y=f.h/2;y<f.h;y++) for(x=0;x<f.w;x++) holes+=depth_buffer[y*f.w+x]==0;
            assert(holes<f.w/32); /* no black slivers through the ground */
        }
        memcpy(flat,depth_buffer,(size_t)f.w*f.h*sizeof(*flat));
        p=&g.system.planets[g.body]; front=forward(camera_yaw,camera_pitch);
        right=vec(cosi(camera_yaw),0,-sini(camera_yaw)); up=unit(cross(front,right),16384);
        for(y=f.h*3/4;y<f.h*15/16;y+=f.h/16) for(x=f.w*3/8;x<=f.w*5/8;x+=f.w/8) {
            uint32_t d=depth_buffer[y*f.w+x]; int64_t z; Vec at,local;
            assert(d); z=1073741824LL/d;
            at=add(scale(camera_position,1,FP),add(scale(front,z,16384),
                add(scale(right,(x-f.w/2)*z/(f.w/2),16384),scale(up,(f.h/2-y)*z/(f.w/2),16384))));
            local=sub(at,p->center);
            assert(llabs(length(local)-p->radius-terrain_height(p,local))<32);
        }
        s.gfx.texture=1; render_graphics(&s.gfx); render_performance_scene(&g,&f);
        assert(!memcmp(flat,depth_buffer,(size_t)f.w*f.h*sizeof(*flat)));
        /* Check the small window's actual recentering edges, including the
           negative side where floor snapping previously stretched the fade. */
        for(x=-512;x<=512;x+=64) {
            int step=256,edge,along;
            for(edge=-1;edge<=1;edge+=2) for(along=-4;along<=4;along++) {
                int u=terrain_floor(x+step,step*2)*step*2+edge*TERRAIN_LOW_GRID*step;
                int v=along*step,pv=terrain_floor(v,step*2)*step*2;
                Vec child=terrain_grid_vertex(p,2,u,v,step,x,0);
                Vec a=terrain_grid_vertex(p,2,u,pv,step*2,x,0);
                Vec b=terrain_grid_vertex(p,2,u,pv+step*2,step*2,x,0);
                assert(length(sub(child,add(a,scale(sub(b,a),v-pv,step*2))))<4);
            }
        }
    }
    free(flat); free(f.pixels); render_shutdown(); render_graphics(&saved);
    puts("PASS Low/Minimum city ground matches collision, flat/textured depth agrees and small terrain LOD edges join in ECS/AGA/RTG");
}
static void scaled_flight_labels(void) {
    static Game g;
    Frame f={320,200,0,NULL,{0}};
    Settings s; Graphics saved=graphics;
    uint8_t *baseline=malloc(640*480);
    int mode,pixel_size,x,y;
    f.pixels=malloc(640*480+16); assert(f.pixels && baseline);
    for(mode=0;mode<3;mode++) {
        int height;
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        game_init(&g,0x53504143); g.docked=g.frame_body=-1; g.pos=vec(0,AU_METRES*FP,0);
        g.yaw=g.pitch=g.roll=0; g.nports=0; g.target=-1; g.target_body=g.body=0;
        memset(g.npc,0,sizeof(g.npc)); g.system.count=1;
        g.system.planets[0].center=add(scale(g.pos,1,FP),vec(0,0,10000000));
        g.system.planets[0].radius=1000; g.system.planets[0].atmosphere=0;
        g.system.planets[0].climate=VACUUM; g.system.planets[0].parent=-1;
        snprintf(g.system.planets[0].name,sizeof(g.system.planets[0].name),"TEXT SCALE");
        settings_defaults(&s,1); s.gfx.labels=1; s.gfx.stars=0;
        for(pixel_size=0;pixel_size<4;pixel_size++) {
            s.gfx.scale=pixel_size; render_graphics(&s.gfx); assert(render_init(&f));
            memset(f.pixels+f.w*f.h,0xa5,16); render_game(&g,&f); height=ui_view_height(&g,&f);
            if(!pixel_size) memcpy(baseline,f.pixels,(size_t)f.w*f.h);
            /* Target name, view/contact status and world body label use the
               same glyph pixels and layout at 1x, 2x, 3x and 4x 3D pixels. */
            for(y=8;y<40;y++) for(x=7;x<150;x++) assert(f.pixels[y*f.w+x]==baseline[y*f.w+x]);
            for(y=height/2+6;y<height/2+14;y++)
                for(x=f.w/2+7;x<f.w/2+67;x++) assert(f.pixels[y*f.w+x]==baseline[y*f.w+x]);
            for(x=0;x<16;x++) assert(f.pixels[f.w*f.h+x]==0xa5);
        }
        for(y=0;y<2;y++) {
            Frame expected=f;
            int phase=y,i;
            expected.pixels=baseline; frame=&expected; draw_scale=1;
            memset(baseline,0,(size_t)f.w*f.h);
            if(phase) hyperspace_tunnel_labels(&g,32); else hyperspace_charge_labels(32);
            g.hyperspace=phase ? HYPER_CHARGE+33 : 33;
            for(pixel_size=0;pixel_size<4;pixel_size++) {
                s.gfx.scale=pixel_size; render_graphics(&s.gfx); render_game(&g,&f);
                for(i=0;i<f.w*30;i++) if(baseline[i]) assert(f.pixels[i]==baseline[i]);
            }
        }
    }
    free(baseline); free(f.pixels); render_shutdown(); render_graphics(&saved);
    puts("PASS native-size flight and body labels independent of 1x/2x/3x/4x 3D pixel size in ECS/AGA/RTG");
}
/* Curved ring boundaries must retain holes, clip to the viewport, and
   compare the true planar depth against foreground geometry. */
static void curved_ring_depth(void) {
    Frame f={320,200,1,NULL,{0}};
    Planet p={0};
    int i, pose;
    f.pixels=malloc(320*200); assert(f.pixels && render_init(&f));
    frame=&f; view_h=f.h; view_top=view_left=view_right_inset=proj_dx=proj_dy=0;
    graphics.rings=4; p.radius=1000; p.rings=1;
    camera_position=vec(0,0,-4000LL*FP); camera_yaw=camera_pitch=camera_roll=0;
    memset(f.pixels,0,320*200); memset(depth_buffer,0,320*200*sizeof(*depth_buffer));
    assert(ring_curves(&p));
    assert(f.pixels[100*320+160]==0); /* central hole */
    assert(f.pixels[100*320+235]==19);
    assert(depth_buffer[100*320+235]==1073741824U/4000);
    /* A foreground surface across the right half must win every test. */
    for(i=0;i<320*200;i++) {
        f.pixels[i]=7;
        depth_buffer[i]=1073741824U/(i%320>=160 ? 2000 : 8000);
    }
    assert(ring_curves(&p));
    assert(f.pixels[100*320+235]==7 && f.pixels[100*320+85]==19);
    for(i=0;i<320*200;i++) if(i%320>=160) assert(f.pixels[i]==7);
    /* Rolled/pitched cameras, including the opposite side of the plane:
       verify every written depth against a double-precision ray/plane hit. */
    for(pose=0;pose<4;pose++) {
        int count=0;
        int64_t height;
        camera_position=vec(0,0,(pose==2 ? 4000LL : -4000LL)*FP);
        camera_yaw=pose==2 ? 512 : 0; camera_pitch=pose==1 ? 48 : 0; camera_roll=pose*123;
        view_left=pose==3 ? 150 : 0; view_top=pose==3 ? 70 : 0;
        view_right_inset=pose==3 ? 80 : 0; proj_dx=pose==3 ? 23 : 0; proj_dy=pose==3 ? -17 : 0;
        memset(f.pixels,0,320*200); memset(depth_buffer,0,320*200*sizeof(*depth_buffer));
        assert(ring_curves(&p)); height=p.center.z-camera_position.z/FP;
        for(i=0;i<320*200;i++) if(depth_buffer[i]) {
            int x=i%320,y=i/320;
            double dot=view_matrix.m[0][2]*(x-160-proj_dx)+view_matrix.m[1][2]*(100+proj_dy-y)+view_matrix.m[2][2]*160;
            assert(x>=view_left && x<320-view_right_inset && y>=view_top);
            double expected=1073741824.0*dot/(height*160.0*16384.0);
            double error=depth_buffer[i]-expected;
            assert(error>-2 && error<2); count++;
        }
        assert(count>1000);
    }
    view_left=view_top=view_right_inset=proj_dx=proj_dy=0;
    /* Near-plane crossing: request the existing clipped mesh, never leave
       half a curve in the crossing table or partially draw an annulus. */
    camera_position=vec(0,-1800LL*FP,-100LL*FP);
    aim_angles(vec(0,1800,100),&camera_yaw,&camera_pitch); camera_roll=0;
    memset(f.pixels,7,320*200);
    assert(!ring_curves(&p));
    for(i=0;i<320*200;i++) assert(f.pixels[i]==7);
    assert(cl_spans.top==cl_spans.height && !cl_spans.bottom);
    graphics.rings=0; assert(ring_curves(&p));
    for(i=0;i<320*200;i++) assert(f.pixels[i]==7);
    free(f.pixels); render_shutdown();
    puts("PASS curved ring holes, foreground occlusion, planar depth, rolled views and near-plane fallback");
}
int main(int argc, char **argv) {
    if(argc>1 && !strcmp(argv[1],"--low-graphics")) { low_city_terrain(); scaled_flight_labels(); return 0; }
    curved_ring_depth();
    port_scenery();
    if(argc>1 && !strcmp(argv[1],"--ports")) return 0;
    classic_intro_sun_occlusion();
    intro_frames();
    persistent_target_markers();
    night_and_twilight_skies();
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
    orbital_ground_materials();
    terrain_smooth_motion();
    terrain_fixed_geometry();
    close_ground_detail();
    procedural_geology();
    environment_rendering();
    compact_display();
    orbital_and_radar();
    hetha_target_bearing();
    warp_readout();
    speed_readout();
    destruction_visibility();
    weapon_visibility();
    body_labels();
    rolled_camera();
    low_city_terrain();
    scaled_flight_labels();
    puts("PASS depth, polar contrast, PAL/NTSC layout, compact menus and 200-line C2P");
    return 0;
}
