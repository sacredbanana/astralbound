/* SPDX-License-Identifier: Unlicense; fixtures use the actual game renderer. */
#include "performance.h"
#include <string.h>
const char *performance_names[PERFORMANCE_SCENES] = {"ORBIT AND RINGS","LOW TERRAIN","DESERT TERRAIN","ICE TERRAIN",
    "SURFACE CITY","ORBITAL STATION","RAIN AND FORESTS","INSIDE CLOUDS","SNOW AND FOG","COMBAT AND DEBRIS",
    "HYPERSPACE","SYSTEM MAP","GALAXY MAP","SERVICE PORTRAITS"};
static Vec cross14(Vec a, Vec b) { return vec((a.y*b.z-a.z*b.y)/16384,(a.z*b.x-a.x*b.z)/16384,(a.x*b.y-a.y*b.x)/16384); }
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
        p->rings=1;
        g->pos=add(scale(p->center,FP,1),vec(0,(p->radius+p->atmosphere+35000LL)*FP,-60000LL*FP));
        aim_angles(sub(scale(p->center,FP,1),g->pos),&g->yaw,&g->pitch);
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
            Vec candidate=radial(forward(a,b),p->radius+200);
            if(precipitation(p,candidate,g->tick)==(scene==8 ? 2 : 1) &&
               (scene==8 || terrain_material(p,candidate)==2)) { local=candidate; found=1; break; }
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
        aim_angles(sub(unit(vec(-local.y,local.x,0),16384),unit(local,3000)),&g->yaw,&g->pitch);
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
        g->laser=1; g->combat_target=2;
    }
    if(scene==10) g->hyperspace=HYPER_CHARGE+HZ*2;
    if(scene==11) { g->page=SYSTEM_MAP; g->system_display=1; }
    if(scene==12) g->page=MAP;
    if(scene==13) { g->docked=0; g->page=SERVICES; g->service_selection=0; }
}
void performance_begin(Performance *p, Game *g, const Settings *s, int selected) {
    memset(p,0,sizeof(*p)); p->report.settings=*s;
    p->selected=selected; p->scene=selected<0 ? 0 : selected; p->warmup=1;
    performance_scene(g,p->scene);
}
void performance_animate(Performance *p, Game *g) {
    /* A fixed number of camera steps per rendered frame keeps workloads
       comparable when one preset is much faster than another. No flight ticks. */
    p->step++; g->ui_clock=(unsigned)p->step*2;
    if(p->scene<=3) g->yaw=(p->step/4)&31;
    if(p->scene==10) g->hyperspace=HYPER_CHARGE+1+(p->step*2)%HYPER_TUNNEL;
    if(p->scene==9) {
        int i; for(i=0;i<MAX_SMOKE;i++) g->smoke[i].age=(i+p->step)%50;
        for(i=0;i<4;i++) g->blasts[i].age=4+(i*7+p->step)%36;
    }
}
int performance_record(Performance *p, Game *g, uint64_t render_us, uint64_t output_us, uint64_t total_us) {
    PerformanceResult *r=&p->report.result[p->scene];
    if(p->warmup) { p->warmup=0; return 0; }
    r->frames++; r->render_us+=render_us; r->output_us+=output_us; r->total_us+=total_us;
    if(total_us>r->slowest_us) r->slowest_us=total_us>UINT32_MAX ? UINT32_MAX : (uint32_t)total_us;
    if((r->total_us>=2000000 && r->frames>=4) || r->frames>=180) {
        if(p->selected>=0 || p->scene==PERFORMANCE_SCENES-1) { p->report.complete=1; return 1; }
        p->scene++; p->warmup=1; p->step=0; performance_scene(g,p->scene); return -1;
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
