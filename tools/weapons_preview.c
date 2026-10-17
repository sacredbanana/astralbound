/* SPDX-License-Identifier: Unlicense; main-game combat, with real firing ticks. */
#include "astralbound.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
int main(void) {
    Frame f={320,200,0,NULL,{0}};
    Settings s;
    int mode,t,i;
    for(mode=0;mode<3;mode++) {
        f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : mode==1 ? 256 : 200; f.enhanced=mode!=0;
        settings_defaults(&s,1); settings_preset(&s,mode ? QUALITY_HIGH : QUALITY_LOW);
        render_graphics(&s.gfx); f.pixels=malloc((size_t)f.w*f.h);
        if(!f.pixels || !render_init(&f)) return 1;
        game_init(&g,0x53504143); g.docked=g.frame_body=-1; g.page=FLIGHT;
        g.pos=vec(0,AU_METRES*FP,0); g.vel=vec(0,0,0); g.yaw=g.pitch=g.roll=0;
        g.ship=3; g.gear=0; g.tick=5; g.combat_target=0; g.missiles=2;
        memset(g.npc,0,sizeof(g.npc));
        for(i=0;i<4;i++) {
            g.npc[i]=(Npc){add(g.pos,vec((i==3 ? 0 : (i-1)*650LL)*FP,(i%2)*130*FP,(1500+i*500LL)*FP)),
                          vec((i%2 ? -40 : 40)*FP,0,0),1000,1,1,-1,0};
            g.npc_role[i]=i==2 ? POLICE : RAIDER; g.npc_ship[i]=i+1; g.npc_port[i]=-1;
        }
        if(!fire_missile(&g)) return 2;
        for(t=0;t<64;t++) {
            char path[100]; FILE *out;
            game_tick(&g,(Controls){.fire=1});
            /* Render the same combat through both normal gameplay cameras. */
            g.camera_mode=t<32 ? 2 : 0;
            render_game(&g,&f);
            snprintf(path,sizeof(path),"build/evidence/weapons-%d-%02d.ppm",mode,t);
            out=fopen(path,"wb"); if(!out) return 1;
            fprintf(out,"P6\n%d %d\n255\n",f.w,f.h);
            for(i=0;i<f.w*f.h;i++) {
                uint32_t c=f.palette[f.pixels[i]];
                fputc(c>>16,out); fputc(c>>8,out); fputc(c,out);
            }
            fclose(out);
        }
        free(f.pixels); render_shutdown();
    }
    return 0;
}
