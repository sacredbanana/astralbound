/* SPDX-License-Identifier: Unlicense
   Fixed-tick, bounded destruction effects shared by native and host renderers.
   Death advances these alone; trading, combat and the world clock stop. */
#include "astralbound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

Vec destruction_position(const Game *g, Vec pos, int body) {
    if (body < 0) return pos;
    return add(scale(g->system.planets[body].center, FP, 1),
               body_rotate(&g->system.planets[body], pos, 0));
}
void destruction_reset(Game *g) {
    memset(g->blasts, 0, sizeof(g->blasts));
    memset(g->smoke, 0, sizeof(g->smoke));
    memset(g->wrecks, 0, sizeof(g->wrecks));
    g->death_started = g->smoke_next = 0;
    g->blast_serial = g->destruction_clock = 0;
}
static void smoke(Game *g, Vec pos, Vec vel, int body, int size) {
    Smoke *s = &g->smoke[g->smoke_next++ % MAX_SMOKE];
    s->active = 1; s->age = 0; s->body = body; s->size = size;
    s->seed = hash32(g->destruction_clock + (unsigned)g->smoke_next * 7919U);
    s->pos = pos;
    s->vel = add(body < 0 ? vel : scale(vel, 1, 12), scale(body < 0 ? vec(0, 16384, 0) : unit(pos, 16384),
                                        12 * FP, 16384));
    if (g->smoke_next >= MAX_SMOKE * 2) g->smoke_next -= MAX_SMOKE;
}
static void blast(Game *g, Vec pos, Vec vel, int body, int size) {
    Blast *b = NULL;
    int i;
    for (i = 0; i < MAX_BLASTS; i++) {
        if (!g->blasts[i].active) { b = &g->blasts[i]; break; }
        if (!b || g->blasts[i].age > b->age) b = &g->blasts[i];
    }
    b->active = 1; b->age = 0; b->body = body; b->size = size;
    b->pos = pos; b->vel = body < 0 ? vel : vec(0, 0, 0);
    b->seed = hash32(g->seed ^ ++g->blast_serial * 2654435761U);
    for (i = 0; i < 8; i++) {
        Vec d = forward((int)(hash32(b->seed + (unsigned)i) & 1023), i * 53 - 180);
        smoke(g, add(pos, scale(d, size * FP / 3, 16384)), b->vel, body, size / 2);
    }
}
static void impact(Game *g, Wreck *w, int player) {
    if (w->body >= 0) {
        const Planet *p = &g->system.planets[w->body];
        Vec world = body_rotate(p, scale(w->pos, 1, FP), 0);
        int height = terrain_height(p, world);
        w->pos = radial(w->pos, ((int64_t)p->radius + height + 10) * FP);
        w->vel = vec(0, 0, 0);
    }
    blast(g, w->pos, w->vel, w->body,
          70 + ship_specs[w->ship].width * 2);
    w->state = player ? 2 : 0; w->age = 0;
    if (player) snprintf(g->message, sizeof(g->message), "SHIP LOST - R TO RESTART");
}
static void start(Game *g, Wreck *w, Vec pos, Vec vel, int ship,
                  int yaw, int pitch, int roll, int player, int impact_body) {
    int body = nearest_body(g, pos);
    const Planet *p = &g->system.planets[body];
    Vec local = sub(pos, scale(p->center, FP, 1));
    int64_t altitude = length(local) / FP - p->radius;
    int ceiling = p->atmosphere > TERRAIN_DETAIL_ALTITUDE ? p->atmosphere : TERRAIN_DETAIL_ALTITUDE;
    memset(w, 0, sizeof(*w));
    w->state = 1; w->body = -1; w->ship = ship;
    w->yaw = yaw; w->pitch = pitch; w->roll = roll;
    w->pos = pos; w->vel = vel;
    if (p->type != GAS_GIANT && altitude < ceiling + 2000) {
        w->body = body;
        w->pos = body_rotate(p, local, 1);
        /* Convert from the current translated frame into rotating ground air. */
        vel = add(vel, sub(body_velocity(&g->system, g->frame_body, g->tick),
                           body_velocity(&g->system, body, g->tick)));
        w->vel = body_rotate(p, sub(vel, surface_velocity(p, scale(local, 1, FP))), 1);
        /* Keep System Drive momentum from skipping the entire crash shot. */
        if (length(w->vel) > ATMOSPHERE_SPEED * FP) w->vel = unit(w->vel, ATMOSPHERE_SPEED * FP);
        smoke(g, w->pos, w->vel, body, 22);
    }
    if (impact_body >= 0) {
        p = &g->system.planets[impact_body];
        w->body = impact_body;
        w->pos = body_rotate(p, sub(pos, scale(p->center, FP, 1)), 1);
        impact(g, w, player);
    } else if (w->body < 0) impact(g, w, player);
    else if (altitude_at(g, pos) <= 12) impact(g, w, player);
}
void destruction_npc(Game *g, int index) {
    Npc *n = &g->npc[index];
    int yaw = 0, pitch = 0;
    aim_angles(n->vel, &yaw, &pitch);
    start(g, &g->wrecks[index + 1], n->pos, n->vel, g->npc_ship[index],
          yaw, pitch, 0, 0, -1);
}
void destruction_player(Game *g, int impact_body) {
    if (g->death_started) return;
    g->death_started = 1; g->dead = 1; g->hull = 0;
    g->death_yaw = g->yaw;
    g->page = FLIGHT; g->camera_mode = 2;
    g->autopilot = g->drive_manual = g->drive_active = g->entry_brake = g->landing = g->on_foot = 0;
    g->hyperspace = g->laser = g->throttle = g->ui_fire = g->ui_down = 0;
    g->ui_capture = -1; g->warp = g->effective_warp = 1;
    start(g, &g->wrecks[0], g->pos, g->vel, g->ship,
          g->yaw, g->pitch, g->roll, 1, impact_body);
    snprintf(g->message, sizeof(g->message), g->wrecks[0].state == 1 ?
             "SHIP DISABLED - TERRAIN IMPACT IMMINENT" : "SHIP LOST - R TO RESTART");
}
void destruction_rebase(Game *g, Vec delta, Vec velocity) {
    int i;
    for (i = 0; i < MAX_BLASTS; i++) if (g->blasts[i].active && g->blasts[i].body < 0) {
        g->blasts[i].pos = add(g->blasts[i].pos, delta);
        g->blasts[i].vel = add(g->blasts[i].vel, velocity);
    }
    for (i = 0; i < MAX_SMOKE; i++) if (g->smoke[i].active && g->smoke[i].body < 0) {
        g->smoke[i].pos = add(g->smoke[i].pos, delta);
        g->smoke[i].vel = add(g->smoke[i].vel, velocity);
    }
}
void destruction_tick(Game *g) {
    int i;
    g->destruction_clock++;
    for (i = 0; i < MAX_SMOKE; i++) {
        Smoke *s = &g->smoke[i];
        if (!s->active) continue;
        if (++s->age >= SMOKE_TICKS) { s->active = 0; continue; }
        s->pos = add(s->pos, scale(s->vel, 1, HZ));
    }
    for (i = 0; i < MAX_BLASTS; i++) {
        Blast *b = &g->blasts[i];
        if (!b->active) continue;
        if (++b->age >= BLAST_TICKS) { b->active = 0; continue; }
        b->pos = add(b->pos, scale(b->vel, 1, HZ));
        if (b->age < 3 * HZ && b->age % 8 == 0)
            smoke(g, b->pos, b->vel, b->body, b->size / 2);
    }
    for (i = 0; i <= MAX_NPC; i++) {
        Wreck *w = &g->wrecks[i];
        Vec old, next, world_old, world_next;
        const Planet *p;
        if (!w->state) continue;
        w->age++;
        if (w->state == 2) {
            if (w->body < 0) w->pos = add(w->pos, scale(w->vel, 1, HZ));
            continue;
        }
        p = &g->system.planets[w->body];
        old = w->pos;
        /* No engines or lift survive hull failure. Gravity and light drag give
           a visible ballistic fall even for an initially hovering ship. */
        w->vel = sub(w->vel, scale(unit(old, (p->gravity > 0 ? p->gravity : 2) * FP), 1, HZ));
        w->vel = sub(w->vel, scale(w->vel, 1, HZ * 12));
        next = add(old, scale(w->vel, 1, HZ));
        world_old = destruction_position(g, old, w->body);
        world_next = destruction_position(g, next, w->body);
        if (terrain_impact(g, world_old, world_next, w->body)) {
            /* Bisect the swept segment to seat the burst on the first terrain
               crossing, including high-speed hillside strikes. */
            Vec a = old, b = next;
            int j;
            for (j = 0; j < 12; j++) {
                Vec mid = add(a, scale(sub(b, a), 1, 2));
                if (terrain_impact(g, destruction_position(g, a, w->body),
                                      destruction_position(g, mid, w->body), w->body)) b = mid;
                else a = mid;
            }
            w->pos = b; impact(g, w, i == 0);
        } else {
            w->pos = next;
            w->yaw = (w->yaw + 1) & 1023;
            w->roll = (w->roll + 7) & 1023;
            w->pitch = (int)(sini(w->age * 2) * 110 / 16384) - 40;
            if (w->age % 3 == 0) {
                Vec tail = body_rotate(p, forward(w->yaw, w->pitch), 1);
                tail = sub(w->pos, scale(tail, ship_specs[w->ship].depth * FP / 2, 16384));
                smoke(g, tail, w->vel, w->body, 12);
            }
        }
    }
    if (g->death_started) {
        Wreck *w = &g->wrecks[0];
        g->pos = destruction_position(g, w->pos, w->body);
        g->yaw = w->yaw; g->pitch = w->pitch; g->roll = w->roll;
    }
}
