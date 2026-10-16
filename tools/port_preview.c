/* SPDX-License-Identifier: Unlicense; reproducible port approach/lighting views. */
#include "astralbound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void output(Frame *f, const char *path) {
    FILE *out=fopen(path,"wb");
    int i;
    if(!out) exit(1);
    fprintf(out,"P6\n%d %d\n255\n",f->w,f->h);
    for(i=0;i<f->w*f->h;i++) {
        uint32_t c=f->palette[f->pixels[i]];
        fputc(c>>16,out); fputc(c>>8,out); fputc(c,out);
    }
    fclose(out);
}
int main(void) {
    static Game g;
    int mode, scene, phase;
    for(mode=0;mode<3;mode++) for(scene=0;scene<4;scene++) {
        Frame f={320,256,0,NULL,{0}};
        const Port *port;
        Vec up,right,front,offset;
        int i=0;
        char path[160];
        game_init(&g,0x53504143);
        if(scene) for(i=0;i<g.nports;i++)
            if(!g.ports[i].surface && hash32(g.system.seed^(unsigned)i*7919U)%3==(unsigned)(scene-1)) break;
        if(i==g.nports) return 1;
        g.docked=-1; g.target=i; g.resource_target=-1;
        g.ui_visible=0;
        memset(g.npc,0,sizeof(g.npc));
        port=&g.ports[i]; up=port->normal;
        right=unit(vec(up.y,-up.x,0),16384);
        if(!length(right)) right=vec(16384,0,0);
        front=unit(vec((right.y*up.z-right.z*up.y)/16384,
                       (right.z*up.x-right.x*up.z)/16384,
                       (right.x*up.y-right.y*up.x)/16384),16384);
        /* Three orbital installations expose the solar wings and undercarriage. */
        if(scene) g.ports[i].rotation=(scene-1)*85;
        offset=add(scale(up,scene ? 680 : 470,16384),
                   add(scale(front,scene ? -1150 : -1000,16384),scale(right,scene ? 650 : 440,16384)));
        g.pos=add(port->pos,scale(offset,FP,1));
        aim_angles(sub(add(port->pos,scale(up,50*FP,16384)),g.pos),&g.yaw,&g.pitch);
        f.enhanced=mode!=0;
        if(mode==2) { f.w=640; f.h=480; }
        f.pixels=malloc((size_t)f.w*f.h);
        if(!f.pixels || !render_init(&f)) return 1;
        for(phase=0;phase<((mode==2 && (scene==0 || scene==2)) ? 16 : 2);phase++) {
            g.ui_clock=(unsigned)phase*((mode==2 && (scene==0 || scene==2)) ? 4 : 16);
            render_game(&g,&f);
            snprintf(path,sizeof(path),"build/evidence/port-%s-%d-%02d.ppm",
                     mode==2 ? "rtg" : mode ? "aga" : "ecs",scene,phase);
            output(&f,path);
        }
        free(f.pixels); render_shutdown();
    }
    puts("Port approaches: surface and three orbital classes, animated lights, ECS/AGA/RTG");
    return 0;
}
