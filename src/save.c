/* SPDX-License-Identifier: Unlicense
   SPC1: magic, generator version, payload length, FNV1a checksum (all BE32),
   then explicit BE32/BE64 fields. Never serialize C structure layout. */
#include "space.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct {
    unsigned char data[8192];
    size_t pos;
    int read, error;
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
static void payload(Stream *s, Game *g) {
    int i;
    Sector sector_id = g->system.sector;
    FIELD(g->seed);
    FIELD(g->tick);
    sector(s, &sector_id);
    if (s->read)
        setup_system(g, sector_id);
    vector(s, &g->pos);
    vector(s, &g->vel);
    FIELD(g->yaw);
    FIELD(g->pitch);
    FIELD(g->target);
    FIELD(g->docked);
    FIELD(g->gear);
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
        FIELD(g->stock[i]);
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
    FIELD(g->camera_mode);
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
static int valid(const Game *g) {
    int i;
    if (!good_sector(g->system.sector) || !good_sector(g->selected) ||
        !good_vec(g->pos, 100000000000LL) || !good_vec(g->vel, 10000000) || g->ship < 0 ||
        g->ship > 2 || g->target < 0 || g->target >= port_count(g) || g->docked < -2 ||
        g->docked >= port_count(g) || g->hull < 0 || g->hull > 100 || g->shield < 0 ||
        g->shield > 100 || g->fuel < 0 || g->fuel > 100 || g->heat < 0 || g->heat > 100 ||
        g->credits < 0 || g->credits > 100000000 || g->pitch < -250 || g->pitch > 250 ||
        g->yaw < 0 || g->yaw > 1023 || g->page < 0 || g->page > HELP || g->selection < 0 ||
        g->selection >= MAX_MISSIONS || g->upgrade < 0 || g->upgrade > 3 || g->throttle < 0 ||
        g->throttle > 4000 || g->camera_mode < 0 || g->camera_mode > 2)
        return 0;
    if (g->warp != 1 && g->warp != 4 && g->warp != 16 && g->warp != 64 && g->warp != 256)
        return 0;
    for (i = 0; i < GOODS; i++)
        if (g->cargo[i] < 0 || g->cargo[i] > 140 || g->stock[i] < 0 || g->stock[i] > 10000)
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
        if (!good_vec(g->npc[i].pos, 100000000000LL) || !good_vec(g->npc[i].vel, 10000000) ||
            g->npc[i].mission < -1 || g->npc[i].mission >= MAX_MISSIONS)
            return 0;
    return cargo_used(g) <= ship_capacity[g->ship];
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
    payload(&s, &copy);
    len = s.pos;
    if (s.error)
        return 0;
    s.pos = 0;
    word(&s, 0x53504331);
    word(&s, GENERATOR_VERSION);
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
    uint32_t size, crc;
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
    if (word(&s, 0) != 0x53504331 || word(&s, 0) != GENERATOR_VERSION)
        return 0;
    size = word(&s, 0);
    crc = word(&s, 0);
    if (size != len - 16 || crc != checksum(s.data + 16, len - 16))
        return 0;
    game_init(&copy, 1);
    payload(&s, &copy);
    if (s.error || s.pos != len || !valid(&copy))
        return 0;
    copy.body = nearest_body(&copy, copy.pos);
    copy.paused = 0;
    copy.effective_warp = 1;
    *g = copy;
    snprintf(g->message, sizeof(g->message), "COMMANDER RESTORED");
    return 1;
}
