/* SPDX-License-Identifier: Unlicense */
#include "galaxy.h"
#include "solar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
static void write_frame(Frame *f,const char *name) {
    FILE *out;int i;
    render_init(f);render_game(&g,f);out=fopen(name,"wb");if(!out)exit(1);
    fprintf(out,"P6\n%d %d\n255\n",f->w,f->h);
    for(i=0;i<f->w*f->h;i++) { uint32_t c=f->palette[f->pixels[i]];fputc(c>>16,out);fputc(c>>8,out);fputc(c,out); }
    fclose(out);
}
int main(void) {
    Frame f={640,480,1,NULL,{0}};
    f.pixels=malloc(640*480);game_init(&g,0x53504143);game_action(&g,'g');
    write_frame(&f,"build/evidence/galaxy-local.ppm");
    strcpy(g.galaxy_query,"alpha");g.galaxy_searching=1;
    write_frame(&f,"build/evidence/galaxy-search.ppm");
    galaxy_key(&g,'\r');galaxy_key(&g,GAL_CLEAR);galaxy_key(&g,GAL_INFO);
    write_frame(&f,"build/evidence/galaxy-details.ppm");
    g.galaxy_detail=0;galaxy_key(&g,GAL_OVERVIEW);g.galaxy_pitch=190;
    write_frame(&f,"build/evidence/galaxy-overview.ppm");
    f.w=320;f.h=200;f.enhanced=0;galaxy_key(&g,GAL_HOME);g.galaxy_extent=8000;g.galaxy_pitch=100;
    write_frame(&f,"build/evidence/galaxy-ecs.ppm");
    setup_system(&g,catalog_sector(0));g.target=-1;g.target_body=solar_body_index(&g.system,"Earth");
    g.page=SYSTEM_MAP;g.system_display=2;
    write_frame(&f,"build/evidence/sol-earth.ppm");
    g.target_body=solar_body_index(&g.system,"Titan");
    write_frame(&f,"build/evidence/sol-titan.ppm");
    g.target_body=solar_body_index(&g.system,"Sedna");
    write_frame(&f,"build/evidence/sol-sedna.ppm");
    g.system_display=0;g.target_body=480;
    write_frame(&f,"build/evidence/sol-moons.ppm");
    free(f.pixels);render_shutdown();return 0;
}
