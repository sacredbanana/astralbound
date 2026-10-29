/* SPDX-License-Identifier: Unlicense
   SPC1: magic, generator version, payload length, FNV1a checksum (all BE32),
   then explicit BE32/BE64 fields. Never serialize C structure layout. */
#include "astralbound.h"
#include "galaxy.h"
#include "solar.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct {
    unsigned char data[24576];
    size_t pos;
    int read, error, relocated;
} Stream;
static uint32_t word(Stream *s, uint32_t value) {
    int i;
    uint32_t r = 0;
    if (s->pos + 4 > sizeof(s->data)) {
        s->error = 1;
        return 0;
    }
    for (i = 3; i >= 0; i--) {
        if (s->read)
            r = (r << 8) | s->data[s->pos++];
        else
            s->data[s->pos++] = (unsigned char)(value >> (i * 8));
    }
    return s->read ? r : value;
}
static int64_t wide(Stream *s, int64_t v) {
    uint64_t u = (uint64_t)v;
    uint32_t h = word(s, (uint32_t)(u >> 32)), l = word(s, (uint32_t)u);
    u = ((uint64_t)h << 32) | l;
    memcpy(&v, &u, 8);
    return v;
}
static void vector(Stream *s, Vec *v) {
    v->x = wide(s, v->x);
    v->y = wide(s, v->y);
    v->z = wide(s, v->z);
}
#define FIELD(x) (x) = word(s, (uint32_t)(x))
static void sector(Stream *s, Sector *v) {
    FIELD(v->x);
    FIELD(v->y);
    FIELD(v->z);
}
static void mission(Stream *s, Mission *m) {
    FIELD(m->id);
    FIELD(m->type);
    FIELD(m->state);
    sector(s, &m->destination);
    FIELD(m->port);
    FIELD(m->quantity);
    FIELD(m->good);
    FIELD(m->reward);
    FIELD(m->progress);
    FIELD(m->spawned);
    FIELD(m->deadline);
}
static void career_payload(Stream *s, Game *g) {
    int i, j;
    uint32_t tag = word(s, 0x43415236U);
    if (tag != 0x43415236U) { s->error = 1; return; }
    FIELD(g->crew); FIELD(g->cabins); FIELD(g->equipment);
    FIELD(g->missiles); FIELD(g->countermeasures);
    FIELD(g->turret_upgrade[0]); FIELD(g->turret_upgrade[1]);
    FIELD(g->wage_tick); FIELD(g->encounter_tick);
    for (i = 0; i < FACTIONS; i++) {
        FIELD(g->reputation[i]); FIELD(g->service[i]); FIELD(g->medals[i]); FIELD(g->wanted[i]);
    }
    for (i = 0; i < MAX_MISSIONS * 2; i++) {
        Mission *m = i < MAX_MISSIONS ? &g->board[i] : &g->contracts[i - MAX_MISSIONS];
        FIELD(m->faction); FIELD(m->required_rep); FIELD(m->passengers);
        FIELD(m->legs); FIELD(m->leg); FIELD(m->negotiated);
    }
    for (i = 0; i < MAX_NPC; i++) { FIELD(g->npc_role[i]); FIELD(g->npc_ship[i]); }
    for (i = 0; i < MAX_PODS; i++) {
        CargoPod *p = &g->pods[i];
        vector(s, &p->pos); vector(s, &p->vel);
        FIELD(p->active); FIELD(p->good); FIELD(p->quantity); FIELD(p->mission);
    }
    for (i = 0; i < MAX_MISSILES; i++) {
        Missile *m = &g->projectiles[i];
        vector(s, &m->pos); vector(s, &m->vel);
        FIELD(m->active); FIELD(m->target); FIELD(m->ttl);
    }
    FIELD(g->rig_count);
    if (g->rig_count < 0 || g->rig_count > MAX_RIGS) { s->error = 1; return; }
    for (i = 0; i < g->rig_count; i++) {
        MiningRig *r = &g->rigs[i];
        sector(s, &r->sector); FIELD(r->body); FIELD(r->stock);
        vector(s, &r->anchor); FIELD(r->last_tick);
    }
    FIELD(g->deposit_count);
    if (g->deposit_count < 0 || g->deposit_count > MAX_DEPOSITS) { s->error = 1; return; }
    for (i = 0; i < g->deposit_count; i++) {
        sector(s, &g->deposits[i].sector);
        for (j = 0; j < ASTEROIDS; j++) FIELD(g->deposits[i].ore[j]);
    }
}
static void payload(Stream *s, Game *g, uint32_t version) {
    int i, j, legacy_stock[GOODS];
    Sector sector_id = g->system.sector;
    FIELD(g->seed);
    FIELD(g->tick);
    sector(s, &sector_id);
    if (s->read) {
        if(version<9 && !galaxy_exists(g->seed,sector_id)) {
            sector_id=(Sector){0,0,0};s->relocated=1;
        }
        setup_system(g, sector_id);
    }
    vector(s, &g->pos);
    vector(s, &g->vel);
    FIELD(g->yaw);
    FIELD(g->pitch);
    FIELD(g->target);
    FIELD(g->docked);
    { int legacy_gear = 1; FIELD(legacy_gear); (void)legacy_gear; } /* landing gear retired */
    FIELD(g->autopilot);
    FIELD(g->assist);
    FIELD(g->throttle);
    FIELD(g->hull);
    FIELD(g->shield);
    FIELD(g->fuel);
    FIELD(g->heat);
    FIELD(g->credits);
    FIELD(g->ship);
    FIELD(g->upgrade);
    FIELD(g->kills);
    for (i = 0; i < GOODS; i++) {
        FIELD(g->cargo[i]);
        legacy_stock[i] = market_stock(g, i);
        FIELD(legacy_stock[i]);
        if (s->read && (legacy_stock[i] < 0 || legacy_stock[i] > 10000))
            s->error = 1;
    }
    for (i = 0; i < MAX_MISSIONS; i++) {
        mission(s, &g->board[i]);
        mission(s, &g->contracts[i]);
    }
    for (i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        vector(s, &n->pos);
        vector(s, &n->vel);
        FIELD(n->hull);
        FIELD(n->active);
        FIELD(n->hostile);
        FIELD(n->mission);
        FIELD(n->cooldown);
    }
    sector(s, &g->selected);
    FIELD(g->selection);
    FIELD(g->page);
    FIELD(g->warp);
    FIELD(g->dead);
    FIELD(g->shots);
    FIELD(g->fuel_burn);
    FIELD(g->camera_mode);
    FIELD(g->dock_stage);
    FIELD(g->target_body);
    FIELD(g->frame_body);
    FIELD(g->clearance);
    FIELD(g->explored_count);
    if (g->explored_count < 0 || g->explored_count > MAX_EXPLORED) {
        s->error = 1;
        return;
    }
    for (i = 0; i < g->explored_count; i++) {
        sector(s, &g->explored[i].sector);
        FIELD(g->explored[i].visited);
        FIELD(g->explored[i].surveyed);
        FIELD(g->explored[i].first_tick);
        FIELD(g->explored[i].last_tick);
    }
    if (version >= 4) {
        FIELD(g->market_count);
        if (g->market_count < 0 || g->market_count > MAX_MARKETS) {
            s->error = 1;
            return;
        }
        for (i = 0; i < g->market_count; i++) {
            Market *m = &g->markets[i];
            sector(s, &m->sector);
            FIELD(m->port);
            FIELD(m->last_tick);
            for (j = 0; j < GOODS; j++)
                FIELD(m->stock[j]);
        }
    } else if (s->read && g->docked >= 0 && g->docked < g->nports) {
        Market *m = &g->markets[0];
        g->market_count = 1;
        m->sector = g->system.sector;
        m->port = g->docked;
        m->last_tick = g->tick;
        for (j = 0; j < GOODS; j++)
            m->stock[j] = legacy_stock[j];
    }
    if (version >= 6) career_payload(s, g);
    else if (s->read) {
        g->wage_tick = g->encounter_tick = g->tick;
        g->crew = ship_specs[g->ship < 0 || g->ship >= SHIPS ? 0 : g->ship].crew;
        g->turret_upgrade[0] = g->turret_upgrade[1] = g->upgrade;
        for (i = 0; i < MAX_MISSIONS; i++) {
            g->board[i].faction = g->contracts[i].faction = system_faction(g);
            g->board[i].legs = g->contracts[i].legs = 1;
            g->board[i].passengers = g->contracts[i].passengers = 0;
            g->board[i].leg = g->contracts[i].leg = 0;
            g->board[i].negotiated = g->contracts[i].negotiated = 0;
            g->board[i].required_rep = g->contracts[i].required_rep = -100;
        }
    }
    if (version >= 7) FIELD(g->roll);
    if (version >= 8) {
        for(i=0;i<16;i++) { FIELD(g->solar_visited[i]); FIELD(g->solar_surveyed[i]); }
        FIELD(g->waypoint_set); sector(s,&g->waypoint);
    }
}
#undef FIELD
static uint32_t checksum(const unsigned char *p, size_t n) {
    uint32_t h = 2166136261U;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619U;
    return h;
}
static int good_sector(Sector s) {
    return s.x >= -1000000 && s.x <= 1000000 && s.y >= -1000000 && s.y <= 1000000 &&
           s.z >= -1000000 && s.z <= 1000000;
}
static int good_vec(Vec v, int64_t bound) {
    return v.x >= -bound && v.x <= bound && v.y >= -bound && v.y <= bound && v.z >= -bound &&
           v.z <= bound;
}
/* Sol's pre-version-10 layout, the mirror image: see migrate_axes. */
static Vec swap_xz(Vec v) { return vec(v.z, v.y, v.x); }
/* Version 9 keeps the binary payload but changes population and Sol layout.
   Remap retained bodies by identity; removed local frames return safely to Earth. */
static int migrate_catalog(Game *g,int relocated) {
    uint32_t visited[16]={0},surveyed[16]={0};
    int i,frame_removed=0,board_removed=relocated;
    for(i=0;i<512;i++) {
        uint32_t bit=1U<<(i%32);int body=solar_legacy_body(i);
        if((g->solar_surveyed[i/32]&bit) && !(g->solar_visited[i/32]&bit)) return 0;
        if(i>=solar_legacy_count() && ((g->solar_visited[i/32]|g->solar_surveyed[i/32])&bit)) return 0;
        if(body>=0) {
            if(g->solar_visited[i/32]&bit) visited[body/32]|=1U<<(body%32);
            if(g->solar_surveyed[i/32]&bit) surveyed[body/32]|=1U<<(body%32);
        }
    }
    memcpy(g->solar_visited,visited,sizeof(visited));memcpy(g->solar_surveyed,surveyed,sizeof(surveyed));
    if(!relocated && same_sector(g->system.sector,catalog_sector(0))) {
        if(g->target_body< -1 || g->target_body>=solar_legacy_count() || g->frame_body< -1 || g->frame_body>=solar_legacy_count()) return 0;
        if(g->target_body>=0) {
            g->target_body=solar_legacy_body(g->target_body);
            if(g->target_body<0) { g->target=-1;g->target_body=2;g->autopilot=0; }
        }
        if(g->frame_body>=0) {
            g->frame_body=solar_legacy_body(g->frame_body);
            if(g->frame_body<0) { frame_removed=1;g->frame_body=2; }
        }
        if(frame_removed) {
            const Planet *earth=&g->system.planets[2];
            /* In the old layout too: migrate_axes follows. */
            g->docked=-1;g->pos=swap_xz(add(scale(earth->center,FP,1),vec(0,((int64_t)earth->radius+200000)*FP,0)));
            g->vel=vec(0,0,0);g->target=-1;g->target_body=2;g->autopilot=0;g->throttle=0;
        }
    }
    if(relocated) {
        g->docked=0;g->frame_body=g->ports[0].body;g->target=0;g->target_body=-1;
        g->pos=g->ports[0].pos;g->vel=vec(0,0,0);g->throttle=g->autopilot=0;g->warp=1;
        g->clearance=-1;g->dock_stage=0;
        memset(g->npc,0,sizeof(g->npc));memset(g->pods,0,sizeof(g->pods));
        memset(g->projectiles,0,sizeof(g->projectiles));
        for(i=0;i<MAX_MISSIONS;i++) g->contracts[i].spawned=0;
    }
    for(i=0;i<g->rig_count;i++) {
        MiningRig *rig=&g->rigs[i];int remove=!galaxy_exists(g->seed,rig->sector);
        if(same_sector(rig->sector,catalog_sector(0))) {
            rig->body=solar_legacy_body(rig->body);if(rig->body<0) remove=1;
        }
        if(remove) {
            int j;for(j=i+1;j<g->rig_count;j++) g->rigs[j-1]=g->rigs[j];g->rig_count--;i--;
            if(g->credits<=99999500) g->credits+=500;
        }
    }
    /* A deleted destination must not leave a passenger/freight slot locked.
       Cancel without the normal failure reputation penalty. */
    for(i=0;i<MAX_MISSIONS;i++) {
        Mission *m=&g->contracts[i];
        if(m->state==ACTIVE && !galaxy_exists(g->seed,m->destination)) m->state=FAILED;
        if(g->board[i].state==OFFERED && !galaxy_exists(g->seed,g->board[i].destination)) board_removed=1;
    }
    if(board_removed) refresh_board(g);
    if(!galaxy_exists(g->seed,g->selected)) g->selected=g->system.sector;
    if(g->waypoint_set && !galaxy_exists(g->seed,g->waypoint)) g->waypoint_set=0;
    return 1;
}
/* Version 10 unmirrors Sol (game x/z were ecliptic Y/X, now X/Y) and gives
   its bodies their real spin axes. Every Sol position and velocity swaps x
   and z; a commander on or near a body instead keeps the same body-fixed
   place, carried from the old upright spin to the new. */
static void migrate_axes(Game *g) {
    int i;
    update_world(g);
    if (g->frame_body >= 0 && g->frame_body < g->system.count) {
        const Planet *p = &g->system.planets[g->frame_body];
        Planet old = *p;
        Vec centre = scale(p->center, FP, 1), q = sub(g->pos, swap_xz(centre));
        old.tilted = 0; old.spin_phase = 0;
        set_rotation(&old, g->tick);
        if (length(q) / FP < (int64_t)p->radius + p->atmosphere + 200000) {
            Vec moving = sub(sub(g->vel, swap_xz(p->velocity)), surface_velocity(&old, scale(q, 1, FP)));
            Vec local = body_rotate(p, body_rotate(&old, q, 1), 0);
            g->pos = add(centre, local);
            g->vel = add(add(p->velocity, surface_velocity(p, scale(local, 1, FP))),
                         body_rotate(p, body_rotate(&old, moving, 1), 0));
        } else {
            g->pos = swap_xz(g->pos); g->vel = swap_xz(g->vel);
        }
    } else {
        g->pos = swap_xz(g->pos); g->vel = swap_xz(g->vel);
    }
    for (i = 0; i < MAX_NPC; i++) { g->npc[i].pos = swap_xz(g->npc[i].pos); g->npc[i].vel = swap_xz(g->npc[i].vel); }
    for (i = 0; i < MAX_PODS; i++) { g->pods[i].pos = swap_xz(g->pods[i].pos); g->pods[i].vel = swap_xz(g->pods[i].vel); }
    for (i = 0; i < MAX_MISSILES; i++) {
        g->projectiles[i].pos = swap_xz(g->projectiles[i].pos); g->projectiles[i].vel = swap_xz(g->projectiles[i].vel);
    }
}
static int valid(const Game *g) {
    int i;
    if(g->waypoint_set<0 || g->waypoint_set>1 || !good_sector(g->waypoint)) return 0;
    for(i=0;i<16;i++) if(g->solar_surveyed[i] & ~g->solar_visited[i]) return 0;
    for(i=solar_body_count();i<512;i++) if((g->solar_visited[i/32]|g->solar_surveyed[i/32])&(1U<<(i%32))) return 0;
    if (!g->system.count || !good_sector(g->system.sector) || !good_sector(g->selected) ||
        !good_vec(g->pos, 80000000000000000LL) || !good_vec(g->vel, (SYSTEM_DRIVE_SPEED + 1000000) * FP) || g->ship < 0 ||
        g->ship >= SHIPS ||
        (g->target < -1 || g->target >= g->nports ||
         (g->target < 0 && (g->target_body < 0 || g->target_body >= g->system.count))) ||
        g->docked < -2 || g->docked >= port_count(g) || g->hull < 0 || g->hull > 100 ||
        g->shield < 0 || g->shield > 100 || g->fuel < 0 || g->fuel > 100 || g->heat < 0 ||
        g->heat > 100 || g->credits < 0 || g->credits > 100000000 || g->pitch < -256 ||
        g->pitch > 256 || g->yaw < 0 || g->yaw > 1023 || g->roll < 0 || g->roll > 1023 ||
        g->page < 0 || g->page > SERVICES ||
        g->selection < 0 || g->selection >= MAX_MISSIONS || g->upgrade < 0 || g->upgrade > 3 ||
        g->throttle < 0 || g->throttle > 4000 || g->camera_mode < 0 || g->camera_mode > 4)
        return 0;
    if (g->warp != 1 && g->warp != 4 && g->warp != 16 && g->warp != 64 &&
        g->warp != 256 && g->warp != 1024 && g->warp != MAX_WARP)
        return 0;
    for (i = 0; i < GOODS; i++)
        if (g->cargo[i] < 0 || g->cargo[i] > 180)
            return 0;
    for (i = 0; i < MAX_MISSIONS * 2; i++) {
        const Mission *m = i < MAX_MISSIONS ? &g->contracts[i] : &g->board[i - MAX_MISSIONS];
        if (m->type < 0 || m->type >= MISSION_TYPES || m->state < 0 || m->state > FAILED ||
            m->port < 0 || m->port >= MAX_PORTS || m->quantity < 0 || m->quantity > 8 ||
            m->good < 0 || m->good >= GOODS || m->reward < 0 || m->reward > 10000 ||
            !good_sector(m->destination) || m->progress < 0 || m->progress > 10000)
            return 0;
    }
    for (i = 0; i < MAX_NPC; i++)
        if (!good_vec(g->npc[i].pos, 2000000000000000LL) || !good_vec(g->npc[i].vel, (SYSTEM_DRIVE_SPEED + 1000000) * FP) ||
            g->npc[i].mission < -1 || g->npc[i].mission >= MAX_MISSIONS)
            return 0;
    if (g->fuel_burn >= 50000U * FP || g->dock_stage < 0 || g->dock_stage > 1 ||
        g->target_body < -1 || g->target_body >= g->system.count || g->frame_body < -1 ||
        g->frame_body >= g->system.count || g->clearance < -1 || g->clearance >= g->nports ||
        g->explored_count < 0 || g->explored_count > MAX_EXPLORED)
        return 0;
    for (i = 0; i < g->explored_count; i++) {
        const Exploration *e = &g->explored[i];
        if (!good_sector(e->sector) || (e->surveyed & ~e->visited) ||
            e->visited >= (1U << GENERATED_PLANETS) || e->first_tick > e->last_tick ||
            e->last_tick > g->tick || exploration_index(g, e->sector) != i)
            return 0;
    }
    if (g->market_count < 0 || g->market_count > MAX_MARKETS)
        return 0;
    for (i = 0; i < g->market_count; i++) {
        const Market *m = &g->markets[i];
        int j;
        if (!good_sector(m->sector) || m->port < 0 || m->port >= MAX_PORTS ||
            m->last_tick > g->tick || market_index(g, m->sector, m->port) != i)
            return 0;
        for (j = 0; j < GOODS; j++)
            if (m->stock[j] < 0 || m->stock[j] > 10000)
                return 0;
    }
    if (g->crew < 0 || g->crew > 5 || g->cabins < 0 || g->cabins > 8 ||
        g->equipment < 0 || (g->equipment & ~((1 << EQUIP_TYPES) - 1)) ||
        g->missiles < 0 || g->missiles > ship_specs[g->ship].missiles ||
        g->turret_upgrade[0] < 0 || g->turret_upgrade[0] > 3 ||
        g->turret_upgrade[1] < 0 || g->turret_upgrade[1] > 3 ||
        g->countermeasures < 0 || g->countermeasures > 20 ||
        g->wage_tick > g->tick || g->encounter_tick > g->tick ||
        g->rig_count < 0 || g->rig_count > MAX_RIGS ||
        g->deposit_count < 0 || g->deposit_count > MAX_DEPOSITS) return 0;
    for (i = 0; i < FACTIONS; i++) if (g->reputation[i] < -100 || g->reputation[i] > 100 ||
        g->service[i] < 0 || g->service[i] > 10000 || g->medals[i] != g->service[i] / 5 ||
        g->wanted[i] < 0 || g->wanted[i] > 1000000) return 0;
    for (i = 0; i < MAX_MISSIONS * 2; i++) {
        const Mission *m = i < MAX_MISSIONS ? &g->board[i] : &g->contracts[i - MAX_MISSIONS];
        if (m->faction < 0 || m->faction >= FACTIONS || m->required_rep < -100 ||
            m->required_rep > 100 || m->passengers < 0 || m->passengers > 3 ||
            m->legs < 0 || m->legs > 3 || m->leg < 0 || m->leg > m->legs ||
            m->negotiated < 0 || m->negotiated > 1 || m->spawned < 0 || m->spawned > 1) return 0;
    }
    for (i = 0; i < MAX_NPC; i++) if (g->npc_role[i] < 0 || g->npc_role[i] > INSTALLATION_TARGET ||
        g->npc_ship[i] < 0 || g->npc_ship[i] >= SHIPS) return 0;
    for (i = 0; i < MAX_PODS; i++) {
        const CargoPod *p = &g->pods[i];
        if (!good_vec(p->pos, 2000000000000000LL) || !good_vec(p->vel, (SYSTEM_DRIVE_SPEED + 1000000) * FP) ||
            p->active < 0 || p->active > 1 || p->good < 0 || p->good >= GOODS ||
            p->quantity < 0 || p->quantity > 8 || p->mission < -1 || p->mission >= MAX_MISSIONS) return 0;
    }
    for (i = 0; i < MAX_MISSILES; i++) {
        const Missile *m = &g->projectiles[i];
        if (!good_vec(m->pos, 2000000000000000LL) || !good_vec(m->vel, (SYSTEM_DRIVE_SPEED + 1000000) * FP) ||
            m->active < 0 || m->active > 1 || m->target < 0 || m->target >= MAX_NPC ||
            m->ttl < 0 || m->ttl > HZ * 20) return 0;
    }
    for (i = 0; i < g->rig_count; i++) {
        const MiningRig *r = &g->rigs[i];
        if (!good_sector(r->sector) || r->body < 0 || r->body >= MAX_PLANETS ||
            r->stock < 0 || r->stock > 20 || r->last_tick > g->tick ||
            !good_vec(r->anchor, 100000000000LL)) return 0;
    }
    for (i = 0; i < g->deposit_count; i++) {
        int j;
        if (!good_sector(g->deposits[i].sector)) return 0;
        for (j = 0; j < i; j++) if (g->deposits[i].sector.x == g->deposits[j].sector.x &&
            g->deposits[i].sector.y == g->deposits[j].sector.y &&
            g->deposits[i].sector.z == g->deposits[j].sector.z) return 0;
        for (j = 0; j < ASTEROIDS; j++) if (g->deposits[i].ore[j] < 0 || g->deposits[i].ore[j] > 4) return 0;
    }
    return cargo_used(g) <= ship_capacity[g->ship] && passengers_used(g) <= g->cabins * 2;
}
int save_game(const Game *g, const char *path) {
    Stream s = {0};
    Game copy = *g;
    FILE *f;
    char tmp[512], backup[512];
    size_t len;
    int ok;
    if (strlen(path) > 480 || !valid(g))
        return 0;
    s.pos = 16;
    payload(&s, &copy, SAVE_VERSION);
    len = s.pos;
    if (s.error)
        return 0;
    s.pos = 0;
    word(&s, 0x53504331);
    word(&s, SAVE_VERSION);
    word(&s, (uint32_t)(len - 16));
    word(&s, checksum(s.data + 16, len - 16));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;
    ok = fwrite(s.data, 1, len, f) == len;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return 0;
    }
    if (rename(tmp, path) != 0) {
        /* AmigaDOS does not replace an existing destination. Move the old
           complete slot aside only after the new file has been closed. */
        f = fopen(path, "rb");
        if (!f) {
            remove(tmp);
            return 0;
        }
        fclose(f);
        snprintf(backup, sizeof(backup), "%s.bak", path);
        remove(backup);
        if (rename(path, backup) != 0) {
            remove(tmp);
            return 0;
        }
        if (rename(tmp, path) != 0) {
            rename(backup, path);
            remove(tmp);
            return 0;
        }
    }
    return 1;
}
int load_game(Game *g, const char *path) {
    Stream s = {0};
    Game copy;
    FILE *f = fopen(path, "rb");
    size_t len;
    uint32_t size, crc, version;
    if (!f)
        return 0;
    len = fread(s.data, 1, sizeof(s.data), f);
    if (ferror(f) || !feof(f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    if (len < 16)
        return 0;
    s.read = 1;
    if (word(&s, 0) != 0x53504331)
        return 0;
    version = word(&s, 0);
    if (version != GENERATOR_VERSION && version != 4 && version != 5 && version != 6 && version != 7 && version != 8 && version != 9 &&
        version != SAVE_VERSION)
        return 0;
    size = word(&s, 0);
    crc = word(&s, 0);
    if (size != len - 16 || crc != checksum(s.data + 16, len - 16))
        return 0;
    game_init(&copy, 1);
    payload(&s, &copy, version);
    if(version<9 && !s.error && !migrate_catalog(&copy,s.relocated)) return 0;
    if (version < 10 && !s.error && same_sector(copy.system.sector, catalog_sector(0))) migrate_axes(&copy);
    if (s.error || s.pos != len || !valid(&copy))
        return 0;
    copy.body = nearest_body(&copy, copy.pos);
    /* v3/v4 ground coordinates predate axial rotation. Reattach stationary
       ground commanders to the corresponding body-fixed terrain direction. */
    if (version < 5 && copy.docked == -2) {
        const Planet *p = &copy.system.planets[copy.body];
        Vec local = sub(scale(copy.pos, 1, FP), p->center);
        local = body_rotate(p, local, 0);
        local = radial(local, p->radius + terrain_height(p, local) + 12);
        copy.pos = scale(add(p->center, local), FP, 1);
        copy.vel = surface_velocity(p, local);
        copy.frame_body = copy.body;
    }
    /* Regenerated geology can rise above an older commander's position.
       Re-seat ground saves and lift buried airborne saves before flight resumes. */
    if (copy.docked < 0) {
        int64_t clearance = altitude_at(&copy,copy.pos);
        if (copy.docked != -2 && clearance<0) {
            const Planet *p=&copy.system.planets[copy.body];
            Vec local=sub(scale(copy.pos,1,FP),p->center);
            local=radial(local,p->radius+terrain_height(p,local)+50);
            copy.pos=scale(add(p->center,local),FP,1);
            copy.vel=surface_velocity(p,local);
        }
    }
    /* A landed hull lying below its keel's rest height - an older save of a
       larger hull, or regenerated ground - rests again where it stands. */
    if (copy.docked == -2) {
        const Planet *p = &copy.system.planets[copy.body];
        if (altitude_at(&copy, copy.pos) < ship_specs[copy.ship].keel + REST_CLEARANCE - 1) landed_pose(&copy);
        else copy.ground_anchor = body_rotate(p, sub(copy.pos, scale(p->center, FP, 1)), 1);
    }
    if (copy.docked >= 0) {
        copy.pos = copy.ports[copy.docked].pos;
        copy.vel = port_relative_velocity(&copy, copy.docked);
    }
    copy.ui_x = g->ui_x;
    copy.ui_y = g->ui_y;
    copy.ui_visible = g->ui_visible;
    copy.ui_down = copy.ui_fire = copy.ui_weapons = 0;
    copy.ui_capture = -1;
    copy.ui_quit = 0;
    { Sector waypoint=copy.waypoint; int active=copy.waypoint_set;
      galaxy_reset(&copy); copy.waypoint=waypoint; copy.waypoint_set=active; }
    copy.paused = 0;
    copy.turret_yaw = copy.yaw;
    copy.turret_pitch = copy.pitch;
    copy.combat_target = -1;
    copy.roll_fraction = copy.pitch_fraction = 0;
    copy.drive_manual = copy.drive_active = 0;
    copy.effective_warp = 1;
    *g = copy;
    snprintf(g->message, sizeof(g->message), "COMMANDER RESTORED");
    return 1;
}
