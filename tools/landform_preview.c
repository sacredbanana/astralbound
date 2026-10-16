/* SPDX-License-Identifier: Unlicense; actual generated-world terrain fixtures. */
#include "space.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void write_frame(const Frame *f, const char *path) {
    FILE *out=fopen(path,"wb"); int i;
    if (!out) exit(1);
    fprintf(out,"P6\n%d %d\n255\n",f->w,f->h);
    for (i=0;i<f->w*f->h;i++) {
        unsigned c=f->palette[f->pixels[i]];
        fputc(c>>16,out); fputc(c>>8,out); fputc(c,out);
    }
    fclose(out);
}
int main(void) {
    static const BodyType types[]={ROCKY,OCEAN,DESERT,ICE,MOON};
    static const char *names[]={"mountains","coasts","canyons","rifts","craters"};
    int type,mode,angle;
    for (type=0;type<5;type++) {
        Game g; Planet *p=NULL; int seed,body=0,u,v,bestu=0,bestv=0,best=0;
        Vec site,eye,look; char path[160];
        for (seed=1;seed<100;seed++) {
            game_init(&g,(uint32_t)seed);
            for (body=0;body<g.system.count;body++) if (g.system.planets[body].type==types[type]) break;
            if (body<g.system.count && (types[type]!=OCEAN ||
                terrain_height(&g.system.planets[body],vec(64000,g.system.planets[body].radius,0))>600)) {
                p=&g.system.planets[body];break;
            }
        }
        if (!p) return 1;
        /* Select a pronounced natural slope in a 160 km region, away from
           pads. These are real seeds/types, so the commander reloads natively. */
        for (v=-80000;v<=80000;v+=2000) for (u=8000;u<=168000;u+=2000) {
            Vec a=body_rotate(p,vec(u,p->radius,v),0);
            Vec b=body_rotate(p,vec(u+6000,p->radius,v+6000),0);
            int score=abs(terrain_height(p,a)-terrain_height(p,b));
            if (score>best) {best=score;bestu=u;bestv=v;}
        }
        site=vec(bestu,p->radius,bestv);
        for (angle=0;angle<1024;angle+=8) {
            if (cosi(angle)<10000) continue;
            g.tick=(uint32_t)((uint64_t)rotation_period(p)*HZ*angle/1024);
            update_world(&g);
            if (surface_light(&g.system,p,body_rotate(p,site,0))>6000) break;
        }
        g.body=g.frame_body=g.target_body=body; g.docked=-1; g.target=-1;
        g.paused=1; g.ui_visible=0; memset(g.npc,0,sizeof(g.npc));
        printf("%s: seed %d body %d slope %d m / 8.5 km, site %d,%d\n",names[type],seed,body,best,bestu,bestv);
        for (angle=0;angle<2;angle++) {
            Vec cam=add(site,vec(angle ? 18000 : -18000,0,0));
            eye=body_rotate(p,cam,0);
            eye=radial(eye,p->radius+terrain_height(p,eye)+2200);
            look=body_rotate(p,site,0);
            look=radial(look,p->radius+terrain_height(p,look)+200);
            g.pos=scale(add(p->center,eye),FP,1); g.vel=surface_velocity(p,eye);
            aim_angles(sub(look,eye),&g.yaw,&g.pitch);
            for (mode=0;mode<3;mode++) {
                Frame f={320,256,0,0,{0}}; clock_t start; int n;
                f.enhanced=mode!=0; if (mode==2) {f.w=640;f.h=480;}
                f.pixels=malloc((size_t)f.w*f.h);
                if (!f.pixels || !render_init(&f)) return 1;
                render_game(&g,&f); start=clock();
                for(n=0;n<3;n++) render_game(&g,&f);
                printf("  view %d mode %d %.2f ms/frame\n",angle,mode,(double)(clock()-start)*1000/CLOCKS_PER_SEC/3);
                snprintf(path,sizeof(path),"build/evidence/landform-%s-%d-%d.ppm",names[type],angle,mode);
                write_frame(&f,path); free(f.pixels);render_shutdown();
            }
            snprintf(path,sizeof(path),"build/evidence/landform-%s-%d.sav",names[type],angle);
            if (!save_game(&g,path)) return 1;
        }
    }
    return 0;
}
