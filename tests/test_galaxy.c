/* SPDX-License-Identifier: Unlicense */
#include "galaxy.h"
#include "solar.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long __stack=1048576;
static Game g,h;
static System s;
static int star(const char *name) {
    int i; for(i=0;i<star_catalog_count;i++) if(!strcmp(star_catalog[i].name,name)) return i;
    assert(!"missing real star"); return -1;
}
static void put_word(unsigned char *p,uint32_t v) {
    p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;
}
static void catalog(void) {
    int i,j,types=0; Sector sol=catalog_sector(star("Sol"));
    assert(star_catalog_count>=50);
    assert(length(galaxy_position(sol))==0);
    assert(galaxy_distance(sol,catalog_sector(star("Proxima Centauri")))>4200);
    assert(galaxy_distance(sol,catalog_sector(star("Proxima Centauri")))<4300);
    assert(galaxy_distance(sol,catalog_sector(star("Sirius")))>8500);
    assert(galaxy_distance(sol,catalog_sector(star("Sirius")))<8700);
    assert(galaxy_distance(catalog_sector(star("Proxima Centauri")),catalog_sector(star("Alpha Centauri")))<300);
    for(i=0;i<star_catalog_count;i++) {
        generate_system(123,catalog_sector(i),&s);
        assert(!strcmp(s.name,star_catalog[i].name) && s.count>0 && s.stars==star_catalog[i].count);
        for(j=0;j<s.stars;j++) assert(s.star[j].type==stellar_type(star_catalog[i].spectra[j]));
        for(j=i+1;j<star_catalog_count;j++) assert(!same_sector(catalog_sector(i),catalog_sector(j)));
    }
    for(i=0;i<3000;i++) {
        Sector sector={i,18,19};
        if(!galaxy_exists(123,sector)) continue;
        generate_system(123,sector,&s);
        assert(s.stars && s.star[0].type==galaxy_type(123,sector));
        { char name[24];galaxy_name(123,sector,name);assert(!strcmp(name,s.name)); }
        types|=1<<s.star[0].type;
    }
    assert((types&(1<<ST_BLACK_HOLE)) && (types&(1<<ST_NEUTRON)) && (types&(1<<ST_RED_GIANT)) && (types&(1<<ST_WHITE_DWARF)) && (types&(1<<ST_BROWN_DWARF)));
    puts("PASS catalog identities, all stellar classes, precise distances and generated/real system consistency");
}
static void solar(void) {
    int i,j,earth,moon,sedna,moons=0;
    Sector sol=catalog_sector(star("Sol"));
    generate_system(123,sol,&s);
    assert(s.count==484 && s.count<=MAX_PLANETS);
    earth=solar_body_index(&s,"Earth"); moon=solar_body_index(&s,"Moon"); sedna=solar_body_index(&s,"Sedna");
    { Vec earth_pos=body_position(&s,earth,0);
      assert(earth_pos.x>AU_METRES*96/100 && earth_pos.x<AU_METRES*99/100);
      assert(earth_pos.z< -AU_METRES*15/100 && earth_pos.z> -AU_METRES*20/100); }
    assert(earth==2 && s.planets[moon].parent==earth && sedna>=0);
    assert(s.planets[earth].radius==6371000 && llabs(s.planets[earth].orbit-AU_METRES)<1000000);
    assert(s.planets[solar_body_index(&s,"Neptune")].orbit>29*AU_METRES);
    assert(s.planets[solar_body_index(&s,"Pluto")].period>4000000000ULL);
    assert(s.planets[solar_body_index(&s,"Charon")].parent==solar_body_index(&s,"Pluto"));
    for(i=0;i<s.count;i++) {
        Vec p=body_position(&s,i,0),q=body_position(&s,i,123456789);
        const Planet *b=&s.planets[i];
        assert(b->period && b->radius>0 && b->parent<i && b->catalog);
        assert(length(sub(p,b->parent<0?vec(0,0,0):body_position(&s,b->parent,0)))<=b->orbit*2);
        assert(length(q)>0);
        for(j=i+1;j<s.count;j++) assert(strcmp(b->name,s.planets[j].name));
        moons+=b->parent>=0;
    }
    assert(moons==463);
    game_init(&g,123); setup_system(&g,sol); g.target=-1; g.target_body=sedna; g.docked=-1;
    g.frame_body=g.body=sedna; g.pos=add(scale(g.system.planets[sedna].center,FP,1),vec(0,(g.system.planets[sedna].radius+200000LL)*FP,0));
    { Vec origin; int64_t extent; int parent;
      g.target_body=earth;g.map_local=1;
      ui_orbit_geometry(&g,&origin,&extent,&parent);
      assert(parent==earth && extent>384400000 && extent<500000000);
      g.target_body=sedna;g.map_local=0; }
    g.vel=vec(0,0,0); g.waypoint=catalog_sector(star("Sirius"));g.waypoint_set=1;
    /* Exercise body IDs well beyond 32 and long orbital periods through saves. */
    g.body=480; record_exploration(&g);
    assert(g.solar_visited[15]&1U);
    assert(save_game(&g,"galaxy-test.sav")); assert(load_game(&h,"galaxy-test.sav"));
    assert(h.waypoint_set && same_sector(h.waypoint,g.waypoint) && h.target_body==sedna);
    assert(!memcmp(g.solar_visited,h.solar_visited,sizeof(g.solar_visited)));
    { unsigned char data[24576];size_t size,k;uint32_t sum=2166136261U;FILE *f=fopen("galaxy-test.sav","rb");
      assert(f);size=fread(data,1,sizeof(data),f);fclose(f);assert(size>160);size-=144;
      put_word(data+4,7);put_word(data+8,(uint32_t)(size-16));
      for(k=16;k<size;k++) sum=(sum^data[k])*16777619U;
      put_word(data+12,sum);f=fopen("galaxy-test.sav","wb");assert(f && fwrite(data,1,size,f)==size);fclose(f);
      assert(load_game(&h,"galaxy-test.sav") && !h.waypoint_set && !h.solar_visited[15] && h.roll==g.roll);
      g.solar_visited[15]|=1U<<31;assert(!save_game(&g,"galaxy-test.sav"));g.solar_visited[15]&=~(1U<<31); }
    remove("galaxy-test.sav");
    { Frame f={320,200,0,NULL,{0}}; Controls controls={0};
      f.pixels=malloc(320*200);assert(f.pixels && render_init(&f));
      for(i=0;i<3;i++) {
          int body=i==0?earth:i==1?moon:sedna;
          g.frame_body=g.body=body; g.target=-1;g.target_body=body;g.page=FLIGHT;
          g.pos=add(scale(g.system.planets[body].center,FP,1),vec(0,(g.system.planets[body].radius+500000LL)*FP,0));
          g.vel=vec(0,0,0);g.pitch=-200;
          game_tick(&g,controls);render_game(&g,&f);assert(!g.dead);
      }
      free(f.pixels);render_shutdown(); }

    puts("PASS Sol planets, all 459 JPL moons plus dwarf moons, minor bodies, ellipses, long periods and v8 saves");
}
static void click(Frame *f,int action) {
    UiWidget v[UI_MAX_WIDGETS]; int n=ui_widgets(&g,f,v),i,scale=ui_scale(f);
    for(i=0;i<n;i++) if(v[i].action==action) {
        int x=(v[i].x+v[i].w/2)*scale,y=(v[i].y+v[i].h/2)*scale;
        ui_pointer(&g,f,x,y,1); assert(!ui_fire(&g,f)); ui_pointer(&g,f,x,y,0);return;
    }
    assert(!"missing map control");
}
static void interaction(Frame *f) {
    int i,n,x,y,d,scale=ui_scale(f); Sector result[GALAXY_SEARCH],picked;
    const GalaxyMarker *m;
    game_init(&g,123); game_action(&g,'g');
    click(f,GAL_SEARCH);
    for(i=0;"sirius"[i];i++) game_action(&g,"sirius"[i]);
    assert(!strcmp(g.galaxy_query,"sirius") && g.galaxy_searching && g.page==MAP && !g.drive_manual);
    assert(galaxy_search(&g,result,GALAXY_SEARCH)==1);
    game_action(&g,8); game_action(&g,'s'); game_action(&g,'\r');
    assert(same_sector(g.selected,catalog_sector(star("Sirius"))));
    click(f,GAL_WAYPOINT); assert(g.waypoint_set && same_sector(g.waypoint,g.selected));
    click(f,GAL_CLEAR); click(f,GAL_INFO); assert(g.galaxy_detail); click(f,GAL_INFO);
    click(f,GAL_HOME); assert(!memcmp(&g.galaxy_center,&(Vec){500,500,500},sizeof(Vec)));
    game_action(&g,'+'); assert(g.galaxy_extent==8000);
    m=galaxy_markers(&g,f,&n); assert(n>0 && n<GALAXY_MARKERS);
    for(i=0;i<n;i++) if(m[i].catalog>=0 && m[i].y<f->h/scale-82) {
        x=m[i].x; y=m[i].y;
        assert(galaxy_pick(&g,f,x,y,&picked));
        ui_pointer(&g,f,x*scale,y*scale,1); ui_pointer(&g,f,x*scale,y*scale,0);
        assert(same_sector(g.selected,picked)); break;
    }
    assert(i<n);
    x=g.galaxy_yaw;ui_pointer(&g,f,80*scale,60*scale,1);ui_pointer(&g,f,100*scale,60*scale,1);ui_pointer(&g,f,100*scale,60*scale,0);
    assert(g.galaxy_yaw!=x && !ui_fire(&g,f));
    click(f,GAL_PAN); {Vec center=g.galaxy_center; ui_pointer(&g,f,80*scale,60*scale,1);ui_pointer(&g,f,100*scale,70*scale,1);ui_pointer(&g,f,100*scale,70*scale,0);assert(memcmp(&center,&g.galaxy_center,sizeof(Vec)));}
    click(f,GAL_PAN);
    for(i=0;i<100;i++) game_action(&g,'-');
    assert(g.galaxy_extent==60000000);
    for(i=0;i<100;i++) game_action(&g,'+');
    assert(g.galaxy_extent==2000);
    click(f,GAL_OVERVIEW); assert(g.galaxy_extent==40000000);
    click(f,GAL_FOCUS); assert(galaxy_project(&g,f,galaxy_position(g.selected),&x,&y,&d));
    /* Search aliases and long names stay accessible independently of zoom. */
    strcpy(g.galaxy_query,"alpha centauri");assert(galaxy_search(&g,result,GALAXY_SEARCH)==2);
    g.galaxy_searching=1;ui_quit_key(&g,27);assert(!g.galaxy_searching && !g.ui_quit);
    f->pixels=malloc((size_t)f->w*f->h);assert(f->pixels && render_init(f));render_game(&g,f);free(f->pixels);f->pixels=NULL;render_shutdown();
    printf("PASS 3D picking/search/drag/pan/zoom/waypoint/render at %dx%d\n",f->w,f->h);
}
static void jumping(void) {
    int before;
    game_init(&g,123);game_action(&g,'l');memset(g.npc,0,sizeof(g.npc));
    g.pos=add(scale(g.system.planets[0].center,FP,1),vec(0,(g.system.planets[0].radius+200000LL)*FP,0));
    g.selected=catalog_sector(star("Sol"));before=g.fuel;
    assert(!strcmp(galaxy_jump_status(&g,g.selected),"JUMP READY"));
    assert(jump(&g,g.selected)); assert(!strcmp(g.system.name,"Sol") && g.system.count==484 && g.fuel==before-2);
    g.waypoint=g.selected;g.waypoint_set=1;
    assert(!jump(&g,catalog_sector(star("Sirius")))) ;
    /* The map's keyboard and mouse jump action must retain master's charge,
       revalidation and tunnel animation instead of bypassing them. */
    game_init(&g,123);game_action(&g,'l');memset(g.npc,0,sizeof(g.npc));
    g.pos=add(scale(g.system.planets[0].center,FP,1),vec(0,(g.system.planets[0].radius+200000LL)*FP,0));
    g.selected=catalog_sector(star("Sol"));g.page=MAP;before=g.fuel;
    { Frame f={320,200,0,NULL,{0}};Controls controls={0};
      click(&f,'x');assert(g.hyperspace==1 && g.page==FLIGHT && strcmp(g.system.name,"Sol"));
      game_action(&g,'g');assert(g.page==FLIGHT && g.hyperspace==1);
      game_frame(&g,controls,HYPER_CHARGE);assert(!strcmp(g.system.name,"Sol") && g.fuel==before-2);
      game_frame(&g,controls,HYPER_TOTAL);assert(!g.hyperspace); }
    puts("PASS catalog targets use precise physical jump distance and existing flight safety gates");
}
int main(void) {
    Frame f={320,200,0,NULL,{0}};
    catalog();solar();interaction(&f);f.h=256;interaction(&f);f.w=640;f.h=480;f.enhanced=1;interaction(&f);jumping();return 0;
}
