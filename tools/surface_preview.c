/* SPDX-License-Identifier: Unlicense; geological surface evidence fixtures. */
#include "astralbound.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    static const BodyType types[]={ROCKY,DESERT,ICE,MOON};
    static const char *names[]={"rock","dunes","ice","craters"};
    int type, mode, i;
    for(type=0;type<4;type++) for(mode=0;mode<2;mode++) {
        Game g; Frame f={320,256,0,0,{0}}; Planet *p;
        char path[160]; FILE *out; Vec local; int spin;
        game_init(&g,0x53504143); g.docked=-1; g.paused=1;
        p=&g.system.planets[0]; p->type=types[type];
        /* Identical seed, light, location and camera expose only the body
           type's geology. Keep clear of buildings and cloud layers. */
        p->atmosphere=0; p->climate=VACUUM;
        for(spin=0;spin<1024;spin+=8) {
            g.tick=(uint32_t)((uint64_t)rotation_period(p)*HZ*spin/1024);
            update_world(&g);
            if(surface_light(&g.system,p,body_rotate(p,vec(5000,p->radius,-2000),0))>12000) break;
        }
        local=body_rotate(p,vec(5000,p->radius,-2000),0);
        local=radial(local,p->radius+terrain_height(p,local)+350);
        g.pos=scale(add(p->center,local),FP,1); g.vel=surface_velocity(p,local);
        aim_angles(sub(unit(vec(-local.y,local.x,0),8000),unit(local,16384)),&g.yaw,&g.pitch);
        g.target=-1; g.target_body=0;
        f.enhanced=mode; f.pixels=malloc((size_t)f.w*f.h);
        if(!f.pixels || !render_init(&f)) return 1;
        render_game(&g,&f);
        snprintf(path,sizeof(path),"build/evidence/surface-%s-%s.ppm",names[type],mode ? "aga" : "ecs");
        out=fopen(path,"wb"); if(!out) return 1;
        fprintf(out,"P6\n%d %d\n255\n",f.w,f.h);
        for(i=0;i<f.w*f.h;i++) {
            unsigned c=f.palette[f.pixels[i]];
            fputc(c>>16,out); fputc(c>>8,out); fputc(c,out);
        }
        fclose(out); free(f.pixels); render_shutdown();
    }
    puts("Geology previews: identical location/light/seed, rock/dunes/ice/craters in ECS/AGA");
    return 0;
}
