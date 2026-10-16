/* SPDX-License-Identifier: Unlicense; deterministic world evidence fixtures. */
#include "astralbound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rtg;
static void write_frame(Game *g, const char *name) {
    Frame f = {320,256,1,0,{0}};
    char path[160]; int i; FILE *out;
    if (rtg) { f.w=640; f.h=480; }
    f.pixels = malloc((size_t)f.w*f.h); if (!f.pixels || !render_init(&f)) exit(1);
    render_game(g,&f);
    snprintf(path,sizeof(path),"build/evidence/environment-%s%s.ppm",name,rtg ? "-rtg" : "");
    out = fopen(path,"wb"); if (!out) exit(1);
    fprintf(out,"P6\n%d %d\n255\n",f.w,f.h);
    for (i=0;i<f.w*f.h;i++) { unsigned c=f.palette[f.pixels[i]];
        fputc(c>>16,out); fputc(c>>8,out); fputc(c,out); }
    fclose(out); free(f.pixels); render_shutdown();
    snprintf(path,sizeof(path),"build/evidence/environment-%s%s.sav",name,rtg ? "-rtg" : "");
    if (!save_game(g,path)) exit(1);
}
int main(int argc, char **argv) {
    rtg=argc>1 && !strcmp(argv[1],"--rtg");
    Game g; int a,b, found=0; Vec local={0}; Planet *p;
    game_init(&g,37); p=&g.system.planets[0]; g.docked=-1;
    for (b=-180;b<=180 && !found;b+=8) for (a=0;a<1024;a+=8) {
        local=radial(forward(a,b),p->radius+200);
        if (precipitation(p,local,g.tick)==1 && terrain_material(p,local)==2) {found=1;break;}
    }
    if (!found) return 2;
    /* Keep the real generated sun; select the illuminated side by time. */
    for (a=0;a<1024;a+=8) {
        g.tick=(uint32_t)((uint64_t)rotation_period(p)*HZ*a/1024); update_world(&g);
        Vec turned=body_rotate(p,local,0);
        if (surface_light(&g.system,p,turned)>12000) break;
    }
    local=body_rotate(p,local,0);
    g.pos=scale(add(p->center,local),FP,1); g.vel=surface_velocity(p,local);
    g.page=FLIGHT; g.target=-1; g.target_body=0; g.gear=0;
    aim_angles(forward(0,0),&g.yaw,&g.pitch);
    /* Face a tangent, slightly down, independent of planetary latitude. */
    Vec tangent=vec(-local.y,local.x,0);
    aim_angles(sub(unit(tangent,16384),unit(local,3000)),&g.yaw,&g.pitch);
    write_frame(&g,"rain-flora");
    g.pos=scale(add(p->center,radial(local,p->radius+1800)),FP,1);
    write_frame(&g,"cloud");
    g.pos=scale(add(p->center,radial(local,p->radius+4000)),FP,1);
    write_frame(&g,"above-cloud");
    found=0;
    for (b=-180;b<=180 && !found;b+=8) for (a=0;a<1024;a+=8) {
        Vec snow=body_rotate(p,radial(forward(a,b),p->radius+200),0);
        if (precipitation(p,snow,g.tick)!=2 || surface_light(&g.system,p,snow)<500) continue;
        g.pos=scale(add(p->center,snow),FP,1); g.vel=surface_velocity(p,snow);
        aim_angles(sub(unit(vec(-snow.y,snow.x,0),16384),unit(snow,3000)),&g.yaw,&g.pitch);
        write_frame(&g,"snow"); found=1; break;
    }
    if (!found) return 3;
    g.pos=scale(add(p->center,scale(radial(local,p->radius+200),-1,1)),FP,1);
    write_frame(&g,"night");
    for (b=0;b<2;b++) {
        if (b) { g.tick += rotation_period(p)*HZ/4; update_world(&g); }
        g.pos=scale(add(p->center,vec(p->radius/5,p->radius*6LL/5,p->radius*3LL/5)),FP,1);
        aim_angles(sub(scale(p->center,FP,1),g.pos),&g.yaw,&g.pitch);
        write_frame(&g,b ? "rotation-quarter" : "rotation-start");
    }
    printf("Environment previews: seed 37, tick %lu, rain/flora, cloud, above-cloud, snow, night\n",(unsigned long)g.tick);
    return 0;
}
