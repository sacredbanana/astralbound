/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <stdio.h>
#include <string.h>
const char *goods_names[GOODS] = {"WATER",    "GRAIN",     "ALLOYS",   "MEDICINE",
                                  "CIRCUITS", "MACHINERY", "CRYSTALS", "FUEL CELLS"};
const char *mission_names[MISSION_TYPES] = {"COURIER",    "FREIGHT", "ORBIT PATROL",
                                            "AIR PATROL", "SURVEY",  "BOUNTY"};
const int ship_capacity[3] = {20, 60, 140};
static uint32_t next(uint32_t *s) {
    *s = hash32(*s + 0x9e3779b9U);
    return *s;
}
void generate_system(uint32_t seed, Sector s, System *out) {
    static const char *a[] = {"VE", "KA", "OR", "SE", "TA", "NI", "LU", "AE",
                              "MI", "RU", "DA", "IO", "ZE", "UL", "BA", "HE"};
    static const char *b[] = {"LORA", "NARA", "VEL", "SEN", "RIA", "THA", "MIN", "RIS",
                              "RON",  "LIS",  "DRA", "ZEN", "VAR", "TIS", "LAN", "MAR"};
    uint32_t r = sector_seed(seed, s);
    int i;
    memset(out, 0, sizeof(*out));
    out->seed = r;
    out->sector = s;
    {
        unsigned first = next(&r) % 16, last = next(&r) % 16;
        snprintf(out->name, sizeof(out->name), "%s%s", a[first], b[last]);
    }
    out->economy = next(&r) % 4;
    out->government = next(&r) % 4;
    out->count = 2 + next(&r) % 5;
    for (i = 0; i < out->count; i++) {
        Planet *p = &out->planets[i];
        p->seed = next(&r);
        p->center = i ? scale(forward((int)(next(&r) % 1024), 0), 1200000LL + i * 1800000LL, 16384)
                      : vec(0, 0, 0);
        p->radius = 60000 + (next(&r) % 120000);
        p->gravity = 2 + next(&r) % 9;
        p->atmosphere = i == 0 ? 12000 : next(&r) % 16000;
        snprintf(p->name, sizeof(p->name), "%.20s %c", out->name, 'B' + i);
    }
}
void refresh_board(Game *g) {
    int i;
    uint32_t day = g->tick / (HZ * 86400U),
             r = hash32(g->system.seed + day * 173U + (uint32_t)(g->docked + 1) * 113U);
    for (i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &g->board[i];
        memset(m, 0, sizeof(*m));
        m->id = next(&r);
        m->type = (MissionType)(i % MISSION_TYPES);
        m->state = OFFERED;
        m->destination = g->system.sector;
        if (m->type == COURIER || m->type == FREIGHT) {
            m->destination.x += (int)(next(&r) % 3) - 1;
            m->destination.z += 1;
        }
        {
            System dest;
            generate_system(g->seed, m->destination, &dest);
            m->port = next(&r) % (dest.count * PORTS_PER_PLANET);
        }
        m->good = next(&r) % GOODS;
        m->quantity = m->type == FREIGHT ? 2 + next(&r) % 6 : 0;
        m->reward = 400 + next(&r) % 2400;
        m->deadline = g->tick + HZ * (3600U + next(&r) % 7200U);
    }
}
int cargo_used(const Game *g) {
    int i, n = 0;
    for (i = 0; i < GOODS; i++)
        n += g->cargo[i];
    for (i = 0; i < MAX_MISSIONS; i++)
        if (g->contracts[i].state == ACTIVE)
            n += g->contracts[i].quantity;
    return n;
}
int price(const Game *g, int good) {
    static const int base[GOODS] = {12, 25, 90, 170, 290, 390, 600, 60};
    uint32_t h = hash32(g->system.seed + (uint32_t)good * 7919U + (uint32_t)(g->docked + 1) * 41U);
    return base[good] * (70 + (int)(h % 61) + (good % 4 == g->system.economy ? -25 : 15)) / 100;
}
int trade(Game *g, int good, int buy) {
    int p;
    if (g->docked < 0 || good < 0 || good >= GOODS || g->dead)
        return 0;
    p = price(g, good);
    if (buy) {
        if (g->credits < p || g->stock[good] < 1 || cargo_used(g) >= ship_capacity[g->ship])
            return 0;
        g->credits -= p;
        g->cargo[good]++;
        g->stock[good]--;
    } else {
        if (!g->cargo[good])
            return 0;
        g->credits += p * 9 / 10;
        g->cargo[good]--;
        g->stock[good]++;
    }
    return 1;
}
int accept_mission(Game *g, int index) {
    int i, slot = -1;
    Mission *m;
    if (g->docked < 0 || index < 0 || index >= MAX_MISSIONS || g->dead)
        return 0;
    m = &g->board[index];
    if (m->state != OFFERED || g->tick >= m->deadline ||
        cargo_used(g) + m->quantity > ship_capacity[g->ship])
        return 0;
    for (i = 0; i < MAX_MISSIONS; i++) {
        if (g->contracts[i].id == m->id)
            return 0;
        if (g->contracts[i].state != ACTIVE && slot < 0)
            slot = i;
    }
    if (slot < 0)
        return 0;
    g->contracts[slot] = *m;
    g->contracts[slot].state = ACTIVE;
    m->state = ACTIVE;
    return 1;
}
