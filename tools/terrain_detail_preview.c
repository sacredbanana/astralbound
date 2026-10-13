/* SPDX-License-Identifier: Unlicense; repeatable close approach evidence. */
#include "space.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(void) {
    Game g; Frame f={320,256,0,0,{0}};
    int mode, height, i, n; const int heights[]={1500,500,120,30};
    game_init(&g,0x53504143); g.docked=-1; g.target=-1; g.target_body=0;
    /* Move away from city buildings while retaining generated terrain/light. */
    for (mode=0;mode<3;mode++) {
        f.enhanced=mode!=0; f.w=mode==2 ? 640 : 320; f.h=mode==2 ? 480 : 256;
        f.pixels=malloc((size_t)f.w*f.h); if (!f.pixels || !render_init(&f)) return 1;
        for (height=0;height<4;height++) {
            char path[160]; FILE *out; clock_t start;
            const Planet *p=&g.system.planets[0];
            Vec local=vec(1800,p->radius,-600);
            local.y+=terrain_height(p,local)+heights[height];
            g.pos=scale(add(p->center,local),FP,1); g.vel=surface_velocity(p,local); g.paused=1;
            g.yaw=0; g.pitch=-100;
            render_game(&g,&f); start=clock();
            for (n=0;n<20;n++) render_game(&g,&f);
            printf("%s height %d: %.2f ms/frame\n",mode==0 ? "ECS" : mode==1 ? "AGA" : "RTG",heights[height],
                (double)(clock()-start)*1000/CLOCKS_PER_SEC/20);
            snprintf(path,sizeof(path),"build/evidence/ground-detail-%d-%d.ppm",mode,heights[height]);
            out=fopen(path,"wb"); if (!out) return 1;
            fprintf(out,"P6\n%d %d\n255\n",f.w,f.h);
            for (i=0;i<f.w*f.h;i++) { unsigned c=f.palette[f.pixels[i]]; fputc(c>>16,out);fputc(c>>8,out);fputc(c,out); }
            fclose(out);
            if (mode==0 && heights[height]==120 && !save_game(&g,"build/evidence/ground-detail.sav")) return 1;
        }
        free(f.pixels); render_shutdown();
    }
    return 0;
}
