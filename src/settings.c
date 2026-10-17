/* SPDX-License-Identifier: Unlicense; versioned, bounded, portable preferences. */
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
const char *quality_names[5] = {"LOW", "MEDIUM", "HIGH", "ULTRA", "CUSTOM"};
const char *video_names[4] = {"ECS 32 COLOURS", "AGA 256 COLOURS", "RTG 8 BIT", "RTG 16 BIT"};
#define OPT(member, title, hint, max, ...) {title, hint, offsetof(Graphics,member), max, {__VA_ARGS__}}
const GraphicsOption graphics_options[GRAPHICS_OPTIONS] = {
    OPT(scale,"3D PIXEL SIZE","LARGER PIXELS SAVE FILL WORK. HUD STAYS SHARP.",3,"1X","2X","3X","4X"),
    OPT(terrain,"TERRAIN MESH","COARSE LAND STILL USES EXACT FLIGHT COLLISION.",4,"MINIMUM","COARSE","MEDIUM","FINE","EXTREME"),
    OPT(texture,"SURFACE SHADING","FLAT REMOVES PER PIXEL TERRAIN SAMPLING.",3,"FLAT","MATERIAL","TEXTURED","FULL"),
    OPT(planets,"PLANET FACETS","DISTANT SPHERES: 8 TO 32 LONGITUDE FACETS.",2,"8","16","32"),
    OPT(clouds,"CLOUD MESH","OFF REMOVES VOLUMETRIC CLOUD GEOMETRY.",3,"OFF","LOW","HIGH","EXTREME"),
    OPT(cloud_range,"CLOUD DISTANCE","NUMBER OF CLOUD CELL RINGS AROUND THE VIEW.",4,"1","2","3","5","8"),
    OPT(trees,"FORESTS","DECORATIVE TREES. NO EFFECT ON LAND HEIGHT.",3,"OFF","SPARSE","NORMAL","DENSE"),
    OPT(ports,"PORT DETAIL","SIMPLE STATIONS AND BUILDINGS TO FULL DETAIL.",2,"SIMPLE","NORMAL","FULL"),
    OPT(port_range,"PORT DISTANCE","SCENERY RANGE. NAVIGATION TARGETS STAY VISIBLE.",4,"4 KM","10 KM","40 KM","80 KM","160 KM"),
    OPT(particles,"SMOKE AND DEBRIS","REDUCES VISIBLE EFFECTS WITHOUT CHANGING DAMAGE.",3,"MINIMUM","LOW","HIGH","FULL"),
    OPT(stars,"STAR FIELD","NUMBER OF BACKGROUND STARS.",4,"OFF","30","60","120","480"),
    OPT(rings,"PLANET RINGS","RING MESH SEGMENTS.",4,"OFF","8","16","32","64"),
    OPT(sky,"TWILIGHT GLOW","OFF USES SIMPLE SKY BANDS.",1,"OFF","ON"),
    OPT(weather,"RAIN AND CLOUD FOG","VISUAL WEATHER. FLIGHT ATMOSPHERE IS UNCHANGED.",1,"OFF","ON"),
    OPT(lights,"PORT LIGHTS","BEACONS AND LANDING LIGHT GLOWS.",1,"OFF","ON"),
    OPT(labels,"BODY LABELS","BODY NAME OVERLAYS. TARGET MARKERS STAY ON.",1,"OFF","ON"),
    OPT(triangles,"TRIANGLE BUDGET","GEOMETRY CAP AND MEMORY: 512 TO 16384.",5,"512","1024","2048","4096","8192","16384"),
    OPT(ships,"SHIP DETAIL","SIMPLE HULLS SAVE TRANSFORM AND CLIPPING WORK.",1,"SIMPLE","FULL")
};
static const Graphics presets[4] = {
    {1,1,0,0,0,0,0,0,1,0,1,1,0,0,0,0,1,0},
    {1,2,1,1,1,1,1,1,2,1,2,2,0,1,1,0,2,1},
    {0,3,3,2,2,3,2,2,3,3,3,3,1,1,1,1,3,1},
    {0,4,3,2,3,4,3,2,4,3,4,4,1,1,1,1,5,1}
};
int graphics_value(const Graphics *g, int o) {
    if(o<0 || o>=GRAPHICS_OPTIONS) return 0;
    return *(const int *)((const char *)g+graphics_options[o].offset);
}
static int *graphics_slot(Graphics *g, int o) { return (int *)((char *)g+graphics_options[o].offset); }
void settings_preset(Settings *s, int preset) {
    if(preset<0 || preset>QUALITY_CUSTOM) return;
    s->preset=preset;
    if(preset<QUALITY_CUSTOM) s->gfx=presets[preset];
}
void settings_defaults(Settings *s, int amiga) {
    memset(s,0,sizeof(*s)); s->mode=amiga ? VIDEO_ECS : VIDEO_AGA;
    s->compact=amiga; s->intro=1; s->sound=1; s->rtg_size=1;
    settings_preset(s,amiga ? QUALITY_LOW : QUALITY_HIGH);
}
void graphics_change(Settings *s, int o, int direction) {
    int *value;
    if(o<0 || o>=GRAPHICS_OPTIONS) return;
    value=graphics_slot(&s->gfx,o);
    *value=(*value+graphics_options[o].maximum+1+direction)%(graphics_options[o].maximum+1);
    s->preset=QUALITY_CUSTOM;
}
static int bounded(int v,int max,int fallback) { return v>=0 && v<=max ? v : fallback; }
void settings_validate(Settings *s) {
    int i;
    s->mode=bounded(s->mode,3,0); s->compact=bounded(s->compact,1,1);
    s->rtg_size=bounded(s->rtg_size,3,1); s->preset=bounded(s->preset,4,0);
    s->intro=bounded(s->intro,1,1); s->sound=bounded(s->sound,1,1); s->test_scene=bounded(s->test_scene,14,0);
    for(i=0;i<GRAPHICS_OPTIONS;i++) *graphics_slot(&s->gfx,i)=bounded(graphics_value(&s->gfx,i),graphics_options[i].maximum,0);
    if(s->preset<QUALITY_CUSTOM) s->gfx=presets[s->preset];
}
void settings_dimensions(const Settings *s, int pal, int *w, int *h) {
    static const int widths[]={320,640,800,1024}, heights[]={240,480,600,768};
    if(s->mode>=VIDEO_RTG) { *w=widths[s->rtg_size]; *h=heights[s->rtg_size]; }
    else { *w=320; *h=pal && !s->compact ? 256 : 200; }
}
int settings_load(Settings *s, const char *path) {
    FILE *file=fopen(path,"r"); char line[128]; int version, i; Settings loaded=*s;
    if(!file) return 0;
    if(!fgets(line,sizeof(line),file) || sscanf(line,"ASTRALBOUND_SETTINGS %d",&version)!=1 || version!=1) { fclose(file); return 0; }
    while(fgets(line,sizeof(line),file)) {
        char key[40], tail; long value;
        if(sscanf(line,"%39s %ld %c",key,&value,&tail)!=2 || value<0 || value>INT_MAX) continue;
        if(!strcmp(key,"mode")) loaded.mode=(int)value;
        else if(!strcmp(key,"compact")) loaded.compact=(int)value;
        else if(!strcmp(key,"rtg_size")) loaded.rtg_size=(int)value;
        else if(!strcmp(key,"preset")) loaded.preset=(int)value;
        else if(!strcmp(key,"intro")) loaded.intro=(int)value;
        else if(!strcmp(key,"sound")) loaded.sound=(int)value;
        else if(!strcmp(key,"test_scene")) loaded.test_scene=(int)value;
        else for(i=0;i<GRAPHICS_OPTIONS;i++) { char name[32]; snprintf(name,sizeof(name),"graphics_%d",i);
            if(!strcmp(key,name)) *graphics_slot(&loaded.gfx,i)=(int)value; }
    }
    i=!ferror(file); fclose(file);
    if(!i) return 0;
    settings_validate(&loaded); *s=loaded; return 1;
}
int settings_save(const Settings *settings, const char *path) {
    char temporary[512], backup[512]; int i, ok; FILE *file; Settings s=*settings;
    settings_validate(&s);
    if(snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary) ||
       snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup)) return 0;
    file=fopen(temporary,"w"); if(!file) return 0;
    fprintf(file,"ASTRALBOUND_SETTINGS 1\nmode %d\ncompact %d\nrtg_size %d\npreset %d\nintro %d\nsound %d\ntest_scene %d\n",
            s.mode,s.compact,s.rtg_size,s.preset,s.intro,s.sound,s.test_scene);
    for(i=0;i<GRAPHICS_OPTIONS;i++) fprintf(file,"graphics_%d %d\n",i,graphics_value(&s.gfx,i));
    ok=!ferror(file); if(fclose(file)) ok=0;
    if(!ok) { remove(temporary); return 0; }
    /* AmigaDOS Rename cannot replace a destination. Keep the old file until
       the complete replacement is closed, and restore it on rename failure. */
    remove(backup);
    if(rename(path,backup) && (file=fopen(path,"r"))!=NULL) { fclose(file); remove(temporary); return 0; }
    if(rename(temporary,path)) { rename(backup,path); remove(temporary); return 0; }
    remove(backup); return 1;
}
