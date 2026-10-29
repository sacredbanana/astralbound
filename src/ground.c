/* SPDX-License-Identifier: Unlicense */
/* On foot. A commander landed on solid ground may leave the ship and walk.
   The walker lives in body-fixed Q8 coordinates, like a landed hull, so the
   planet's rotation carries both together. Ground sites are deterministic
   points of interest on a body-fixed lattice: the anchor for activities and
   future ground missions (survey, salvage, rescue) away from any port. */
#include "astralbound.h"
#include <stdio.h>
#include <stdlib.h>
const char *site_names[SITE_KINDS] = {"MINERAL OUTCROP", "WRECKAGE", "SURVEY BEACON"};
static void say(Game *g, const char *s) {
    snprintf(g->message, sizeof(g->message), "%s", s);
}
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static int64_t dot(Vec a, Vec b) {
    return (a.x * b.x + a.y * b.y + a.z * b.z) / 16384;
}
static Vec cross(Vec a, Vec b) {
    return vec((a.y * b.z - a.z * b.y) / 16384, (a.z * b.x - a.x * b.z) / 16384,
               (a.x * b.y - a.y * b.x) / 16384);
}
/* Q14 east (the direction of spin about +Z) and north at a Q14 up. */
static void tangent_frame(Vec up, Vec *east, Vec *north) {
    Vec e = vec(-up.y, up.x, 0);
    if (length(e) < 4096) e = vec(16384, 0, 0);
    e = unit(sub(e, scale(up, dot(e, up), 16384)), 16384);
    *east = e;
    *north = vec((up.y * e.z - up.z * e.y) / 16384, (up.z * e.x - up.x * e.z) / 16384,
                 (up.x * e.y - up.y * e.x) / 16384);
}
static Planet still_body(const Planet *p) {
    Planet still = *p;
    body_frame(&still);
    return still;
}
/* Body-fixed Q8 ground point under a body-fixed Q8 position. */
static Vec ground_point(const Planet *still, Vec fixed) {
    return radial(fixed, (int64_t)still->radius * FP + terrain_height_fine(still, fixed));
}
static int dry(const Planet *still, Vec fixed) {
    return terrain_material(still, scale(fixed, 1, FP)) != 0;
}
/* The landed hull's body-fixed Q8 rest point, on a pad or open ground. */
static Vec ship_anchor(const Game *g) {
    const Planet *p = &g->system.planets[g->body];
    if (g->docked >= 0)
        return body_rotate(p, sub(g->ports[g->docked].pos, scale(p->center, FP, 1)), 1);
    return g->ground_anchor;
}
const char *disembark_refusal(const Game *g) {
    int i;
    if (g->on_foot) return "ALREADY OUTSIDE";
    if (g->dead || g->hyperspace || g->docked == -1) return "LAND FIRST";
    if (g->docked >= 0 && !g->ports[g->docked].surface) return "STATION BERTH";
    if (g->system.planets[g->body].type == GAS_GIANT) return "NO SOLID SURFACE";
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && g->npc[i].hostile &&
            length(sub(g->npc[i].pos, g->pos)) < 5000LL * FP)
            return "HOSTILES NEARBY";
    return NULL;
}
/* Step down beside the hull on the first dry side, facing it. */
int disembark(Game *g) {
    const Planet *p;
    Planet still;
    Vec anchor, up, east, north, step;
    int i, reach;
    if (disembark_refusal(g)) return 0;
    p = &g->system.planets[g->body];
    still = still_body(p);
    anchor = ship_anchor(g);
    up = unit(anchor, 16384);
    tangent_frame(up, &east, &north);
    reach = ship_specs[g->ship].width / 2 + 12;
    for (i = 0; i < 5; i++) {
        Vec axis = i % 2 ? north : east;
        step = i == 4 ? vec(0, 0, 0) : scale(axis, (i < 2 ? reach : -reach) * (int64_t)FP, 16384);
        g->walker = ground_point(&still, add(anchor, step));
        if (dry(&still, g->walker)) break;
    }
    /* Face back toward the hull. */
    g->walk_yaw = i == 4 ? 0 : attitude_angle(-dot(step, east), -dot(step, north));
    g->walk_pitch = 0;
    g->walker_prev = g->walker;
    g->walk_yaw_prev = g->walk_yaw;
    g->walk_pitch_prev = 0;
    g->on_foot = 1;
    g->page = FLIGHT;
    g->warp = g->effective_warp = 1;
    g->throttle = 0;
    say(g, "ON FOOT - WASD WALK / ARROWS LOOK / E USE / K BOARD");
    return 1;
}
/* Along the ground: a tall hull rests high above its footprint. */
int64_t walker_ship_distance(const Game *g) {
    Vec d = sub(g->walker, ship_anchor(g)), up = unit(g->walker, 16384);
    return length(sub(d, scale(up, dot(d, up), 16384))) / FP;
}
int board_reach(const Game *g) {
    const ShipSpec *s = &ship_specs[g->ship];
    return (s->width > s->depth ? s->width : s->depth) / 2 + 25;
}
int embark(Game *g) {
    if (!g->on_foot || walker_ship_distance(g) > board_reach(g))
        return 0;
    g->on_foot = 0;
    say(g, g->docked >= 0 ? "ABOARD - L LAUNCH / S SERVICES" : "ABOARD - L LAUNCH / K DISEMBARK");
    return 1;
}
Vec walker_position(const Game *g) {
    const Planet *p = &g->system.planets[g->body];
    return add(scale(p->center, FP, 1), body_rotate(p, g->walker, 0));
}
/* Arrows (or a second stick) turn and look; W/S walk and A/D sidestep, or a
   stick's analogue deflection sets the pace. Water, cliffs and the suit's
   range from the ship stop the commander rather than ending the excursion.
   The camera's screen right is up x heading, the mirror of east from north,
   so turning or stepping right goes against the bearing. */
void ground_tick(Game *g, Controls c) {
    const Planet *p = &g->system.planets[g->body];
    Planet still;
    Vec up, east, north, heading, right, move, next;
    int64_t step, rise, pace;
    int ahead, aside;
    g->walker_prev = g->walker;
    g->walk_yaw_prev = g->walk_yaw;
    g->walk_pitch_prev = g->walk_pitch;
    if (g->page != FLIGHT)
        return;
    g->walk_yaw = (g->walk_yaw - c.axis_roll) & 1023;
    g->walk_pitch -= c.axis_pitch;
    if (g->walk_pitch > 200) g->walk_pitch = 200;
    if (g->walk_pitch < -200) g->walk_pitch = -200;
    ahead = c.thrust ? c.thrust * 256 : clamp(c.axis_walk, -256, 256);
    aside = c.strafe ? c.strafe * 256 : clamp(c.axis_strafe, -256, 256);
    if (!ahead && !aside)
        return;
    /* Backing up is half pace. */
    if (ahead < 0) ahead /= 2;
    still = still_body(p);
    up = unit(g->walker, 16384);
    tangent_frame(up, &east, &north);
    heading = add(scale(north, cosi(g->walk_yaw), 16384), scale(east, sini(g->walk_yaw), 16384));
    right = sub(scale(north, sini(g->walk_yaw), 16384), scale(east, cosi(g->walk_yaw), 16384));
    move = add(scale(heading, ahead, 256), scale(right, aside, 256));
    /* A diagonal is no faster than a straight walk. */
    pace = length(move);
    if (pace > 16384) {
        move = unit(move, 16384);
        pace = 16384;
    }
    step = (int64_t)WALK_SPEED * FP / HZ * pace / 16384;
    if (!step)
        return;
    next = ground_point(&still, add(g->walker, scale(move, (int64_t)WALK_SPEED * FP / HZ, 16384)));
    if (!dry(&still, next)) {
        say(g, "WATER AHEAD - TURN BACK");
        return;
    }
    rise = length(next) - length(g->walker);
    if (rise * 5 > step * 6) {
        say(g, "TOO STEEP TO CLIMB");
        return;
    }
    if (length(sub(next, ship_anchor(g))) > (int64_t)WALK_RANGE * FP &&
        length(sub(next, ship_anchor(g))) > length(sub(g->walker, ship_anchor(g)))) {
        say(g, "SUIT RANGE LIMIT - HEAD BACK TO THE SHIP");
        return;
    }
    g->walker = next;
}
static Vec walker_look(Vec up, int yaw, int pitch) {
    Vec east, north, look;
    tangent_frame(up, &east, &north);
    look = add(scale(north, cosi(yaw), 16384), scale(east, sini(yaw), 16384));
    return add(scale(look, cosi(pitch), 16384), scale(up, sini(pitch), 16384));
}
/* Eye above the feet, looking along the heading with the horizon level.
   Table angles step a third of a degree, enough to make the ground shudder
   at walking height, so axes (when wanted) gives the exact world Q14 right,
   up and forward camera rows. Between ticks the pose is blended draw_lag of
   the way back to the previous tick, so a 32 Hz walk moves evenly. */
void ground_view(const Game *g, Vec *eye, Vec axes[3], int *yaw, int *pitch, int *roll) {
    const Planet *p = &g->system.planets[g->body];
    int lag = g->paused || g->ui_quit ? 0 : clamp(g->draw_lag, 0, 256);
    Vec feet = add(g->walker, scale(sub(g->walker_prev, g->walker), lag, 256));
    Vec up = unit(feet, 16384), look, side, right, level;
    look = walker_look(up, g->walk_yaw, g->walk_pitch);
    if (lag)
        look = add(look, scale(sub(walker_look(up, g->walk_yaw_prev, g->walk_pitch_prev), look), lag, 256));
    look = unit(look, 16384);
    side = unit(cross(up, look), 16384);
    if (axes) {
        axes[0] = body_rotate(p, side, 0);
        axes[1] = body_rotate(p, cross(look, side), 0);
        axes[2] = body_rotate(p, look, 0);
    }
    look = body_rotate(p, look, 0);
    up = body_rotate(p, up, 0);
    *eye = add(add(scale(p->center, FP, 1), body_rotate(p, feet, 0)), scale(up, EYE_HEIGHT * FP, 16384));
    *yaw = g->yaw;
    *pitch = 0;
    aim_angles(look, yaw, pitch);
    right = vec(cosi(*yaw), 0, -sini(*yaw));
    level = vec(-(int64_t)sini(*pitch) * sini(*yaw) / 16384, cosi(*pitch),
                -(int64_t)sini(*pitch) * cosi(*yaw) / 16384);
    *roll = attitude_angle(dot(up, right), dot(up, level));
}
static int claimed(const Game *g, uint32_t id) {
    int i;
    for (i = 0; i < GROUND_CLAIMS; i++)
        if (g->site_claims[i] == id) return 1;
    return 0;
}
/* Unused sites in the 3x3 lattice cells around a Q8 system position. Each
   SITE_CELL metre cell of a cube face holds at most one site, so the same
   ground always shows the same sites, wherever the commander lands. The
   lattice is pure, so the last neighbourhood's terrain samples are kept. */
static struct {
    uint32_t seed;
    int body, face, cu, cv, count, valid;
    GroundSite sites[GROUND_SITES];
} memo;
int ground_sites(const Game *g, Vec around, GroundSite *out, int max) {
    int body = nearest_body(g, around), n = 0, axis = 0, face, u, v, cu, cv, dx, dy, i;
    const Planet *p = &g->system.planets[body];
    Planet still;
    Vec q;
    int64_t dominant;
    if (p->type == GAS_GIANT || !max)
        return 0;
    q = body_rotate(p, scale(sub(around, scale(p->center, FP, 1)), 1, FP), 1);
    dominant = llabs(q.x);
    if (llabs(q.y) > dominant) { axis = 1; dominant = llabs(q.y); }
    if (llabs(q.z) > dominant) { axis = 2; dominant = llabs(q.z); }
    if (!dominant) return 0;
    face = axis * 2 + ((axis == 0 ? q.x : axis == 1 ? q.y : q.z) < 0);
    q = scale(q, p->radius, dominant);
    u = (int)(axis == 0 ? q.y : q.x);
    v = (int)(axis == 2 ? q.y : q.z);
    cu = u >= 0 ? u / SITE_CELL : (u - SITE_CELL + 1) / SITE_CELL;
    cv = v >= 0 ? v / SITE_CELL : (v - SITE_CELL + 1) / SITE_CELL;
    if (memo.valid && memo.seed == p->seed && memo.body == body && memo.face == face &&
        memo.cu == cu && memo.cv == cv) {
        for (i = 0; i < memo.count && n < max; i++)
            if (!claimed(g, memo.sites[i].id)) out[n++] = memo.sites[i];
        return n;
    }
    still = still_body(p);
    memo.valid = 0;
    memo.count = 0;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            uint32_t h = hash32(p->seed ^ hash32((uint32_t)(cu + dx) * 73856093U ^
                                                 (uint32_t)(cv + dy) * 19349663U ^
                                                 (uint32_t)face * 83492791U));
            int su, sv, sign = face & 1 ? -p->radius : p->radius, near_port = 0;
            Vec d;
            GroundSite s;
            if (h % 100 >= 45)
                continue;
            su = (cu + dx) * SITE_CELL + (int)((h >> 8) % SITE_CELL);
            sv = (cv + dy) * SITE_CELL + (int)((h >> 18) % SITE_CELL);
            d = axis == 0 ? vec(sign, su, sv) : axis == 1 ? vec(su, sign, sv) : vec(su, sv, sign);
            d = scale(radial(d, p->radius), FP, 1);
            if (!dry(&still, d))
                continue;
            /* Ports keep their aprons clear. */
            for (i = 0; i < g->nports && !near_port; i++)
                near_port = g->ports[i].surface && g->ports[i].body == body &&
                    length(sub(body_rotate(p, sub(g->ports[i].pos, scale(p->center, FP, 1)), 1), d)) <
                        3000LL * FP;
            if (near_port)
                continue;
            s.anchor = ground_point(&still, d);
            s.id = hash32(h ^ (uint32_t)body * 2654435761U) | 1U;
            s.body = body;
            s.kind = (h >> 28) < 8 ? SITE_OUTCROP : (h >> 28) < 13 ? SITE_WRECKAGE : SITE_BEACON;
            memo.sites[memo.count++] = s;
        }
    memo.seed = p->seed; memo.body = body; memo.face = face;
    memo.cu = cu; memo.cv = cv; memo.valid = 1;
    for (i = 0; i < memo.count && n < max; i++)
        if (!claimed(g, memo.sites[i].id)) out[n++] = memo.sites[i];
    return n;
}
Vec site_position(const Game *g, const GroundSite *s) {
    const Planet *p = &g->system.planets[s->body];
    return add(scale(p->center, FP, 1), body_rotate(p, s->anchor, 0));
}
/* Nearest unused site to the walker (or, aboard, to the ship). */
int nearest_site(const Game *g, GroundSite *site, int64_t *metres) {
    GroundSite sites[GROUND_SITES];
    Vec from = g->on_foot ? walker_position(g) : g->pos;
    int i, n = ground_sites(g, from, sites, GROUND_SITES), best = -1;
    int64_t best_d = INT64_MAX;
    for (i = 0; i < n; i++) {
        int64_t d = length(sub(site_position(g, &sites[i]), from)) / FP;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0)
        return 0;
    *site = sites[best];
    *metres = best_d;
    return 1;
}
/* Ground activities. Each kind resolves here; a site is used once. */
int use_site(Game *g) {
    GroundSite s;
    int64_t metres;
    int good = -1, reward = 0;
    if (!g->on_foot || !nearest_site(g, &s, &metres) || metres > SITE_REACH) {
        say(g, "NOTHING IN REACH - WALK TO A MARKER");
        return 0;
    }
    switch (s.kind) {
    case SITE_OUTCROP: good = 6; break;                 /* CRYSTALS */
    case SITE_WRECKAGE: good = s.id & 2 ? 2 : 5; break; /* ALLOYS / MACHINERY */
    default: reward = 150 + (int)(s.id % 100); break;
    }
    if (good >= 0 && cargo_used(g) >= ship_capacity[g->ship]) {
        say(g, "HOLD FULL - LEFT IN PLACE");
        return 0;
    }
    if (good >= 0) {
        g->cargo[good]++;
        snprintf(g->message, sizeof(g->message), "%s - 1T %s TO THE HOLD",
                 s.kind == SITE_OUTCROP ? "SAMPLE CUT" : "SALVAGED", goods_names[good]);
    } else {
        g->credits += reward;
        snprintf(g->message, sizeof(g->message), "BEACON SURVEY SOLD - %d CR", reward);
    }
    g->site_claims[g->site_claim_next] = s.id;
    g->site_claim_next = (g->site_claim_next + 1) % GROUND_CLAIMS;
    return 1;
}
/* E (or a tap of fire): use a site in reach, else board a ship in reach. */
void ground_interact(Game *g) {
    GroundSite s;
    int64_t metres;
    if (g->on_foot && !(nearest_site(g, &s, &metres) && metres <= SITE_REACH) && embark(g))
        return;
    use_site(g);
}
