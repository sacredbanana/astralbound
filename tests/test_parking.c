/* SPDX-License-Identifier: Unlicense */
/* Parking without landing gear: DOCK flies in and settles, LAND descends in
   place when the ground is safe, and a landed commander may walk outside. */
#include "astralbound.h"
#include "controller.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SEED 0x53504143
static int find_port(const Game *g, const char *name) {
    int i;
    for (i = 0; i < g->nports; i++)
        if (strstr(g->ports[i].name, name)) return i;
    assert(0);
    return -1;
}
static void fly(Game *g, int seconds) {
    Controls c = {0};
    int i;
    for (i = 0; i < seconds * HZ && g->docked == -1 && !g->dead; i++)
        game_tick(g, c);
}
static int64_t rest_height(const Game *g) {
    const Planet *p = &g->system.planets[g->body];
    Vec local = sub(g->pos, scale(p->center, FP, 1));
    return length(local) / FP - p->radius - terrain_height(p, scale(local, 1, FP));
}
/* Hover height metres above dry, gentle ground on body, away from ports. */
static void place_over_land(Game *g, int body, int height) {
    const Planet *p = &g->system.planets[body];
    int a, b;
    memset(g->npc, 0, sizeof(g->npc));
    for (a = 0; a < 1024; a += 16)
        for (b = -200; b <= 200; b += 40) {
            Vec dir = forward(a, b), local;
            int ground;
            local = radial(dir, p->radius);
            ground = terrain_height(p, local);
            local = radial(dir, (int64_t)p->radius + ground + height);
            g->docked = -1;
            g->body = g->frame_body = body;
            g->pos = add(scale(p->center, FP, 1), scale(local, FP, 1));
            g->vel = surface_velocity(p, local);
            g->autopilot = g->landing = 0;
            g->assist = 1;
            g->throttle = 0;
            if (park_mode(g, NULL) == PARK_LAND && !land_refusal(g) && ground > 50) return;
        }
    assert(0);
}
static void dock_from_pad_departure(void) {
    Game g;
    int port, enabled, active;
    game_init(&g, SEED);
    port = g.docked;
    assert(park_mode(&g, NULL) == PARK_NONE);
    /* Sitting on the pad the parking button opens the hatch. */
    assert(!strcmp(park_label(&g, &enabled, &active), "EXIT") && enabled && park_key(&g) == 'k');
    game_action(&g, 'l');
    fly(&g, 5);
    assert(g.docked == -1 && park_mode(&g, NULL) == PARK_DOCK);
    assert(!strcmp(park_label(&g, &enabled, &active), "DOCK") && enabled && !active);
    game_action(&g, 'd');
    assert(g.autopilot && g.target == port);
    assert(!strcmp(park_label(&g, &enabled, &active), "DOCK") && enabled && active);
    fly(&g, 300);
    printf("pad dock: tick %lu, hull %d\n", (unsigned long)g.tick, g.hull);
    assert(g.docked == port && g.hull == 100 && !g.autopilot);
    /* DOCK pressed again in flight aborts the approach. */
    game_action(&g, 'l');
    fly(&g, 2);
    game_action(&g, 'd');
    assert(g.autopilot);
    game_action(&g, 'd');
    assert(!g.autopilot && g.assist && strstr(g.message, "ABORTED"));
}
/* Pressed from behind a station, DOCK swings wide of the hull before it
   turns into the corridor instead of flying straight through the disc. */
static void dock_station_from_behind(void) {
    Game g;
    int port, i;
    int64_t widest = 0;
    Vec side;
    game_init(&g, SEED);
    game_action(&g, 'l');
    port = find_port(&g, "B RELAY");
    memset(g.npc, 0, sizeof(g.npc));
    g.frame_body = g.body = g.ports[port].body;
    side = vec(g.ports[port].normal.y, -g.ports[port].normal.x, 0);
    if (length(side) < 4096) side = vec(0, g.ports[port].normal.z, -g.ports[port].normal.y);
    g.pos = add(g.ports[port].pos, add(scale(g.ports[port].normal, -4000LL * FP, 16384),
                                       scale(unit(side, 16384), 60LL * FP, 16384)));
    g.vel = port_relative_velocity(&g, port);
    g.target = -1;
    g.target_body = 0;
    assert(park_mode(&g, &i) == PARK_DOCK && i == port);
    game_action(&g, 'd');
    assert(g.autopilot && g.target == port);
    for (i = 0; i < 900 * HZ && g.docked == -1 && !g.dead; i++) {
        Vec rel = sub(g.pos, g.ports[port].pos);
        int64_t ahead = (rel.x * g.ports[port].normal.x + rel.y * g.ports[port].normal.y +
                         rel.z * g.ports[port].normal.z) / 16384;
        int64_t lateral = length(sub(rel, scale(g.ports[port].normal, ahead, 16384))) / FP;
        if (lateral > widest) widest = lateral;
        game_tick(&g, (Controls){0});
    }
    printf("station dock from behind: %d ticks, %lld m wide, hull %d\n", i, (long long)widest, g.hull);
    assert(g.docked == port && g.hull == 100 && widest > 360); /* hull radius 246 m */
}
static void land_in_place(void) {
    Game g;
    int enabled, active, body;
    int64_t speed = 0;
    Vec site, anchor;
    game_init(&g, SEED);
    body = g.ports[g.docked].body;
    game_action(&g, 'l');
    place_over_land(&g, body, 2500);
    assert(!strcmp(park_label(&g, &enabled, &active), "LAND") && enabled && !active);
    site = body_rotate(&g.system.planets[body], sub(g.pos, scale(g.system.planets[body].center, FP, 1)), 1);
    game_action(&g, 'd');
    assert(g.landing == body + 1 && !g.autopilot);
    /* "Where you are": the touchdown point is body-fixed, directly beneath. */
    anchor = g.land_anchor;
    assert(length(sub(unit(site, 16384), unit(anchor, 16384))) <= 2);
    assert(!strcmp(park_label(&g, &enabled, &active), "LAND") && enabled && active);
    {
        Controls c = {0};
        int i;
        for (i = 0; i < 600 * HZ && g.docked == -1 && !g.dead; i++) {
            speed = length(sub(g.vel, surface_velocity(&g.system.planets[body],
                    scale(sub(g.pos, scale(g.system.planets[body].center, FP, 1)), 1, FP))));
            game_tick(&g, c);
        }
        printf("land in place: %d ticks, touchdown %lld cm/s, hull %d\n", i,
               (long long)(speed * 100 / FP), g.hull);
    }
    assert(g.docked == -2 && !g.dead && g.hull == 100 && !g.landing);
    assert(speed < 4 * FP && llabs(rest_height(&g) - 12) <= 1);
    assert(g.ground_anchor.x == anchor.x && g.ground_anchor.y == anchor.y && g.ground_anchor.z == anchor.z);
    assert(park_mode(&g, NULL) == PARK_NONE);
    /* Aborting a landing hands back an assisted hover. */
    game_action(&g, 'l');
    place_over_land(&g, body, 2500);
    game_action(&g, 'd');
    assert(g.landing);
    game_action(&g, 'd');
    assert(!g.landing && g.assist && !g.throttle && strstr(g.message, "ABORTED"));
}
static void land_refusals(void) {
    Game g;
    int i, a, enabled, active, ocean = -1, body;
    game_init(&g, SEED);
    body = g.ports[g.docked].body;
    game_action(&g, 'l');
    /* Deep space: neither dock nor land. */
    g.pos = add(scale(g.system.planets[body].center, FP, 1),
                vec(0, ((int64_t)g.system.planets[body].radius + 900000) * FP, 0));
    g.vel = vec(0, 0, 0);
    memset(g.npc, 0, sizeof(g.npc));
    assert(park_mode(&g, NULL) == PARK_NONE);
    assert(!strcmp(park_label(&g, &enabled, &active), "DOCK") && !enabled);
    game_action(&g, 'd');
    assert(!g.autopilot && !g.landing && strstr(g.message, "NO PORT"));
    /* Hostile traffic blocks a landing. */
    place_over_land(&g, body, 1500);
    g.npc[0].active = g.npc[0].hostile = 1;
    g.npc[0].hull = 50;
    g.npc[0].pos = add(g.pos, vec(2000LL * FP, 0, 0));
    game_action(&g, 'd');
    assert(!g.landing && strstr(g.message, "HOSTILES"));
    memset(g.npc, 0, sizeof(g.npc));
    /* Open water. */
    for (i = 0; i < g.system.count && ocean < 0; i++)
        if (g.system.planets[i].type == OCEAN) ocean = i;
    if (ocean >= 0) {
        const Planet *p = &g.system.planets[ocean];
        for (a = 0; a < 1024; a += 8) {
            Vec local = radial(forward(a, 0), (int64_t)p->radius + 1000);
            if (terrain_material(p, local)) continue;
            g.body = g.frame_body = ocean;
            g.pos = add(scale(p->center, FP, 1), scale(local, FP, 1));
            g.vel = surface_velocity(p, local);
            if (park_mode(&g, NULL) == PARK_LAND) break;
        }
        assert(a < 1024);
        assert(land_refusal(&g) && !strcmp(land_refusal(&g), "WATER BELOW"));
        game_action(&g, 'd');
        assert(!g.landing && strstr(g.message, "WATER"));
    }
    /* Gas giants have nothing to stand on. */
    for (i = 0; i < g.system.count && g.system.planets[i].type != GAS_GIANT; i++) {}
    if (i < g.system.count) {
        const Planet *p = &g.system.planets[i];
        g.body = g.frame_body = i;
        g.pos = add(scale(p->center, FP, 1), vec(0, ((int64_t)p->radius + 5000) * FP, 0));
        assert(park_mode(&g, NULL) != PARK_LAND);
    }
    puts("PASS dock from launch and abort, station corridor swing, land in place, refusals");
}
/* Any slow contact is a landing now that the ship has no gear to forget. */
static void gearless_touchdown(void) {
    Game g;
    int body, i;
    game_init(&g, SEED);
    body = g.ports[g.docked].body;
    game_action(&g, 'l');
    /* A 24 m free fall reaches the ground far above 8 m/s: a crash. */
    place_over_land(&g, body, 30);
    g.assist = 0;
    for (i = 0; i < 60 * HZ && g.docked == -1 && g.hull > 0; i++)
        game_tick(&g, (Controls){0});
    assert(g.docked == -1 && g.hull <= 0);
    /* Two metres above contact height settles gently onto the ground. */
    game_init(&g, SEED);
    game_action(&g, 'l');
    place_over_land(&g, body, 8);
    g.assist = 0;
    for (i = 0; i < 60 * HZ && g.docked == -1 && !g.dead; i++)
        game_tick(&g, (Controls){0});
    assert(g.docked == -2 && !g.dead && g.hull == 100);
    assert(llabs(rest_height(&g) - 12) <= 1);
}
static void on_foot(void) {
    Game g, h;
    GroundSite sites[GROUND_SITES], again[GROUND_SITES], site;
    Controls walk = {0};
    int i, a, n, body, enabled, active, credits;
    int64_t metres, start;
    const Planet *p;
    char path[] = "build/parking-test.sav";
    UiWidget v[UI_MAX_WIDGETS];
    Frame f = {320, 200, 0, NULL, {0}};
    game_init(&g, SEED);
    /* A station berth has no ground. */
    game_action(&g, 'l');
    g.pos = g.ports[find_port(&g, "B RELAY")].pos;
    g.docked = find_port(&g, "B RELAY");
    assert(!disembark(&g) && !strcmp(disembark_refusal(&g), "STATION BERTH"));
    game_init(&g, SEED);
    /* A surface pad does. */
    assert(g.ports[g.docked].surface && !disembark_refusal(&g));
    game_action(&g, 'k');
    assert(g.on_foot && walker_ship_distance(&g) <= board_reach(&g));
    /* S walks back; it does not open the port's services from outside. */
    game_action(&g, 's');
    assert(g.page == FLIGHT && g.on_foot);
    game_action(&g, 'k');
    assert(!g.on_foot);
    game_action(&g, 's');
    assert(g.page == SERVICES);
    g.page = FLIGHT;
    /* E beside the hull boards when no site is in reach. */
    game_action(&g, 'k');
    if (!(nearest_site(&g, &site, &metres) && metres <= SITE_REACH)) {
        game_action(&g, 'e');
        assert(!g.on_foot);
    } else game_action(&g, 'k');
    /* Open ground after a LAND. */
    body = g.ports[g.docked].body;
    game_action(&g, 'l');
    assert(!strcmp(disembark_refusal(&g), "LAND FIRST"));
    place_over_land(&g, body, 300);
    game_action(&g, 'd');
    fly(&g, 300);
    assert(g.docked == -2);
    p = &g.system.planets[body];
    /* Landed, LAND becomes EXIT; there is no separate hatch button. */
    n = ui_widgets(&g, &f, v);
    for (i = 0; i < n && strcmp(v[i].label, "LAND"); i++) {}
    assert(i == n);
    for (i = 0; i < n && v[i].action != 'k'; i++) {}
    assert(i < n && !strcmp(v[i].label, "EXIT") && v[i].enabled);
    a = i;
    for (i++; i < n && v[i].action != 'k'; i++) {}
    assert(i == n);
    game_action(&g, v[a].action);
    assert(g.on_foot && safe_warp(&g) == 1);
    /* Outside, the same slot uses a site in reach or else boards. */
    n = ui_widgets(&g, &f, v);
    for (i = 0; i < n && v[i].action != 'k'; i++) {}
    assert(i == n);
    for (i = 0; i < n && v[i].action != 'l'; i++) {}
    assert(i < n && !v[i].enabled);
    for (i = 0; i < n && strcmp(v[i].label,
         nearest_site(&g, &site, &metres) && metres <= SITE_REACH ? "USE" : "BOARD"); i++) {}
    assert(i < n && v[i].enabled && v[i].action == 'e');
    game_action(&g, 'l');
    assert(g.docked == -2 && g.on_foot && strstr(g.message, "BOARD FIRST"));
    /* Walk away: feet stay on the ground as the planet turns beneath. */
    start = walker_ship_distance(&g);
    walk.thrust = 1;
    g.walk_yaw = (g.walk_yaw + 512) & 1023; /* face away from the hull */
    for (i = 0; i < 20 * HZ; i++) {
        Vec feet = sub(walker_position(&g), scale(p->center, FP, 1));
        Planet still = *p;
        game_tick(&g, walk);
        set_rotation(&still, 0);
        assert(llabs(length(g.walker) - (int64_t)p->radius * FP -
                     terrain_height_fine(&still, g.walker)) < FP / 8);
        (void)feet;
    }
    printf("walked %lld m from the hull\n", (long long)(walker_ship_distance(&g) - start));
    assert(walker_ship_distance(&g) > start + 20 || strstr(g.message, "WATER") || strstr(g.message, "STEEP"));
    /* A/D sidestep at walking pace without turning; a diagonal is no
       faster, and half an analogue stick is half pace. */
    {
        Controls side = {0}, diagonal = {0}, half = {0};
        int yaw = g.walk_yaw;
        int64_t moved;
        Vec before;
        side.strafe = 1;
        diagonal.thrust = diagonal.strafe = -1;
        half.axis_walk = 128;
        g.message[0] = 0;
        before = g.walker;
        for (i = 0; i < HZ; i++) game_tick(&g, side);
        moved = length(sub(g.walker, before));
        assert(g.walk_yaw == yaw);
        assert(g.message[0] || (moved > 5 * FP / 2 && moved < 7 * FP / 2));
        g.message[0] = 0;
        before = g.walker;
        for (i = 0; i < HZ; i++) game_tick(&g, diagonal);
        moved = length(sub(g.walker, before));
        assert(g.message[0] || (moved > 3 * FP / 2 && moved < 7 * FP / 2));
        g.message[0] = 0;
        before = g.walker;
        for (i = 0; i < HZ; i++) game_tick(&g, half);
        moved = length(sub(g.walker, before));
        assert(g.message[0] || (moved > 5 * FP / 4 && moved < 7 * FP / 4));
    }
    /* A pad's left stick walks and sidesteps, the right stick looks, the
       shoulders sidestep, green/Y and yellow/X look and blue/B boards. */
    {
        Controller pad = {0};
        Controls c = {0};
        controller_calibrate(&pad, 128, 128);
        controller_apply(&pad, &g, &c, 0, 255, 0, 2, 0, 255);
        assert(c.axis_walk == 256 && c.axis_strafe == 256);
        assert(c.axis_roll == -256 && c.axis_pitch == 256 && !c.thrust && !c.roll);
        c = (Controls){0};
        controller_apply(&pad, &g, &c, 0, 255, 128, 1, 128, 128);
        assert(c.axis_roll == 256 && !c.axis_strafe && !c.axis_walk);
        c = (Controls){0};
        controller_apply(&pad, &g, &c, PAD_THRUST | PAD_AUTO | PAD_FIRE, 128, 128, 0, 128, 128);
        assert(c.strafe == 1 && c.pitch == -1 && !c.fire);
        c = (Controls){0};
        controller_apply(&pad, &g, &c, PAD_REVERSE | PAD_TARGET, 128, 128, 0, 128, 128);
        assert(c.strafe == -1 && c.pitch == 1);
        if (walker_ship_distance(&g) > board_reach(&g)) {
            controller_apply(&pad, &g, &c, PAD_BRAKE, 128, 128, 0, 128, 128);
            assert(g.on_foot && strstr(g.message, "WALK BACK"));
        }
    }
    /* The eye stands above the feet with a level horizon. */
    {
        Vec eye;
        int yaw, pitch, roll;
        g.walk_pitch = 0;
        ground_view(&g, &eye, &yaw, &pitch, &roll);
        assert(llabs(length(sub(eye, walker_position(&g))) - EYE_HEIGHT * FP) < FP / 4);
    }
    /* Too far to board. */
    if (walker_ship_distance(&g) > board_reach(&g)) {
        game_action(&g, 'k');
        assert(g.on_foot);
    }
    /* Sites are deterministic and used once. */
    n = ground_sites(&g, walker_position(&g), sites, GROUND_SITES);
    assert(n == ground_sites(&g, walker_position(&g), again, GROUND_SITES));
    for (i = 0; i < n; i++)
        assert(sites[i].id == again[i].id && sites[i].kind == again[i].kind &&
               sites[i].anchor.x == again[i].anchor.x && sites[i].anchor.z == again[i].anchor.z);
    /* Find any site within a few kilometres to stand on. */
    for (i = 0; i < 64 && !n; i++) {
        Vec probe = add(g.walker, scale(unit(vec(g.walker.y, -g.walker.x, 0), 16384), (int64_t)SITE_CELL * FP * (i + 1), 16384));
        Planet still = *p;
        set_rotation(&still, 0);
        g.walker = radial(probe, (int64_t)p->radius * FP + terrain_height_fine(&still, probe));
        n = ground_sites(&g, walker_position(&g), sites, GROUND_SITES);
    }
    assert(n > 0);
    for (i = 0; i < n; i++) assert(sites[i].kind >= 0 && sites[i].kind < SITE_KINDS && sites[i].id);
    g.walker = sites[0].anchor;
    assert(nearest_site(&g, &site, &metres) && site.id == sites[0].id && metres <= SITE_REACH);
    assert(!strcmp(park_label(&g, &enabled, &active), "USE") && enabled);
    credits = g.credits;
    i = cargo_used(&g);
    /* D is a sidestep on foot; E uses the site. */
    game_action(&g, 'd');
    assert(cargo_used(&g) == i && g.credits == credits);
    game_action(&g, 'e');
    assert(cargo_used(&g) == i + 1 || g.credits > credits);
    assert(!nearest_site(&g, &site, &metres) || site.id != sites[0].id);
    game_action(&g, 'e');
    assert(strstr(g.message, "NOTHING IN REACH") || g.credits > credits || cargo_used(&g) > i);
    /* Saves keep the landed ship and put the commander back aboard. */
    assert(save_game(&g, path));
    memset(&h, 0, sizeof(h));
    assert(load_game(&h, path));
    remove(path);
    assert(h.docked == -2 && !h.on_foot && !h.landing);
    puts("PASS gear-free touchdown, disembark/board, walking, sidestep, pad controls, ground sites and saves");
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    dock_from_pad_departure();
    dock_station_from_behind();
    land_in_place();
    land_refusals();
    gearless_touchdown();
    on_foot();
    puts("All parking tests passed.");
    return 0;
}
