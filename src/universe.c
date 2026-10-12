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
const char *body_names[BODY_TYPES] = {"ROCK", "OCEAN", "DESERT", "ICE", "GAS GIANT", "MOON"};
/* Density is an integer four-arm spiral with a disc and central bulge.
   One map sector is one light year; the procedural disc spans 30000 sectors. */
int galaxy_density(Sector s) {
    int64_t radial = length(vec(s.x, 0, s.z)), height = s.y < 0 ? -(int64_t)s.y : s.y;
    int yaw = 0, pitch = 0, arm, disc;
    if (radial > 30000 || height > 3000)
        return 1;
    aim_angles(vec(s.x, 0, s.z), &yaw, &pitch);
    arm = (yaw - (int)(radial / 50)) & 255;
    if (arm > 128)
        arm = 256 - arm;
    disc = (int)((30000 - radial) * (3000 - height) / 900000);
    return 5 + disc + (128 - arm) / 3 + (radial < 2000 ? 25 : 0);
}
/* Q30 sine polynomial for analytic orbits. The flight Q14 table is too
   coarse at AU scales and linear interpolation gives abrupt velocity changes
   at station-orbit segment boundaries. Range reduction keeps x <= pi/2;
   the thirteenth-order expansion stays within one Q30 unit in practice. */
static int64_t sine_fine(uint64_t angle) {
    uint64_t phase = angle & ((1024ULL << 30) - 1);
    int quadrant = (int)(phase >> 38);
    int64_t x = (int64_t)(phase & ((256ULL << 30) - 1)), x2, term, sum;
    if (quadrant & 1)
        x = (256LL << 30) - x;
    x = (x / 256) * 1686629713LL / (1LL << 30) +
        (x % 256) * 1686629713LL / (256LL << 30); /* pi/2 in Q30 */
    x2 = x * x / (1LL << 30);
    term = sum = x;
    term = term * x2 / (1LL << 30) / 6;
    sum -= term;
    term = term * x2 / (1LL << 30) / 20;
    sum += term;
    term = term * x2 / (1LL << 30) / 42;
    sum -= term;
    term = term * x2 / (1LL << 30) / 72;
    sum += term;
    term = term * x2 / (1LL << 30) / 110;
    sum -= term;
    term = term * x2 / (1LL << 30) / 156;
    sum += term;
    return quadrant >= 2 ? -sum : sum;
}
static Vec orbit_position(int64_t radius, uint32_t period, int phase, int inclination,
                          uint32_t tick) {
    uint64_t cycle = (uint64_t)period * HZ;
    uint64_t time = (uint64_t)tick % cycle;
    uint64_t coarse = (time << 30) / cycle;
    uint64_t remainder = (time << 30) % cycle;
    uint64_t angle = (coarse << 10) + (remainder << 10) / cycle + ((uint64_t)(unsigned)phase << 30);
    int64_t x = sine_fine(angle), z = sine_fine(angle + (256ULL << 30));
    Vec p = scale(vec(x, 0, z), radius, 1LL << 30);
    p.y = p.z * sini(inclination) / 16384;
    p.z = p.z * cosi(inclination) / 16384;
    return p;
}
Vec body_position(const System *s, int body, uint32_t tick) {
    const Planet *p = &s->planets[body];
    Vec position = orbit_position(p->orbit, p->period, p->phase, p->inclination, tick);
    if (p->parent >= 0)
        position = add(position, body_position(s, p->parent, tick));
    return position;
}
Vec body_velocity(const System *s, int body, uint32_t tick) {
    /* A centred one-second derivative is stable on both 68000 and host. */
    if (body < 0)
        return vec(0, 0, 0);
    if (s->world_ready && s->orbit_tick == tick / HZ * HZ)
        return s->planets[body].velocity;
    if (tick < HZ)
        return scale(sub(body_position(s, body, tick + HZ), body_position(s, body, tick)), FP, 1);
    return scale(sub(body_position(s, body, tick + HZ), body_position(s, body, tick - HZ)), FP, 2);
}
int system_port_count(const System *s) {
    int i, n = 0;
    for (i = 0; i < s->count; i++)
        if (s->planets[i].settled)
            n += s->planets[i].type == GAS_GIANT ? 2 : 4;
    return n;
}
void generate_system(uint32_t seed, Sector s, System *out) {
    static const char *a[] = {"VE", "KA", "OR", "SE", "TA", "NI", "LU", "AE",
                              "MI", "RU", "DA", "IO", "ZE", "UL", "BA", "HE"};
    static const char *b[] = {"LORA", "NARA", "VEL", "SEN", "RIA", "THA", "MIN", "RIS",
                              "RON",  "LIS",  "DRA", "ZEN", "VAR", "TIS", "LAN", "MAR"};
    uint32_t r = sector_seed(seed, s);
    int i, primaries, frontier = length(vec(s.x, s.y, s.z)) > 8;
    memset(out, 0, sizeof(*out));
    out->seed = r;
    out->sector = s;
    {
        unsigned first = next(&r) % 16, last = next(&r) % 16;
        snprintf(out->name, sizeof(out->name), "%s%s", a[first], b[last]);
    }
    out->economy = next(&r) % 4;
    out->government = next(&r) % 4;
    out->density = galaxy_density(s);
    /* Sparse cells in the disc/arms contain stars; the local inhabited
       bubble remains connected for the initial courier/trading routes. */
    if (frontier && hash32(out->seed ^ 0x47414c58U) % 100 >= (unsigned)out->density)
        return;
    out->stars = 1 + (next(&r) % 4 == 0);
    out->star[0].radius = 696000000;
    if (out->stars == 2) {
        out->star[0].orbit = 2000000000LL;
        out->star[1].orbit = 6000000000LL;
        out->star[1].radius = 350000000;
        out->star[0].period = out->star[1].period = 500000;
        out->star[1].phase = 512;
    }
    primaries = 3 + next(&r) % 4;
    out->count = primaries;
    for (i = 0; i < primaries; i++) {
        Planet *p = &out->planets[i];
        p->seed = next(&r);
        p->parent = -1;
        p->type = i == 0 ? OCEAN : (BodyType)(next(&r) % 5);
        p->radius =
            p->type == GAS_GIANT ? 30000000 + next(&r) % 40000000 : 2500000 + next(&r) % 6000000;
        p->gravity = p->type == GAS_GIANT ? 15 + next(&r) % 12 : 3 + next(&r) % 10;
        p->orbit = AU_METRES / 3 + (int64_t)i * i * AU_METRES / 2;
        /* Kepler period for one solar mass, rounded to seconds. Radius is
           divided by a million kilometres before cubing to stay in uint64. */
        {
            uint64_t million_km = (uint64_t)p->orbit / 1000000000ULL;
            p->period = (uint32_t)isqrt64(million_km * million_km * million_km * 296000000ULL);
        }
        p->phase = next(&r) % 1024;
        p->inclination = (int)(next(&r) % 17) - 8;
        p->influence = p->orbit / 100;
        p->rings = p->type == GAS_GIANT || (p->type == ICE && next(&r) % 3 == 0);
        p->settled = i == 0 ? (!frontier || next(&r) % 100 < (unsigned)out->density / 8)
                            : (!frontier && next(&r) % 5 == 0);
        p->climate = p->type == OCEAN       ? TEMPERATE
                     : p->type == DESERT    ? DENSE
                     : p->type == ICE       ? FROZEN
                     : p->type == GAS_GIANT ? HYDROGEN
                                            : THIN;
        p->atmosphere = p->climate == TEMPERATE  ? 80000
                        : p->climate == DENSE    ? 150000
                        : p->climate == HYDROGEN ? 400000
                        : p->climate == FROZEN   ? 8000
                                                 : 20000;
        snprintf(p->name, sizeof(p->name), "%.20s %c", out->name, 'B' + i);
    }
    for (i = 0; i < primaries && out->count < MAX_PLANETS; i++) {
        if (i == 0 || out->planets[i].type == GAS_GIANT || next(&r) % 2 == 0) {
            Planet *p = &out->planets[out->count++];
            p->seed = next(&r);
            p->parent = i;
            p->type = MOON;
            p->climate = VACUUM;
            p->radius = 300000 + next(&r) % 1500000;
            p->gravity = 1 + next(&r) % 3;
            p->orbit = (int64_t)out->planets[i].radius * 8 + 30000000;
            p->period = (uint32_t)(isqrt64((uint64_t)p->orbit / out->planets[i].gravity) *
                                   (uint64_t)p->orbit / out->planets[i].radius * 6283 / 1000);
            p->phase = next(&r) % 1024;
            p->inclination = (int)(next(&r) % 33) - 16;
            p->influence = p->orbit / 5;
            p->settled = !frontier && next(&r) % 8 == 0;
            snprintf(p->name, sizeof(p->name), "%.18s %c M", out->name, 'B' + i);
        }
    }
    for (i = 0; i < out->count; i++)
        out->planets[i].center = body_position(out, i, 0);
}
static Vec port_offset(const Planet *p, int slot, uint32_t tick) {
    if (slot == 0)
        return vec(0, (int64_t)(p->radius + 12) * FP, 0);
    if (slot == 2)
        return radial(forward(120, 180), (int64_t)(p->radius + 12) * FP);
    {
        int64_t radius = p->radius + p->atmosphere + (slot == 1 ? 100000LL : 120000LL);
        uint32_t period = (uint32_t)(isqrt64((uint64_t)radius / p->gravity) * (uint64_t)radius /
                                     p->radius * 6283 / 1000);
        Vec q = orbit_position(radius, period, slot == 1 ? 0 : 256, 0, tick);
        return scale(vec(q.x, q.z, 0), FP, 1);
    }
}
void update_world(Game *g) {
    int i;
    uint32_t epoch = g->tick / HZ * HZ;
    int refresh = !g->system.world_ready || g->system.orbit_tick != epoch;
    for (i = 0; i < g->system.count; i++) {
        Planet *p = &g->system.planets[i];
        if (refresh) {
            p->orbit_start = body_position(&g->system, i, epoch);
            p->orbit_end = body_position(&g->system, i, epoch + HZ);
            p->velocity = scale(sub(p->orbit_end, p->orbit_start), FP, 1);
        }
        p->center = add(p->orbit_start, scale(sub(p->orbit_end, p->orbit_start), g->tick % HZ, HZ));
    }
    g->system.orbit_tick = epoch;
    g->system.world_ready = 1;
    for (i = 0; i < g->system.stars; i++) {
        Star *s = &g->system.star[i];
        s->center =
            s->orbit ? orbit_position(s->orbit, s->period, s->phase, 0, g->tick) : vec(0, 0, 0);
    }
    for (i = 0; i < g->nports; i++) {
        Port *port = &g->ports[i];
        const Planet *p = &g->system.planets[port->body];
        int slot = port->surface ? (strstr(port->name, "HAVEN") ? 0 : 2)
                                 : (strstr(port->name, "RELAY") ? 1 : 3);
        Vec offset;
        if (refresh) {
            port->orbit_start = port_offset(p, slot, epoch);
            port->orbit_end = port_offset(p, slot, epoch + HZ);
        }
        offset = add(port->orbit_start,
                     scale(sub(port->orbit_end, port->orbit_start), g->tick % HZ, HZ));
        port->pos = add(scale(p->center, FP, 1), offset);
        port->normal = unit(offset, 16384);
        port->velocity = body_velocity(&g->system, port->body, g->tick);
        if (!port->surface) {
            Vec v = sub(port->orbit_end, port->orbit_start);
            port->velocity = add(port->velocity, v);
        }
        port->rotation = (int)((uint64_t)g->tick * 1024 / (HZ * 600)) & 1023;
    }
}
Vec frame_velocity(const Game *g) {
    return body_velocity(&g->system, g->frame_body, g->tick);
}
Vec port_relative_velocity(const Game *g, int port) {
    return sub(g->ports[port].velocity, frame_velocity(g));
}
Vec navigation_position(const Game *g) {
    if (g->target >= 0)
        return g->ports[g->target].pos;
    {
        const Planet *p = &g->system.planets[g->target_body];
        return add(scale(p->center, FP, 1),
                   vec(0, (int64_t)(p->radius + p->atmosphere + 200000) * FP, 0));
    }
}
Vec navigation_velocity(const Game *g) {
    return g->target >= 0
               ? port_relative_velocity(g, g->target)
               : sub(body_velocity(&g->system, g->target_body, g->tick), frame_velocity(g));
}
int request_clearance(Game *g, int port) {
    Port *p;
    unsigned phase;
    if (port < 0 || port >= g->nports)
        return 0;
    p = &g->ports[port];
    if (g->clearance == port)
        return 1;
    phase = (g->tick / HZ + (unsigned)port * 17) % 120;
    if (!p->surface && phase < 12) {
        p->busy_until = g->tick + (12 - phase) * HZ;
        return 0;
    }
    g->clearance = port;
    p->busy_until = 0;
    return 1;
}
/* Hollow station: a 60 m aperture and solid annulus to 240 m, with a
   100 m deep docking throat. Swept queries include fast wall crossings. */
int station_impact(const Game *g, Vec from, Vec to, int port) {
    const Port *p = &g->ports[port];
    Vec a, b, start, end;
    int64_t az, bz, first = 0, last = 65536;
    if (p->surface)
        return 0;
    a = scale(sub(from, sub(p->pos, scale(port_relative_velocity(g, port), 1, HZ))), 1, FP);
    b = scale(sub(to, p->pos), 1, FP);
    if (!swept_sphere(a, b, vec(0, 0, 0), 260))
        return 0;
    az = (a.x * p->normal.x + a.y * p->normal.y + a.z * p->normal.z) / 16384;
    bz = (b.x * p->normal.x + b.y * p->normal.y + b.z * p->normal.z) / 16384;
    if (az == bz) {
        if (az < -56 || az > 56)
            return 0;
    } else {
        int64_t enter = (-56 - az) * 65536 / (bz - az), leave = (56 - az) * 65536 / (bz - az);
        if (enter > leave) {
            int64_t swap = enter;
            enter = leave;
            leave = swap;
        }
        if (enter > first)
            first = enter;
        if (leave < last)
            last = leave;
        if (first > last)
            return 0;
    }
    start = add(a, scale(sub(b, a), first, 65536));
    end = add(a, scale(sub(b, a), last, 65536));
    az = (start.x * p->normal.x + start.y * p->normal.y + start.z * p->normal.z) / 16384;
    bz = (end.x * p->normal.x + end.y * p->normal.y + end.z * p->normal.z) / 16384;
    start = sub(start, scale(p->normal, az, 16384));
    end = sub(end, scale(p->normal, bz, 16384));
    if (length(start) < 54 && length(end) < 54)
        return 0;
    return swept_sphere(start, end, vec(0, 0, 0), 246);
}
int exploration_index(const Game *g, Sector s) {
    int i;
    for (i = 0; i < g->explored_count; i++)
        if (g->explored[i].sector.x == s.x && g->explored[i].sector.y == s.y &&
            g->explored[i].sector.z == s.z)
            return i;
    return -1;
}
void record_exploration(Game *g) {
    int i = exploration_index(g, g->system.sector);
    Exploration *e;
    if (i < 0) {
        if (g->explored_count == MAX_EXPLORED)
            return; /* Preserve all previous discoveries; never silently evict one. */
        i = g->explored_count++;
        memset(&g->explored[i], 0, sizeof(g->explored[i]));
        g->explored[i].sector = g->system.sector;
        g->explored[i].first_tick = g->tick;
    }
    e = &g->explored[i];
    e->last_tick = g->tick;
    e->visited |= 1U << g->body;
    if (altitude_at(g, g->pos) < 100000)
        e->surveyed |= 1U << g->body;
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
            /* Offer deliveries only where there is a real port. Nearby core
               sectors are inhabited; frontier offers fall back to this system. */
            if (!system_port_count(&dest)) {
                m->destination = g->system.sector;
                dest = g->system;
            }
            m->port = system_port_count(&dest)
                          ? next(&r) % (system_port_count(&dest) > 4 ? 4 : system_port_count(&dest))
                          : 0;
        }
        m->good = next(&r) % GOODS;
        m->quantity = m->type == FREIGHT ? 2 + next(&r) % 6 : 0;
        m->reward = 400 + next(&r) % 2400;
        m->deadline = g->tick + HZ * ((m->type == COURIER || m->type == FREIGHT ? 21600U : 3600U) +
                                      next(&r) % 7200U);
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
/* Only traded markets occupy the bounded commander ledger. Quotes lazily
   recover towards generated supply, one tonne per simulated hour. */
int market_index(const Game *g, Sector sector, int port) {
    int i;
    for (i = 0; i < g->market_count; i++) {
        const Market *m = &g->markets[i];
        if (m->port == port && m->sector.x == sector.x && m->sector.y == sector.y &&
            m->sector.z == sector.z)
            return i;
    }
    return -1;
}
static int normal_stock(const Game *g, int good) {
    uint32_t h = hash32(g->system.seed + (uint32_t)good * 7919U +
                       (uint32_t)(g->docked + 1) * 113U);
    return 10 + (int)(h % 80);
}
int market_stock(const Game *g, int good) {
    int i, stock, normal, recovery;
    if (good < 0 || good >= GOODS || g->docked < 0 || g->docked >= g->nports)
        return 0;
    normal = normal_stock(g, good);
    i = market_index(g, g->system.sector, g->docked);
    if (i < 0)
        return normal;
    stock = g->markets[i].stock[good];
    recovery = (int)((g->tick - g->markets[i].last_tick) / MARKET_INTERVAL);
    if (stock < normal)
        return stock + (recovery < normal - stock ? recovery : normal - stock);
    return stock - (recovery < stock - normal ? recovery : stock - normal);
}
int price(const Game *g, int good) {
    static const int base[GOODS] = {12, 25, 90, 170, 290, 390, 600, 60};
    uint32_t h;
    int normal, supply, scarcity, p;
    if (good < 0 || good >= GOODS || g->docked < 0 || g->docked >= g->nports)
        return 0;
    h = hash32(g->system.seed + (uint32_t)good * 7919U + (uint32_t)(g->docked + 1) * 41U);
    normal = normal_stock(g, good);
    supply = market_stock(g, good);
    scarcity = (normal - supply) * 50 / normal;
    if (scarcity < -25)
        scarcity = -25;
    p = base[good] * (70 + (int)(h % 61) +
                     (good % 4 == g->system.economy ? -25 : 15)) / 100;
    p = p * (100 + scarcity) / 100;
    return p > 0 ? p : 1;
}
int sell_price(const Game *g, int good) {
    return price(g, good) * 9 / 10;
}
int trade(Game *g, int good, int buy) {
    int p, i, j, stock;
    Market *m;
    if (g->docked < 0 || g->docked >= g->nports || good < 0 || good >= GOODS || g->dead)
        return 0;
    stock = market_stock(g, good);
    p = buy ? price(g, good) : sell_price(g, good);
    if (buy) {
        if (g->credits < p || stock < 1 || cargo_used(g) >= ship_capacity[g->ship])
            return 0;
    } else if (!g->cargo[good] || stock >= 10000 || g->credits > 100000000 - p)
        return 0;
    i = market_index(g, g->system.sector, g->docked);
    if (i < 0) {
        if (g->market_count == MAX_MARKETS)
            return 0;
        i = g->market_count++;
        g->markets[i].sector = g->system.sector;
        g->markets[i].port = g->docked;
        g->markets[i].last_tick = g->tick;
        for (j = 0; j < GOODS; j++)
            g->markets[i].stock[j] = normal_stock(g, j);
    }
    m = &g->markets[i];
    for (j = 0; j < GOODS; j++)
        m->stock[j] = market_stock(g, j);
    /* Keep fractional replenishment time when trades happen within the hour. */
    m->last_tick += ((g->tick - m->last_tick) / MARKET_INTERVAL) * MARKET_INTERVAL;
    g->credits += buy ? -p : p;
    g->cargo[good] += buy ? 1 : -1;
    m->stock[good] += buy ? -1 : 1;
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
