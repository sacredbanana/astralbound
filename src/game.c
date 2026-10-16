/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "galaxy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int clamp(int x, int lo, int hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
static void say(Game *g, const char *s) {
    snprintf(g->message, sizeof(g->message), "%s", s);
}
int port_count(const Game *g) {
    return g->nports;
}
void setup_system(Game *g, Sector sector) {
    int i, b;
    g->drive_manual = g->drive_active = 0;
    generate_system(g->seed, sector, &g->system);
    memset(g->ports, 0, sizeof(g->ports));
    g->nports = 0;
    g->clearance = -1;
    g->dock_stage = 0;
    g->target_body = -1;
    g->combat_target = -1;
    g->frame_body = 0;
    for (b = 0; b < g->system.count; b++) {
        const Planet *p = &g->system.planets[b];
        if (!p->settled)
            continue;
        for (i = 0; i < PORTS_PER_PLANET; i++) {
            Port *port;
            if (p->type == GAS_GIANT && i % 2 == 0)
                continue;
            port = &g->ports[g->nports++];
            port->surface = i % 2 == 0;
            port->body = b;
            snprintf(port->name, sizeof(port->name), "%.12s %c %s", g->system.name, 'B' + b,
                     i == 0   ? "HAVEN"
                     : i == 1 ? "RELAY"
                     : i == 2 ? "REACH"
                              : "ANCHOR");
        }
    }
    update_world(g);
    memset(g->npc, 0, sizeof(g->npc));
    for (i = 0; i < MAX_MISSIONS; i++)
        if (g->contracts[i].state == ACTIVE)
            g->contracts[i].spawned = 0;
    career_system(g);
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
    g->equipment = (1 << DORSAL) | (1 << VENTRAL);
    g->warp = g->effective_warp = 1;
    g->target = 1;
    g->page = FLIGHT;
    record_exploration(g);
    for (i = 0; i < MAX_MISSIONS; i++)
        g->contracts[i].state = COMPLETE;
    refresh_board(g);
    say(g, "ASTRALBOUND / FLIGHT PROTOTYPE - H FOR HELP");
    galaxy_reset(g);
}
/* Turrets use independent world angles. Steering them never changes flight guidance. */
void view_angles(const Game *g, int *yaw, int *pitch) {
    *yaw = g->yaw;
    *pitch = g->pitch;
    if (g->camera_mode == 1) {
        *yaw = (g->yaw + 512) & 1023;
        *pitch = -g->pitch;
    } else if (g->camera_mode >= 3) {
        *yaw = g->turret_yaw;
        *pitch = g->turret_pitch;
    }
}
/* Positive roll banks the right wing down. Pitch rotates about the banked
   right axis, so pulling up after a quarter roll turns across the horizon. */
static Vec level_up(int yaw, int pitch) {
    return vec(-(int64_t)sini(pitch) * sini(yaw) / 16384, cosi(pitch),
               -(int64_t)sini(pitch) * cosi(yaw) / 16384);
}
/* Integer atan2 in table angles. Search the cross-product sign instead of
   maximizing a dot product: rounded trig lengths otherwise bias tiny turns. */
static int attitude_angle(int64_t x, int64_t z) {
    int lo = 0, hi = 256, a;
    int64_t ax = llabs(x), az = llabs(z);
    if (!ax && !az) return 0;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (ax * cosi(mid) > az * sini(mid)) lo = mid + 1;
        else hi = mid;
    }
    a = lo;
    if (a && llabs(ax * cosi(a - 1) - az * sini(a - 1)) <
             llabs(ax * cosi(a) - az * sini(a))) a--;
    if (z < 0) a = 512 - a;
    return (x < 0 ? -a : a) & 1023;
}
static void pitch_ship(Game *g, int angle) {
    Vec f, right, up, next, unbanked_right, unbanked_up;
    int64_t horizontal;
    if (!angle) return;
    if ((!g->roll || g->roll == 512) &&
        abs(g->pitch + (g->roll ? -angle : angle)) < 256) {
        g->pitch += g->roll ? -angle : angle;
        return;
    }
    f = forward(g->yaw, g->pitch);
    right = vec(cosi(g->yaw), 0, -sini(g->yaw));
    up = level_up(g->yaw, g->pitch);
    up = add(scale(right, sini(g->roll), 16384), scale(up, cosi(g->roll), 16384));
    next = add(scale(f, cosi(angle), 16384), scale(up, sini(angle), 16384));
    up = sub(scale(up, cosi(angle), 16384), scale(f, sini(angle), 16384));
    horizontal = isqrt64((uint64_t)(next.x * next.x + next.z * next.z));
    if (horizontal > 32) g->yaw = attitude_angle(next.x, next.z);
    g->pitch = attitude_angle(next.y, horizontal);
    if (g->pitch > 512) g->pitch -= 1024;
    unbanked_right = vec(cosi(g->yaw), 0, -sini(g->yaw));
    unbanked_up = level_up(g->yaw, g->pitch);
    g->roll = attitude_angle(up.x * unbanked_right.x + up.y * unbanked_right.y + up.z * unbanked_right.z,
                             up.x * unbanked_up.x + up.y * unbanked_up.y + up.z * unbanked_up.z);
}
int combat_target(const Game *g) {
    int i = g->combat_target;
    return i >= 0 && i < MAX_NPC && g->npc[i].active &&
                   length(sub(g->npc[i].pos, g->pos)) <= 10000LL * FP ? i : -1;
}
/* System Drive is available to every hull. The nearest safety shell, rather
   than the selected target alone, limits cruise speed. Four seconds of room
   and a conservative braking envelope leave time for frame/route changes. */
static int64_t drive_limit(const Game *g) {
    int i;
    int64_t ceiling_room = muldiv(SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_SPEED, SYSTEM_DRIVE_ACCEL);
    int64_t room = ceiling_room;
    for (i = 0; i < g->system.count; i++) {
        const Planet *p = &g->system.planets[i];
        int64_t r = length(sub(scale(g->pos, 1, FP), p->center)) -
                    p->radius - p->atmosphere - 30000LL;
        if (r < room) room = r;
    }
    for (i = 0; i < g->system.stars; i++) {
        const Star *s = &g->system.star[i];
        int64_t r = length(sub(scale(g->pos, 1, FP), s->center)) -
                    s->radius - 1000000000LL;
        if (r < room) room = r;
    }
    for (i = 0; i < g->nports; i++) {
        int64_t r = length(sub(g->pos, g->ports[i].pos)) / FP - 10000;
        if (r < room) room = r;
    }
    /* Passive police circle inside their port's own 10 km shell. */
    for (i = 0; i < MAX_NPC; i++) if (g->npc[i].active && !police_patrol(g, i)) {
        int64_t r = length(sub(g->pos, g->npc[i].pos)) / FP - 20000;
        if (r < room) room = r;
    }
    for (i = 0; i < ASTEROIDS; i++) {
        int64_t r = length(sub(g->pos, asteroid_position(g, i))) / FP - 20000;
        if (r < room) room = r;
    }
    if (room <= 0) return 0;
    if (room == ceiling_room) return SYSTEM_DRIVE_SPEED;
    {
        int64_t limit = 600 + room / 4;
        int64_t brake = (int64_t)isqrt_product((uint64_t)room, SYSTEM_DRIVE_ACCEL);
        if (limit > brake) limit = brake;
        return limit < SYSTEM_DRIVE_SPEED ? limit : SYSTEM_DRIVE_SPEED;
    }
}
/* Physical reasons the drive cannot produce cruise thrust. Autopilot
   handover, detours and laser fire only withhold an armed drive briefly. */
static const char *drive_fault(const Game *g) {
    if (g->dead || g->docked != -1) return "DOCKED";
    if (g->fuel <= 0) return "NO FUEL";
    if (drive_limit(g) <= 4000) return "EXCLUSION ZONE";
    return NULL;
}
int drive_available(const Game *g) {
    return !drive_fault(g);
}
/* An armed drive that can no longer run disarms itself and zeroes the set
   speed, so cruise thrust and the engine note stop together. */
static void drive_watchdog(Game *g) {
    const char *fault;
    if (!g->drive_manual || !(fault = drive_fault(g)))
        return;
    g->drive_manual = 0;
    g->throttle = 0;
    snprintf(g->message, sizeof(g->message), "SYSTEM DRIVE DISENGAGED - %s", fault);
}
static Vec autopilot_route(const Game *g, int *detour);
static int autopilot_detour(const Game *g) {
    int detour;
    autopilot_route(g, &detour);
    return detour;
}
static int drive_requested(const Game *g) {
    /* Guidance may limit thrust for safe capture without disarming the drive.
       The 1,000 km handover leaves the weakest loaded hull room to stop from
       the 4 km/s terminal cap on ordinary engines (about 600 km). */
    if (g->autopilot &&
        (autopilot_detour(g) ||
         length(sub(g->pos, navigation_position(g))) < 1000000LL * FP))
        return 0;
    return g->docked == -1 && g->fuel > 0 && !g->dead && !g->laser &&
           g->drive_manual;
}
static int ordinary_thrust(const Game *g) {
    const ShipSpec *s = &ship_specs[g->ship];
    return s->acceleration * s->mass / (s->mass + cargo_used(g)) * FP;
}
static int64_t drive_thrust(const Game *g) {
    /* Retain powerful brakes after disengaging, including a restored save
       with cruise momentum. Never clamp or teleport the ship's velocity. */
    int64_t speed = length(g->vel);
    if (g->autopilot && (g->target >= 0 || g->target_body >= 0))
        speed = length(sub(g->vel, navigation_velocity(g)));
    return g->fuel > 0 && g->docked == -1 &&
        ((drive_requested(g) && drive_limit(g) > 4000) ||
         length(g->vel) > 2000000LL * FP ||
         (g->drive_active && speed > (g->autopilot ? 4000LL : 8LL) * FP))
        ? SYSTEM_DRIVE_ACCEL * FP : ordinary_thrust(g);
}
static void burn_fuel(Game *g, Vec engine, int ticks, int drive) {
    uint64_t impulse = (uint64_t)length(engine) * ticks / HZ;
    /* Field drive impulse costs the same as ordinary engines at the same
       fraction of output; huge cruise acceleration does not empty the tank. */
    if (drive) impulse = impulse * 60 / SYSTEM_DRIVE_ACCEL;
    impulse += g->fuel_burn;
    while (impulse >= 50000U * FP) {
        impulse -= 50000U * FP;
        if (g->fuel > 0) g->fuel--;
    }
    g->fuel_burn = (uint32_t)impulse;
}
int safe_warp(const Game *g) {
    /* Local hazards choose fixed physics ticks in cruise_interval; they do
       not prevent the player from accelerating time. */
    return g->paused || g->dead || g->ui_quit ? 1 : clamp(g->warp, 1, MAX_WARP);
}
/* Detours (climb-outs, around-body and stellar avoidance) report themselves
   so callers can withhold the drive. The destination's own approach points,
   a station corridor or the point above a pad, count as the destination. */
static Vec autopilot_route(const Game *g, int *detour) {
    const Port *port = g->target >= 0 ? &g->ports[g->target] : NULL;
    int destination_body = port ? port->body : g->target_body;
    Vec dest = navigation_position(g);
    *detour = 1;
    /* Enter rotating stations only through their outward traffic corridor. */
    if (port && (!g->dock_stage || g->clearance != g->target))
        dest = add(dest, scale(port->normal, 2000 * FP, 16384));
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
        /* A shallow interplanetary bearing can clear the solid surface while
           still trapping the ship in atmospheric cruise. Finish the climb
           before handing over to the remote destination, even on a clear ray. */
        if (destination_body != b &&
            alt < (int64_t)(p->atmosphere + 100000) * FP &&
            length(dlocal) > r + (int64_t)(p->atmosphere + 200000) * FP)
            return add(center, radial(local, r + (int64_t)(p->atmosphere + 200000) * FP));
        if (port && port->surface && port->body == b && length(sub(du,up)) > 4) {
            /* Approach above the moving pad before descending through the
               terrain shell. A tangent-only detour can orbit a loaded ship
               forever as the surface port rotates away beneath it. */
            Vec overhead=add(port->pos,scale(port->normal,(TERRAIN_MAX_HEIGHT+2000LL)*FP,16384));
            int64_t safe = p->radius + TERRAIN_MAX_HEIGHT + 1000,
                    top = p->radius + p->atmosphere + 100000LL,
                    reach = isqrt64((uint64_t)(top * top - safe * safe)) +
                            isqrt64((uint64_t)((safe + 1000) * (safe + 1000) - safe * safe));
            if (!swept_sphere(scale(g->pos,1,FP),scale(overhead,1,FP),p->center,(int)safe)) {
                *detour = 0;
                return overhead;
            }
            /* The horizon hides a pad that would be in view from the climb
               ceiling. Climbing restores the line of sight within seconds and
               the two waypoints alternate, jerking the nose back and forth.
               Glide along the tangent to the sight-line shell instead: as the
               overhead point rises into view, its bearing meets this one. */
            if (alt < (int64_t)(p->atmosphere + 100000) * FP &&
                length(sub(du, up)) * p->radius / 16384 < reach + reach / 8) {
                Vec tangent = unit(sub(du, scale(up, cosine, 16384)), 16384);
                int64_t shell = safe * FP, radius = length(local);
                int64_t sine, lead = 30000LL + (alt > (int64_t)p->atmosphere * FP
                                                ? alt / FP - p->atmosphere : 0);
                if (alt < TERRAIN_MAX_HEIGHT * FP)
                    return add(center, radial(local, shell + 1000LL * FP));
                if (radius <= shell)
                    return add(add(g->pos, scale(tangent, lead * FP, 16384)),
                               scale(up, shell - radius, 16384));
                /* Sine of the dip below the local horizon, kept in metres:
                   a Q14 cosine is too coarse just above the shell. */
                sine = (int64_t)isqrt64((uint64_t)((radius - shell) / FP * ((radius + shell) / FP))) *
                       16384 / (radius / FP);
                return add(g->pos, unit(sub(scale(tangent, isqrt64((uint64_t)(16384 * 16384 - sine * sine)), 16384),
                                            scale(up, sine, 16384)), (int)(lead * FP)));
            }
        }
        if (length(sub(du, up)) > 4 &&
            swept_sphere(scale(g->pos, 1, FP), scale(dest, 1, FP), p->center, p->radius + TERRAIN_MAX_HEIGHT + 1000)) {
            Vec tangent = sub(du, scale(up, cosine, 16384));
            int64_t safe = r + (int64_t)(p->atmosphere + 200000) * FP;
            if (length(tangent) < 32)
                tangent = vec(up.y, -up.x, 0);
            if (length(tangent) < 32)
                tangent = vec(16384, 0, 0);
            if (alt < (int64_t)(p->atmosphere + 100000) * FP)
                return add(center, scale(up, safe, 16384));
            /* Keep the waypoint moving along the great circle. */
            return add(center, scale(unit(add(up, unit(tangent, 1500)), 16384), safe, 16384));
        }
    }
    for (b = 0; b < g->system.stars; b++) {
        const Star *s = &g->system.star[b];
        if (swept_sphere(scale(g->pos, 1, FP), scale(dest, 1, FP), s->center,
                         s->radius + 1000000000)) {
            Vec up = unit(sub(g->pos, scale(s->center, FP, 1)), 16384);
            Vec side = vec(up.z, 0, -up.x);
            if (!length(side))
                side = vec(16384, 0, 0);
            return add(scale(s->center, FP, 1), scale(unit(add(up, unit(side, 8000)), 16384),
                                                      (s->radius + 2000000000LL) * FP, 16384));
        }
    }
    *detour = 0;
    return dest;
}
static int64_t along(Vec a, Vec unit_axis) {
    return scale(vec(a.x,0,0),unit_axis.x,16384).x +
           scale(vec(a.y,0,0),unit_axis.y,16384).x +
           scale(vec(a.z,0,0),unit_axis.z,16384).x;
}
/* Descending into an atmosphere from space arms an emergency brake. It sheds
   the air-relative descent at ENTRY_BRAKE_ACCEL, braking harder whenever the
   room left demands it, so the descent always stops above ENTRY_BRAKE_FLOOR.
   It corrects the integrated velocity before displacement, and caps one
   tick's descent at the room left, so no single fast tick crosses the floor.
   Thin shells (frozen worlds) arm at twice the floor to leave braking room. */
static void entry_brake(Game *g) {
    const Planet *p;
    Vec local, up, air;
    int64_t descent, room, envelope, limit;
    int i;
    if (g->autopilot) {
        g->entry_brake = 0;
        return;
    }
    for (i = 0; !g->entry_brake && i < g->system.count; i++) {
        int64_t interface;
        p = &g->system.planets[i];
        interface = (int64_t)p->radius + (p->atmosphere > 2 * ENTRY_BRAKE_FLOOR
                                              ? p->atmosphere : 2 * ENTRY_BRAKE_FLOOR);
        local = sub(scale(g->pos, 1, FP), p->center);
        /* Inclusive: a tick ending exactly on the interface (whole metres)
           must still arm on the next one. Only inbound motion counts. */
        if (p->atmosphere && length(local) >= interface &&
            swept_sphere(scale(g->pos, 1, FP), scale(add(g->pos, scale(g->vel, 1, HZ)), 1, FP),
                         p->center, interface) &&
            along(sub(g->vel, sub(body_velocity(&g->system, i, g->tick), frame_velocity(g))),
                  unit(local, 16384)) < 0) {
            g->entry_brake = i + 1;
            g->throttle = 0;
            say(g, "ATMOSPHERIC ENTRY - EMERGENCY BRAKE");
        }
    }
    if (g->entry_brake > g->system.count)
        g->entry_brake = 0;
    if (!g->entry_brake)
        return;
    p = &g->system.planets[g->entry_brake - 1];
    local = sub(scale(g->pos, 1, FP), p->center);
    up = unit(local, 16384);
    air = add(surface_velocity(p, local),
              sub(body_velocity(&g->system, g->entry_brake - 1, g->tick), frame_velocity(g)));
    descent = -along(sub(g->vel, air), up);
    /* 50 m of margin absorbs Q8 rounding and the release tick's gravity. */
    room = length(local) - p->radius - ENTRY_BRAKE_FLOOR - 50;
    envelope = room > 0 ? (int64_t)isqrt64((uint64_t)(2 * ENTRY_BRAKE_ACCEL * room)) * FP : 0;
    if (envelope > room * FP * HZ)
        envelope = room > 0 ? room * FP * HZ : 0;
    limit = descent - ENTRY_BRAKE_ACCEL * FP / HZ;
    if (limit > envelope)
        limit = envelope;
    if (limit < 0)
        limit = 0;
    if (descent > limit)
        g->vel = add(g->vel, scale(up, descent - limit, 16384));
    if (!limit) {
        /* Hand back a hover: assist at zero throttle holds the air speed. */
        g->entry_brake = 0;
        g->assist = 1;
        g->throttle = 0;
        say(g, "DESCENT ARRESTED - ASSIST HOLDING ALTITUDE");
    }
}
/* Cleared to descend from the gate and still over the pad: the autopilot
   flies the rest like a lander instead of diving nose-first at the pad. */
static int pad_settling(const Game *g) {
    const Port *pad;
    Vec off;
    int64_t height;
    if (!g->autopilot || g->target < 0 || !g->ports[g->target].surface ||
        !g->dock_stage || g->clearance != g->target)
        return 0;
    pad = &g->ports[g->target];
    off = sub(g->pos, pad->pos);
    if (length(off) > 2500LL * FP)
        return 0;
    height = along(off, pad->normal);
    return height < 2500LL * FP && length(sub(off, scale(pad->normal, height, 16384))) < 100LL * FP;
}
/* Gravity brings the ship down. The engines hold back the sink rate, tapering
   to a couple of metres per second at the gear, and cancel drift over the
   pad; they never push the ship towards the ground. */
static Vec pad_settle_thrust(const Game *g, Vec gravity, int64_t thrust) {
    const Port *pad = &g->ports[g->target];
    Vec n = pad->normal, off = sub(g->pos, pad->pos), drift, want, engine;
    int64_t height = along(off, n), sink, push;
    drift = sub(off, scale(n, height, 16384));
    /* Constant 2 m/s^2 braking that meets the pad at 2 m/s. */
    sink = (int64_t)isqrt64(4ULL * FP * FP + (uint64_t)(height > 0 ? height : 0) * 4 * FP);
    if (sink > 30 * FP)
        sink = 30 * FP;
    want = scale(n, -sink, 16384);
    want = sub(want, length(drift) > 20 * FP ? unit(drift, 5 * FP) : scale(drift, 1, 4));
    engine = sub(scale(sub(want, sub(g->vel, port_relative_velocity(g, g->target))), 4, 1), gravity);
    push = along(engine, n);
    if (push < 0)
        engine = sub(engine, scale(n, push, 16384));
    return length(engine) > thrust ? unit(engine, thrust) : engine;
}
/* Turn an attitude angle at most three table steps per tick. */
static int slew_angle(int angle, int goal) {
    return (angle + clamp(((goal - angle + 1536) & 1023) - 512, -3, 3)) & 1023;
}
/* Level the wings and nose against the pad's up, keeping the current heading
   (or, when the nose points straight down at the pad, the right wing). */
static void level_attitude(Game *g, Vec up) {
    Vec heading = forward(g->yaw, 0), nose, right;
    int yaw = g->yaw, pitch;
    nose = sub(heading, scale(up, along(heading, up), 16384));
    if (length(nose) < 4096) {
        right = vec(cosi(g->yaw), 0, -sini(g->yaw));
        nose = sub(right, scale(up, along(right, up), 16384));
    }
    aim_angles(nose, &yaw, &pitch);
    right = vec(cosi(yaw), 0, -sini(yaw));
    g->yaw = slew_angle(g->yaw, yaw);
    g->pitch = clamp(g->pitch + clamp(pitch - g->pitch, -3, 3), -250, 250);
    g->roll = slew_angle(g->roll, attitude_angle(along(up, right), along(up, level_up(yaw, pitch))));
}
void game_tick(Game *g, Controls c) {
    Vec acceleration = vec(0, 0, 0), gravity, old, frame_delta;
    int i, touchdown;
    int64_t r, dist, alt, ground_alt;
    Vec local, center;
    const Planet *planet;
    int64_t thrust = ordinary_thrust(g);
    if (g->paused || g->dead || g->ui_quit)
        return;
    /* Translated local frames preserve relative precision and do not inject
       orbital velocity into ground flight. Rebase velocity on frame changes. */
    frame_delta = g->frame_body < 0 ? vec(0, 0, 0) : g->system.planets[g->frame_body].center;
    g->tick++;
    update_world(g);
    frame_delta = g->frame_body < 0
                      ? vec(0, 0, 0)
                      : scale(sub(g->system.planets[g->frame_body].center, frame_delta), FP, 1);
    g->pos = add(g->pos, frame_delta);
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active)
            g->npc[i].pos = add(g->npc[i].pos, frame_delta);
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].active)
        g->pods[i].pos = add(g->pods[i].pos, frame_delta);
    for (i = 0; i < MAX_MISSILES; i++) if (g->projectiles[i].active)
        g->projectiles[i].pos = add(g->projectiles[i].pos, frame_delta);
    career_time(g);
    if (g->laser)
        g->laser--;
    if (g->heat > 0 && g->tick % 8 == 0)
        g->heat--;
    if (g->shield < 100 && g->tick % HZ == 0)
        g->shield++;
    /* Preserve sub-angle stick rates over fixed physics ticks. Digital keys win. */
    {
        int ay = clamp(c.axis_roll, -256, 256), ap = clamp(c.axis_pitch, -256, 256);
        int dy, dp;
        if (!ay || c.roll) g->roll_fraction = 0;
        if (!ap || c.pitch) g->pitch_fraction = 0;
        g->roll_fraction += c.roll ? 0 : ay * 3;
        g->pitch_fraction += c.pitch ? 0 : ap * 3;
        dy = c.roll ? c.roll * 3 : g->roll_fraction / 256;
        dp = c.pitch ? c.pitch * 3 : g->pitch_fraction / 256;
        g->roll_fraction %= 256; g->pitch_fraction %= 256;
        c.axis_roll = dy; c.axis_pitch = dp;
    }
    if (g->camera_mode >= 3 && g->page == FLIGHT) {
        g->turret_yaw = (g->turret_yaw + c.axis_roll) & 1023;
        g->turret_pitch = clamp(g->turret_pitch + c.axis_pitch, -250, 250);
        c.roll = c.pitch = c.axis_roll = c.axis_pitch = 0;
    }
    if (g->docked != -1) {
        g->drive_manual = g->drive_active = 0;
        g->entry_brake = 0;
        if (g->docked == -2) {
            const Planet *p = &g->system.planets[g->body];
            Vec local_ground;
            if (!length(g->ground_anchor)) {
                Planet before = *p;
                set_rotation(&before, g->tick - 1);
                g->ground_anchor = body_rotate(&before,
                    sub(g->pos, scale(p->center, FP, 1)), 1);
            }
            /* Analytic placement avoids accumulated rounding drift while landed. */
            local_ground = body_rotate(p, g->ground_anchor, 0);
            g->pos = add(scale(p->center, FP, 1), local_ground);
            g->vel = surface_velocity(p, scale(local_ground, 1, FP));
        }
        if (g->docked >= 0) {
            g->body = g->ports[g->docked].body;
            g->frame_body = g->body;
            g->pos = g->ports[g->docked].pos;
            g->vel = port_relative_velocity(g, g->docked);
        }
        if (g->tick % HZ == 0)
            record_exploration(g);
        career_missions(g);
        return;
    }
    drive_watchdog(g);
    g->body = nearest_body(g, g->pos);
    planet = &g->system.planets[g->body];
    {
        int new_frame =
            length(sub(scale(g->pos, 1, FP), planet->center)) < planet->influence ? g->body : -1;
        if (new_frame != g->frame_body) {
            Vec transfer = sub(frame_velocity(g), body_velocity(&g->system, new_frame, g->tick));
            g->vel = add(g->vel, transfer);
            for (i = 0; i < MAX_NPC; i++)
                if (g->npc[i].active)
                    g->npc[i].vel = add(g->npc[i].vel, transfer);
            for (i = 0; i < MAX_PODS; i++) if (g->pods[i].active)
                g->pods[i].vel = add(g->pods[i].vel, transfer);
            for (i = 0; i < MAX_MISSILES; i++) if (g->projectiles[i].active)
                g->projectiles[i].vel = add(g->projectiles[i].vel, transfer);
            g->frame_body = new_frame;
        }
    }
    if (g->tick % HZ == 0)
        record_exploration(g);
    center = scale(planet->center, FP, 1);
    local = sub(g->pos, center);
    r = (int64_t)planet->radius * FP;
    dist = length(local);
    alt = altitude_at(g, g->pos);
    gravity = unit(local, -planet->gravity * FP);
    if (dist > r)
        gravity = scale(scale(gravity, r, dist), r, dist);
    if (g->frame_body < 0) {
        /* Solar gravity in the system frame. */
        Vec solar = sub(g->pos, scale(g->system.star[0].center, FP, 1));
        int64_t solar_r = length(solar) / FP;
        if (solar_r > 1000000000LL) {
            int64_t a = 132712440000000LL / (solar_r / 1000) * FP / (solar_r / 1000);
            gravity = add(gravity, unit(solar, -(int)(a > 100 * FP ? 100 * FP : a)));
        }
    }
    thrust = drive_thrust(g);
    g->drive_active = thrust == SYSTEM_DRIVE_ACCEL * FP;
    if (g->autopilot) {
        Vec target;
        int settling;
        if (g->target >= 0 && g->clearance == g->target) {
            Vec gate = sub(
                add(g->ports[g->target].pos, scale(g->ports[g->target].normal, 2000 * FP, 16384)),
                scale(navigation_velocity(g), 1, HZ));
            if (length(sub(g->pos, gate)) < 60 * FP &&
                length(sub(g->vel, navigation_velocity(g))) < 12 * FP)
                g->dock_stage = 1;
        }
        int detour;
        target = autopilot_route(g, &detour);
        settling = pad_settling(g);
        if (settling)
            level_attitude(g, g->ports[g->target].normal);
        else
            g->roll = slew_angle(g->roll, 0);
        /* Hold the nose while settling onto a gate or pad: the bearing of a
           point a few metres away swings wildly with every correction. */
        if (!settling && g->tick % 4 == 0 && length(sub(target, g->pos)) > 100 * FP) {
            int yaw = g->yaw, pitch = g->pitch, turn;
            aim_angles(sub(target, g->pos), &yaw, &pitch);
            turn = ((yaw - g->yaw + 1536) & 1023) - 512;
            g->yaw = (g->yaw + clamp(turn, -12, 12)) & 1023;
            g->pitch = clamp(g->pitch + clamp(pitch - g->pitch, -12, 12), -250, 250);
        }
        Vec relative_vel = g->vel;
        /* Climb with the rotating ground. Cancelling its tangential velocity
           during departure makes the ship slide sideways into nearby ranges. */
        if (g->target >= 0 && g->ports[g->target].surface &&
            g->ports[g->target].body == g->body)
            relative_vel = sub(relative_vel, surface_velocity(planet,
                               sub(scale(target,1,FP),planet->center)));
        else if (alt < planet->atmosphere + TERRAIN_MAX_HEIGHT + 2000)
            relative_vel = sub(relative_vel, surface_velocity(planet, scale(local,1,FP)));
        int destination_body = g->target >= 0 ? g->ports[g->target].body : g->target_body;
        if ((g->frame_body < 0 || g->frame_body == destination_body) &&
            (g->target < 0 || g->ports[g->target].surface || alt > planet->atmosphere + 50000) &&
            !detour) {
            Vec motion = navigation_velocity(g);
            if (g->frame_body < 0 &&
                length(sub(navigation_position(g), g->pos)) > 10000000LL * FP) {
                /* One-second orbit samples contain metre-scale rounding
                   noise at AU distances. Cruise follows the body's averaged
                   motion; chasing that noise (or a distant station's local
                   orbit) every tick would burn the entire fuel reserve. */
                motion = g->system.planets[destination_body].cruise_velocity;
            }
            /* Apply approach/braking limits to closing speed, not system
               speed: even a slow approach must keep up with orbital motion.
               Waiting until 10,000 km can strand the ship outside that range.
               Leave the departure frame before matching a remote body's
               motion, so escape guidance retains its local thrust authority. */
            relative_vel = sub(g->vel, motion);
            /* Guidance samples the ship before integration. Evaluate the
               moving target at that same instant, avoiding a one-tick
               trailing error (hundreds of metres at orbital speed). */
            target = sub(target, scale(motion, 1, HZ));
        }
        int64_t route = length(sub(target, g->pos)) / FP;
        int64_t maxspeed = alt < planet->atmosphere + 8000 ? 600 : route > 10000000 ? 1000000 : 4000;
        if (g->target >= 0 && length(sub(g->ports[g->target].pos, g->pos)) < 10000 * FP &&
            !request_clearance(g, g->target))
            say(g, "CLEARANCE QUEUE - HOLDING OUTSIDE CORRIDOR");
        if (!g->fuel || (alt < 1000 && planet->gravity * FP >= thrust)) {
            g->autopilot = 0;
            g->warp = 1;
            say(g, "AUTOPILOT DISENGAGED - INSUFFICIENT THRUST AUTHORITY");
        }
        /* A continuous braking envelope replaces an abrupt speed jump at
           the atmosphere boundary, including around-body waypoints. */
        {
            int64_t room = alt - planet->atmosphere - 20000;
            int limit = room > 0 ? (int)isqrt64((uint64_t)room * 60) : 600;
            if (limit < 600)
                limit = 600;
            if (maxspeed > limit)
                maxspeed = limit;
        }
        if (g->drive_active && drive_requested(g) && drive_limit(g) > 4000) {
            int64_t limit = drive_limit(g);
            int64_t approach = 600 + route / 4;
            maxspeed = limit < approach ? limit : approach;
        }
        if (g->dock_stage && g->target >= 0)
            maxspeed = 40;
        acceleration = guidance(g->pos, relative_vel, target, thrust / 4 * 3, maxspeed * FP);
        if (g->target < 0 && length(sub(g->pos, navigation_position(g))) < 100 * FP &&
            length(relative_vel) < 8 * FP) {
            g->autopilot = 0;
            g->throttle = 0;
            g->warp = 1;
            say(g, "BODY WAYPOINT REACHED - LOCAL FRAME FLIGHT");
        }
        acceleration = sub(acceleration, gravity);
        if (length(acceleration) > thrust)
            acceleration = unit(acceleration, thrust);
        if (settling)
            acceleration = pad_settle_thrust(g, gravity, thrust);
        if (g->target >= 0 && length(sub(g->ports[g->target].pos, g->pos)) < 500 * FP)
            g->gear = 1;
    } else {
        g->roll = (g->roll + c.axis_roll) & 1023;
        pitch_ship(g, c.axis_pitch);
        if (c.thrust)
            g->throttle = clamp(g->throttle + c.thrust * 3, 0, 4000);
        if (g->assist || g->drive_active) {
            int64_t set_speed = g->throttle;
            if (g->drive_manual)
                set_speed = (g->drive_active && drive_requested(g) ? drive_limit(g) : 600) *
                            g->throttle / 4000;
            Vec desired = scale(forward(g->yaw, g->pitch), set_speed * FP, 16384);
            if (alt < planet->atmosphere)
                desired = add(desired, surface_velocity(planet, scale(local, 1, FP)));
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
    /* Fuel follows delivered impulse, so small cruise corrections do not
       consume the same fuel as sustained full thrust on AU-scale journeys. */
    burn_fuel(g, acceleration, 1, g->drive_active);
    old = g->pos;
    g->vel = add(g->vel, scale(add(acceleration, gravity), 1, HZ));
    /* Limit air-relative speed before displacement, including an entry from
       vacuum in a single fast tick. Preserve heading and rotating air motion. */
    for (i = 0; i < g->system.count; i++) {
        const Planet *p = &g->system.planets[i];
        Vec air, relative, next, entry;
        int64_t shell = (int64_t)p->radius + p->atmosphere;
        if (!p->atmosphere) continue;
        next = add(g->pos, scale(g->vel, 1, HZ));
        if (!swept_sphere(scale(g->pos, 1, FP), scale(next, 1, FP), p->center, shell))
            continue;
        entry = sub(scale(g->pos, 1, FP), p->center);
        if (length(entry) > shell) entry = radial(entry, shell);
        air = add(surface_velocity(p, entry),
                  sub(body_velocity(&g->system, i, g->tick), frame_velocity(g)));
        relative = sub(g->vel, air);
        if (length(relative) > ATMOSPHERE_SPEED * FP)
            g->vel = add(air, radial(relative, ATMOSPHERE_SPEED * FP));
    }
    entry_brake(g);
    g->pos = add(g->pos, scale(g->vel, 1, HZ));
    /* Atmospheric drag remains density-dependent; overspeed causes no damage. */
    if (alt < planet->atmosphere && alt > 0) {
        int density = atmosphere_density(planet, alt);
        Vec air = surface_velocity(planet, scale(local, 1, FP));
        g->vel = add(air, scale(sub(g->vel, air), 1048576 - density, 1048576));
    }
    /* A settling autopilot docks on touchdown, not by snapping down 35 m. */
    for (i = 0, touchdown = pad_settling(g); i < port_count(g); i++)
        if (length(sub(g->pos, g->ports[i].pos)) < 35 * FP &&
            length(sub(g->vel, port_relative_velocity(g, i))) < 8 * FP && g->gear &&
            (g->ports[i].surface || g->clearance == i) &&
            (!touchdown || i != g->target || along(sub(g->pos, g->ports[i].pos), g->ports[i].normal) < 1 * FP)) {
            g->pos = g->ports[i].pos;
            g->vel = port_relative_velocity(g, i);
            g->body = g->ports[i].body;
            g->frame_body = g->body;
            g->docked = i;
            g->autopilot = 0;
            g->warp = 1;
            g->throttle = 0;
            refresh_board(g);
            say(g, "DOCKED - B CONTRACTS / M MARKET / U SHIPYARD");
            inspect_cargo(g);
            break;
        }
    if (g->docked == -1)
        for (i = 0; i < g->nports; i++)
            if (station_impact(g, old, g->pos, i)) {
                g->hull = 0;
                say(g, "STATION WALL IMPACT");
                break;
            }
    if (g->docked == -1)
        for (i = 0; i < g->system.count; i++)
            if (terrain_impact(g, old, g->pos, i)) {
                const Planet *p = &g->system.planets[i];
                Vec lc = sub(g->pos, scale(p->center, FP, 1));
                if (g->gear && p->type != GAS_GIANT && length(sub(g->vel, surface_velocity(p, scale(lc, 1, FP)))) < 8 * FP) {
                    int height = terrain_height(p, scale(lc, 1, FP));
                    g->pos = add(scale(p->center, FP, 1),
                                 radial(lc, (int64_t)(p->radius + height + 12) * FP));
                    g->vel = surface_velocity(p, scale(lc, 1, FP));
                    g->ground_anchor = body_rotate(p, sub(g->pos, scale(p->center, FP, 1)), 1);
                    g->frame_body = i;
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
    if (g->docked == -1)
        for (i = 0; i < g->system.stars; i++)
            if (swept_sphere(scale(old, 1, FP), scale(g->pos, 1, FP), g->system.star[i].center,
                             g->system.star[i].radius)) {
                g->hull = 0;
                say(g, "STELLAR IMPACT");
            }
    if (c.fire && g->heat < 90 && g->tick % 6 == 0 &&
        (g->camera_mode < 3 || equipped(g, g->camera_mode == 3 ? DORSAL : VENTRAL))) {
        int yaw = g->yaw, pitch = g->pitch, hit = -1, aimed = -1;
        Vec f;
        if (g->camera_mode >= 3)
            view_angles(g, &yaw, &pitch);
        f = forward(yaw, pitch);
        g->laser = 3;
        g->heat += 4;
        g->shots++;
        for (i = 0; i < MAX_NPC; i++) {
            Npc *n = &g->npc[i];
            Vec rel = sub(n->pos, g->pos);
            int64_t l, along, miss;
            if (!n->active)
                continue;
            l = length(rel);
            if (l > 5000 * FP)
                continue;
            along = (rel.x * f.x + rel.y * f.y + rel.z * f.z) / 16384;
            if (along <= 0)
                continue;
            miss = length(sub(rel, scale(f, along, 16384)));
            if (miss < 70 * FP) {
                hit = i;
                break;
            }
            if (miss < 250 * FP && g->npc_role[i] == POLICE)
                aimed = i; /* a near miss on police still counts as firing on them */
        }
        police_discharge(g, hit >= 0 ? hit : aimed);
        if (hit >= 0)
            damage_npc(g, hit, 20 + (g->camera_mode >= 3 ? g->turret_upgrade[g->camera_mode - 3] : g->upgrade) * 10);
    }
    for (i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        Vec rel, rv, desired, avoid, nold;
        int64_t l;
        if (!n->active)
            continue;
        if (g->npc_role[i] == INSTALLATION_TARGET) {
            const Planet *p = &g->system.planets[nearest_body(g, n->pos)];
            Planet previous = *p;
            Vec local = sub(n->pos, scale(p->center, FP, 1));
            set_rotation(&previous, g->tick - 1);
            local = body_rotate(p, body_rotate(&previous, local, 1), 0);
            n->pos = add(scale(p->center, FP, 1), local);
            n->vel = surface_velocity(p, scale(local, 1, FP));
            continue;
        }
        rel = sub(g->pos, n->pos);
        rv = sub(g->vel, n->vel);
        l = length(rel);
        if (police_patrol(g, i)) {
            /* Hold station on the port's patrol circle, matching its motion. */
            acceleration = guidance(n->pos, sub(n->vel, port_relative_velocity(g, g->npc_port[i])),
                                    patrol_point(g, i), 40 * FP, 400 * FP);
        } else {
            int police = g->npc_role[i] == POLICE;
            desired = sub(g->pos, unit(rel, 1200 * FP));
            acceleration = guidance(n->pos, sub(n->vel, g->vel), desired, (police ? 80 : 50) * FP,
                                    (police ? 800 : 500) * FP);
        }
        avoid = avoidance(rel, rv, 50 * FP, 300 * FP);
        if (length(avoid))
            acceleration = avoid; /* Ground avoidance always overrides the attack run. */
        ground_alt = altitude_at(g, n->pos) * FP;
        if (ground_alt < 500 * FP)
            acceleration =
                unit(sub(n->pos, scale(g->system.planets[nearest_body(g, n->pos)].center, FP, 1)),
                     60 * FP);
        nold = n->pos;
        n->vel = add(n->vel, scale(acceleration, 1, HZ));
        n->pos = add(n->pos, scale(n->vel, 1, HZ));
        if (swept_sphere(scale(sub(nold, old), 1, FP), scale(sub(n->pos, g->pos), 1, FP),
                         vec(0, 0, 0), 30)) {
            g->hull -= 20;
            say(g, "COLLISION DAMAGE");
            if (g->npc_role[i] == POLICE)
                n->vel = add(g->vel, unit(sub(n->pos, g->pos), 200 * FP)); /* armour shrugs off a ram */
            else
                n->active = 0;
        }
        if (n->cooldown)
            n->cooldown--;
        if (g->npc_role[i] == POLICE) {
            if (n->hostile && l < POLICE_RANGE * FP && l > 400 * FP && !n->cooldown) {
                n->cooldown = POLICE_INTERVAL;
                if (g->shield > 0)
                    g->shield = clamp(g->shield - POLICE_DAMAGE, 0, 100);
                else
                    g->hull -= POLICE_DAMAGE;
            }
        } else if (n->hostile && l < 2500 * FP && l > 400 * FP && !n->cooldown) {
            n->cooldown = HZ * 3;
            if (g->shield > 0)
                g->shield = clamp(g->shield - 8, 0, 100);
            else
                g->hull -= 8;
        }
    }
    career_tick(g, old);
    career_missions(g);
    if (g->hull <= 0) {
        g->hull = 0;
        g->dead = 1;
        g->autopilot = 0;
        g->warp = 1;
        if (!strstr(g->message, "IMPACT"))
            say(g, "SHIP LOST - R TO RESTART");
    }
}
/* Only remote, unobstructed system-frame flight may use cruise integration.
   Bound the entire interval by travel, orbital motion and braking room, then
   halve it until safe. Local physics, combat and capture always use game_tick. */
static int cruise_interval(const Game *g, Controls c, int remaining) {
    int i, step = remaining;
    int64_t accel = drive_thrust(g) / FP;
    /* Rapid drive changes use short bounded cruise steps. */
    if (accel == SYSTEM_DRIVE_ACCEL && step > HZ) step = HZ;
    Vec dest;
    int64_t speed, route, closing;
    if (step < HZ || g->frame_body != -1 || g->paused || g->dead || g->ui_quit || !g->fuel ||
        !g->autopilot || g->docked != -1 || g->laser || c.fire || c.thrust ||
        c.roll || c.pitch || c.axis_roll || c.axis_pitch ||
        (g->target < 0 && g->target_body < 0))
        return 1;
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && !police_patrol(g, i)) return 1;
    for (i = 0; i < MAX_PODS; i++) if (g->pods[i].active) return 1;
    for (i = 0; i < MAX_MISSILES; i++) if (g->projectiles[i].active) return 1;
    if (g->resource_target >= 0) return 1;
    for (i = 0; i < MAX_MISSIONS; i++) {
        const Mission *m = &g->contracts[i];
        if (m->state != ACTIVE)
            continue;
        if (m->type != COURIER && m->type != FREIGHT)
            return 1; /* Patrol spawn and survey progress are tick events. */
        if (m->deadline >= g->tick && m->deadline - g->tick < (uint32_t)step)
            step = (int)(m->deadline - g->tick) + 1;
    }
    if (UINT32_MAX - g->tick < (uint32_t)step)
        return 1;
    dest = navigation_position(g);
    if (autopilot_detour(g))
        return 1;
    speed = length(g->vel) / FP;
    route = length(sub(dest, g->pos)) / FP;
    closing = length(sub(g->vel, navigation_velocity(g))) / FP;
    while (step >= HZ) {
        int safe = 1;
        int64_t seconds = (step + HZ - 1) / HZ;
        int64_t travel = speed * seconds + (int64_t)accel * seconds * seconds;
        /* Return well before the 10,000 km approach-speed boundary. */
        int64_t future_closing = closing + (int64_t)accel * seconds;
        if (route < 20000000LL + muldiv(future_closing, future_closing, accel) +
                    closing * seconds + (int64_t)accel * seconds * seconds)
            safe = 0;
        for (i = 0; i < g->system.count && safe; i++) {
            const Planet *p = &g->system.planets[i];
            int64_t motion = length(p->cruise_velocity) / FP;
            int64_t relative = speed + motion + (int64_t)accel * seconds;
            int64_t shell = p->radius + p->atmosphere + 200000LL +
                            muldiv(relative, relative, accel);
            if (shell < p->influence + 200000LL)
                shell = p->influence + 200000LL;
            if (length(sub(scale(g->pos, 1, FP), p->center)) <=
                shell + travel + 2 * motion * seconds)
                safe = 0;
        }
        for (i = 0; i < g->system.stars && safe; i++) {
            const Star *star = &g->system.star[i];
            int64_t motion = star->period ? star->orbit * 7 / star->period : 0;
            if (length(sub(scale(g->pos, 1, FP), star->center)) <=
                star->radius + 2000000000LL + travel + 2 * motion * seconds)
                safe = 0;
        }
        if (safe)
            return step;
        step /= 2;
    }
    return 1;
}
static void cruise_step(Game *g, int ticks) {
    int destination = g->target >= 0 ? g->ports[g->target].body : g->target_body;
    Vec motion = g->system.planets[destination].cruise_velocity;
    int64_t thrust;
    drive_watchdog(g);
    thrust = drive_thrust(g);
    int64_t room = altitude_at(g, g->pos) -
                   g->system.planets[nearest_body(g, g->pos)].atmosphere - 20000;
    int64_t maxspeed = room > 0 ? (int64_t)isqrt64((uint64_t)room * 60) : 600;
    g->drive_active = thrust == SYSTEM_DRIVE_ACCEL * FP;
    if (g->drive_active && drive_requested(g)) maxspeed = drive_limit(g);
    else maxspeed = clamp((int)maxspeed, 600, 1000000);
    Vec acceleration = guidance_step(g->pos, sub(g->vel, motion), navigation_position(g),
                                     thrust / 4 * 3, maxspeed * FP, ticks);
    const Planet *planet = &g->system.planets[nearest_body(g, g->pos)];
    Vec local = sub(g->pos, scale(planet->center, FP, 1));
    int64_t radius = (int64_t)planet->radius * FP, dist = length(local);
    Vec gravity = unit(local, -planet->gravity * FP), engine;
    Vec solar = sub(g->pos, scale(g->system.star[0].center, FP, 1));
    int64_t solar_r = length(solar) / FP;
    uint32_t before = g->tick;
    if (dist > radius)
        gravity = scale(scale(gravity, radius, dist), radius, dist);
    if (solar_r > 1000000000LL) {
        int64_t a = 132712440000000LL / (solar_r / 1000) * FP / (solar_r / 1000);
        gravity = add(gravity, unit(solar, -(int)(a > 100 * FP ? 100 * FP : a)));
    }
    engine = sub(acceleration, gravity);
    if (length(engine) > thrust)
        engine = unit(engine, thrust);
    acceleration = add(engine, gravity);
    /* Constant-acceleration displacement uses the mean velocity, avoiding
       the large forward-Euler position error of a compressed interval. */
    Vec delta = scale(acceleration, ticks, HZ);
    g->pos = add(g->pos, scale(add(g->vel, scale(delta, 1, 2)), ticks, HZ));
    g->vel = add(g->vel, delta);
    burn_fuel(g, engine, ticks, g->drive_active);
    g->tick += ticks;
    g->heat = clamp(g->heat - (int)(g->tick / 8 - before / 8), 0, 100);
    g->shield = clamp(g->shield + (int)(g->tick / HZ - before / HZ), 0, 100);
    update_world(g);
    g->body = nearest_body(g, g->pos);
    record_exploration(g);
    {
        int yaw = g->yaw, pitch = g->pitch;
        int turns = (int)(g->tick / 4 - before / 4) * 12;
        g->roll = 0;
        aim_angles(sub(navigation_position(g), g->pos), &yaw, &pitch);
        g->yaw = (g->yaw + clamp(((yaw - g->yaw + 1536) & 1023) - 512,
                                -turns, turns)) & 1023;
        g->pitch = clamp(g->pitch + clamp(pitch - g->pitch, -turns, turns), -250, 250);
    }
    career_time(g);
    career_missions(g);
}
/* Advances the jump sequence in real time; nonzero while the ship is inside
   the tunnel, where no system exists to simulate. The jump is re-validated
   at the boundary, so traffic or a dock during the charge aborts it. */
static int hyperspace_frame(Game *g, int ticks) {
    int before = g->hyperspace;
    g->warp = g->effective_warp = 1;
    g->hyperspace += ticks;
    if (before <= HYPER_CHARGE && g->hyperspace > HYPER_CHARGE) {
        const char *refusal = jump_refusal(g, g->hyperspace_destination);
        if (refusal) {
            g->hyperspace = 0;
            snprintf(g->message, sizeof(g->message), "JUMP ABORTED - %s", refusal);
            return 0;
        }
        jump(g, g->hyperspace_destination);
        snprintf(g->message, sizeof(g->message), "HYPERSPACE TRANSIT - %.24s", g->system.name);
    }
    if (before <= HYPER_CHARGE + HYPER_TUNNEL && g->hyperspace > HYPER_CHARGE + HYPER_TUNNEL)
        say(g, "JUMP COMPLETE - T SELECTS PORT / P AUTOPILOT");
    if (g->hyperspace >= HYPER_TOTAL)
        g->hyperspace = 0;
    return g->hyperspace > HYPER_CHARGE && g->hyperspace <= HYPER_CHARGE + HYPER_TUNNEL;
}
void game_frame(Game *g, Controls c, int ticks) {
    uint32_t before = g->tick;
    int i, steps = 0;
    if (ticks > 0)
        g->ui_clock += (uint32_t)ticks; /* real time, independent of warp and pause */
    if (g->dead)
        g->hyperspace = 0;
    if (ticks <= 0 || g->paused || g->dead || g->ui_quit)
        return;
    if (g->hyperspace && hyperspace_frame(g, ticks))
        return;
    for (i = 0; i < ticks && steps < MAX_FRAME_STEPS; i++) {
        int w = safe_warp(g), advanced = 0;
        while (advanced < w && steps < MAX_FRAME_STEPS) {
            int step, safe = safe_warp(g);
            if (g->dead || g->paused || advanced >= safe)
                break;
            step = cruise_interval(g, c, (safe < w ? safe : w) - advanced);
            if (step > 1)
                cruise_step(g, step);
            else
                game_tick(g, c);
            advanced += step;
            steps++;
        }
    }
    /* Warp is a requested rate; bounded local work may deliver less. */
    g->effective_warp = (int)((g->tick - before) / (uint32_t)ticks);
    if (g->effective_warp < 1)
        g->effective_warp = 1;
}

/* NULL when the jump may proceed, else the reason shown to the commander. */
const char *jump_refusal(const Game *g, Sector d) {
    int i;
    int64_t distance = galaxy_distance(g->system.sector,d);
    if (g->dead || g->docked != -1) return "DOCKED";
    if (distance == 0 || distance > 6000 || d.x < -1000000 || d.x > 1000000 ||
        d.y < -1000000 || d.y > 1000000 || d.z < -1000000 || d.z > 1000000)
        return "OUT OF RANGE";
    if (altitude_at(g, g->pos) < 12000) return "ALTITUDE BELOW 12 KM";
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && !police_patrol(g, i) &&
            length(sub(g->npc[i].pos, g->pos)) < 20000 * FP)
            return "TRAFFIC NEARBY";
    if (!galaxy_exists(g->seed,d)) return "EMPTY SECTOR";
    if (g->fuel < galaxy_jump_cost(g,d)) return "INSUFFICIENT FUEL";
    return NULL;
}
int jump(Game *g, Sector d) {
    int cost = galaxy_jump_cost(g,d);
    if (jump_refusal(g, d)) return 0;
    g->fuel -= cost;
    g->tick += HZ * 300;
    career_time(g);
    setup_system(g, d);
    /* Arrive over the host star's pole, 100,000 km beyond its exclusion
       shell. Polar placement avoids the planetary and binary orbital plane. */
    g->pos = add(scale(g->system.star[0].center, FP, 1),
                 vec(0, (g->system.star[0].radius + 1100000000LL) * FP, 0));
    g->vel = vec(0, 0, 0);
    g->body = nearest_body(g, g->pos);
    g->frame_body = -1;
    g->autopilot = 0;
    g->throttle = 0;
    g->entry_brake = 0;
    g->target = g->nports ? (g->nports > 1 ? 1 : 0) : -1;
    g->target_body = g->nports ? -1 : 0;
    g->warp = 1;
    record_exploration(g);
    g->page = FLIGHT;
    refresh_board(g);
    say(g, "JUMP COMPLETE - T SELECTS PORT / P AUTOPILOT");
    return 1;
}
void game_action(Game *g, int k) {
    if (g->ui_quit) return;
    if (k == 'r' && g->dead) {
        game_init(g, g->seed);
        return;
    }
    if (g->dead)
        return;
    /* Only pause reaches the ship while the hyperspace sequence runs. */
    if (g->hyperspace && k != ' ')
        return;
    if (g->page==MAP && galaxy_key(g,k)) return;
    switch (k) {
    case 'h':
        g->page = g->page == HELP ? FLIGHT : HELP;
        break;
    case 'v':
        if (g->page == FLIGHT) {
            g->camera_mode = (g->camera_mode + 1) % 5;
            if (g->camera_mode >= 3) {
                g->turret_yaw = g->yaw;
                g->turret_pitch = clamp(g->pitch + (g->camera_mode == 3 ? 128 : -128), -250, 250);
            }
        }
        g->page = FLIGHT;
        break;
    case 'r': {
        int i, start = combat_target(g);
        g->combat_target = -1;
        for (i = 1; i <= MAX_NPC; i++) {
            int n = (start + i) % MAX_NPC;
            if (g->npc[n].active && length(sub(g->npc[n].pos, g->pos)) <= 10000LL * FP) {
                g->combat_target = n;
                break;
            }
        }
        break;
    }
    case 'z':
        g->page = SYSTEM_MAP;
        g->system_display = 1;
        break;
    case 'i':
        g->page = SYSTEM_MAP;
        g->system_display = 2;
        break;
    case 'y':
        g->page = SYSTEM_MAP;
        g->system_display = 0;
        break;
    case 'g':
        if(!g->galaxy_ready) galaxy_reset(g);
        g->page = MAP;
        break;
    case 'b':
        if (g->page == MARKET) {
            if (black_market_available(g)) g->black_market = !g->black_market;
            say(g, g->black_market ? "BLACK MARKET ENABLED" : "REGULATED MARKET");
            break;
        }
        if (g->page == BOARD) { g->board_page = !g->board_page; refresh_board(g); }
        g->page = BOARD;
        g->selection = 0;
        break;
    case 'm':
        g->page = MARKET;
        g->selection = 0;
        break;
    case 'u':
        g->selection = 0;
        g->page = SHIPYARD;
        break;
    case 'j':
        g->page = LOG;
        break;
    case 'c':
        if (g->page == SYSTEM_MAP && g->system_display == 1)
            g->map_local = !g->map_local;
        g->selected = g->system.sector;
        break;
    case 'q':
        g->resource_target = -1;
        g->target_body = (g->target_body + 1) % g->system.count;
        g->target = -1;
        g->dock_stage = 0;
        g->clearance = -1;
        break;
    case 't':
        g->resource_target = -1;
        if (g->nports) {
            g->target = (g->target + 1) % g->nports;
            g->target_body = -1;
            g->dock_stage = 0;
            g->clearance = -1;
        }
        break;
    case 'p':
        if (g->docked == -1 && (g->target >= 0 || g->target_body >= 0)) {
            g->autopilot = !g->autopilot;
            g->dock_stage = 0;
        }
        break;
    case 'a':
        if (g->docked == -1 && !g->drive_manual && drive_fault(g)) {
            snprintf(g->message, sizeof(g->message), "SYSTEM DRIVE UNAVAILABLE - %s", drive_fault(g));
        } else if (g->docked == -1) {
            g->drive_manual = !g->drive_manual;
            g->assist = 1;
            g->throttle = g->drive_manual ? 4000 : 0;
            say(g, g->drive_manual ? "SYSTEM DRIVE ARMED - W/S CRUISE SPEED"
                                  : "SYSTEM DRIVE DISENGAGED - BRAKING");
        }
        break;
    case 'd':
        say(g, request_clearance(g, g->target) ? "DOCKING CLEARANCE GRANTED" : "CLEARANCE QUEUED");
        break;
    case 'e':
        if (g->drive_manual || g->drive_active) g->throttle = 0;
        g->drive_manual = 0;
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
        g->warp = g->warp >= MAX_WARP ? MAX_WARP : g->warp * 4;
        break;
    case '[':
        g->warp = g->warp <= 1 ? 1 : g->warp / 4;
        break;
    case 'l':
        if (g->docked != -1 && g->crew < ship_specs[g->ship].crew) {
            say(g, "INSUFFICIENT CREW - 9 THEN + TO RECRUIT"); break;
        }
        if (g->docked != -1) {
            int departure = g->docked >= 0;
            Vec up = unit(sub(g->pos, scale(g->system.planets[g->body].center, FP, 1)), 16384);
            g->pos = add(g->pos, scale(up, 80 * FP, 16384));
            g->vel = add(g->docked >= 0 ? port_relative_velocity(g, g->docked) : g->vel,
                         scale(up, 20 * FP, 16384));
            g->clearance = -1;
            g->dock_stage = 0;
            g->docked = -1;
            g->ground_anchor = vec(0, 0, 0);
            g->page = FLIGHT;
            g->throttle = 0;
            say(g, "LAUNCHED - P AUTOPILOT / H HELP");
            if (departure) inspect_cargo(g);
        }
        break;
    case 'x': {
        const char *refusal = jump_refusal(g, g->selected);
        if (refusal) {
            snprintf(g->message, sizeof(g->message), "JUMP DENIED - %s", refusal);
            break;
        }
        /* The jump itself happens when the charge completes; see hyperspace_frame. */
        g->hyperspace = 1;
        g->hyperspace_destination = g->selected;
        g->page = FLIGHT;
        g->autopilot = 0;
        g->warp = 1;
        say(g, "HYPERSPACE DRIVE CHARGING");
        break;
    }
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
    case '9': g->page = CAREER; g->selection = 0; break;
    case '0': g->page = EQUIPMENT; g->selection = 0; break;
    case ';': {
        int i, start = g->resource_target >= ASTEROIDS ? g->resource_target - ASTEROIDS + 1 : 0;
        for (i = 0; i < g->rig_count; i++) {
            int index = (start + i) % g->rig_count;
            MiningRig *r = &g->rigs[index];
            if (r->sector.x == g->system.sector.x && r->sector.y == g->system.sector.y && r->sector.z == g->system.sector.z) {
                g->resource_target = ASTEROIDS + index; g->target = -1; g->target_body = r->body;
                g->clearance = -1; g->dock_stage = 0;
                say(g, "RIG WAYPOINT - P APPROACH / MANUAL LAND / N RECOVER");
                break;
            }
        }
        if (i == g->rig_count) say(g, "NO RIG IN CURRENT SYSTEM");
        break;
    }
    case '\\':
        g->resource_target = (g->resource_target + 1) % ASTEROIDS;
        g->target = -1; g->target_body = 0; g->clearance = -1; g->dock_stage = 0;
        say(g, "ASTEROID WAYPOINT SELECTED - P AUTO / O MINE / N COLLECT");
        break;
    case ',': say(g, fire_missile(g) ? "MISSILE AWAY" : "NO MISSILE / NO RADAR TARGET"); break;
    case '.': if (!countermeasure(g)) say(g, "NO COUNTERMEASURES"); break;
    case '/': escape_pod(g); break;
    case '\r':
        if (g->page == EQUIPMENT)
            say(g, buy_equipment(g, g->selection) ? "EQUIPMENT PURCHASED" : "PURCHASE REFUSED");
        if (g->page == SHIPYARD) {
            const char *refusal = ship_refusal(g, g->selection);
            if (buy_ship(g, g->selection)) say(g, "HULL PURCHASED");
            else snprintf(g->message, sizeof(g->message), "HULL REFUSED: %s", refusal);
        }
        if (g->page == BOARD)
            say(g, accept_mission(g, g->selection) ? "CONTRACT ACCEPTED - J FOR DETAILS"
                                                   : "CANNOT ACCEPT CONTRACT");
        break;
    case '+':
        if (g->page == CAREER) say(g, recruit_crew(g, 1) ? "CREW HIRED - 25 CR PER HOUR" : "RECRUITMENT REFUSED");
        if (g->page == BOARD) say(g, negotiate_mission(g, g->selection) ? "TERMS: +20% PAY / -25% TIME" : "NEGOTIATION REFUSED");
        if (g->page == MARKET)
            say(g, trade(g, g->selection, 1) ? "PURCHASED 1 TONNE" : "PURCHASE REFUSED");
        break;
    case '-':
        if (g->page == EQUIPMENT) say(g, sell_equipment(g, g->selection) ? "EQUIPMENT SOLD AT HALF PRICE" : "SALE REFUSED");
        if (g->page == CAREER) recruit_crew(g, 0);
        if (g->page == MARKET)
            say(g, trade(g, g->selection, 0) ? "SOLD 1 TONNE" : "SALE REFUSED");
        break;
    case 'f':
        if (g->page == CAREER) { say(g, settle_fine(g) ? "FINE PAID" : "CANNOT PAY FINE"); break; }
        if (g->docked >= 0 && g->credits >= 100) {
            g->credits -= 100;
            g->fuel = g->hull = g->shield = 100;
            g->fuel_burn = 0;
            say(g, "REFUELLED AND REPAIRED - 100 CR");
        }
        break;
    case 'o':
        if (g->page == FLIGHT && g->docked < 0) {
            if (!(g->docked == -2 ? deploy_rig(g) : mine_asteroid(g))) say(g, "MINING REFUSED / AIM AT ASTEROID");
            break;
        }
        say(g, upgrade_weapon(g, g->page == EQUIPMENT && g->selection == DORSAL ? 1 :
                              g->page == EQUIPMENT && g->selection == VENTRAL ? 2 : 0)
            ? "WEAPON MOUNT UPGRADED" : "UPGRADE REFUSED");
        break;
    case 'n':
        if (g->docked < 0) {
            if (!(g->docked == -2 ? recover_rig(g) : collect_cargo(g))) say(g, "NO NEARBY CARGO/RIG OR HOLD FULL");
        } else if (!buy_ship(g, g->ship + 1)) say(g, "HULL PURCHASE REFUSED");
        break;
    case 1001:
    case 1002:
        if (g->page == SHIPYARD)
            g->selection = (g->selection + SHIPS + (k == 1001 ? -1 : 1)) % SHIPS;
        if (g->page == SYSTEM_MAP) {
            int step = k == 1001 ? -1 : 1;
            if (g->target < 0)
                g->target_body = (g->target_body + g->system.count + step) % g->system.count;
            else if (g->nports)
                g->target = (g->target + g->nports + step) % g->nports;
            g->clearance = -1;
            g->dock_stage = 0;
        }
        break;
    default:
        break;
    }
}
