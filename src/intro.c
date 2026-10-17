/* SPDX-License-Identifier: Unlicense
   The opening title sequence: one courier's run from dawn at Haven to
   sanctuary under a red giant. Every actor follows keyframed paths in a
   scene-local frame built from the real world (port, planet and sun), so
   playback is identical on every machine and never runs flight physics.
   World time is frozen per act; only lamps, station spin and effects move. */
#include "intro.h"
#include "galaxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S(seconds) ((int)((seconds) * HZ))
/* Shot boundaries in ticks. The jump keeps the game's own sequence length. */
enum {
    T_OPEN = 0, T_APPROACH = S(14), T_PAD = S(21), T_LIFT = S(27), T_CLIMB = S(38),
    T_HIGH = S(46), T_RELAY = S(54), T_AMBUSH = S(68), T_PASS = S(74), T_CHASE = S(82),
    T_KILL = S(88), T_MISSILE = S(94), T_AFTER = S(102), T_TURN = S(110), T_JUMP = S(116),
    T_ARRIVE = S(116) + HYPER_TOTAL, T_FOLLOW = S(134), T_POLICE = S(140), T_DOCK = S(150),
    T_TITLE = S(164)
};
typedef struct { Vec o, r, u, f; } Stage; /* origin Q8 system position, Q14 axes */
typedef struct { int t; int32_t x, y, z; } Key; /* ticks from the act start, metres */
enum { RAIDER_ONE, RAIDER_TWO, POLICE_ONE, POLICE_TWO };
static struct {
    Stage ground, climb, city, relay, convoy, dock;
    Vec liftoff_eye; /* Q8: corner of the pad's clear square behind the climb */
    uint32_t dawn, relay_tick, dock_tick;
    Sector home, destination;
    int station, jumped;
    Vec hero_front, hero_up, hero_right; /* Q14 hull axes after the last placement */
} play;

static Vec cross14(Vec a, Vec b) {
    return vec((a.y * b.z - a.z * b.y) / 16384, (a.z * b.x - a.x * b.z) / 16384,
               (a.x * b.y - a.y * b.x) / 16384);
}
static int64_t dot14(Vec a, Vec b) { return (a.x * b.x + a.y * b.y + a.z * b.z) / 16384; }
/* Right-handed like the hulls: right = up x front, front = right x up. */
static Stage stage(Vec origin, Vec up, Vec front) {
    Stage s;
    s.o = origin;
    s.u = unit(up, 16384);
    s.r = unit(cross14(s.u, front), 16384);
    if (length(s.r) < 8192) s.r = unit(cross14(s.u, vec(16384, 0, 0)), 16384);
    s.f = cross14(s.r, s.u);
    return s;
}
/* Stage-local Q8 metres to a system Q8 position. */
static Vec place(const Stage *s, Vec m) {
    return add(s->o, add(scale(s->r, m.x, 16384), add(scale(s->u, m.y, 16384), scale(s->f, m.z, 16384))));
}
static Vec at(const Stage *s, int64_t x, int64_t y, int64_t z) {
    return place(s, vec(x * FP, y * FP, z * FP));
}
static Vec axis(const Stage *s, int64_t x, int64_t y, int64_t z) {
    return unit(add(scale(s->r, x, 1), add(scale(s->u, y, 1), scale(s->f, z, 1))), 16384);
}
static Vec key_vec(const Key *k) {
    return vec((int64_t)k->x * FP, (int64_t)k->y * FP, (int64_t)k->z * FP);
}
static int same_key(const Key *a, const Key *b) { return a->x == b->x && a->y == b->y && a->z == b->z; }
/* Cubic Hermite through the keys, tangents from neighbouring keys scaled to
   each segment's duration. A repeated position is a hold: zero tangent, so
   ships ease out of and into rest. Returns stage-local Q8 metres. */
static Vec path(const Key *k, int n, int t) {
    int i = 0, span;
    int64_t s, s2, s3, h00, h10, h01, h11;
    Vec p0, p1, m0, m1;
    if (t <= k[0].t) return key_vec(&k[0]);
    if (t >= k[n - 1].t) return key_vec(&k[n - 1]);
    while (t >= k[i + 1].t) i++;
    span = k[i + 1].t - k[i].t;
    p0 = key_vec(&k[i]); p1 = key_vec(&k[i + 1]);
    if (same_key(&k[i], &k[i + 1])) return p0;
    m0 = i == 0 ? sub(p1, p0) : same_key(&k[i - 1], &k[i]) ? vec(0, 0, 0) :
         scale(sub(p1, key_vec(&k[i - 1])), span, k[i + 1].t - k[i - 1].t);
    m1 = i + 2 >= n ? sub(p1, p0) : same_key(&k[i + 1], &k[i + 2]) ? vec(0, 0, 0) :
         scale(sub(key_vec(&k[i + 2]), p0), span, k[i + 2].t - k[i].t);
    s = (int64_t)(t - k[i].t) * 4096 / span;
    s2 = s * s / 4096; s3 = s2 * s / 4096;
    h00 = 2 * s3 - 3 * s2 + 4096; h10 = s3 - 2 * s2 + s;
    h01 = 3 * s2 - 2 * s3; h11 = s3 - s2;
    return vec((p0.x * h00 + m0.x * h10 + p1.x * h01 + m1.x * h11) / 4096,
               (p0.y * h00 + m0.y * h10 + p1.y * h01 + m1.y * h11) / 4096,
               (p0.z * h00 + m0.z * h10 + p1.z * h01 + m1.z * h11) / 4096);
}
#define PATH(keys, t) path(keys, (int)(sizeof(keys) / sizeof(*keys)), t)
/* Table angle with the given sine and cosine components. */
static int angle_of(int64_t s, int64_t c) {
    int yaw = 0, pitch = 0;
    if (!s && !c) return 0;
    aim_angles(vec(s, 0, c), &yaw, &pitch);
    return yaw;
}
/* Yaw, pitch and bank that put the nose on front and the roof towards up,
   with the same conventions as commander attitude and the flight camera. */
static void orient(Vec front, Vec up, int *yaw, int *pitch, int *roll) {
    Vec right0, up0;
    aim_angles(front, yaw, pitch);
    right0 = vec(cosi(*yaw), 0, -sini(*yaw));
    up0 = vec(-(int64_t)sini(*pitch) * sini(*yaw) / 16384, cosi(*pitch),
              -(int64_t)sini(*pitch) * cosi(*yaw) / 16384);
    *roll = angle_of(dot14(up, right0), dot14(up, up0));
}
static void look(Intro *in, Vec eye, Vec target, Vec up) {
    in->eye = eye;
    orient(sub(target, eye), up, &in->yaw, &in->pitch, &in->roll);
}
/* Rotate up about front by a bank angle; positive banks the right wing down. */
static Vec banked(Vec front, Vec up, int angle) {
    Vec right = cross14(up, front);
    return add(scale(up, cosi(angle), 16384), scale(right, sini(angle), 16384));
}
static Vec blend(Vec a, Vec b, int k) { /* k 0..256 */
    return add(a, scale(sub(b, a), k, 256));
}
static int ramp(int t, int from, int to) { /* 0..256 */
    return t <= from ? 0 : t >= to ? 256 : (t - from) * 256 / (to - from);
}
static int ease(int t, int from, int to) {
    int k = ramp(t, from, to);
    return k * k * (768 - 2 * k) / 65536;
}
static void hero(Intro *in, Game *g, Vec pos, Vec front, Vec up) {
    g->pos = pos;
    play.hero_front = unit(front, 16384);
    play.hero_up = unit(sub(up, scale(play.hero_front, dot14(up, play.hero_front), 16384)), 16384);
    play.hero_right = cross14(play.hero_up, play.hero_front);
    orient(play.hero_front, play.hero_up, &g->yaw, &g->pitch, &g->roll);
    in->hero = 1;
}
/* Hull-relative point of the commander's ship, metres. */
static Vec hull(const Game *g, int x, int y, int z) {
    return add(g->pos, add(scale(play.hero_right, (int64_t)x * FP, 16384),
                           add(scale(play.hero_up, (int64_t)y * FP, 16384),
                               scale(play.hero_front, (int64_t)z * FP, 16384))));
}
static void glow(Intro *in, Vec p, int64_t radius, int core, int halo) {
    if (in->glows < INTRO_GLOWS) in->glow[in->glows++] = (IntroGlow){p, radius, core, halo};
}
static void beam(Intro *in, Vec a, Vec b, int colour) {
    if (in->beams < INTRO_BEAMS) in->beam[in->beams++] = (IntroBeam){a, b, colour};
}
static void caption(Intro *in, const char *text, int t, int from, int to) {
    if (t < from || t >= to) return;
    if (text != in->caption_text) snprintf(in->caption_text, sizeof(in->caption_text), "%s", text);
    in->caption = in->caption_text;
    in->caption_chars = (t - from) / 2 + 1;
}
/* Exhausts of the commander's Kestrel: twin pods aft, flickering. */
static void exhaust(Intro *in, const Game *g, int power, uint32_t t) {
    int side, flicker = (int)(hash32(t * 7919U) % 5);
    if (power <= 0) return;
    for (side = -1; side <= 1; side += 2)
        glow(in, hull(g, side * 12, 0, -92), 3 + power * (9 + flicker) / 256, 31, power > 160 ? 13 : 12);
}
static void puff(Game *g, Vec pos, Vec vel, int size) {
    Smoke *s = &g->smoke[g->smoke_next++ % MAX_SMOKE];
    memset(s, 0, sizeof(*s));
    s->active = 1; s->body = -1; s->size = size; s->pos = pos; s->vel = vel;
    s->seed = hash32(g->destruction_clock * 7919U + (unsigned)g->smoke_next);
    if (g->smoke_next >= MAX_SMOKE * 2) g->smoke_next -= MAX_SMOKE;
}
/* A free-space detonation through the game's own blast effect and sound. */
static void boom(Game *g, Vec pos, Vec vel, int size) {
    Blast *b = NULL;
    int i;
    for (i = 0; i < MAX_BLASTS; i++) {
        if (!g->blasts[i].active) { b = &g->blasts[i]; break; }
        if (!b || g->blasts[i].age > b->age) b = &g->blasts[i];
    }
    memset(b, 0, sizeof(*b));
    b->active = 1; b->body = -1; b->size = size; b->pos = pos; b->vel = vel;
    b->seed = hash32(g->seed ^ ++g->blast_serial * 2654435761U);
    for (i = 0; i < 8; i++) {
        Vec d = forward((int)(hash32(b->seed + (unsigned)i) & 1023), i * 53 - 180);
        puff(g, add(pos, scale(d, size * FP / 3, 16384)), vel, size / 2);
    }
}
static void npc(Game *g, int i, int ship, int role, Vec pos, Vec vel) {
    Npc *n = &g->npc[i];
    n->pos = pos; n->vel = vel; n->active = 1; n->hull = 60;
    n->hostile = role == RAIDER; n->mission = -1;
    g->npc_role[i] = role; g->npc_ship[i] = ship;
}
static void explode(Game *g, int i) {
    if (!g->npc[i].active) return;
    destruction_npc(g, i);
    g->npc[i].active = 0;
}
/* Muzzle of an NPC hull, metres ahead of its centre along its velocity. */
static Vec nose(const Game *g, int i) {
    return add(g->npc[i].pos, scale(unit(g->npc[i].vel, 16384), (int64_t)ship_specs[g->npc_ship[i]].depth * FP, 16384));
}
static int burst(int t, int from, int to, int period, int on) {
    return t >= from && t < to && (t - from) % period < on;
}
/* Bolts: shots fired every period ticks across [from, to), each a short
   streak crossing from muzzle to a seeded miss point in eight ticks. */
static void volley(Intro *in, Game *g, int t, int from, int to, int period,
                   Vec muzzle, Vec aim, int colour, int miss, uint32_t seed) {
    int j;
    if (t < from) return;
    if (t < to && (t - from) % period == 0) g->shots++;
    for (j = (t - from) / period; j >= 0; j--) {
        int fired = from + j * period, age = t - fired;
        uint32_t h = hash32(seed + (unsigned)j * 2654435761U);
        Vec target, d;
        if (age >= 8) break;
        if (fired >= to) continue;
        target = add(aim, vec(((int)(h & 255) - 128) * miss * FP / 128, ((int)(h >> 8 & 255) - 128) * miss * FP / 128,
                              ((int)(h >> 16 & 255) - 128) * miss * FP / 128));
        d = sub(target, muzzle);
        beam(in, add(muzzle, scale(d, age, 8)), add(muzzle, scale(d, age + 2, 8)), colour);
    }
}
static void spark(Intro *in, Game *g, Vec at_point, int t) {
    glow(in, at_point, 9 + (int)(hash32((unsigned)t) % 8), 31, 13);
    if (g->shield > 20) g->shield--;
}
/* ---- World selection --------------------------------------------------- */
static Vec sun_from(const Game *g, Vec q8) {
    return unit(sub(g->system.star[0].center, scale(q8, 1, FP)), 16384);
}
/* The primary star as a soft disc: hides the 16-sided fan of a giant and
   gives a sun at planetary distance its bloom. */
static void star_glow(Intro *in, const Game *g, int percent, int core, int halo) {
    const Star *s = &g->system.star[0];
    glow(in, scale(s->center, FP, 1), s->radius / 100 * percent, core, halo);
}
/* Elevation (Q14 sine) of the host star seen from a port. */
static int port_sun(Game *g, int port, uint32_t tick) {
    g->tick = tick;
    update_world(g);
    return (int)dot14(g->ports[port].normal, sun_from(g, g->ports[port].pos));
}
/* First tick in the span where the star rises through the elevation:
   a coarse scan, then bisection. Falls back to the best sample. */
static uint32_t sunrise(Game *g, int port, int elevation, uint32_t span) {
    uint32_t step = span / 96, t, best = 0, lo, hi;
    int previous = port_sun(g, port, 0), i, error = 1 << 30;
    for (t = step; t < span; t += step) {
        int e = port_sun(g, port, t);
        if (abs(e - elevation) < error) { error = abs(e - elevation); best = t; }
        if (previous < elevation && e >= elevation) {
            lo = t - step; hi = t;
            for (i = 0; i < 12 && hi - lo > 1; i++) {
                uint32_t mid = lo + (hi - lo) / 2;
                if (port_sun(g, port, mid) >= elevation) hi = mid; else lo = mid;
            }
            return hi;
        }
        previous = e;
    }
    return best;
}
static int orbital_port(const Game *g) {
    int i;
    for (i = 0; i < g->nports; i++) if (!g->ports[i].surface) return i;
    return -1;
}
/* A red giant within one jump makes the arrival; any system with a station
   will do if the galaxy differs. Leaves the chosen system loaded. */
static Sector pick_destination(Game *g) {
    static const Sector candidates[] = {{4, 0, -1}, {-1, 0, -1}, {-1, 0, 1}, {0, 0, 1}};
    int i;
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(*candidates)); i++) {
        if (!galaxy_exists(g->seed, candidates[i])) continue;
        setup_system(g, candidates[i]);
        if (orbital_port(g) >= 0) return candidates[i];
    }
    setup_system(g, play.home);
    return play.home;
}
static void clear_actors(Game *g) {
    memset(g->npc, 0, sizeof(g->npc));
    destruction_reset(g);
}
static void stage_world(Game *g, uint32_t tick) {
    g->tick = tick;
    update_world(g);
}
/* Keep a camera clear of terrain on the given body. */
static Vec above_ground(const Game *g, Vec pos, int body, int clearance) {
    const Planet *p = &g->system.planets[body];
    Vec centre = scale(p->center, FP, 1), rel = sub(pos, centre);
    int64_t floor = (int64_t)p->radius + terrain_height(p, scale(rel, 1, FP)) + clearance;
    return length(rel) < floor * FP ? add(centre, radial(rel, floor * FP)) : pos;
}
/* Stage-relative velocity: Q8 metres per second along a keyed path. */
static Vec path_velocity(const Stage *s, const Key *k, int n, int t) {
    return scale(sub(place(s, path(k, n, t + 1)), place(s, path(k, n, t - 1))), HZ, 2);
}
/* ---- Act 0: orbital sunrise -------------------------------------------- */
static void act_open(Intro *in, Game *g, int t) {
    const Planet *p = &g->system.planets[0];
    Vec c = scale(p->center, FP, 1), sun = sun_from(g, c), side, d, eye, look_at, right;
    /* Angle from the anti-solar point: the star clears the limb mid-shot. */
    int a = 108 + t * 68 / T_APPROACH, i;
    int64_t dist = (int64_t)p->radius * (140 - t * 10 / T_APPROACH) / 100, far;
    side = unit(cross14(sun, vec(0, 16384, 0)), 16384);
    if (length(side) < 8192) side = unit(cross14(sun, vec(16384, 0, 0)), 16384);
    d = add(scale(sun, -cosi(a), 16384), scale(side, sini(a), 16384));
    eye = add(c, scale(d, dist * FP, 16384));
    look_at = add(eye, add(scale(sun, 100, 1), scale(unit(sub(c, eye), 16384), 22, 1)));
    look(in, eye, look_at, d);
    in->hero = 0;
    star_glow(in, g, 900, 31, 15);
    /* An anamorphic streak through the star, hidden with it by the limb. */
    right = cross14(d, unit(sub(look_at, eye), 16384));
    far = length(sub(g->system.star[0].center, scale(eye, 1, FP)));
    for (i = 0; i < 2; i++) {
        Vec centre = scale(g->system.star[0].center, FP, 1);
        Vec reach = scale(unit(right, 16384), far / (i ? 3 : 7) * FP, 16384);
        beam(in, sub(centre, reach), add(centre, reach), i ? 27 : 13);
    }
    in->fade = 16 - ramp(t, 0, S(3)) / 16;
    if (t > T_APPROACH - S(1)) in->fade = ramp(t, T_APPROACH - S(1), T_APPROACH) / 16;
    snprintf(in->caption_text, sizeof(in->caption_text), "%.24s SYSTEM", g->system.name);
    caption(in, in->caption_text, t, S(3), S(12));
}
/* ---- Act 1: Haven at dawn, lift-off and climb ---------------------------- */
/* Haven lies in a deep crater; the climb stage points along the lowest gap
   in its rim, so the courier leaves the way the camera came in. */
static const Key lift_keys[] = {
    {0, 0, 0, 0}, {S(1.5), 0, 0, 0}, {S(3), 0, 28, 0}, {S(5), 0, 110, 0},
    {S(7), 0, 300, 120}, {S(9), 0, 760, 520}, {S(11), 0, 1500, 1150},
    {S(13), 0, 2550, 2050}, {S(15), 0, 3800, 3250}, {S(17), 0, 5300, 4800},
    {S(19), 0, 7200, 6800}};
static const Key high_keys[] = {
    {0, 0, 42000, 60000}, {S(2), 0, 47000, 66000}, {S(4), 0, 52500, 72500},
    {S(6), 0, 58500, 79500}, {S(8), 0, 65000, 87000}};
static const Key approach_keys[] = {
    {0, 60, 2650, 4200}, {S(1.5), 40, 2700, 3200}, {S(3.5), 20, 1700, 2000},
    {S(5.5), 10, 600, 1050}, {S(7), 0, 260, 650}};
/* Haven's crater rim is lowest along one heading: the approach comes in
   over it and the climb leaves through it. */
static Vec rim_gap(const Game *g) {
    const Planet *p = &g->system.planets[g->ports[0].body];
    Vec centre = scale(p->center, FP, 1), best = play.ground.f;
    int a, r, lowest = 1 << 30;
    for (a = 0; a < 1024; a += 32) {
        Vec d = axis(&play.ground, sini(a), 0, cosi(a));
        int high = -(1 << 30);
        for (r = 2000; r <= 5000; r += 1000) {
            int h = terrain_height(p, scale(sub(add(g->ports[0].pos, scale(d, (int64_t)r * FP, 16384)), centre), 1, FP));
            if (h > high) high = h;
        }
        if (high < lowest) { lowest = high; best = d; }
    }
    return best;
}
/* Nose pitch during lift-off: level until the hover ends, then rotating up. */
static int lift_pitch(int t) { return ease(t, S(4.5), S(9)) * 140 / 256; }
static void lift_hero(Intro *in, Game *g, int t) {
    const Stage *s = &play.climb;
    int p = lift_pitch(t), wobble = t < S(6) ? sini(t * 13) * 5 / 16384 : 0;
    Vec front = axis(s, 0, sini(p), cosi(p)), up = axis(s, 0, cosi(p), -sini(p));
    Vec pos = place(s, PATH(lift_keys, t));
    if (t >= S(9)) {
        /* After the turn the nose follows the climb itself. */
        Vec v = sub(place(s, PATH(lift_keys, t + 2)), place(s, PATH(lift_keys, t - 2)));
        front = unit(blend(front, unit(v, 16384), ramp(t, S(9), S(10))), 16384);
        up = sub(s->u, scale(front, dot14(s->u, front), 16384));
    }
    hero(in, g, pos, front, banked(front, up, wobble));
    g->throttle = t < S(1.5) ? 0 : t < S(6) ? 1600 : 4000;
    if (t >= S(1.5)) {
        int power = t < S(6) ? 120 + ramp(t, S(1.5), S(3)) / 3 : 230;
        exhaust(in, g, power, (uint32_t)t);
        if (t < S(7)) glow(in, hull(g, 0, -12, -10), 5 + (int)(hash32((unsigned)t) % 3), 15, 12);
    }
    /* Dust thrown off the pad by the lift jets. */
    if (t >= S(2) && t < S(5) && t % 6 == 0) {
        uint32_t h = hash32((unsigned)t * 31U);
        int a = (int)(h & 1023), r = 50 + (int)(h >> 10) % 30;
        puff(g, at(s, sini(a) * r / 16384, 3, cosi(a) * r / 16384),
             scale(axis(s, sini(a), 0, cosi(a)), 45 * FP, 16384), 9);
    }
}
static void act_ground(Intro *in, Game *g, int t) {
    const Stage *s = &play.climb;
    int body = g->ports[0].body;
    in->hero = 1;
    if (t < T_PAD) {
        /* Over the crater rim and down into the valley city. */
        int k = t - T_APPROACH;
        Vec eye = above_ground(g, place(s, PATH(approach_keys, k)), body, 120);
        hero(in, g, g->ports[0].pos, s->f, s->u);
        look(in, eye, blend(at(s, 0, 900, -2500), at(s, 0, 40, 0), ease(k, S(1), S(6))), s->u);
        in->fade = 16 - ramp(k, 0, S(1)) / 16;
        caption(in, g->ports[0].name, k, S(1.5), S(6.5));
    } else if (t < T_LIFT) {
        /* High orbit round the courier on its pad. */
        int k = t - T_PAD, a = 90 + k * 100 / (T_LIFT - T_PAD), r = 220 - k * 30 / (T_LIFT - T_PAD);
        Vec eye = above_ground(g, at(s, sini(a) * r / 16384, 100 - k / 12, cosi(a) * r / 16384), body, 50);
        hero(in, g, g->ports[0].pos, s->f, s->u);
        look(in, eye, at(s, 0, 0, 20), s->u);
    } else {
        int k = t - T_LIFT;
        Vec eye = above_ground(g, play.liftoff_eye, body, 16);
        lift_hero(in, g, k);
        /* The camera trails its subject slightly, like a hand-held lens. */
        look(in, eye, blend(at(s, 0, 20, 0), g->pos, 64 + ramp(k, S(1), S(5)) * 3 / 4), s->u);
    }
}
static void act_climb(Intro *in, Game *g, int t) {
    const Stage *s = &play.climb;
    if (t < T_HIGH) {
        int k = t - T_LIFT;
        Vec eye;
        lift_hero(in, g, k);
        eye = add(g->pos, add(scale(play.hero_front, -340 * FP, 16384),
                              add(scale(s->u, 200 * FP, 16384), scale(play.hero_right, 140 * FP, 16384))));
        look(in, eye, add(g->pos, scale(play.hero_front, 150 * FP, 16384)), s->u);
        g->throttle = 4000;
    } else {
        /* Fly alongside, then let the courier pull away into the dark. */
        int k = t - T_HIGH, lag = k < S(4.5) ? k : S(4.5) + (k - S(4.5)) * 9 / 10;
        Vec v, front, up, pos = place(s, PATH(high_keys, k)), alongside = place(s, PATH(high_keys, lag));
        v = sub(place(s, PATH(high_keys, k + 2)), place(s, PATH(high_keys, k - 2)));
        front = unit(v, 16384);
        up = sub(s->u, scale(front, dot14(s->u, front), 16384));
        hero(in, g, pos, front, banked(front, up, 90 - ease(k, S(5), S(8)) / 3));
        look(in, add(alongside, add(scale(s->r, 250 * FP, 16384), scale(s->u, 150 * FP, 16384))),
             g->pos, s->u);
        exhaust(in, g, 256, (uint32_t)t);
        g->throttle = 4000;
        if (k > S(7)) in->fade = ramp(k, S(7), S(8)) / 16;
    }
}
/* ---- Act 2: past the Relay ---------------------------------------------- */
static const Key relay_keys[] = {
    {0, -1050, 570, 1100}, {S(3), -595, 260, 620}, {S(6), -214, 99, 194},
    {S(9), 400, 60, -500}, {S(12), 1200, 100, -1900}, {S(14), 1700, 150, -2800}};
static void act_relay(Intro *in, Game *g, int t) {
    const Stage *s = &play.relay;
    int k = t - T_RELAY;
    Vec pos = place(s, PATH(relay_keys, k)), v, front, up, eye, target;
    v = sub(place(s, PATH(relay_keys, k + 2)), place(s, PATH(relay_keys, k - 2)));
    front = unit(v, 16384);
    up = sub(s->u, scale(front, dot14(s->u, front), 16384));
    hero(in, g, pos, front, banked(front, up, -90 + sini(k * 3) * 40 / 16384));
    exhaust(in, g, 220, (uint32_t)t);
    g->throttle = 3000;
    eye = at(s, -560 + k * 60 / S(14), 330 - k * 30 / S(14), 760 - k * 50 / S(14));
    target = blend(s->o, g->pos, 64 + ramp(k, S(2), S(10)) * 3 / 4);
    look(in, eye, target, s->u);
    g->ports[play.station].rotation = (int)(k * 1024 / S(30)) & 1023;
    in->fade = 16 - ramp(k, 0, S(1)) / 16;
}
/* ---- Act 3: ambush ------------------------------------------------------- */
/* All actors move in a convoy frame cruising at 250 m/s; the planet,
   stars and wreckage stay in the system frame and fall behind. */
static const Key hero_keys[] = {
    {0, 0, 0, 0}, {S(13), 0, 0, 0}, {S(15), 90, 40, 0}, {S(17), 140, 20, 0},
    {S(19), 110, 0, 0}, {S(26), 110, 0, 0}, {S(34), 60, -20, 0}, {S(48), 0, 0, 0}};
static const Key raider_one[] = {
    {S(2), -520, 140, 5200}, {S(4), -330, 90, 2600}, {S(6), -120, 30, 500},
    {S(6.6), -80, 20, -150}, {S(8), -260, 90, -1800}, {S(10), -950, 420, -2700},
    {S(12), -420, 160, -1500}, {S(14), -200, 60, -900}, {S(16), 60, 120, -620},
    {S(18), 160, 40, -520}, {S(20), 140, 20, -420}, {S(22), 125, 15, -330}, {S(23.5), 150, 40, -380}};
static const Key raider_two[] = {
    {S(2.2), 480, -90, 5400}, {S(4.2), 300, -60, 2800}, {S(6.2), 140, -30, 600},
    {S(6.9), 110, -30, -200}, {S(8.4), 330, -120, -1900}, {S(10.5), 1000, -350, -2900},
    {S(13), 500, -100, -2000}, {S(18), 380, 60, -1700}, {S(24), 300, 90, -1900},
    {S(27), 260, 80, -2000}, {S(34), -200, 200, -2600}, {S(42), -100, 150, -2200},
    {S(48), 0, 100, -1800}};
#define MISSILE_LAUNCH S(26.5)
#define FLARES S(29.6)
#define DETONATE S(31.5)
static Stage convoy_at(int k) {
    Stage m = play.convoy;
    m.o = add(m.o, scale(m.f, 250LL * FP * k, (int64_t)HZ * 16384));
    return m;
}
static Vec in_convoy(int k, Vec local) { Stage m = convoy_at(k); return place(&m, local); }
static Vec actor(const Key *keys, int n, int k) { return in_convoy(k, path(keys, n, k)); }
#define ACTOR(keys, k) actor(keys, (int)(sizeof(keys) / sizeof(*keys)), k)
static Vec velocity_of(const Key *keys, int n, int k) {
    return scale(sub(actor(keys, n, k + 1), actor(keys, n, k - 1)), HZ, 2);
}
#define VELOCITY(keys, k) velocity_of(keys, (int)(sizeof(keys) / sizeof(*keys)), k)
static Vec convoy_velocity(void) { return scale(play.convoy.f, 250 * FP, 16384); }
/* Flares drift out to the left and above; the first one takes the missile.
   Convoy-local Q8 metres. */
static Vec flare_local(int i, int k) {
    int dt = k - FLARES;
    Vec start = PATH(hero_keys, FLARES);
    return add(start, vec((int64_t)(-40 - 12 * i) * FP * dt / HZ, (int64_t)(25 + 8 * i) * FP * dt / HZ,
                          (int64_t)-45 * FP * dt / HZ));
}
static Vec missile_local(int k) {
    Vec end = flare_local(0, DETONATE), launch = PATH(raider_two, MISSILE_LAUNCH);
    Key keys[4];
    keys[0] = (Key){MISSILE_LAUNCH, (int32_t)(launch.x / FP), (int32_t)(launch.y / FP), (int32_t)(launch.z / FP)};
    keys[1] = (Key){S(28.5), 150, 70, -900};
    keys[2] = (Key){S(30.2), 70, 40, -260};
    keys[3] = (Key){DETONATE, (int32_t)(end.x / FP), (int32_t)(end.y / FP), (int32_t)(end.z / FP)};
    return path(keys, 4, k);
}
/* Hero attitude in the ambush: rolls, a Newtonian flip to face the chaser,
   a flip back, and finally a climbing turn towards the star. */
static void ambush_hero(Intro *in, Game *g, int k) {
    Stage m = convoy_at(k);
    Vec pos = in_convoy(k, PATH(hero_keys, k)), front, up = m.u;
    int flip = ease(k, S(18), S(20)) - ease(k, S(24.5), S(26.5)), roll = ease(k, S(13.5), S(16.5)) * 4;
    front = add(scale(m.f, cosi(flip * 2), 16384), scale(m.r, sini(flip * 2), 16384));
    if (k >= S(42)) {
        Vec sun = sun_from(g, pos);
        front = unit(blend(front, sun, ease(k, S(42), S(47))), 16384);
        up = sub(m.u, scale(front, dot14(m.u, front), 16384));
        roll += ease(k, S(42), S(44)) / 4 - ease(k, S(44), S(47)) / 4;
    }
    hero(in, g, pos, front, banked(front, up, roll));
    exhaust(in, g, flip ? 140 : 210, (uint32_t)k);
    g->throttle = 2600;
    if (k >= DETONATE && k % 4 == 0)
        puff(g, hull(g, 8, 4, -70), convoy_velocity(), 8 + (int)(hash32((unsigned)k) % 5));
}
static void ambush_raiders(Intro *in, Game *g, int k) {
    Vec h = g->pos;
    int i;
    if (k >= S(2.5) && k <= S(23.5)) npc(g, RAIDER_ONE, 0, RAIDER, ACTOR(raider_one, k), VELOCITY(raider_one, k));
    if (k == S(23.5)) explode(g, RAIDER_ONE);
    if (k >= S(2.7)) npc(g, RAIDER_TWO, 0, RAIDER, ACTOR(raider_two, k), VELOCITY(raider_two, k));
    /* Arrival clouds: a white bloom that collapses as each hull drops out. */
    if (k >= S(2) && k < S(3.2))
        glow(in, ACTOR(raider_one, S(2)), 10 + 260 * sini(ramp(k, S(2), S(3.2)) * 2) / 16384, 31, 27);
    if (k >= S(2.2) && k < S(3.4))
        glow(in, ACTOR(raider_two, S(2.2)), 10 + 260 * sini(ramp(k, S(2.2), S(3.4)) * 2) / 16384, 31, 27);
    if (g->npc[RAIDER_ONE].active) {
        /* Head-on strafing pass, then bursts from astern and return fire. */
        volley(in, g, k, S(4.5), S(6.5), 6, nose(g, RAIDER_ONE), h, 23, k > S(6) ? 8 : 70, 11);
        volley(in, g, k, S(12.5), S(17.5), 9, nose(g, RAIDER_ONE), h, 23, 50, 13);
        volley(in, g, k, S(20.5), S(23.2), 11, nose(g, RAIDER_ONE), h, 23, 40, 17);
    }
    volley(in, g, k, S(4.8), S(6.8), 7, nose(g, RAIDER_TWO), h, 23, k > S(6.2) ? 8 : 80, 19);
    if (k == S(6.2) || k == S(6.5)) spark(in, g, hull(g, -20, 6, 40), k);
    if (k >= S(15) && k < S(15.2)) spark(in, g, hull(g, 10, 8, -60), k);
    if (k >= S(16.8) && k < S(17)) spark(in, g, hull(g, -30, 2, -40), k);
    /* Long-range shots from the survivor. */
    volley(in, g, k, S(36), S(47), 20, nose(g, RAIDER_TWO), h, 23, 120, 23);
    /* Missile: a fading exhaust trail, decoyed by flares. */
    if (k >= MISSILE_LAUNCH && k < DETONATE) {
        for (i = 0; i < 6; i++) {
            int when = k - i * 3;
            if (when < MISSILE_LAUNCH) break;
            glow(in, in_convoy(when, missile_local(when)), i ? 6 - i : 6, i < 2 ? 31 : 15, i < 2 ? 12 : 7);
        }
    }
    if (k == MISSILE_LAUNCH) g->shots++;
    if (k >= FLARES && k < DETONATE + S(3))
        for (i = 0; i < 3; i++)
            if (i || k < DETONATE)
                glow(in, in_convoy(k, flare_local(i, k)), 8 + (int)(hash32((unsigned)(k * 3 + i)) % 7), 31, 12);
    if (k == DETONATE) {
        boom(g, in_convoy(k, flare_local(0, k)), convoy_velocity(), 110);
        spark(in, g, hull(g, -40, 10, 0), k);
    }
}
static void act_ambush(Intro *in, Game *g, int t) {
    int k = t - T_AMBUSH;
    Stage m = convoy_at(k);
    ambush_hero(in, g, k);
    ambush_raiders(in, g, k);
    if (t < T_PASS) {
        /* Riding behind the courier as the raiders drop in ahead. */
        look(in, in_convoy(k, vec((70 - k / 8) * FP, 120 * FP, (-380 + k / 4) * FP)),
             in_convoy(k, vec(0, 0, 900 * FP)), m.u);
        in->fade = 16 - ramp(k, 0, S(1)) / 16;
    } else if (t < T_CHASE) {
        int j = t - T_PASS;
        Vec eye = in_convoy(k, vec(-240 * FP, 170 * FP, 160 * FP)), r1 = g->npc[RAIDER_ONE].pos;
        /* Lead the courier towards the raider without losing it from frame. */
        Vec lead = scale(unit(sub(r1, g->pos), 16384), (j < S(1) ? 0 : 140) * FP, 16384);
        look(in, eye, add(g->pos, lead), m.u);
    } else if (t < T_KILL) {
        /* From behind the chasing raider. */
        Vec r1 = g->npc[RAIDER_ONE].pos, f = unit(g->npc[RAIDER_ONE].vel, 16384);
        Vec eye = add(r1, add(scale(f, -150 * FP, 16384), scale(m.u, 55 * FP, 16384)));
        look(in, eye, blend(r1, g->pos, 200), m.u);
    } else if (t < T_MISSILE) {
        /* The commander's own view. */
        in->hero = 0; in->cockpit = 1;
        in->eye = g->pos;
        in->yaw = g->yaw; in->pitch = g->pitch; in->roll = g->roll;
        if (burst(k, S(21), S(23.5), 8, 4)) {
            in->guns = 1;
            if ((k - S(21)) % 8 == 0) g->shots++;
        }
        if (k >= S(22) && k < S(23.5) && k % 4 == 0 && g->npc[RAIDER_ONE].active)
            glow(in, add(g->npc[RAIDER_ONE].pos, vec(0, 0, 0)), 8, 31, 12);
        if (k >= S(23.5) && k < S(23.5) + 3) in->flash = 6 - (k - S(23.5)) * 2;
    } else if (t < T_AFTER) {
        /* Missile camera: ride behind it, then hold as it meets the flare. */
        int hold = DETONATE - S(2), j = k < MISSILE_LAUNCH + 2 ? MISSILE_LAUNCH + 2 : k > hold ? hold : k;
        Vec p = missile_local(j), d = unit(sub(missile_local(j + 2), p), 16384), eye, target;
        eye = add(p, add(scale(d, -140 * FP, 16384), vec(0, 40 * FP, 0)));
        if (k < MISSILE_LAUNCH) eye = add(PATH(raider_two, k), vec(60 * FP, 90 * FP, -260 * FP));
        target = k < MISSILE_LAUNCH ? g->npc[RAIDER_TWO].pos :
                 k < DETONATE ? blend(in_convoy(k, missile_local(k)), g->pos, 60) :
                 blend(in_convoy(k, missile_local(DETONATE)), g->pos, ramp(k, DETONATE, DETONATE + S(2)) * 2 / 3);
        look(in, in_convoy(k, eye), target, m.u);
    } else if (t < T_TURN) {
        Vec eye = in_convoy(k, vec((-300 + (k - S(34))) * FP, 190 * FP, 260 * FP));
        look(in, eye, blend(g->pos, g->npc[RAIDER_TWO].pos, 30), m.u);
    } else {
        Vec eye = in_convoy(k, vec(150 * FP, 90 * FP, 340 * FP));
        look(in, eye, g->pos, m.u);
    }
}
/* ---- Act 4: the jump ------------------------------------------------------ */
static const Key arrival_keys[] = {
    {0, -2600, 5200, -5200}, {S(8), -1500, 3600, -2900}, {S(14), -800, 2500, -1500},
    {S(20), -300, 1700, -500}, {S(25), -40, 1100, -60}, {S(29), 0, 700, 0},
    {S(33), 0, 300, 0}, {S(36), 0, 40, 0}, {S(38), 0, -60, 0}};
static void arrive(Game *g) {
    setup_system(g, play.destination);
    clear_actors(g);
    play.station = orbital_port(g);
    if (play.station < 0) play.station = 0;
    stage_world(g, play.dock_tick);
    play.dock = stage(g->ports[play.station].pos, g->ports[play.station].normal, vec(0, 0, 16384));
    g->target = play.station;
    g->body = g->ports[play.station].body;
    play.jumped = 1;
}
static void act_jump(Intro *in, Game *g, int t) {
    int k = t - T_JUMP;
    g->hyperspace = k + 1;
    if (!play.jumped && g->hyperspace > HYPER_CHARGE) arrive(g);
    if (!play.jumped) {
        ambush_hero(in, g, T_JUMP - T_AMBUSH);
        volley(in, g, k, 0, HYPER_CHARGE - 8, 18, nose(g, RAIDER_TWO), g->pos, 23, 140, 29);
    } else {
        Vec pos = place(&play.dock, PATH(arrival_keys, 0));
        hero(in, g, pos, unit(sub(place(&play.dock, PATH(arrival_keys, S(2))), pos), 16384), play.dock.u);
        star_glow(in, g, 130, 24, 23);
    }
    in->hero = 0; in->cockpit = 1;
    in->eye = g->pos; in->yaw = g->yaw; in->pitch = g->pitch; in->roll = g->roll;
    g->throttle = 0;
}
/* ---- Act 5: sanctuary ----------------------------------------------------- */
static const Key follower_keys[] = {
    {S(9), -3200, 6400, -6800}, {S(12), -2600, 5200, -5000}, {S(16), -1700, 3900, -3300},
    {S(19), -1300, 3300, -2600}, {S(21.5), -1150, 3000, -2300}};
static const Key police_one[] = {
    {S(10), 0, -100, 0}, {S(11.5), 0, 300, 0}, {S(13.5), -350, 1300, -700},
    {S(16), -800, 2400, -1500}, {S(19), -1000, 2900, -2000}, {S(22), -900, 3000, -2600},
    {S(28), 0, 3500, -4000}};
static const Key police_two[] = {
    {S(10.6), 0, -100, 0}, {S(12.1), 60, 300, 40}, {S(14), -100, 1300, -300},
    {S(16.5), -500, 2500, -1100}, {S(19.5), -900, 3200, -1800}, {S(23), -400, 3800, -2500},
    {S(28), 400, 4500, -3000}};
#define FOLLOWER_DIES S(21.5)
#define DOCK_POINT(keys, k) place(&play.dock, PATH(keys, k))
#define DOCK_VELOCITY(keys, k) path_velocity(&play.dock, keys, (int)(sizeof(keys) / sizeof(*keys)), k)
static void act_sanctuary(Intro *in, Game *g, int t) {
    const Stage *s = &play.dock;
    int k = t - T_ARRIVE, spin = (int)(k * 1024 / S(40)) & 1023;
    Vec pos = DOCK_POINT(arrival_keys, k), v = sub(DOCK_POINT(arrival_keys, k + 2), DOCK_POINT(arrival_keys, k - 2));
    Vec front = unit(v, 16384), up, bore_right, bore_up, sun = sun_from(g, pos);
    int align = ease(k, S(22), S(27));
    g->ports[play.station].rotation = spin;
    star_glow(in, g, 130, 24, 23);
    /* Wings slot between the docking collars and turn with the station. */
    bore_right = axis(s, cosi(spin + 128), 0, sini(spin + 128));
    bore_up = cross14(scale(s->u, -1, 1), bore_right);
    up = sub(s->u, scale(front, dot14(s->u, front), 16384));
    if (length(up) < 2000) up = bore_up;
    front = unit(blend(front, scale(s->u, -1, 1), align), 16384);
    up = blend(unit(up, 16384), bore_up, align);
    hero(in, g, pos, front, up);
    exhaust(in, g, k < S(20) ? 200 : 90, (uint32_t)t);
    g->throttle = k < S(20) ? 2400 : 900;
    if (k % 8 == 0 && k < S(18)) puff(g, hull(g, 8, 4, -70), vec(0, 0, 0), 6);
    /* The raider follows through, the police launch and catch it. */
    if (k >= S(9.4) && k <= FOLLOWER_DIES)
        npc(g, RAIDER_TWO, 0, RAIDER, DOCK_POINT(follower_keys, k), DOCK_VELOCITY(follower_keys, k));
    if (k == FOLLOWER_DIES) explode(g, RAIDER_TWO);
    if (k >= S(9) && k < S(10.2))
        glow(in, DOCK_POINT(follower_keys, S(9)), 10 + 300 * sini(ramp(k, S(9), S(10.2)) * 2) / 16384, 31, 27);
    if (k >= S(10) && k < S(28)) npc(g, POLICE_ONE, 3, POLICE, DOCK_POINT(police_one, k), DOCK_VELOCITY(police_one, k));
    else g->npc[POLICE_ONE].active = 0;
    if (k >= S(10.6) && k < S(28)) npc(g, POLICE_TWO, 3, POLICE, DOCK_POINT(police_two, k), DOCK_VELOCITY(police_two, k));
    else g->npc[POLICE_TWO].active = 0;
    if (g->npc[RAIDER_TWO].active) {
        volley(in, g, k, S(12), S(17), 9, nose(g, RAIDER_TWO), g->pos, 23, 70, 31);
        volley(in, g, k, S(17), FOLLOWER_DIES, 7, nose(g, POLICE_ONE), g->npc[RAIDER_TWO].pos, 13, k > S(19.5) ? 6 : 50, 37);
        volley(in, g, k, S(17.6), FOLLOWER_DIES, 8, nose(g, POLICE_TWO), g->npc[RAIDER_TWO].pos, 13, k > S(20) ? 6 : 50, 41);
    }
    if (k >= S(15.5) && k < S(15.7)) spark(in, g, hull(g, 15, 5, -50), k);
    if (t < T_FOLLOW) {
        /* The courier against the red giant. */
        Vec eye = add(pos, add(scale(sun, -(700 - k) * FP, 16384), scale(s->u, 160 * FP, 16384)));
        look(in, eye, g->pos, s->u);
        snprintf(in->caption_text, sizeof(in->caption_text), "%.24s SYSTEM", g->system.name);
        caption(in, in->caption_text, k, S(1), S(6.5));
    } else if (t < T_POLICE) {
        /* Over the commander's shoulder, looking back the way it came. */
        Vec back = unit(sub(DOCK_POINT(follower_keys, S(9)), g->pos), 16384);
        Vec eye = add(g->pos, add(scale(back, -260 * FP, 16384), scale(s->u, 70 * FP, 16384)));
        look(in, eye, blend(g->pos, DOCK_POINT(follower_keys, S(9)), 60), s->u);
    } else if (t < T_DOCK) {
        /* Riding with the second interceptor into the fight. */
        Vec p2 = DOCK_POINT(police_two, k), d = unit(DOCK_VELOCITY(police_two, k), 16384);
        Vec target = k < FOLLOWER_DIES ? DOCK_POINT(follower_keys, k) : DOCK_POINT(follower_keys, FOLLOWER_DIES);
        Vec eye = add(p2, add(scale(d, -170 * FP, 16384), scale(s->u, 45 * FP, 16384)));
        look(in, eye, blend(p2, target, 170), s->u);
    } else {
        int j = t - T_DOCK;
        Vec eye = add(g->pos, add(scale(s->u, (300 - j / 4) * FP, 16384), scale(axis(s, 1, 0, 1), 140 * FP, 16384)));
        look(in, eye, blend(g->pos, s->o, 90), axis(s, -1, 0, 1));
        if (k >= S(36) && g->docked < 0) g->docked = play.station;
        in->fade = ramp(k, S(34.5), S(37)) / 16;
    }
}
/* ---- Act 6: the title card ------------------------------------------------ */
static void act_title(Intro *in, Game *g, int t) {
    in->title = t - T_TITLE + 1;
    in->hero = 0;
    in->letterbox = 0;
    in->fade = t > INTRO_TICKS - S(1) ? ramp(t, INTRO_TICKS - S(1), INTRO_TICKS) / 16 : 0;
    g->throttle = 0;
}
/* ---- Sequencer ------------------------------------------------------------ */
static void enter(Game *g, int t) {
    if (t == T_OPEN || t == T_APPROACH) stage_world(g, play.dawn);
    if (t == T_RELAY) {
        stage_world(g, play.relay_tick);
        play.station = orbital_port(g);
        play.relay = stage(g->ports[play.station].pos, g->ports[play.station].normal,
                           cross14(g->ports[play.station].normal, sun_from(g, g->ports[play.station].pos)));
        g->target = play.station;
    }
    if (t == T_AMBUSH) {
        Vec up = g->ports[play.station].normal, o = add(g->ports[play.station].pos, scale(up, 60000LL * FP, 16384));
        play.convoy = stage(o, up, cross14(up, sun_from(g, o)));
        clear_actors(g);
        g->target = -1;
    }
}
static void step(Intro *in, Game *g) {
    int t = in->tick;
    in->beams = in->glows = 0;
    in->hero = 1; in->cockpit = in->guns = 0;
    in->fade = in->flash = 0;
    in->caption = NULL; in->caption_chars = 0;
    in->letterbox = 1; in->title = 0;
    enter(g, t);
    g->ui_clock++;
    g->hyperspace = 0;
    if (t < T_APPROACH) act_open(in, g, t);
    else if (t < T_CLIMB) act_ground(in, g, t);
    else if (t < T_RELAY) act_climb(in, g, t);
    else if (t < T_AMBUSH) act_relay(in, g, t);
    else if (t < T_JUMP) act_ambush(in, g, t);
    else if (t < T_ARRIVE) act_jump(in, g, t);
    else if (t < T_TITLE) act_sanctuary(in, g, t);
    else act_title(in, g, t);
    destruction_tick(g);
    if (!in->title) g->body = nearest_body(g, in->eye);
}
void intro_start(Intro *in, Game *g, uint32_t seed) {
    memset(in, 0, sizeof(*in));
    memset(&play, 0, sizeof(play));
    in->seed = seed;
    in->running = 1;
    game_init(g, seed);
    play.home = g->system.sector;
    /* Choose light before the first frame: the destination station's sky,
       a bright morning at Haven and a day-side pass of its Relay. */
    play.destination = pick_destination(g);
    if (orbital_port(g) >= 0)
        play.dock_tick = sunrise(g, orbital_port(g), 4000, HZ * 4U * 3600U);
    setup_system(g, play.home);
    play.dawn = sunrise(g, 0, 4000, rotation_period(&g->system.planets[g->ports[0].body]) * (uint32_t)HZ);
    play.relay_tick = orbital_port(g) >= 0 ?
        sunrise(g, orbital_port(g), 5000, HZ * 4U * 3600U) : play.dawn;
    play.station = orbital_port(g) >= 0 ? orbital_port(g) : 0;
    stage_world(g, play.dawn);
    play.ground = stage(g->ports[0].pos, g->ports[0].normal, sun_from(g, g->ports[0].pos));
    play.climb = stage(g->ports[0].pos, g->ports[0].normal, rim_gap(g));
    {
        /* The city grid uses the port's own body-fixed frame; its blocks keep
           clear of a square round the pad, so a corner of it is open ground. */
        const Planet *p = &g->system.planets[g->ports[0].body];
        int corner, best = 1 << 30;
        play.city = stage(g->ports[0].pos, g->ports[0].normal, cross14(g->ports[0].normal,
                          cross14(body_rotate(p, vec(0, 0, 16384), 0), g->ports[0].normal)));
        for (corner = 0; corner < 4; corner++) {
            Vec eye = at(&play.city, corner & 1 ? 175 : -175, 18, corner & 2 ? 175 : -175);
            int along = (int)dot14(unit(sub(eye, g->ports[0].pos), 16384), play.climb.f);
            if (along < best) { best = along; play.liftoff_eye = eye; }
        }
    }
    g->docked = -1; g->gear = 0; g->target = -1;
    g->ship = 3; /* KESTREL */
    g->cinematic = 1;
    g->page = FLIGHT;
    g->camera_mode = 0;
    step(in, g);
}
int intro_frame(Intro *in, Game *g, int ticks) {
    while (in->running && ticks-- > 0) {
        if (++in->tick >= INTRO_TICKS) { intro_finish(in, g); break; }
        step(in, g);
    }
    return in->running;
}
void intro_finish(Intro *in, Game *g) {
    in->running = 0;
    game_init(g, in->seed);
}
int intro_skippable(const Intro *in) { return in->running && in->tick >= HZ / 2; }
