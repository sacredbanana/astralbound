/* SPDX-License-Identifier: Unlicense
   Bounded visual weapon flight. No allocations, random state or damage here. */
#include "astralbound.h"
#include <string.h>
void weapons_reset(Game *g) {
    memset(g->bolts,0,sizeof(g->bolts));
    g->bolt_next=g->bolt_count=0;
}
void weapon_bolt(Game *g, Vec start, Vec end, Vec drift, int colour) {
    WeaponBolt *b=&g->bolts[g->bolt_next++ % MAX_WEAPON_BOLTS];
    int64_t distance=length(sub(end,start))/FP;
    int ticks=(int)(distance*HZ/16000);
    if(ticks<2) ticks=2;
    if(ticks>HZ/2) ticks=HZ/2;
    if(!b->ticks) g->bolt_count++;
    *b=(WeaponBolt){start,end,drift,0,ticks,colour};
    if(g->bolt_next>=MAX_WEAPON_BOLTS*2) g->bolt_next-=MAX_WEAPON_BOLTS;
}
void weapons_tick(Game *g) {
    int i;
    if(!g->bolt_count) return;
    for(i=0;i<MAX_WEAPON_BOLTS;i++) {
        WeaponBolt *b=&g->bolts[i]; Vec move;
        if(!b->ticks) continue;
        if(++b->age>=b->ticks) { b->ticks=0; g->bolt_count--; continue; }
        move=scale(b->drift,1,HZ);
        b->start=add(b->start,move); b->end=add(b->end,move);
    }
}
void weapons_rebase(Game *g, Vec delta, Vec velocity) {
    int i;
    if(!g->bolt_count) return;
    for(i=0;i<MAX_WEAPON_BOLTS;i++) if(g->bolts[i].ticks) {
        WeaponBolt *b=&g->bolts[i];
        b->start=add(b->start,delta); b->end=add(b->end,delta);
        b->drift=add(b->drift,velocity);
    }
}
void weapon_player(Game *g, Vec direction, int distance) {
    int yaw,pitch,side;
    Vec right,up,muzzle,aim;
    yaw=g->yaw; pitch=g->pitch;
    if(g->camera_mode>=3) view_angles(g,&yaw,&pitch);
    right=vec(cosi(yaw),0,-sini(yaw));
    if(g->camera_mode<3) {
        up=vec(-(int64_t)sini(pitch)*sini(yaw)/16384,cosi(pitch),
               -(int64_t)sini(pitch)*cosi(yaw)/16384);
        right=sub(scale(right,cosi(g->roll),16384),scale(up,sini(g->roll),16384));
    }
    if(distance<ship_specs[g->ship].depth+40) distance=ship_specs[g->ship].depth+40;
    aim=add(g->pos,scale(direction,(int64_t)distance*FP,16384));
    muzzle=add(g->pos,scale(direction,(ship_specs[g->ship].depth+12LL)*FP,16384));
    for(side=-1;side<=1;side+=2)
        weapon_bolt(g,add(muzzle,scale(right,side*(int64_t)(ship_specs[g->ship].width+4)*FP/2,16384)),
                    aim,g->vel,12);
}
void weapon_npc(Game *g, int index) {
    const Npc *n=&g->npc[index];
    Vec direction=unit(sub(g->pos,n->pos),16384);
    Vec muzzle=add(n->pos,scale(direction,(ship_specs[g->npc_ship[index]].depth+8LL)*FP,16384));
    weapon_bolt(g,muzzle,g->pos,n->vel,g->npc_role[index]==POLICE ? 13 : 11);
}
