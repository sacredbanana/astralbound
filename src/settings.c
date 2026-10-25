/* SPDX-License-Identifier: Unlicense; versioned, bounded, portable preferences. */
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
const char *quality_names[5] = {"LOW", "MEDIUM", "HIGH", "ULTRA", "CUSTOM"};
const char *video_names[4] = {"ECS 32 COLOURS", "AGA 256 COLOURS", "RTG 8 BIT", "RTG 16 BIT"};
const char *renderer_names[2] = {"ENHANCED", "CLASSIC"};
#define OPT(member, title, hint, max, ...) {title, hint, offsetof(Graphics,member), max, {__VA_ARGS__}}
const GraphicsOption graphics_options[GRAPHICS_OPTIONS] = {
    /* Keep the legacy graphics_0 slot for old configuration files only. */
    OPT(scale,"RESERVED","",0,"1X"),
    OPT(terrain,"TERRAIN MESH","COARSE LAND STILL USES EXACT FLIGHT COLLISION.",4,"MINIMUM","COARSE","MEDIUM","FINE","EXTREME"),
    OPT(texture,"SURFACE SHADING","FLAT REMOVES PER PIXEL TERRAIN SAMPLING.",3,"FLAT","MATERIAL","TEXTURED","FULL"),
    OPT(planets,"PLANET DETAIL","DETAIL IN PLANET SURFACES AND CURVED BOUNDARIES.",2,"LOW","MEDIUM","HIGH"),
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
    OPT(ships,"SHIP DETAIL","SIMPLE HULLS SAVE TRANSFORM AND CLIPPING WORK.",1,"SIMPLE","FULL"),
    OPT(aurorae,"PLANETARY AURORAE","ANIMATED POLAR LIGHTS IN SURFACE AND ORBITAL VIEWS.",1,"OFF","ON")
};
static const Graphics presets[4] = {
    {0,1,0,0,0,0,0,0,1,0,1,1,0,0,0,0,1,0,0},
    {0,2,1,1,1,1,1,1,2,1,2,2,0,1,1,0,2,1,0},
    {0,3,3,2,2,3,2,2,3,3,3,3,1,1,1,1,3,1,0},
    {0,4,3,2,3,4,3,2,4,3,4,4,1,1,1,1,5,1,1}
};
int graphics_value(const Graphics *g, int o) {
    if(o<0 || o>=GRAPHICS_OPTIONS) return 0;
    return *(const int *)((const char *)g+graphics_options[o].offset);
}
static int *graphics_slot(Graphics *g, int o) { return (int *)((char *)g+graphics_options[o].offset); }
void settings_preset(Settings *s, int preset) {
    if(preset<0 || preset>QUALITY_CUSTOM) return;
    s->preset=preset;
    if(preset<QUALITY_CUSTOM) {
        s->gfx=presets[preset];
        s->renderer=preset<=QUALITY_MEDIUM ? RENDERER_CLASSIC : RENDERER_ENHANCED;
        s->bezier=preset!=QUALITY_LOW;
    }
    s->gfx.scale=0;
}
void settings_defaults(Settings *s, int amiga) {
    memset(s,0,sizeof(*s)); s->mode=amiga ? VIDEO_ECS : VIDEO_AGA;
    s->compact=amiga; s->intro=1; s->sound=1; s->rtg_size=1; s->bezier=1;
    settings_preset(s,QUALITY_MEDIUM);
}
int graphics_available(const Settings *s, int o) {
    return o>0 && o<GRAPHICS_OPTIONS &&
        (s->renderer!=RENDERER_CLASSIC || (o!=2 && o!=16));
}
void graphics_change(Settings *s, int o, int direction) {
    int *value;
    if(!graphics_available(s,o)) return;
    value=graphics_slot(&s->gfx,o);
    if(o==11 && (s->renderer==RENDERER_CLASSIC || s->bezier)) *value=*value ? 0 : 2;
    else if(s->renderer==RENDERER_CLASSIC && o==1) {
        int v=*value<1 ? 1 : *value>3 ? 3 : *value;
        *value=1+(v-1+3+direction)%3;
    } else if(s->renderer==RENDERER_CLASSIC && o==4) {
        int v=*value>2 ? 2 : *value;
        *value=(v+3+direction)%3;
    } else *value=(*value+graphics_options[o].maximum+1+direction)%(graphics_options[o].maximum+1);
    s->preset=QUALITY_CUSTOM;
}
static int bounded(int v,int max,int fallback) { return v>=0 && v<=max ? v : fallback; }
void settings_validate(Settings *s) {
    int i;
    s->mode=bounded(s->mode,3,0); s->compact=bounded(s->compact,1,1);
    s->rtg_size=bounded(s->rtg_size,3,1); s->preset=bounded(s->preset,4,0);
    s->intro=bounded(s->intro,1,1); s->sound=bounded(s->sound,1,1); s->test_scene=bounded(s->test_scene,14,0);
    s->renderer=bounded(s->renderer,1,0);
    s->bezier=bounded(s->bezier,1,1);
    for(i=0;i<GRAPHICS_OPTIONS;i++) *graphics_slot(&s->gfx,i)=bounded(graphics_value(&s->gfx,i),graphics_options[i].maximum,0);
    settings_preset(s,s->preset);
}
void settings_dimensions(const Settings *s, int pal, int *w, int *h) {
    static const int widths[]={320,640,800,1024}, heights[]={240,480,600,768};
    if(s->mode>=VIDEO_RTG) { *w=widths[s->rtg_size]; *h=heights[s->rtg_size]; }
    else { *w=320; *h=pal && !s->compact ? 256 : 200; }
}
int settings_load(Settings *s, const char *path) {
    FILE *file=fopen(path,"r"); char line[128]; int version, i; Settings loaded=*s;
    if(!file) return 0;
    loaded.gfx.aurorae=0;
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
        else if(!strcmp(key,"renderer")) loaded.renderer=(int)value;
        else if(!strcmp(key,"bezier")) loaded.bezier=(int)value;
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
    fprintf(file,"ASTRALBOUND_SETTINGS 1\nmode %d\ncompact %d\nrtg_size %d\npreset %d\nintro %d\nsound %d\ntest_scene %d\nrenderer %d\nbezier %d\n",
            s.mode,s.compact,s.rtg_size,s.preset,s.intro,s.sound,s.test_scene,s.renderer,s.bezier);
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
