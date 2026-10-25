/* SPDX-License-Identifier: Unlicense; deterministic polar-night evidence. */
#ifndef ASTRALBOUND_AURORA_FIXTURE_H
#define ASTRALBOUND_AURORA_FIXTURE_H
#include "astralbound.h"
#include <string.h>
static void aurora_fixture(Game *g, int surface, int pole) {
    Planet *p;
    Vec up, front, right, screen_up;
    int unused;
    game_init(g,37);
    g->docked = g->frame_body = g->target = -1;
    g->target_body = g->body = 0;
    g->nports = 0; g->system.count = g->system.stars = 1;
    g->ui_visible = 0; g->page = FLIGHT; g->tick = 1200;
    memset(g->npc,0,sizeof(g->npc));
    p = &g->system.planets[0];
    memset(p,0,sizeof(*p));
    p->seed = 37; p->parent = -1; p->radius = 6400000;
    p->atmosphere = 80000; p->type = ROCKY; p->climate = TEMPERATE;
    strcpy(p->name,"Aurora fixture");
    g->system.star[0].center = vec(AU_METRES,AU_METRES,0);
    g->system.star[0].radius = 696000000;
    g->system.star[0].type = ST_G;
    if (!surface) {
        g->pos = scale(vec(0,-p->radius,pole * p->radius * 2LL),FP,1);
        aim_angles(scale(g->pos,-1,1),&g->yaw,&g->pitch);
        g->roll = 0;
        return;
    }
    up = unit(vec(pole * 3500,-7000,pole * 14500),16384);
    g->pos = scale(radial(up,p->radius + terrain_height(p,up) + 300),FP,1);
    front = unit(sub(vec(0,0,pole * 16384),scale(up,pole * up.z,16384)),16384);
    front = add(front,scale(up,3500,16384));
    aim_angles(front,&g->yaw,&g->pitch);
    right = vec(cosi(g->yaw),0,-sini(g->yaw));
    screen_up = vec(-(int64_t)sini(g->pitch)*sini(g->yaw)/16384,cosi(g->pitch),
                    -(int64_t)sini(g->pitch)*cosi(g->yaw)/16384);
    aim_angles(vec((up.x*right.x+up.y*right.y+up.z*right.z)/16384,0,(up.x*screen_up.x+up.y*screen_up.y+up.z*screen_up.z)/16384),&g->roll,&unused);
}
#endif
