/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int same(Sector a, Sector b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static int clamp(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static void say(Game *g, const char *s) {
    snprintf(g->message, sizeof(g->message), "%s", s);
}
int port_count(const Game *g) {
    return g->system.count * PORTS_PER_PLANET;
}
void setup_system(Game *g, Sector sector) {
    int i, b;
    generate_system(g->seed, sector, &g->system);
    memset(g->ports, 0, sizeof(g->ports));
    for (b = 0; b < g->system.count; b++) {
        const Planet *p = &g->system.planets[b];
        int r = p->radius;
        Vec center = scale(p->center, FP, 1);
        Port *ports = &g->ports[b * PORTS_PER_PLANET];
        ports[0].pos = add(center, vec(0, (int64_t)(r + 12) * FP, 0));
        ports[1].pos = add(center, vec(0, (int64_t)(r + 16000) * FP, 0));
        ports[2].pos = add(center, scale(forward(120, 180), (int64_t)(r + 12) * FP, 16384));
        ports[3].pos = add(center, vec((int64_t)(r + 20000) * FP, 0, 0));
        for (i = 0; i < PORTS_PER_PLANET; i++) {
            ports[i].surface = i % 2 == 0;
            ports[i].body = b;
            snprintf(ports[i].name, 24, "%.12s %c %s", g->system.name, 'B' + b,
                     i == 0   ? "HAVEN"
                     : i == 1 ? "RELAY"
                     : i == 2 ? "REACH"
                              : "ANCHOR");
        }
    }
    for (i = 0; i < GOODS; i++)
        g->stock[i] = 10 + hash32(g->system.seed + (unsigned)i) % 80;
    memset(g->npc, 0, sizeof(g->npc));
    for (i = 0; i < MAX_MISSIONS; i++)
        if (g->contracts[i].state == ACTIVE)
            g->contracts[i].spawned = 0;
    g->selected = sector;
    g->body = 0;
}
void game_init(Game *g, uint32_t seed) {
    int i;
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    setup_system(g, (Sector){0, 0, 0});
    g->pos = g->ports[0].pos;
    g->docked = 0;
    g->gear = 1;
    g->assist = 1;
    g->hull = g->shield = g->fuel = 100;
    g->credits = 1500;
    g->warp = g->effective_warp = 1;
    g->target = 1;
    g->page = FLIGHT;
    for (i = 0; i < MAX_MISSIONS; i++)
        g->contracts[i].state = COMPLETE;
    refresh_board(g);
    say(g, "SPACE / FLIGHT PROTOTYPE - H FOR HELP");
}
int safe_warp(const Game *g) {
    int i, w = g->warp;
    int64_t alt = altitude_at(g, g->pos), speed = length(g->vel) / FP;
    if (g->docked != -1 || g->dead || g->laser)
        return 1;
    if (!g->autopilot)
        return 1; /* Compression is a privilege of monitored guidance. */
    if (alt < 3000 || alt < speed * speed / 80 + 2000)
        w = 1;
    else if (alt < 12000 && w > 4)
        w = 4;
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && length(sub(g->npc[i].pos, g->pos)) < 20000 * FP)
            return 1;
    if (length(sub(g->ports[g->target].pos, g->pos)) < 6000 * FP)
        w = 1;
    return w;
}
static Vec autopilot_target(const Game *g) {
    Vec dest = g->ports[g->target].pos;
    int b;
    /* Check every planet along the route, including the departure body.
       A tangent waypoint outside its terrain shell prevents straight-line dives. */
    for (b = 0; b < g->system.count; b++) {
        const Planet *p = &g->system.planets[b];
        Vec center = scale(p->center, FP, 1), local = sub(g->pos, center),
            dlocal = sub(dest, center);
        int64_t r = (int64_t)p->radius * FP, alt = length(local) - r;
        Vec up = unit(local, 16384), du = unit(dlocal, 16384);
        int64_t cosine = (up.x * du.x + up.y * du.y + up.z * du.z) / 16384;
        if (cosine < 16300 &&
            swept_sphere(scale(g->pos, 1, FP), scale(dest, 1, FP), p->center, p->radius + 2500)) {
            Vec tangent = sub(du, scale(up, cosine, 16384));
            int64_t safe = r + 10000 * FP;
            if (length(tangent) < 32)
                tangent = vec(up.y, -up.x, 0);
            if (length(tangent) < 32)
                tangent = vec(16384, 0, 0);
            if (alt < 7000 * FP)
                return add(center, scale(up, safe, 16384));
            /* Keep the waypoint moving along the great circle. */
            return add(center, scale(unit(add(up, unit(tangent, 1500)), 16384), safe, 16384));
        }
    }
    return dest;
}
static void missions(Game *g) {
    int i, j;
    for (i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &g->contracts[i];
        if (m->state != ACTIVE)
            continue;
        if (g->tick > m->deadline) {
            m->state = FAILED;
            for (j = 0; j < MAX_NPC; j++)
                if (g->npc[j].mission == i)
                    g->npc[j].mission = -1;
            say(g, "CONTRACT EXPIRED");
            continue;
        }
        if (!same(m->destination, g->system.sector))
            continue;
        if (m->type == COURIER || m->type == FREIGHT) {
            if (g->docked != m->port)
                continue;
        } else if (m->type == SURVEY) {
            int64_t alt = altitude_at(g, g->pos);
            if (g->docked != -1 || alt < 500 || alt > 6000 || length(g->vel) > 200 * FP)
                continue;
            if (++m->progress < 20 * HZ)
                continue;
        } else {
            int64_t alt = altitude_at(g, g->pos);
            if (!m->spawned && g->docked == -1 &&
                ((m->type == AIR_PATROL && alt > 600 && alt < 6000) ||
                 (m->type != AIR_PATROL && alt > 13000))) {
                for (j = 0; j < MAX_NPC; j++)
                    if (!g->npc[j].active) {
                        Npc *n = &g->npc[j];
                        n->pos = add(g->pos, scale(forward(g->yaw, g->pitch), 2000 * FP, 16384));
                        n->vel = g->vel;
                        n->active = n->hostile = 1;
                        n->hull = 60;
                        n->mission = i;
                        n->cooldown = HZ * 3;
                        m->spawned = 1;
                        break;
                    }
            }
            if (m->progress < 1)
                continue;
        }
        m->state = COMPLETE;
        g->credits += m->reward;
        say(g, "CONTRACT COMPLETE - PAYMENT RECEIVED");
    }
}
void game_tick(Game *g, Controls c) {
    Vec acceleration = vec(0, 0, 0), gravity, old;
    int i;
    int64_t r, dist, alt, radial;
    Vec local, center;
    const Planet *planet;
    int thrust = 60 * FP;
    if (g->paused || g->dead)
        return;
    g->tick++;
    if (g->laser)
        g->laser--;
    if (g->heat > 0 && g->tick % 8 == 0)
        g->heat--;
    if (g->shield < 100 && g->tick % HZ == 0)
        g->shield++;
    if (g->docked != -1) {
        missions(g);
        return;
    }
    g->body = nearest_body(g, g->pos);
    planet = &g->system.planets[g->body];
    center = scale(planet->center, FP, 1);
    local = sub(g->pos, center);
    r = (int64_t)planet->radius * FP;
    dist = length(local);
    alt = altitude_at(g, g->pos);
    gravity = unit(local, -planet->gravity * FP);
    if (dist > r)
        gravity = scale(scale(gravity, r, dist), r, dist);
    if (g->autopilot) {
        Vec target = autopilot_target(g);
        if (g->tick % 4 == 0) {
            int yaw = g->yaw, pitch = g->pitch, turn;
            aim_angles(sub(target, g->pos), &yaw, &pitch);
            turn = ((yaw - g->yaw + 1536) & 1023) - 512;
            g->yaw = (g->yaw + clamp(turn, -12, 12)) & 1023;
            g->pitch = clamp(g->pitch + clamp(pitch - g->pitch, -12, 12), -250, 250);
        }
        acceleration = guidance(g->pos, g->vel, target, thrust,
                                (alt < planet->atmosphere + 8000 ? 600 : 4000) * FP);
        acceleration = sub(acceleration, gravity);
        if (length(acceleration) > thrust)
            acceleration = unit(acceleration, thrust);
        if (length(sub(g->ports[g->target].pos, g->pos)) < 500 * FP)
            g->gear = 1;
    } else {
        g->yaw = (g->yaw + c.yaw * 3) & 1023;
        g->pitch = clamp(g->pitch + c.pitch * 3, -250, 250);
        if (c.thrust)
            g->throttle = clamp(g->throttle + c.thrust * 3, 0, 4000);
        if (g->assist) {
            Vec desired = scale(forward(g->yaw, g->pitch), g->throttle * FP, 16384);
            Vec error = sub(desired, g->vel);
            acceleration = length(error) > thrust ? unit(error, thrust) : error;
            acceleration = sub(acceleration, gravity);
            if (length(acceleration) > thrust)
                acceleration = unit(acceleration, thrust);
        } else if (c.thrust)
            acceleration = scale(forward(g->yaw, g->pitch), c.thrust * thrust, 16384);
    }
    if (g->fuel <= 0)
        acceleration = vec(0, 0, 0);
    if (length(acceleration) > FP && g->tick % (HZ * 60) == 0)
        g->fuel--;
    old = g->pos;
    g->vel = add(g->vel, scale(add(acceleration, gravity), 1, HZ));
    g->pos = add(g->pos, scale(g->vel, 1, HZ));
    /* Atmospheric drag and heating depend on speed, never on render rate. */
    if (alt < planet->atmosphere && alt > 0) {
        int64_t speed = length(g->vel);
        if (speed > 1200 * FP && g->tick % HZ == 0) {
            g->hull -= 2;
            g->heat = clamp(g->heat + 5, 0, 100);
        }
        g->vel = scale(g->vel, 32760, 32768);
    }
    for (i = 0; i < port_count(g); i++)
        if (length(sub(g->pos, g->ports[i].pos)) < 35 * FP && length(g->vel) < 8 * FP && g->gear) {
            g->pos = g->ports[i].pos;
            g->vel = vec(0, 0, 0);
            g->docked = i;
            g->autopilot = 0;
            g->warp = 1;
            g->throttle = 0;
            refresh_board(g);
            say(g, "DOCKED - B CONTRACTS / M MARKET / U SHIPYARD");
            break;
        }
    if (g->docked == -1)
        for (i = 0; i < g->system.count; i++)
            if (terrain_impact(g, old, g->pos, i)) {
                const Planet *p = &g->system.planets[i];
                Vec lc = sub(g->pos, scale(p->center, FP, 1));
                if (g->gear && length(g->vel) < 8 * FP) {
                    int height = terrain_height(p, scale(lc, 1, FP));
                    g->pos = add(scale(p->center, FP, 1), unit(lc, (p->radius + height + 12) * FP));
                    g->vel = vec(0, 0, 0);
                    g->docked = -2;
                    g->autopilot = 0;
                    g->body = i;
                    say(g, "LANDED ON OPEN GROUND - L TO LAUNCH");
                } else {
                    g->hull = 0;
                    say(g, "SURFACE IMPACT - R TO RESTART");
                }
                break;
            }
    if (c.fire && g->heat < 90 && g->tick % 6 == 0) {
        Vec f = forward(g->yaw, g->pitch);
        g->laser = 3;
        g->heat += 4;
        g->shots++;
        for (i = 0; i < MAX_NPC; i++) {
            Npc *n = &g->npc[i];
            Vec rel = sub(n->pos, g->pos);
            int64_t l = length(rel), along = (rel.x * f.x + rel.y * f.y + rel.z * f.z) / 16384;
            if (!n->active || l > 5000 * FP || along <= 0)
                continue;
            if (length(sub(rel, scale(f, along, 16384))) < 70 * FP) {
                n->hull -= 20 + g->upgrade * 10;
                if (n->hull <= 0) {
                    n->active = 0;
                    g->kills++;
                    if (n->mission >= 0)
                        g->contracts[n->mission].progress++;
                    g->credits += 100;
                    say(g, "HOSTILE DISABLED - BOUNTY 100 CR");
                }
                break;
            }
        }
    }
    for (i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        Vec rel, rv, desired, avoid, nold;
        int64_t l;
        if (!n->active)
            continue;
        rel = sub(g->pos, n->pos);
        rv = sub(g->vel, n->vel);
        l = length(rel);
        desired = sub(g->pos, unit(rel, 1200 * FP));
        acceleration = guidance(n->pos, sub(n->vel, g->vel), desired, 50 * FP, 500 * FP);
        avoid = avoidance(rel, rv, 50 * FP, 300 * FP);
        if (length(avoid))
            acceleration = avoid; /* Ground avoidance always overrides the attack run. */
        radial = altitude_at(g, n->pos) * FP;
        if (radial < 500 * FP)
            acceleration =
                unit(sub(n->pos, scale(g->system.planets[nearest_body(g, n->pos)].center, FP, 1)),
                     60 * FP);
        nold = n->pos;
        n->vel = add(n->vel, scale(acceleration, 1, HZ));
        n->pos = add(n->pos, scale(n->vel, 1, HZ));
        if (swept_sphere(scale(sub(nold, old), 1, FP), scale(sub(n->pos, g->pos), 1, FP),
                         vec(0, 0, 0), 30)) {
            g->hull -= 20;
            n->active = 0;
            say(g, "COLLISION DAMAGE");
        }
        if (n->cooldown)
            n->cooldown--;
        if (l < 2500 * FP && l > 400 * FP && !n->cooldown) {
            n->cooldown = HZ * 3;
            if (g->shield > 0)
                g->shield = clamp(g->shield - 8, 0, 100);
            else
                g->hull -= 8;
        }
    }
    missions(g);
    if (g->hull <= 0) {
        g->hull = 0;
        g->dead = 1;
        g->autopilot = 0;
        g->warp = 1;
        say(g, "SHIP LOST - R TO RESTART");
    }
}
void game_frame(Game *g, Controls c, int ticks) {
    int i, j;
    for (i = 0; i < ticks; i++) {
        int w = safe_warp(g);
        g->effective_warp = w;
        for (j = 0; j < w; j++) {
            if (j >= safe_warp(g))
                break;
            game_tick(g, c);
        }
    }
}
int jump(Game *g, Sector d) {
    Vec delta = vec((int64_t)d.x - g->system.sector.x, (int64_t)d.y - g->system.sector.y,
                    (int64_t)d.z - g->system.sector.z);
    int i, cost;
    int64_t distance = length(scale(delta, 100, 1));
    if (g->dead || g->docked != -1 || distance == 0 || distance > 600 || d.x < -1000000 ||
        d.x > 1000000 || d.y < -1000000 || d.y > 1000000 || d.z < -1000000 || d.z > 1000000 ||
        altitude_at(g, g->pos) < 12000)
        return 0;
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && length(sub(g->npc[i].pos, g->pos)) < 20000 * FP)
            return 0;
    cost = (int)(distance + 49) / 50;
    if (g->fuel < cost)
        return 0;
    g->fuel -= cost;
    setup_system(g, d);
    g->pos = add(g->ports[1].pos, vec(0, 4000 * FP, -3000 * FP));
    g->vel = vec(0, 0, 0);
    g->autopilot = 0;
    g->throttle = 0;
    g->target = 1;
    g->warp = 1;
    g->tick += HZ * 300;
    g->page = FLIGHT;
    refresh_board(g);
    say(g, "JUMP COMPLETE - T SELECTS PORT / P AUTOPILOT");
    return 1;
}
void game_action(Game *g, int k) {
    if (k == 'r' && g->dead) {
        game_init(g, g->seed);
        return;
    }
    if (g->dead)
        return;
    switch (k) {
    case 'h':
        g->page = g->page == HELP ? FLIGHT : HELP;
        break;
    case 'v':
        if (g->page == FLIGHT)
            g->camera_mode = (g->camera_mode + 1) % 3;
        g->page = FLIGHT;
        break;
    case 'y':
        g->page = SYSTEM_MAP;
        break;
    case 'g':
        g->page = MAP;
        break;
    case 'b':
        g->page = BOARD;
        g->selection = 0;
        break;
    case 'm':
        g->page = MARKET;
        g->selection = 0;
        break;
    case 'u':
        g->page = SHIPYARD;
        break;
    case 'j':
        g->page = LOG;
        break;
    case 'c':
        g->selected = g->system.sector;
        break;
    case 't':
        g->target = (g->target + 1) % port_count(g);
        break;
    case 'p':
        if (g->docked == -1) {
            g->autopilot = !g->autopilot;
            g->warp = 1;
        }
        break;
    case 'e':
        g->assist = !g->assist;
        g->autopilot = 0;
        break;
    case 'k':
        if (g->docked == -1)
            g->gear = !g->gear;
        break;
    case ' ':
        g->paused = !g->paused;
        break;
    case ']':
        g->warp = g->warp >= 256 ? 256 : g->warp * 4;
        break;
    case '[':
        g->warp = g->warp <= 1 ? 1 : g->warp / 4;
        break;
    case 'l':
        if (g->docked != -1) {
            Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
            g->pos = add(g->pos, scale(up, 80 * FP, 16384));
            g->vel = scale(up, 20 * FP, 16384);
            g->docked = -1;
            g->page = FLIGHT;
            g->throttle = 0;
            say(g, "LAUNCHED - P AUTOPILOT / H HELP");
        }
        break;
    case 'x':
        say(g, jump(g, g->selected) ? "JUMP COMPLETE"
                                    : "JUMP DENIED: ALTITUDE / RANGE / FUEL / TRAFFIC");
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
        g->selection = k - '1';
        break;
    case '\r':
        if (g->page == BOARD)
            say(g, accept_mission(g, g->selection) ? "CONTRACT ACCEPTED - J FOR DETAILS"
                                                   : "CANNOT ACCEPT CONTRACT");
        break;
    case '+':
        if (g->page == MARKET)
            say(g, trade(g, g->selection, 1) ? "PURCHASED 1 TONNE" : "PURCHASE REFUSED");
        break;
    case '-':
        if (g->page == MARKET)
            say(g, trade(g, g->selection, 0) ? "SOLD 1 TONNE" : "NO CARGO TO SELL");
        break;
    case 'f':
        if (g->docked >= 0 && g->credits >= 100) {
            g->credits -= 100;
            g->fuel = g->hull = g->shield = 100;
            say(g, "REFUELLED AND REPAIRED - 100 CR");
        }
        break;
    case 'o':
        if (g->docked >= 0 && g->credits >= 2000 && g->upgrade < 3) {
            g->credits -= 2000;
            g->upgrade++;
            say(g, "LASER UPGRADED");
        }
        break;
    case 'n':
        if (g->docked >= 0 && g->ship < 2 && g->credits >= 8000 * (g->ship + 1)) {
            g->credits -= 8000 * (g->ship + 1);
            g->ship++;
            say(g, "NEW SHIP PURCHASED");
        }
        break;
    case 1001:
        if (g->page == SYSTEM_MAP)
            g->target = (g->target + port_count(g) - 1) % port_count(g);
        if (g->page == MAP && g->selected.z < 1000000)
            g->selected.z++;
        break;
    case 1002:
        if (g->page == SYSTEM_MAP)
            g->target = (g->target + 1) % port_count(g);
        if (g->page == MAP && g->selected.z > -1000000)
            g->selected.z--;
        break;
    case 1003:
        if (g->page == MAP && g->selected.x > -1000000)
            g->selected.x--;
        break;
    case 1004:
        if (g->page == MAP && g->selected.x < 1000000)
            g->selected.x++;
        break;
    default:
        break;
    }
}
