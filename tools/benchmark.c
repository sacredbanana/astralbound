/* SPDX-License-Identifier: Unlicense; CPU workload, not Amiga display FPS. */
#include "performance.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static Game g;
int main(void) {
    Frame f={320,200,0,NULL,{0}};
    Settings s; int preset,scene,i,j;
    f.pixels=malloc(320*200); if(!f.pixels) return 1;
    puts("Host CPU renderer only, ECS 320x200. Excludes C2P, sync, audio and physics.");
    puts("preset scene ms/frame indexed-hash");
    for(preset=0;preset<4;preset++) {
        settings_defaults(&s,1); settings_preset(&s,preset); render_graphics(&s.gfx);
        if(!render_init(&f)) return 1;
        for(scene=0;scene<PERFORMANCE_SCENES;scene++) {
            clock_t start; uint32_t hash=2166136261U;
            performance_scene(&g,scene); render_game(&g,&f);
            start=clock(); for(i=0;i<8;i++) render_game(&g,&f);
            double elapsed=(double)(clock()-start)*1000.0/CLOCKS_PER_SEC/8;
            for(j=0;j<320*200;j++) hash=(hash^f.pixels[j])*16777619U;
            printf("%s / %s: %.3f ms %08lx\n",quality_names[preset],performance_names[scene],elapsed,(unsigned long)hash);
        }
    }
    free(f.pixels); render_shutdown(); return 0;
}
