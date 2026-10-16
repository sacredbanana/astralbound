/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef __AMIGAOS3__
unsigned long __stack = 262144;
#define SLOT "career-test.sav"
#else
#define SLOT "build/career-test.sav"
#endif
static void orbit(Game *g) {
    game_action(g, 'l');
    g->body = g->frame_body = 0;
    g->pos = add(scale(g->system.planets[0].center, FP, 1),
                 vec(0, (g->system.planets[0].radius + 18000LL) * FP, 0));
    g->vel = vec(0, 0, 0); g->autopilot = 0;
}
static Mission *contract(Game *g, MissionType type) {
    Mission *m;
    g->board_page = type == PASSENGER ? 0 : 1;
    refresh_board(g);
    { int i;
      for (i = 0; i < MAX_MISSIONS; i++) if (g->board[i].type == type) break;
      assert(i < MAX_MISSIONS);
      assert(accept_mission(g, i)); }
    m = &g->contracts[0];
    return m;
}
static void test_ships_crew(void) {
    Game g;
    int credits, f;
    game_init(&g, 1); g.credits = 100000;
    assert(buy_ship(&g, 1));
    game_action(&g, 'l'); assert(g.docked == 0);
    assert(recruit_crew(&g, 1));
    game_action(&g, 'l'); assert(g.docked == -1);
    credits = g.credits;
    g.tick = MARKET_INTERVAL * 2 + 15;
    career_time(&g); assert(g.credits == credits - 50);
    career_time(&g); assert(g.credits == credits - 50);
    g.credits = 0; g.tick += MARKET_INTERVAL; career_time(&g); assert(!g.crew);
    g.docked = 0; g.credits = 100000;
    assert(buy_equipment(&g, CABIN)); assert(cargo_used(&g) == 2);
    assert(buy_equipment(&g, MISSILE_RACK));
    assert(buy_equipment(&g, COUNTERMEASURE));
    assert(!buy_equipment(&g, DORSAL));
    assert(upgrade_weapon(&g, 1) && g.turret_upgrade[0] == 1 && !g.upgrade);
    assert(sell_equipment(&g, VENTRAL)); assert(buy_ship(&g, 3));
    assert(!buy_equipment(&g, VENTRAL));
    assert(buy_ship(&g, 5)); assert(ship_specs[g.ship].acceleration == 30);
    assert(buy_ship(&g, 0));
    g.cargo[0] = 19; assert(!buy_equipment(&g, CABIN)); g.cargo[0] = 0;
    /* The shipyard explains the same refusal buy_ship applies. */
    assert(!strcmp(ship_refusal(&g, 0), "CURRENT HULL"));
    g.credits = 100; assert(!strcmp(ship_refusal(&g, 5), "SHORT OF CREDITS") && !buy_ship(&g, 5));
    g.credits = 100000; g.docked = -1; assert(!strcmp(ship_refusal(&g, 5), "DOCK TO BUY")); g.docked = 0;
    g.cargo[0] = 70; assert(!strcmp(ship_refusal(&g, 1), "HOLD TOO SMALL") && !buy_ship(&g, 1)); g.cargo[0] = 0;
    assert(!ship_refusal(&g, 2)); assert(buy_ship(&g, 2) && !strcmp(ship_refusal(&g, 2), "CURRENT HULL"));
    g.page = SHIPYARD; g.selection = 0;
    game_action(&g, 1001); assert(g.selection == SHIPS - 1);
    game_action(&g, 1002); assert(g.selection == 0);
    f = system_faction(&g); g.service[f] = 15; g.medals[f] = 3;
    assert(!strcmp(military_rank(&g, f), "CAPTAIN"));
    puts("PASS hull capacities, equipment limits, crew launch gates, wages and ranks");
}
static void test_terms_passengers(void) {
    Game g;
    Mission *m;
    int i, credits, f;
    game_init(&g, 37); g.credits = 100000;
    f = system_faction(&g); g.board_page = 1; refresh_board(&g);
    assert(g.board[1].type == ASSASSINATION);
    assert(!accept_mission(&g, 1));
    assert(!negotiate_mission(&g, 1));
    g.reputation[f] = 20; assert(negotiate_mission(&g, 1));
    assert(!negotiate_mission(&g, 1));
    g.board_page = 0; refresh_board(&g);
    for (i = 0; i < MAX_MISSIONS; i++) if (g.board[i].type == PASSENGER) break;
    assert(i < MAX_MISSIONS && !accept_mission(&g, i));
    assert(buy_equipment(&g, CABIN)); assert(buy_equipment(&g, CABIN));
    assert(accept_mission(&g, i)); m = &g.contracts[0]; credits = g.credits;
    assert(passengers_used(&g) == m->passengers && passengers_used(&g) > 0);
    setup_system(&g, m->destination); g.docked = m->port;
    career_missions(&g); assert(m->state == COMPLETE && passengers_used(&g) == 0);
    assert(g.credits == credits + m->reward);
    career_missions(&g); assert(g.credits == credits + m->reward);
    game_init(&g, 37); g.credits = 100000; g.reputation[system_faction(&g)] = 20;
    m = contract(&g, MULTILEG);
    setup_system(&g, m->destination); g.docked = m->port;
    career_missions(&g); assert(m->state == ACTIVE && m->leg == 1);
    career_missions(&g); assert(m->leg == 1);
    g.docked = m->port; career_missions(&g); assert(m->leg == 2);
    g.docked = m->port; career_missions(&g); assert(m->state == COMPLETE && m->leg == 3);
    puts("PASS reputation gates, bounded negotiation, passenger reservation, multi-leg payment once");
}
static void test_rescue_escort(void) {
    Game g;
    Mission *m;
    int i, escort;
    game_init(&g, 37); g.credits = 10000; buy_equipment(&g, CABIN);
    m = contract(&g, RESCUE); orbit(&g); career_missions(&g);
    assert(m->spawned && g.pods[0].active);
    g.pos = g.pods[0].pos; g.vel = g.pods[0].vel;
    assert(collect_cargo(&g)); assert(m->progress == 1);
    setup_system(&g, m->destination); g.docked = m->port;
    career_missions(&g); assert(m->state == COMPLETE);
    game_init(&g, 37); g.reputation[system_faction(&g)] = 20;
    m = contract(&g, ESCORT); orbit(&g); career_missions(&g);
    assert(m->spawned);
    for (escort = 0; escort < MAX_NPC; escort++) if (g.npc_role[escort] == CIVILIAN && g.npc[escort].active) break;
    assert(escort < MAX_NPC);
    for (i = 0; i < 120 * HZ; i++) career_missions(&g);
    assert(m->state == COMPLETE);
    game_init(&g, 37); g.reputation[system_faction(&g)] = 20;
    m = contract(&g, ESCORT); orbit(&g); career_missions(&g);
    damage_npc(&g, 0, 100); assert(m->state == FAILED);
    game_init(&g, 37); g.credits = 10000; buy_equipment(&g, CABIN);
    m = contract(&g, RESCUE); orbit(&g); career_missions(&g);
    g.tick = m->deadline + 1; career_missions(&g);
    assert(m->state == FAILED && !passengers_used(&g) && !g.pods[0].active);
    puts("PASS rescue pickup/delivery, escort survival, destruction and deadline cleanup");
}
static void test_combat_contracts(void) {
    Game g;
    Mission *m;
    int type, i, f;
    for (type = ASSASSINATION; type <= INSTALLATION; type++) {
        game_init(&g, 37); f = system_faction(&g); g.reputation[f] = 20;
        m = contract(&g, (MissionType)type); orbit(&g);
        if (type == ASSASSINATION) {
            career_missions(&g); assert(g.npc[0].active);
            damage_npc(&g, 0, 100); career_missions(&g);
            assert(m->state == COMPLETE && g.wanted[(f + 1) % FACTIONS] == 1000);
        } else if (type == RECON) {
            const Planet *p = &g.system.planets[g.ports[m->port].body];
            Vec local = sub(g.ports[m->port].pos, scale(p->center, FP, 1));
            g.body = g.frame_body = g.ports[m->port].body;
            local = radial(local, (p->radius + terrain_height(p, scale(local, 1, FP)) + 2000LL) * FP);
            g.pos = add(scale(p->center, FP, 1), local);
            g.vel = surface_velocity(p, scale(local, 1, FP));
            for (i = 0; i < 30 * HZ; i++) career_missions(&g);
            assert(m->state == COMPLETE && g.service[f] == 1);
        } else {
            g.pos = add(g.ports[m->port].pos, vec(0, 1500 * FP, 0));
            career_missions(&g); assert(m->spawned && g.npc_role[0] == INSTALLATION_TARGET);
            damage_npc(&g, 0, 200); career_missions(&g);
            assert(m->state == COMPLETE && g.service[f] == 1);
        }
    }
    puts("PASS assassination legal consequences, reconnaissance and surface-installation targets");
}
static void test_law_missiles(void) {
    Game g;
    Controls c = {0};
    int i, f;
    game_init(&g, 1); g.system.government = 3; g.credits = 100000;
    f = system_faction(&g);
    assert(!trade(&g, 6, 1));
    g.reputation[f] = 10; g.black_market = 1; assert(trade(&g, 6, 1));
    inspect_cargo(&g); assert(!g.cargo[6] && g.wanted[f] == 100);
    assert(settle_fine(&g)); assert(!g.wanted[f]);
    orbit(&g);
    g.npc[0].active = g.npc[0].hostile = 1; g.npc[0].mission = -1; g.npc[0].hull = 60;
    g.npc[0].pos = add(g.pos, vec(0, 0, 1000 * FP));
    g.npc[0].vel = g.vel; g.combat_target = 0;
    g.missiles = 2; g.countermeasures = 1;
    assert(countermeasure(&g)); assert(g.npc[0].cooldown == HZ * 12);
    assert(!countermeasure(&g));
    g.warp = MAX_WARP;
    assert(fire_missile(&g)); assert(g.missiles == 1 && g.warp == MAX_WARP);
    g.autopilot = 1; assert(safe_warp(&g) == MAX_WARP); g.autopilot = 0;
    for (i = 0; i < HZ * 5 && g.npc[0].active; i++) game_tick(&g, c);
    assert(!g.npc[0].active && g.kills == 1 && g.pods[0].active);
    g.pos = g.pods[0].pos; g.vel = g.pods[0].vel;
    assert(collect_cargo(&g) && g.cargo[2] == 1);
    /* A police encounter is a commander-time event near inhabited ports. */
    g.pos = add(g.ports[1].pos, scale(g.ports[1].normal, 3000 * FP, 16384));
    g.wanted[f] = 100; g.encounter_tick = g.tick; g.tick += HZ * 60;
    career_tick(&g, g.pos);
    for (i = 0; i < MAX_NPC; i++) if (g.npc[i].active && g.npc_role[i] == POLICE) break;
    assert(i < MAX_NPC);
    g.system.government = 0; g.wanted[f] = 0; memset(g.npc, 0, sizeof(g.npc));
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + 18000LL) * FP, 0));
    g.tick += HZ * 60; career_tick(&g, g.pos);
    assert(g.npc[0].active && g.npc_role[0] == RAIDER);
    puts("PASS contraband, black-market trading, customs/fines, police/piracy, missiles, decoys and salvage");
}
static void test_mining_saves(void) {
    Game g, h, before;
    Controls c = {0};
    int i, yaw, pitch;
    game_init(&g, 37); g.credits = 100000;
    assert(buy_equipment(&g, MINING)); assert(buy_equipment(&g, ESCAPE_POD));
    assert(buy_equipment(&g, SCOOP));
    orbit(&g); g.resource_target = 0; g.target = -1; g.target_body = 0;
    g.pos = add(asteroid_position(&g, 0), vec(0, 0, -400 * FP));
    g.vel = sub(body_velocity(&g.system, 0, g.tick), frame_velocity(&g));
    aim_angles(sub(asteroid_position(&g, 0), g.pos), &yaw, &pitch); g.yaw = yaw; g.pitch = pitch;
    assert(mine_asteroid(&g)); assert(g.deposits[0].ore[0] == 3);
    g.pos = add(g.pods[0].pos, vec(0, 0, -150 * FP)); g.vel = g.pods[0].vel;
    assert(collect_cargo(&g) && g.cargo[6] == 1);
    g.laser = 0; g.heat = 0;
    assert(save_game(&g, SLOT) && load_game(&h, SLOT));
    assert(h.deposit_count == 1 && h.deposits[0].ore[0] == 3 && h.resource_target == -1);
    setup_system(&h, (Sector){1, 0, 1}); setup_system(&h, g.system.sector);
    assert(h.deposits[0].ore[0] == 3);
    g.docked = -2; g.body = g.frame_body = 0;
    g.ground_anchor = body_rotate(&g.system.planets[0],
        sub(g.ports[0].pos, scale(g.system.planets[0].center, FP, 1)), 1);
    g.pos = g.ports[0].pos;
    assert(deploy_rig(&g));
    g.tick += MARKET_INTERVAL * 3 + 17; career_time(&g);
    assert(g.rigs[0].stock == 3);
    assert(save_game(&g, SLOT) && load_game(&h, SLOT));
    assert(h.rig_count == 1 && h.rigs[0].stock == 3);
    assert(recover_rig(&g)); assert(!g.rig_count && g.cargo[2] == 3);
    /* All bounded ledgers fit the portable stream simultaneously. */
    game_init(&g, 37);
    for (i = 0; i < MAX_DEPOSITS; i++) { g.deposits[i].sector.x = i; g.deposits[i].ore[0] = 4; }
    g.deposit_count = MAX_DEPOSITS;
    for (i = 0; i < MAX_EXPLORED; i++) { g.explored[i].sector.x = i; g.explored[i].visited = 1; }
    g.explored_count = MAX_EXPLORED;
    for (i = 0; i < MAX_MARKETS; i++) { g.markets[i].sector.x = i; g.markets[i].stock[0] = 2; }
    g.market_count = MAX_MARKETS;
    assert(save_game(&g, SLOT) && load_game(&h, SLOT));
    assert(h.deposit_count == MAX_DEPOSITS && h.market_count == MAX_MARKETS);
    before = h; h.deposits[0].ore[0] = 5; assert(!save_game(&h, SLOT)); h = before;
    h.cabins = -1; assert(!save_game(&h, SLOT));
    game_init(&g, 37); g.credits = 100000; buy_equipment(&g, ESCAPE_POD); orbit(&g);
    g.hull = 1; assert(escape_pod(&g)); assert(g.docked >= 0 && g.hull == 100 && !equipped(&g, ESCAPE_POD));
    assert(!escape_pod(&g));
    /* Scoop uses gas-giant atmosphere and relative air speed. */
    game_init(&g, 37); g.credits = 100000; buy_equipment(&g, SCOOP); orbit(&g);
    for (i = 0; i < g.system.count; i++) if (g.system.planets[i].type == GAS_GIANT) break;
    if (i == g.system.count) { i = 0; g.system.planets[0].climate = HYDROGEN; g.system.planets[0].atmosphere = 100000; }
    g.body = g.frame_body = i; g.fuel = 50; g.tick = HZ;
    { const Planet *p = &g.system.planets[i]; Vec local = vec(0, p->radius + p->atmosphere / 2, 0);
      g.pos = scale(add(p->center, local), FP, 1); g.vel = add(surface_velocity(p, local), vec(100 * FP, 0, 0)); }
    career_tick(&g, g.pos); assert(g.fuel == 51);
    g.paused = 1; game_tick(&g, c); assert(g.fuel == 51);
    remove(SLOT); remove(SLOT ".bak");
    puts("PASS finite saved asteroid deposits, rigs/hourly recovery, full ledgers, malformed state, pod rescue and fuel scoop");
}
static void put_word(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}
static void test_legacy_and_extension(void) {
    Game g, h, before;
    unsigned char data[24576];
    FILE *f;
    size_t len, end, i;
    uint32_t sum;
    int version;
    game_init(&g, 37); g.credits = 100000; g.upgrade = 2; assert(buy_ship(&g, 1));
    assert(save_game(&g, SLOT));
    f = fopen(SLOT, "rb"); assert(f); len = fread(data, 1, sizeof(data), f); fclose(f);
    for (end = 16; end + 4 < len; end += 4) if (data[end] == 0x43 && data[end+1] == 0x41 &&
        data[end+2] == 0x52 && data[end+3] == 0x36) break;
    assert(end < len);
    for (version = 4; version <= 6; version++) {
        size_t legacy_len = version == 6 ? len - 4 : end;
        put_word(data + 4, (uint32_t)version); put_word(data + 8, (uint32_t)(legacy_len - 16));
        sum = 2166136261U; for (i = 16; i < legacy_len; i++) sum = (sum ^ data[i]) * 16777619U;
        put_word(data + 12, sum);
        f = fopen(SLOT, "wb"); assert(f && fwrite(data, 1, legacy_len, f) == legacy_len); fclose(f);
        assert(load_game(&h, SLOT));
        assert(h.crew == (version == 6 ? g.crew : 1) && h.ship == 1 && h.credits == g.credits);
        assert(h.turret_upgrade[0] == (version == 6 ? g.turret_upgrade[0] : 2));
        assert(h.board[6].passengers == (version == 6 ? g.board[6].passengers : 0));
        assert(h.equipment == ((1 << DORSAL) | (1 << VENTRAL)));
        assert(h.roll == 0);
        if (version == 6) h.crew = ship_specs[h.ship].crew;
        game_action(&h, 'l'); assert(h.docked == -1);
    }
    g.crew = 2; g.cabins = 2; g.reputation[0] = 30; g.wanted[1] = 1200;
    g.service[2] = 15; g.medals[2] = 3; g.turret_upgrade[1] = 2;
    g.missiles = 2; g.countermeasures = 3;
    g.projectiles[0] = (Missile){g.pos, g.vel, 1, 0, HZ * 10};
    g.npc[0].pos = g.pos; g.npc[0].active = 1; g.npc[0].hull = 60;
    g.npc[0].mission = -1; g.npc_role[0] = POLICE; g.npc_ship[0] = 3;
    assert(save_game(&g, SLOT) && load_game(&h, SLOT));
    assert(h.crew == 2 && h.cabins == 2 && h.wanted[1] == 1200 && h.reputation[0] == 30);
    assert(h.service[2] == 15 && h.medals[2] == 3 && h.turret_upgrade[1] == 2);
    assert(h.projectiles[0].active && h.projectiles[0].ttl == HZ * 10 && h.npc_role[0] == POLICE);
    before = h;
    f = fopen(SLOT, "rb"); assert(f); len = fread(data, 1, sizeof(data), f); fclose(f);
    /* Recompute the checksum after an invalid crew count: semantic validation,
       not only the file checksum, must reject without touching the live game. */
    put_word(data + end + 4, 100);
    sum = 2166136261U; for (i = 16; i < len; i++) sum = (sum ^ data[i]) * 16777619U;
    put_word(data + 12, sum);
    f = fopen(SLOT, "wb"); assert(f && fwrite(data, 1, len, f) == len); fclose(f);
    assert(!load_game(&h, SLOT) && !memcmp(&h, &before, sizeof(h)));
    remove(SLOT); remove(SLOT ".bak");
    puts("PASS v4/v5/v6 migration, career/combat extension roundtrip and checksummed semantic rejection");
}
int main(void) {
    test_ships_crew(); test_terms_passengers(); test_rescue_escort();
    test_combat_contracts(); test_law_missiles(); test_mining_saves(); test_legacy_and_extension();
    puts("All career tests passed.");
    return 0;
}
