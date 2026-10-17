/* SPDX-License-Identifier: Unlicense; fixtures use the actual game renderer. */
#include "performance.h"
#include <string.h>
#include <stdlib.h>
const char *performance_names[PERFORMANCE_SCENES] = {"ORBIT AND RINGS","LOW TERRAIN","DESERT TERRAIN","ICE TERRAIN",
    "SURFACE CITY","ORBITAL STATION","RAIN AND FORESTS","INSIDE CLOUDS","SNOW AND FOG","COMBAT AND DEBRIS",
    "HYPERSPACE","SYSTEM MAP","GALAXY MAP","SERVICE PORTRAITS"};
static Vec cross14(Vec a, Vec b) { return vec((a.y*b.z-a.z*b.y)/16384,(a.z*b.x-a.x*b.z)/16384,(a.x*b.y-a.y*b.x)/16384); }
static int64_t dot14(Vec a, Vec b) { return (a.x*b.x+a.y*b.y+a.z*b.z)/16384; }
static Vec weather_forward(Vec local) {
    Vec tangent=vec(-local.y,local.x,0);
    if(length(tangent)<100) tangent=vec(0,-local.z,local.y);
    return unit(tangent,16384);
}
/* Match the renderer's yaw/pitch/roll axes to a local planetary horizon.
   A world-level roll of zero can put the ground above an arbitrary latitude. */
static void weather_attitude(Game *g, Vec local, Vec direction) {
    Vec up=unit(local,16384),front,right0,up0; int unused;
    front=unit(sub(direction,scale(up,dot14(direction,up),16384)),16384);
    front=unit(sub(front,scale(up,1400,16384)),16384); /* slight downward view */
    aim_angles(front,&g->yaw,&g->pitch);
    right0=vec(cosi(g->yaw),0,-sini(g->yaw));
    up0=vec(-(int64_t)sini(g->pitch)*sini(g->yaw)/16384,cosi(g->pitch),
            -(int64_t)sini(g->pitch)*cosi(g->yaw)/16384);
    aim_angles(vec(dot14(up,right0),0,dot14(up,up0)),&g->roll,&unused);
}
static Vec weather_position(Vec center, Vec direction, int offset) {
    return radial(add(center,scale(direction,offset,16384)),length(center));
}
static int weather_route(const Planet *p, Vec local, uint32_t tick, int rain) {
    Vec direction=weather_forward(local); int i;
    /* Keep the entire five-second route in the requested precipitation and
       clear of terrain, rather than sampling a storm only at its midpoint. */
    for(i=-2;i<=2;i++) {
        Vec at=weather_position(local,direction,i*225);
        if(precipitation(p,at,tick)!=rain ||
           length(at)-p->radius-terrain_height(p,at)<150) return 0;
    }
    return 1;
}
static void orbit_camera(Game *g, uint64_t elapsed) {
    const Planet *p=&g->system.planets[g->body];
    Vec sun=sub(g->system.star[0].center,p->center),offset;
    int angle;
    if(elapsed>PERFORMANCE_MIN_US) elapsed=PERFORMANCE_MIN_US;
    angle=64+(int)(elapsed*48/PERFORMANCE_MIN_US);
    /* The outer ring reaches two radii. Six radii of stand-off frames the
       whole disc and rings against space, including below the test title.
       Stay on the lit side and re-aim as the camera moves around the body. */
    offset=vec((sun.x<0 ? -1LL : 1LL)*sini(angle)*p->radius*6/16384,
               (sun.y<0 ? -1LL : 1LL)*p->radius,
               (sun.z<0 ? -1LL : 1LL)*cosi(angle)*p->radius*6/16384);
    g->pos=scale(add(p->center,offset),FP,1);
    aim_angles(scale(offset,-1,1),&g->yaw,&g->pitch);
    g->roll=0;
}
static void combat_weapons(Game *g, uint64_t tick) {
    int i;
    weapons_reset(g);
    weapon_player(g,forward(g->yaw,g->pitch),5000);
    for(i=0;i<8;i++) {
        weapon_npc(g,i);
        if(i<4) weapon_bolt(g,g->npc[i].pos,g->npc[7-i].pos,g->npc[i].vel,11);
    }
    for(i=0;i<MAX_WEAPON_BOLTS;i++) if(g->bolts[i].ticks)
        g->bolts[i].age=(int)((tick+i*3)%g->bolts[i].ticks);
    for(i=0;i<2;i++) {
        Vec start=add(g->pos,vec((i ? 320 : -350)*FP,80*FP,300*FP));
        Vec direction=unit(sub(g->npc[7-i].pos,start),16384);
        g->projectiles[i]=(Missile){add(start,scale(direction,(int64_t)(tick%64)*15*FP,16384)),
            add(g->npc[7-i].vel,scale(direction,900*FP,16384)),1,7-i,HZ*20};
    }
    g->tick=(uint32_t)tick;
}
void performance_scene(Game *g, int scene) {
    Planet *p; int i;
    game_init(g,scene>=6 && scene<=8 ? 37 : 0x53504143);
    g->docked=-1; g->page=FLIGHT; g->gear=0; g->ui_visible=0;
    g->target=-1; g->target_body=0; g->body=0;
    memset(g->npc,0,sizeof(g->npc));
    p=&g->system.planets[0];
    g->pos=add(scale(p->center,FP,1),vec(0,(p->radius+220LL)*FP,-900LL*FP));
    g->pitch=-35;
    if(scene==0) {
        for(i=0;i<g->system.count;i++) if(g->system.planets[i].rings) break;
        if(i==g->system.count) i=0;
        g->body=g->target_body=g->frame_body=i;
        g->system.planets[i].rings=1;
        orbit_camera(g,0);
    }
    if(scene==2 || scene==3) { p->type=scene==2 ? DESERT : ICE; p->seed^=(unsigned)scene*7919U; }
    if(scene>=1 && scene<=3) {
        Vec local=sub(scale(g->pos,1,FP),p->center);
        g->pos=scale(add(p->center,radial(local,(int64_t)p->radius+terrain_height(p,local)+220)),FP,1);
    }
    if(scene==4 || scene==5) {
        const Port *port; Vec right,front,offset;
        i=0;
        if(scene==5) for(i=0;i<g->nports;i++) if(!g->ports[i].surface) break;
        if(i==g->nports) i=0;
        port=&g->ports[i]; right=unit(vec(port->normal.y,-port->normal.x,0),16384);
        if(!length(right)) right=vec(16384,0,0);
        front=cross14(right,port->normal);
        offset=add(scale(port->normal,scene==5 ? 680 : 470,16384),add(scale(front,-1150,16384),scale(right,650,16384)));
        g->pos=add(port->pos,scale(offset,FP,1)); g->body=port->body; g->target=i;
        aim_angles(sub(port->pos,g->pos),&g->yaw,&g->pitch);
    }
    if(scene>=6 && scene<=8) {
        int a,b,found=0; Vec local=vec(0,p->radius+200,0);
        for(b=-180;b<=180 && !found;b+=8) for(a=0;a<1024;a+=8) {
            Vec direction=forward(a,b);
            int height=terrain_height(p,direction)+300;
            if(height<350) height=350;
            if(height>(scene==8 ? 1650 : 1300)) continue;
            Vec candidate=radial(direction,p->radius+height);
            if(precipitation(p,candidate,g->tick)==(scene==8 ? 2 : 1) &&
               (scene==8 || terrain_material(p,candidate)==2) &&
               surface_light(&g->system,p,candidate)>6000 &&
               weather_route(p,candidate,g->tick,scene==8 ? 2 : 1)) { local=candidate; found=1; break; }
        }
        if(scene==7) {
            Vec fixed=body_rotate(p,local,1),at=fixed;
            int face,u,v,du,dv,best=0;
            for(face=0;face<6;face++) {
                if(!cloud_face_cell(p,face,fixed,(int64_t)cloud_spacing(p)*3,&u,&v)) continue;
                for(dv=-2;dv<=2;dv++) for(du=-2;du<=2;du++) {
                    const Cloud *c=cloud_at(p,face,u+du,v+dv,g->tick);
                    if(c && c->puff[0].radius>best) {
                        const CloudPuff *f=&c->puff[0]; best=f->radius;
                        at=add(c->base,add(scale(c->east,f->x,16384),add(scale(c->north,f->y,16384),scale(c->up,f->z,16384))));
                    }
                }
            }
            local=body_rotate(p,at,0);
        }
        g->pos=scale(add(p->center,local),FP,1);
        weather_attitude(g,local,weather_forward(local));
    }
    if(scene==9) {
        Vec front=forward(0,0);
        g->pos=vec(0,AU_METRES*FP,0); g->yaw=g->pitch=0;
        for(i=0;i<8;i++) {
            Vec at=add(g->pos,vec((i-4LL)*170*FP,((i%3)-1)*90LL*FP,1000LL*FP+i*140LL*FP));
            g->npc[i]=(Npc){at,scale(front,100*FP,16384),100,1,1,-1,0}; g->npc_ship[i]=i%SHIPS;
        }
        for(i=0;i<MAX_SMOKE;i++) g->smoke[i]=(Smoke){add(g->pos,vec((i%12-6)*60LL*FP,(i/12-4)*40LL*FP,(500+i*12LL)*FP)),vec(0,0,0),1,i%40,-1,12+(i%15),hash32((unsigned)i)};
        for(i=0;i<4;i++) g->blasts[i]=(Blast){add(g->pos,vec((i-2)*300LL*FP,0,(1400+i*300LL)*FP)),vec(0,0,0),1,12+i*4,-1,100,hash32((unsigned)i)};
        g->combat_target=2;
        combat_weapons(g,0);
    }
    if(scene==10) g->hyperspace=HYPER_CHARGE+HZ*2;
    if(scene==11) { g->page=SYSTEM_MAP; g->system_display=1; }
    if(scene==12) g->page=MAP;
    if(scene==13) { g->docked=0; g->page=SERVICES; g->service_selection=0; }
}
static void performance_prepare(Performance *p, Game *g) {
    performance_scene(g,p->scene);
    p->flight_tick=g->tick;
    if(p->scene>=6 && p->scene<=8) {
        const Planet *body=&g->system.planets[g->body];
        p->flight_center=sub(scale(g->pos,1,FP),body->center);
        p->flight_forward=weather_forward(p->flight_center);
        p->flight_span=450;
        if(p->scene==7) {
            static const int spans[]={1600,2400,3200,4800};
            int angle,span,best=INT32_MAX;
            Vec side=cross14(unit(p->flight_center,16384),p->flight_forward),best_direction=p->flight_forward;
            /* Cross the core from thinner air to thinner air, preferring
               short routes and keeping the camera above the terrain. */
            for(span=0;span<4 && best;span++) for(angle=0;angle<1024;angle+=128) {
                Vec direction=add(scale(p->flight_forward,cosi(angle),16384),scale(side,sini(angle),16384));
                int i,clear=1,a,b,score;
                for(i=-2;i<=2;i++) {
                    Vec at=weather_position(p->flight_center,direction,spans[span]*i/2);
                    if(length(at)-body->radius-terrain_height(body,at)<150) { clear=0; break; }
                }
                if(!clear) continue;
                a=cloud_density(body,weather_position(p->flight_center,direction,-spans[span]),g->tick);
                b=cloud_density(body,weather_position(p->flight_center,direction,spans[span]),g->tick);
                score=(a>b ? a : b)*513+a+b;
                if(score<best) { best=score; best_direction=direction; p->flight_span=spans[span]; }
                if(!best) break;
            }
            p->flight_forward=best_direction;
        }
    }
}
void performance_begin(Performance *p, Game *g, const Settings *s, int selected) {
    memset(p,0,sizeof(*p)); p->report.settings=*s;
    p->selected=selected; p->scene=selected<0 ? 0 : selected; p->warmup=1; p->audio_scene=-1;
    performance_prepare(p,g);
}
void performance_animate(Performance *p, Game *g) {
    /* Weather fly-throughs use measured wall time so their speed and length
       stay the same across machines and presets. No flight physics is run. */
    p->step++; g->ui_clock=(unsigned)p->step*2;
    if(p->scene>=6 && p->scene<=8) {
        const Planet *body=&g->system.planets[g->body];
        uint64_t elapsed=p->report.result[p->scene].total_us;
        int offset;
        Vec local,direction,up;
        if(elapsed>PERFORMANCE_MIN_US) elapsed=PERFORMANCE_MIN_US;
        offset=-p->flight_span+(int)(elapsed*(unsigned)p->flight_span*2/PERFORMANCE_MIN_US);
        local=weather_position(p->flight_center,p->flight_forward,offset);
        up=unit(local,16384);
        direction=unit(sub(p->flight_forward,scale(up,dot14(p->flight_forward,up),16384)),16384);
        g->pos=scale(add(body->center,local),FP,1);
        g->tick=p->flight_tick+(uint32_t)(elapsed*HZ/1000000);
        g->ui_clock=(uint32_t)(elapsed*HZ/1000000);
        g->vel=add(surface_velocity(body,local),scale(direction,p->flight_span*2LL*FP/5,16384));
        weather_attitude(g,local,direction);
    }
    if(p->scene==0) orbit_camera(g,p->report.result[0].total_us);
    else if(p->scene<=3) g->yaw=(p->step/4)&31;
    if(p->scene==10) {
        uint64_t ticks=p->report.result[10].total_us*HZ/1000000;
        if(ticks>=HYPER_TUNNEL) ticks=HYPER_TUNNEL-1;
        g->hyperspace=HYPER_CHARGE+1+(int)ticks;
    }
    if(p->scene==9) {
        int i; uint64_t tick=p->report.result[9].total_us*HZ/1000000;
        combat_weapons(g,tick);
        for(i=0;i<MAX_SMOKE;i++) g->smoke[i].age=(i+p->step)%50;
        for(i=0;i<4;i++) g->blasts[i].age=4+(i*7+p->step)%36;
    }
}
int performance_audio(Performance *p, SpaceAudio *sound, const Game *g) {
    if(p->audio_scene!=p->scene) {
        /* A new fixture is not a journey between star systems. Discard the
           previous scene's voices and synchronize its seed/docking history. */
        audio_init(sound,g);
        audio_observe(sound,g);
        p->audio_scene=p->scene;
        if(p->scene==10) {
            sound->effect=SOUND_JUMP;
            sound->effect_pos=0;
            return SOUND_JUMP;
        }
        return SOUND_NONE;
    }
    return audio_observe(sound,g);
}
int performance_record(Performance *p, Game *g, uint64_t render_us, uint64_t output_us, uint64_t total_us) {
    PerformanceResult *r=&p->report.result[p->scene];
    if(p->warmup) { p->warmup=0; return 0; }
    r->frames++; r->render_us+=render_us; r->output_us+=output_us; r->total_us+=total_us;
    if(total_us>r->slowest_us) r->slowest_us=total_us>UINT32_MAX ? UINT32_MAX : (uint32_t)total_us;
    if(r->total_us>=PERFORMANCE_MIN_US && r->frames>=4) {
        if(p->selected>=0 || p->scene==PERFORMANCE_SCENES-1) { p->report.complete=1; return 1; }
        p->scene++; p->warmup=1; p->step=0; performance_prepare(p,g); return -1;
    }
    return 0;
}

void performance_print(FILE *out, const PerformanceReport *report) {
    int i;
    fprintf(out,"Astralbound display benchmark: %s / %s / %dx%d / %s\n",
            video_names[report->settings.mode],quality_names[report->settings.preset],
            report->width,report->height,report->complete ? "complete" : "partial");
    fprintf(out,"FPS includes display output and synchronization on this machine/configuration.\n");
    for(i=0;i<PERFORMANCE_SCENES;i++) {
        const PerformanceResult *r=&report->result[i];
        unsigned fps=r->total_us ? (unsigned)((uint64_t)r->frames*10000000/r->total_us) : 0;
        if(!r->frames) continue;
        fprintf(out,"%s: %u.%u FPS; render %lu us; output %lu us; max %lu ms; %u frames\n",
                performance_names[i],fps/10,fps%10,(unsigned long)(r->render_us/r->frames),
                (unsigned long)(r->output_us/r->frames),(unsigned long)(r->slowest_us/1000),r->frames);
    }
}
