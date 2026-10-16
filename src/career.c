/* SPDX-License-Identifier: Unlicense
   Bounded career, resource and encounter simulation shared by host and Amiga.
   All recurring events use commander time; no allocation or OS calls here. */
#include "astralbound.h"
#include <stdio.h>
#include <string.h>
const ShipSpec ship_specs[SHIPS] = {
    {"CINDER", 30, 60, 20, 0, 3, 2, 35, 70, 12, 0},
    {"LOOM", 85, 48, 60, 1, 3, 4, 60, 75, 25, 8000},
    {"HEARTH", 220, 36, 140, 2, 3, 6, 110, 110, 45, 16000},
    {"KESTREL", 50, 90, 35, 1, 1, 8, 65, 100, 9, 22000},
    {"QUARRY", 160, 42, 90, 2, 2, 4, 45, 140, 35, 26000},
    {"WAYFARER", 300, 30, 180, 3, 3, 8, 95, 170, 55, 40000}
};
const char *faction_names[FACTIONS] = {"AURORA UNION", "MERIDIAN LEAGUE", "FREE REACH"};
const char *equipment_names[EQUIP_TYPES] = {"PASSENGER CABIN", "MINING LASER/RIG", "FUEL SCOOP",
    "MISSILE", "COUNTERMEASURE", "DORSAL TURRET", "VENTRAL TURRET", "ESCAPE POD"};
const int equipment_cost[EQUIP_TYPES] = {600, 1200, 900, 250, 80, 1400, 1400, 800};
static int same(Sector a, Sector b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static void say(Game *g, const char *s) { snprintf(g->message, sizeof(g->message), "%s", s); }
static void pay(Game *g, int value) {
    g->credits = (int)((int64_t)g->credits + value > 100000000 ? 100000000 : g->credits + value);
}
int system_faction(const Game *g) { return (int)(g->system.seed % FACTIONS); }
const char *military_rank(const Game *g, int f) {
    static const char *rank[] = {"CIVILIAN", "CADET", "ENSIGN", "LIEUTENANT", "COMMANDER", "CAPTAIN"};
    return rank[clamp(g->service[f] / 3, 0, 5)];
}
int equipped(const Game *g, Equipment item) { return !!(g->equipment & (1 << item)); }
int passengers_used(const Game *g) {
    int i, n = 0;
    for (i = 0; i < MAX_MISSIONS; i++) if (g->contracts[i].state == ACTIVE)
        n += g->contracts[i].passengers;
    return n;
}
/* First reason buy_ship would refuse a hull, or NULL when the sale can go ahead. */
const char *ship_refusal(const Game *g, int s) {
    const ShipSpec *spec;
    if (s < 0 || s >= SHIPS || g->dead) return "UNAVAILABLE";
    if (s == g->ship) return "CURRENT HULL";
    if (g->docked < 0) return "DOCK TO BUY";
    spec = &ship_specs[s];
    if (g->credits < spec->cost) return "SHORT OF CREDITS";
    if (cargo_used(g) > spec->capacity) return "HOLD TOO SMALL";
    if (g->missiles > spec->missiles) return "TOO MANY MISSILES";
    if (g->crew > spec->crew + 2) return "CREW TOO LARGE";
    if (equipped(g, DORSAL) && !(spec->mounts & 1)) return "NO DORSAL MOUNT";
    if (equipped(g, VENTRAL) && !(spec->mounts & 2)) return "NO VENTRAL MOUNT";
    return NULL;
}
int buy_ship(Game *g, int s) {
    if (ship_refusal(g, s)) return 0;
    /* No resale credit: prevents cycling free hulls to manufacture credits. */
    g->credits -= ship_specs[s].cost;
    g->ship = s;
    say(g, "HULL PURCHASED - CHECK CREW BEFORE LAUNCH");
    return 1;
}
int buy_equipment(Game *g, int item) {
    if (item < 0 || item >= EQUIP_TYPES || g->docked < 0 || g->dead ||
        g->credits < equipment_cost[item]) return 0;
    if (item == CABIN) {
        if (g->cabins >= 8 || cargo_used(g) + 2 > ship_capacity[g->ship]) return 0;
        g->cabins++;
    } else if (item == MISSILE_RACK) {
        if (g->missiles >= ship_specs[g->ship].missiles) return 0;
        g->missiles++;
    } else if (item == COUNTERMEASURE) {
        if (g->countermeasures >= 20) return 0;
        g->countermeasures++;
    } else {
        if (equipped(g, (Equipment)item) ||
            (item == DORSAL && !(ship_specs[g->ship].mounts & 1)) ||
            (item == VENTRAL && !(ship_specs[g->ship].mounts & 2))) return 0;
        g->equipment |= 1 << item;
    }
    g->credits -= equipment_cost[item];
    return 1;
}
int sell_equipment(Game *g, int item) {
    if (g->docked < 0 || g->dead || item < 0 || item >= EQUIP_TYPES) return 0;
    if (item == CABIN) {
        if (!g->cabins || passengers_used(g) > (g->cabins - 1) * 2) return 0;
        g->cabins--;
    } else if (item == MISSILE_RACK) {
        if (!g->missiles) return 0;
        g->missiles--;
    } else if (item == COUNTERMEASURE) {
        if (!g->countermeasures) return 0;
        g->countermeasures--;
    } else {
        if (!equipped(g, (Equipment)item)) return 0;
        g->equipment &= ~(1 << item);
        if (item == DORSAL || item == VENTRAL) g->turret_upgrade[item == VENTRAL] = 0;
    }
    pay(g, equipment_cost[item] / 2);
    return 1;
}
int upgrade_weapon(Game *g, int mount) {
    int *level;
    if (mount < 0 || mount > 2 || g->docked < 0 || g->credits < 2000 || g->dead ||
        (mount && !equipped(g, mount == 1 ? DORSAL : VENTRAL))) return 0;
    level = mount ? &g->turret_upgrade[mount - 1] : &g->upgrade;
    if (*level >= 3) return 0;
    (*level)++; g->credits -= 2000;
    return 1;
}
int recruit_crew(Game *g, int hire) {
    if (g->docked < 0 || g->dead) return 0;
    if (hire) {
        if (g->crew >= ship_specs[g->ship].crew + 2 || g->credits < 200) return 0;
        g->crew++; g->credits -= 200;
    } else {
        if (!g->crew) return 0;
        g->crew--;
    }
    return 1;
}
int negotiate_mission(Game *g, int index) {
    Mission *m;
    if (index < 0 || index >= MAX_MISSIONS || g->docked < 0) return 0;
    m = &g->board[index];
    if (m->state != OFFERED || m->negotiated || g->tick >= m->deadline ||
        g->reputation[m->faction] < 3 || m->reward > 8000 || m->deadline - g->tick < HZ * 600) return 0;
    m->negotiated = 1;
    m->reward += m->reward / 5;
    m->deadline = g->tick + (m->deadline - g->tick) * 3 / 4;
    return 1;
}
int illegal_good(const Game *g, int good) {
    /* Governments 2/3 control crystals; 3 also restricts advanced circuits. */
    return (g->system.government >= 2 && good == 6) || (g->system.government == 3 && good == 4);
}
int black_market_available(const Game *g) {
    return g->docked >= 0 && (g->system.government == 0 || g->reputation[system_faction(g)] >= 5);
}
void inspect_cargo(Game *g) {
    int i, value = 0, f = system_faction(g);
    if (!g->system.government) return;
    for (i = 0; i < GOODS; i++) if (illegal_good(g, i) && g->cargo[i]) {
        value += g->cargo[i] * 100;
        g->cargo[i] = 0;
    }
    if (value) {
        g->wanted[f] = clamp(g->wanted[f] + value, 0, 1000000);
        g->reputation[f] = clamp(g->reputation[f] - 3, -100, 100);
        say(g, "CUSTOMS CONFISCATED CONTRABAND - 9 THEN F TO PAY FINE");
    }
}
int settle_fine(Game *g) {
    int f = system_faction(g), i;
    if (g->docked < 0 || g->dead || !g->wanted[f] || g->credits < g->wanted[f]) return 0;
    g->credits -= g->wanted[f]; g->wanted[f] = 0;
    /* Paid-up commanders are left alone; pursuers return to port patrol. */
    for (i = 0; i < MAX_NPC; i++) if (g->npc_role[i] == POLICE) g->npc[i].hostile = 0;
    g->police_alert = 0;
    return 1;
}
/* Every port keeps two police ships circling it, launched only while the
   commander is near so the twelve NPC slots stay available for contracts.
   Stations are circled across their traffic corridor; surface ports above
   the pad. Port assignment and patrol phase derive from position and slot,
   so saves need no extra state. */
#define PATROLS_PER_PORT 2
#define MAX_PATROLS 6
#define PATROL_RADIUS 2500
#define PATROL_LAUNCH 50000LL
#define PATROL_RECALL 80000LL
#define POLICE_WITNESS 10000LL
#define WEAPONS_FINE 500
#define WARRANT 1000
static int nearest_port(const Game *g, Vec pos) {
    int i, best = 0;
    int64_t distance = INT64_MAX;
    for (i = 0; i < g->nports; i++) {
        int64_t d = length(sub(pos, g->ports[i].pos));
        if (d < distance) { best = i; distance = d; }
    }
    return best;
}
int police_patrol(const Game *g, int i) {
    return g->npc[i].active && g->npc_role[i] == POLICE && !g->npc[i].hostile;
}
Vec patrol_point(const Game *g, int i) {
    const Port *port = &g->ports[clamp(g->npc_port[i], 0, g->nports ? g->nports - 1 : 0)];
    Vec n = port->normal, a, b, point;
    /* 90 second circuit; 397/1024 turn between slots keeps port mates apart. */
    int angle = (int)(((uint64_t)g->tick * 1024 / (HZ * 90) + (unsigned)i * 397) & 1023);
    a = n.y > 12000 || n.y < -12000 ? vec(0, n.z, -n.y) : vec(-n.z, 0, n.x);
    a = unit(a, 16384);
    b = unit(vec((n.y * a.z - n.z * a.y) / 16384, (n.z * a.x - n.x * a.z) / 16384,
                 (n.x * a.y - n.y * a.x) / 16384), 16384);
    point = add(port->pos, add(scale(a, (int64_t)cosi(angle) * PATROL_RADIUS * FP, 16384LL * 16384),
                               scale(b, (int64_t)sini(angle) * PATROL_RADIUS * FP, 16384LL * 16384)));
    if (port->surface) {
        int64_t alt;
        point = add(point, scale(n, 1500 * FP, 16384));
        alt = altitude_at(g, point);
        if (alt < 800) point = add(point, scale(n, (800 - alt) * FP, 16384));
    }
    return point;
}
static void police_hostile(Game *g, const char *why) {
    int i;
    for (i = 0; i < MAX_NPC; i++) if (police_patrol(g, i)) {
        g->npc[i].hostile = 1;
        if (g->npc[i].cooldown > HZ) g->npc[i].cooldown = HZ;
    }
    if (g->police_alert != 2) say(g, why);
    g->police_alert = 2;
}
void police_patrols(Game *g) {
    int i, j, count, total = 0;
    if (!g->nports || g->docked != -1) return;
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc_role[i] == POLICE) {
        g->npc_port[i] = nearest_port(g, g->npc[i].pos);
        if (g->npc[i].hostile) g->police_alert = 2; /* also restores a loaded pursuit */
        else if (length(sub(g->npc[i].pos, g->pos)) > PATROL_RECALL * FP) {
            g->npc[i].active = 0;
            continue;
        }
        total++;
    }
    if (g->tick - g->police_tick >= 0x80000000U) return; /* reinforcements still en route */
    for (j = 0; j < g->nports; j++) {
        if (length(sub(g->ports[j].pos, g->pos)) > PATROL_LAUNCH * FP) continue;
        for (count = 0, i = 0; i < MAX_NPC; i++)
            if (g->npc[i].active && g->npc_role[i] == POLICE && g->npc_port[i] == j) count++;
        while (count < PATROLS_PER_PORT && total < MAX_PATROLS) {
            Npc *n;
            for (i = 0; i < MAX_NPC && g->npc[i].active; i++) {}
            if (i == MAX_NPC) return;
            n = &g->npc[i];
            memset(n, 0, sizeof(*n));
            n->active = 1; n->hull = POLICE_HULL; n->mission = -1; n->cooldown = HZ * 3;
            n->hostile = g->police_alert == 2;
            g->npc_role[i] = POLICE; g->npc_ship[i] = 3; g->npc_port[i] = j;
            n->pos = patrol_point(g, i);
            n->vel = port_relative_velocity(g, j);
            count++; total++;
        }
    }
}
/* The commander fired a laser or missile. target is the contact hit or
   aimed at, or -1. Engaging an attacker is lawful; any other discharge in
   sight of police earns a fine, and a second one (after two seconds to
   release the trigger) or a shot at police turns every patrol hostile. */
void police_discharge(Game *g, int target) {
    int i, f = system_faction(g), witness = 0;
    if (target >= 0 && g->npc_role[target] == POLICE) {
        if (g->police_alert != 2) g->wanted[f] = clamp(g->wanted[f] + WEAPONS_FINE, 0, 1000000);
        police_hostile(g, "POLICE FIRED UPON - PATROLS ENGAGING");
        return;
    }
    if (target >= 0 && g->npc[target].hostile) return;
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc_role[i] == POLICE &&
        length(sub(g->npc[i].pos, g->pos)) < POLICE_WITNESS * FP) witness = 1;
    if (!witness) return;
    if (!g->police_alert) {
        g->police_alert = 1; g->police_warned = g->tick;
        g->wanted[f] = clamp(g->wanted[f] + WEAPONS_FINE, 0, 1000000);
        g->reputation[f] = clamp(g->reputation[f] - 1, -100, 100);
        say(g, "POLICE: WEAPONS FINE 500 CR - CEASE FIRE");
    } else if (g->police_alert == 1 && g->tick - g->police_warned > HZ * 2) {
        g->wanted[f] = clamp(g->wanted[f] + WEAPONS_FINE, 0, 1000000);
        police_hostile(g, "POLICE: WARNING IGNORED - PATROLS ENGAGING");
    }
}
void career_time(Game *g) {
    uint32_t hours = (g->tick - g->wage_tick) / MARKET_INTERVAL;
    int i;
    if (hours) {
        uint64_t bill = (uint64_t)hours * g->crew * 25;
        g->wage_tick += hours * MARKET_INTERVAL;
        if (bill > (uint32_t)g->credits) {
            g->credits = 0; g->crew = 0;
            say(g, "UNPAID CREW LEFT - RECRUIT AT A PORT");
        } else g->credits -= (int)bill;
    }
    for (i = 0; i < g->rig_count; i++) {
        MiningRig *r = &g->rigs[i];
        uint32_t produced = (g->tick - r->last_tick) / MARKET_INTERVAL;
        r->stock = produced > (uint32_t)(20 - r->stock) ? 20 : r->stock + (int)produced;
        r->last_tick += produced * MARKET_INTERVAL;
    }
}
void career_system(Game *g) {
    int i;
    memset(g->npc_role, 0, sizeof(g->npc_role));
    memset(g->npc_ship, 0, sizeof(g->npc_ship));
    memset(g->pods, 0, sizeof(g->pods));
    memset(g->projectiles, 0, sizeof(g->projectiles));
    g->resource_target = -1;
    g->black_market = 0;
    g->police_alert = 0; g->police_tick = g->tick;
    g->encounter_tick = g->tick;
    for (i = 0; i < MAX_MISSIONS; i++) if (g->contracts[i].state == ACTIVE &&
        g->contracts[i].type == RESCUE && g->contracts[i].progress == 1)
        g->contracts[i].spawned = 1; /* Rescued passengers remain aboard on jumps. */
}
static int spawn(Game *g, int mission, int role, int distance) {
    int i;
    for (i = 0; i < MAX_NPC; i++) if (!g->npc[i].active) {
        Npc *n = &g->npc[i];
        memset(n, 0, sizeof(*n));
        n->pos = add(g->pos, scale(forward(g->yaw, g->pitch), distance * FP, 16384));
        n->vel = g->vel; n->active = 1;
        n->hull = role == INSTALLATION_TARGET ? 150 : role == POLICE ? POLICE_HULL : 60;
        n->hostile = role != CIVILIAN;
        n->mission = mission; n->cooldown = HZ * 3;
        g->npc_role[i] = role; g->npc_ship[i] = role == POLICE ? 3 : role == CIVILIAN ? 1 : 0;
        return i;
    }
    return -1;
}
static int pod(Game *g, Vec pos, Vec vel, int good, int quantity, int mission) {
    int i;
    for (i = 0; i < MAX_PODS; i++) if (!g->pods[i].active) {
        CargoPod *p = &g->pods[i];
        p->pos = pos; p->vel = vel; p->active = 1; p->good = good;
        p->quantity = quantity; p->mission = mission;
        return 1;
    }
    return 0;
}
static void fail(Game *g, Mission *m) {
    int i, slot = (int)(m - g->contracts);
    m->state = FAILED;
    g->reputation[m->faction] = clamp(g->reputation[m->faction] - 2, -100, 100);
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].mission == slot) {
        g->npc[i].mission = -1;
        if (g->npc_role[i] == CIVILIAN) g->npc[i].active = 0;
    }
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].mission == slot) g->pods[i].active = 0;
    say(g, "CONTRACT FAILED - REPUTATION LOST");
}
static void complete(Game *g, Mission *m) {
    int military = m->type == ORBIT_PATROL || m->type == AIR_PATROL || m->type == BOUNTY ||
                   m->type == ESCORT || m->type == RECON || m->type == INSTALLATION;
    int i, slot = (int)(m - g->contracts), f = m->faction;
    m->state = COMPLETE;
    pay(g, m->reward);
    g->reputation[f] = clamp(g->reputation[f] + 2, -100, 100);
    if (military) {
        g->service[f] = clamp(g->service[f] + 1, 0, 10000);
        g->medals[f] = g->service[f] / 5;
    }
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].mission == slot) {
        g->npc[i].active = 0; g->npc[i].mission = -1;
    }
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].mission == slot) g->pods[i].active = 0;
    say(g, "CONTRACT COMPLETE - PAYMENT AND REPUTATION RECEIVED");
}
void career_missions(Game *g) {
    int i, j;
    for (i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &g->contracts[i];
        int64_t alt;
        if (m->state != ACTIVE) continue;
        if (g->tick > m->deadline) { fail(g, m); continue; }
        if (!same(m->destination, g->system.sector)) continue;
        alt = altitude_at(g, g->pos);
        if (m->type == COURIER || m->type == FREIGHT || m->type == PASSENGER || m->type == MULTILEG) {
            if (g->docked != m->port) continue;
            if (m->type == MULTILEG && ++m->leg < m->legs) {
                m->port = (m->port + 1) % g->nports;
                say(g, "LEG DELIVERED - J FOR NEXT PORT");
                continue;
            }
        } else if (m->type == SURVEY || m->type == RECON) {
            int body = g->ports[m->port].body;
            if (g->docked != -1 || (m->type == RECON &&
                (g->body != body || length(sub(g->pos, g->ports[m->port].pos)) > 10000 * FP)) ||
                alt < 500 || alt > 6000 ||
                length(sub(g->vel, surface_velocity(&g->system.planets[g->body],
                    sub(scale(g->pos, 1, FP), g->system.planets[g->body].center)))) > 200 * FP) continue;
            if (++m->progress < (m->type == RECON ? 30 : 20) * HZ) continue;
        } else if (m->type == RESCUE) {
            if (!m->spawned && g->docked == -1 && alt > 13000) {
                Vec location = add(g->pos, scale(forward(g->yaw, g->pitch), 700 * FP, 16384));
                if (pod(g, location, g->vel, 0, 1, i)) m->spawned = 1;
            }
            if (m->progress < 1 || g->docked != m->port) continue;
        } else if (m->type == ESCORT) {
            if (!m->spawned && g->docked == -1 && alt > 13000) {
                j = spawn(g, i, CIVILIAN, 800);
                if (j >= 0) { m->spawned = 1; spawn(g, i, RAIDER, 2400); }
            }
            if (!m->spawned) continue;
            for (j = 0; j < MAX_NPC; j++) if (g->npc[j].active && g->npc[j].mission == i &&
                g->npc_role[j] == CIVILIAN) break;
            if (j == MAX_NPC) { fail(g, m); continue; }
            if (g->docked != -1 || length(sub(g->npc[j].pos, g->pos)) > 5000 * FP) continue;
            if (++m->progress < 120 * HZ) continue;
        } else {
            if (!m->spawned && g->docked == -1 &&
                ((m->type == AIR_PATROL && alt > 600 && alt < 6000) ||
                 (m->type == INSTALLATION && length(sub(g->pos, g->ports[m->port].pos)) < 10000 * FP) ||
                 (m->type != AIR_PATROL && m->type != INSTALLATION && alt > 13000))) {
                j = spawn(g, i, m->type == INSTALLATION ? INSTALLATION_TARGET : RAIDER, 2000);
                if (j >= 0) {
                    m->spawned = 1;
                    if (m->type == INSTALLATION) {
                        const Planet *p = &g->system.planets[g->ports[m->port].body];
                        Vec local = sub(scale(g->ports[m->port].pos, 1, FP), p->center);
                        Vec offset = unit(vec(local.z, 0, -local.x), 1000);
                        if (!length(offset)) offset = vec(1000, 0, 0);
                        local = add(local, offset);
                        local = radial(local, p->radius + terrain_height(p, local) + 40);
                        g->npc[j].pos = scale(add(p->center, local), FP, 1);
                        g->npc[j].vel = surface_velocity(p, local);
                    }
                }
            }
            if (m->progress < 1) continue;
        }
        complete(g, m);
    }
}
void damage_npc(Game *g, int index, int damage) {
    Npc *n;
    int f = system_faction(g), role;
    if (index < 0 || index >= MAX_NPC || !g->npc[index].active) return;
    n = &g->npc[index]; role = g->npc_role[index];
    if (role == POLICE || role == CIVILIAN) {
        g->wanted[f] = clamp(g->wanted[f] + 200, 0, 1000000);
        g->reputation[f] = clamp(g->reputation[f] - 2, -100, 100);
    }
    if (role == POLICE) {
        police_hostile(g, "POLICE FIRED UPON - PATROLS ENGAGING");
        damage = damage / POLICE_ARMOUR > 1 ? damage / POLICE_ARMOUR : 1;
    }
    n->hull -= damage;
    if (n->hull > 0) return;
    destruction_npc(g, index);
    n->hull = 0; n->active = 0;
    if (role == POLICE) g->police_tick = g->tick + HZ * 180;
    if (n->mission >= 0 && g->contracts[n->mission].state == ACTIVE) {
        Mission *m = &g->contracts[n->mission];
        if (role == CIVILIAN) fail(g, m);
        else if (m->type != ESCORT) {
            m->progress++;
            if (m->type == ASSASSINATION)
                g->wanted[(m->faction + 1) % FACTIONS] = clamp(g->wanted[(m->faction + 1) % FACTIONS] + 1000, 0, 1000000);
        }
    }
    if (role == RAIDER || role == INSTALLATION_TARGET) {
        g->kills++; pay(g, 100);
        pod(g, n->pos, n->vel, 2, 1, -1);
        say(g, "TARGET DISABLED - 100 CR / N COLLECTS SALVAGE");
    }
}
int fire_missile(Game *g) {
    int i, target = combat_target(g);
    if (g->docked != -1 || g->dead || g->paused || target < 0 || !g->missiles) return 0;
    for (i = 0; i < MAX_MISSILES; i++) if (!g->projectiles[i].active) {
        Missile *m = &g->projectiles[i];
        m->pos = g->pos; m->vel = g->vel; m->target = target;
        m->ttl = HZ * 20; m->active = 1; g->missiles--;
        police_discharge(g, target);
        return 1;
    }
    return 0;
}
int countermeasure(Game *g) {
    int i;
    if (g->docked != -1 || g->dead || g->paused || !g->countermeasures) return 0;
    g->countermeasures--;
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc[i].hostile &&
        length(sub(g->npc[i].pos, g->pos)) < 5000 * FP) g->npc[i].cooldown = HZ * 12;
    say(g, "DECOY CLOUD - HOSTILE FIRE SUPPRESSED FOR 12 SECONDS");
    return 1;
}
int escape_pod(Game *g) {
    int i, destination = -1;
    int64_t distance = INT64_MAX;
    if (g->docked != -1 || !equipped(g, ESCAPE_POD) || g->dead || g->paused) return 0;
    for (i = 0; i < g->nports; i++) {
        int64_t d = length(sub(g->pos, g->ports[i].pos));
        if (d < distance) { destination = i; distance = d; }
    }
    if (destination < 0) { say(g, "NO INHABITED PORT FOR RESCUE"); return 0; }
    for (i = 0; i < MAX_MISSIONS; i++) if (g->contracts[i].state == ACTIVE) fail(g, &g->contracts[i]);
    memset(g->cargo, 0, sizeof(g->cargo));
    g->crew = g->cabins = g->missiles = g->countermeasures = g->upgrade = 0;
    g->turret_upgrade[0] = g->turret_upgrade[1] = 0;
    g->ship = 0; g->equipment = 0;
    g->hull = g->shield = g->fuel = 100; g->heat = g->fuel_burn = 0;
    g->tick += HZ * 3600; career_time(g); update_world(g);
    g->body = g->frame_body = g->ports[destination].body;
    g->docked = destination; g->pos = g->ports[destination].pos;
    g->vel = port_relative_velocity(g, destination);
    g->gear = 1; g->autopilot = g->drive_manual = g->drive_active = 0;
    g->warp = 1; g->throttle = 0; g->clearance = -1; g->dock_stage = 0;
    memset(g->npc, 0, sizeof(g->npc)); career_system(g); refresh_board(g);
    say(g, "POD RESCUE - HULL/CARGO LOST - LOANED A CINDER");
    return 1;
}
Vec asteroid_position(const Game *g, int index) {
    const Planet *p = &g->system.planets[0];
    int angle = (int)(g->system.seed & 1023) + index * 127;
    return add(scale(p->center, FP, 1), vec((int64_t)sini(angle) * 3000 * FP / 16384,
        (int64_t)(p->radius + p->atmosphere + 200000) * FP,
        (int64_t)cosi(angle) * 3000 * FP / 16384));
}
Vec rig_position(const Game *g, int index) {
    const MiningRig *r = &g->rigs[index];
    const Planet *p = &g->system.planets[r->body];
    return add(scale(p->center, FP, 1), body_rotate(p, r->anchor, 0));
}
static Deposit *deposit(Game *g) {
    int i, j;
    for (i = 0; i < g->deposit_count; i++) if (same(g->deposits[i].sector, g->system.sector))
        return &g->deposits[i];
    if (g->deposit_count == MAX_DEPOSITS) return NULL;
    i = g->deposit_count++; g->deposits[i].sector = g->system.sector;
    for (j = 0; j < ASTEROIDS; j++) g->deposits[i].ore[j] = 4;
    return &g->deposits[i];
}
int mine_asteroid(Game *g) {
    int i, yaw, pitch;
    Deposit *d;
    Vec f;
    if (!equipped(g, MINING) || g->docked != -1 || g->paused || g->dead || g->heat > 80) return 0;
    view_angles(g, &yaw, &pitch); f = forward(yaw, pitch);
    for (i = 0; i < ASTEROIDS; i++) {
        Vec rel = sub(asteroid_position(g, i), g->pos);
        int64_t along = (rel.x * f.x + rel.y * f.y + rel.z * f.z) / 16384;
        if (length(rel) > 3000 * FP || along < 0 ||
            length(sub(rel, scale(f, along, 16384))) > 150 * FP) continue;
        d = deposit(g);
        if (!d || !d->ore[i]) return 0;
        if (!pod(g, asteroid_position(g, i), sub(body_velocity(&g->system, 0, g->tick), frame_velocity(g)),
                 i % 3 ? 2 : 6, 1, -1)) return 0;
        d->ore[i]--; g->heat += 10; g->laser = 3; g->shots++;
        say(g, "ORE RELEASED - APPROACH SLOWLY / N COLLECT");
        return 1;
    }
    return 0;
}
int collect_cargo(Game *g) {
    int i;
    if (g->docked != -1 || g->dead || g->paused) return 0;
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].active) {
        CargoPod *p = &g->pods[i];
        if (length(sub(g->pos, p->pos)) > 250 * FP || length(sub(g->vel, p->vel)) > 50 * FP) continue;
        if (p->mission >= 0) {
            Mission *m = &g->contracts[p->mission];
            if (m->state != ACTIVE || m->type != RESCUE) { p->active = 0; continue; }
            m->progress = 1;
        } else {
            if (cargo_used(g) + p->quantity > ship_capacity[g->ship]) return 0;
            g->cargo[p->good] += p->quantity;
        }
        p->active = 0; say(g, "CARGO/PASSENGERS RECOVERED"); return 1;
    }
    return 0;
}
int deploy_rig(Game *g) {
    MiningRig *r;
    if (g->docked != -2 || !equipped(g, MINING) || g->rig_count >= MAX_RIGS ||
        g->credits < 500 || g->dead || g->paused || g->system.planets[g->body].type == GAS_GIANT) return 0;
    r = &g->rigs[g->rig_count++]; r->sector = g->system.sector;
    r->body = g->body; r->anchor = g->ground_anchor; r->stock = 0; r->last_tick = g->tick;
    g->credits -= 500; say(g, "RIG DEPLOYED - 1 T/HOUR - N RECOVERS NEAR LANDED SITE"); return 1;
}
int recover_rig(Game *g) {
    int i;
    if (g->docked != -2 || g->dead || g->paused) return 0;
    career_time(g);
    for (i = 0; i < g->rig_count; i++) {
        MiningRig *r = &g->rigs[i];
        int room = ship_capacity[g->ship] - cargo_used(g), quantity;
        if (!same(r->sector, g->system.sector) || r->body != g->body ||
            length(sub(r->anchor, g->ground_anchor)) > 500 * FP) continue;
        quantity = r->stock < room ? r->stock : room;
        g->cargo[2] += quantity; r->stock -= quantity;
        if (!r->stock) { g->rigs[i] = g->rigs[--g->rig_count]; g->resource_target = -1; pay(g, 250); }
        say(g, "RIG RECOVERED - ALLOYS LOADED / EMPTY RIG SOLD FOR 250 CR"); return 1;
    }
    return 0;
}
void career_tick(Game *g, Vec old) {
    int i, f = system_faction(g);
    Vec displacement = sub(g->pos, old);
    career_time(g);
    /* Pods are in the same translated frame as NPCs. Their velocity excludes
       frame translation; the caller translates their positions with the world. */
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].active)
        g->pods[i].pos = add(g->pods[i].pos, scale(g->pods[i].vel, 1, HZ));
    for (i = 0; i < MAX_MISSILES; i++) if (g->projectiles[i].active) {
        Missile *m = &g->projectiles[i];
        Npc *n = &g->npc[m->target];
        Vec from = m->pos;
        if (!n->active || --m->ttl <= 0) { m->active = 0; continue; }
        /* 900 m/s relative homing with swept collision; fixed physics only. */
        m->vel = add(n->vel, unit(sub(n->pos, m->pos), 900 * FP));
        m->pos = add(m->pos, scale(m->vel, 1, HZ));
        if (swept_sphere(scale(sub(from, sub(n->pos, scale(n->vel, 1, HZ))), 1, FP),
                         scale(sub(m->pos, n->pos), 1, FP), vec(0, 0, 0), 70)) {
            damage_npc(g, m->target, 90); m->active = 0;
        } else if (terrain_impact(g, from, m->pos, nearest_body(g, m->pos))) m->active = 0;
    }
    if (g->docked != -1) return;
    if (g->tick % HZ == 0) {
        /* Police damage control outpaces anything short of a sustained barrage. */
        for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc_role[i] == POLICE)
            g->npc[i].hull = clamp(g->npc[i].hull + POLICE_REPAIR, 0, POLICE_HULL);
        police_patrols(g);
    }
    if (equipped(g, SCOOP) && g->tick % HZ == 0 && g->fuel < 100) {
        const Planet *p = &g->system.planets[g->body];
        int64_t alt = altitude_at(g, g->pos);
        int64_t speed = length(sub(g->vel, surface_velocity(p, sub(scale(g->pos, 1, FP), p->center))));
        if (p->climate == HYDROGEN && alt > p->atmosphere / 3 && alt < p->atmosphere &&
            speed > 50 * FP && speed < 800 * FP) { g->fuel++; say(g, "HYDROGEN SCOOPING"); }
    }
    if (g->tick - g->encounter_tick >= HZ * 60U) {
        int near = 0;
        g->encounter_tick = g->tick;
        for (i = 0; i < g->nports; i++) if (length(sub(g->pos, g->ports[i].pos)) < 20000 * FP) near = 1;
        if (near && g->system.government) {
            inspect_cargo(g);
            police_patrols(g);
            /* Small fines are settled at a port; a warrant sets patrols on the commander. */
            if (g->wanted[f] >= WARRANT) police_hostile(g, "POLICE: OUTSTANDING WARRANT - ENGAGING");
        } else if (!g->system.government && cargo_used(g) > g->cabins * 2 &&
                   altitude_at(g, g->pos) > 13000 && length(displacement) < 2000 * FP) {
            for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc[i].hostile) break;
            if (i == MAX_NPC) { spawn(g, -1, RAIDER, 2400); say(g, "PIRATE INTERCEPT"); }
        }
    }
    for (i = 0; i < ASTEROIDS; i++) {
        Vec p = asteroid_position(g, i);
        Vec motion = sub(body_velocity(&g->system, 0, g->tick), frame_velocity(g));
        if (swept_sphere(scale(sub(old, sub(p, scale(motion, 1, HZ))), 1, FP),
                         scale(sub(g->pos, p), 1, FP), vec(0, 0, 0), 90)) {
            g->hull = 0; say(g, "ASTEROID IMPACT"); break;
        }
    }
    /* Raiders in escort encounters attack the escorted civilian. */
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && g->npc_role[i] == CIVILIAN &&
        g->tick % (HZ * 3) == 0) {
        int j;
        for (j = 0; j < MAX_NPC; j++) if (g->npc[j].active && g->npc[j].hostile &&
            g->npc[j].mission == g->npc[i].mission &&
            length(sub(g->npc[j].pos, g->npc[i].pos)) < 3000 * FP) {
            g->npc[i].hull -= 2;
            if (g->npc[i].hull <= 0) {
                destruction_npc(g, i);
                g->npc[i].active = 0;
                if (g->npc[i].mission >= 0) fail(g, &g->contracts[g->npc[i].mission]);
            }
            break;
        }
    }
}
