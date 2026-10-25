/* SPDX-License-Identifier: Unlicense */
#include "aurora_fixture.h"
#include "settings.h"
#include "performance.h"
#include <stdio.h>
#include <stdlib.h>
static Game g;
int main(void) {
    int renderer, mode, surface, i;
    Settings s;
    Frame f = {320,200,0,NULL,{0}};
    for (renderer = 0; renderer < 2; renderer++) for (mode = 0; mode < 3; mode++) {
        f.w = mode == 2 ? 640 : 320; f.h = mode == 2 ? 480 : mode ? 256 : 200;
        f.enhanced = mode != 0;
        f.pixels = malloc((size_t)f.w*f.h);
        settings_defaults(&s,0); settings_preset(&s,QUALITY_ULTRA);
        s.gfx.clouds = s.gfx.weather = s.gfx.trees = 0;
        render_graphics(&s.gfx); render_renderer(renderer);
        if (!f.pixels || !render_init(&f)) return 1;
        for (surface = 0; surface < 2; surface++) {
            char path[160]; FILE *out;
            aurora_fixture(&g,surface,1);
            render_performance_scene(&g,&f);
            snprintf(path,sizeof(path),"build/evidence/aurora-%s-%s-%s.ppm",
                     renderer ? "classic" : "enhanced",mode == 2 ? "rtg" : mode ? "aga" : "ecs",surface ? "surface" : "orbit");
            out = fopen(path,"wb"); if (!out) return 1;
            fprintf(out,"P6\n%d %d\n255\n",f.w,f.h);
            for (i = 0; i < f.w*f.h; i++) {
                uint32_t c = f.palette[f.pixels[i]];
                fputc(c>>16,out); fputc(c>>8,out); fputc(c,out);
            }
            fclose(out); puts(path);
        }
        free(f.pixels); render_shutdown();
    }
    return 0;
}
