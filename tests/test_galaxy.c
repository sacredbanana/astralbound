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
static void population(void) {
    Sector samples[1000],a,b;uint32_t seeds[]={0,123,0xffffffffU};int i,j,k,n=0;
    assert(galaxy_system_count()==GALAXY_SYSTEMS);
    for(k=0;k<3;k++) {
        for(i=0;i<1000;i++) {
            uint32_t id=i<104?(unsigned)i:i==999?GALAXY_SYSTEMS-1:hash32((unsigned)i)%GALAXY_SYSTEMS;
            assert(galaxy_system_at(seeds[k],id,&a) && galaxy_exists(seeds[k],a));
            assert(galaxy_system_at(seeds[k],id,&b) && same_sector(a,b));
            for(j=0;j<i;j++) assert(!same_sector(a,samples[j]));
            samples[i]=a;
        }
        assert(!galaxy_system_at(seeds[k],GALAXY_SYSTEMS,&a));
        assert(!galaxy_exists(seeds[k],(Sector){10000,0,10000}));
        assert(!galaxy_exists(seeds[k],(Sector){0,301,0}));
    }
    for(i=-8;i<=8;i++) for(j=-8;j<=8;j++) for(k=-8;k<=8;k++)
        n+=galaxy_exists(123,(Sector){i,j,k});
    assert(n>=9 && n<200);
    printf("PASS exactly one billion systems; indexed samples unique and occupied; %d systems in starter 17-ly cube\n",n);
}
/* The v8 and v9 payload layouts match. Patch the fixed body fields and tail
   into genuine v8 IDs, then recompute the checksum as a legacy writer would. */
static void legacy_fixture(int old_body,int removed_sector,int version) {
    unsigned char data[24576];size_t size,k;uint32_t sum=2166136261U;
    const int body_offset=148+GOODS*8+MAX_MISSIONS*104+MAX_NPC*68+44;
    FILE *f;
    assert(save_game(&g,"galaxy-test.sav"));f=fopen("galaxy-test.sav","rb");assert(f);
    size=fread(data,1,sizeof(data),f);fclose(f);
    if(old_body>=0) {
        put_word(data+body_offset,(unsigned)old_body);put_word(data+body_offset+4,(unsigned)old_body);
        memset(data+size-144,0,128);
        put_word(data+size-144+(old_body/32)*8,1U<<(old_body%32));
        put_word(data+size-144+(old_body/32)*8+4,1U<<(old_body%32));
    }
    if(removed_sector) { put_word(data+24,40000);put_word(data+28,0);put_word(data+32,40000); }
    if(version<8) size-=144;
    put_word(data+4,(unsigned)version);put_word(data+8,(unsigned)(size-16));
    for(k=16;k<size;k++) sum=(sum^data[k])*16777619U;
    put_word(data+12,sum);f=fopen("galaxy-test.sav","wb");assert(f && fwrite(data,1,size,f)==size);fclose(f);
}
static void migration(void) {
    Sector sol=catalog_sector(star("Sol"));int moon,old_moon=21;
    game_init(&g,123);setup_system(&g,sol);moon=solar_body_index(&g.system,"Moon");
    g.docked=-1;g.target=-1;g.target_body=g.frame_body=g.body=moon;
    g.pos=add(scale(g.system.planets[moon].center,FP,1),vec(0,(g.system.planets[moon].radius+200000LL)*FP,0));
    g.vel=vec(0,0,0);g.waypoint=catalog_sector(star("Sirius"));g.waypoint_set=1;
    legacy_fixture(old_moon,0,8);assert(load_game(&h,"galaxy-test.sav"));
    assert(h.target_body==moon && h.frame_body==moon && h.solar_surveyed[moon/32]&(1U<<(moon%32)));
    assert(!memcmp(&g.pos,&h.pos,sizeof(Vec)) && h.waypoint_set);
    g.rig_count=3;
    g.rigs[0]=(MiningRig){sol,21,0,{0,0,0},g.tick};
    g.rigs[1]=(MiningRig){sol,13,0,{0,0,0},g.tick};
    g.rigs[2]=(MiningRig){{40000,0,40000},0,0,{0,0,0},g.tick};
    g.selected=g.waypoint=(Sector){40000,0,40000};
    g.contracts[0]=g.board[0];g.contracts[0].state=ACTIVE;g.contracts[0].destination=g.waypoint;
    legacy_fixture(old_moon,0,8);assert(load_game(&h,"galaxy-test.sav"));
    assert(h.rig_count==1 && h.rigs[0].body==moon && h.credits==g.credits+1000);
    assert(!h.waypoint_set && same_sector(h.selected,sol) && h.contracts[0].state==FAILED);
    assert(!memcmp(h.reputation,g.reputation,sizeof(g.reputation)));
    g.rig_count=0;
    legacy_fixture(13,0,8);assert(load_game(&h,"galaxy-test.sav")); /* removed Vesta frame */
    assert(h.target_body==2 && h.frame_body==2 && h.docked==-1 && altitude_at(&h,h.pos)>100000);
    legacy_fixture(old_moon,1,8);assert(load_game(&h,"galaxy-test.sav"));
    assert(same_sector(h.system.sector,(Sector){0,0,0}) && h.docked==0 && h.target==0);
    game_init(&g,123);g.roll=321;
    legacy_fixture(-1,0,7);assert(load_game(&h,"galaxy-test.sav") && h.roll==321 && !h.waypoint_set);
    remove("galaxy-test.sav");remove("galaxy-test.sav.bak");
    puts("PASS legacy saves remap major moons, relocate removed bodies/systems, and retain v7 compatibility");
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
        Sector sector;
        assert(galaxy_system_at(123,hash32((unsigned)i)%GALAXY_SYSTEMS,&sector));
        assert(galaxy_exists(123,sector));
        generate_system(123,sector,&s);
        assert(s.stars && s.star[0].type==galaxy_type(123,sector));
        { char name[24];galaxy_name(123,sector,name);assert(!strcmp(name,s.name)); }
        types|=1<<s.star[0].type;
    }
    assert((types&(1<<ST_BLACK_HOLE)) && (types&(1<<ST_NEUTRON)) && (types&(1<<ST_RED_GIANT)) && (types&(1<<ST_WHITE_DWARF)) && (types&(1<<ST_BROWN_DWARF)));
    puts("PASS catalog identities, all stellar classes, precise distances and generated/real system consistency");
}
static void solar(void) {
    int i,j,earth,moon,eris,moons=0;
    Sector sol=catalog_sector(star("Sol"));
    generate_system(123,sol,&s);
    assert(s.count==34 && s.count<=MAX_PLANETS);
    earth=solar_body_index(&s,"Earth"); moon=solar_body_index(&s,"Moon"); eris=solar_body_index(&s,"Eris");
    { Vec earth_pos=body_position(&s,earth,0);
      assert(earth_pos.x>AU_METRES*96/100 && earth_pos.x<AU_METRES*99/100);
      assert(earth_pos.z< -AU_METRES*15/100 && earth_pos.z> -AU_METRES*20/100); }
    assert(earth==2 && s.planets[moon].parent==earth && eris>=0);
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
    assert(moons==21);
    assert(solar_body_index(&s,"Vesta")==-1 && solar_body_index(&s,"Sedna")==-1);
    assert(solar_body_index(&s,"Nix")==-1 && solar_body_index(&s,"Puck")==-1);
    assert(solar_body_index(&s,"Titan")>=0 && solar_body_index(&s,"Triton")>=0);
    {
        int pluto=solar_body_index(&s,"Pluto"),titan=solar_body_index(&s,"Titan"),triton=solar_body_index(&s,"Triton");
        BodyFacts facts;
        assert(atmosphere_gases(&s.planets[earth])==AIR_NITROGEN_OXYGEN);
        assert(atmosphere_gases(&s.planets[solar_body_index(&s,"Mars")])==AIR_CARBON_DIOXIDE);
        assert(atmosphere_gases(&s.planets[pluto])==AIR_NITROGEN_METHANE);
        assert(atmosphere_haze(&s.planets[pluto])==HAZE_BLUE);
        assert(atmosphere_gases(&s.planets[titan])==AIR_NITROGEN_METHANE && atmosphere_haze(&s.planets[titan])==HAZE_ORGANIC);
        assert(atmosphere_gases(&s.planets[triton])==AIR_NITROGEN_METHANE && atmosphere_haze(&s.planets[triton])==HAZE_BLUE);
        assert(s.planets[titan].climate!=VACUUM && s.planets[titan].atmosphere==600000);
        assert(s.planets[triton].climate!=VACUUM && s.planets[triton].atmosphere>0);
        assert(atmosphere_gases(&s.planets[solar_body_index(&s,"Neptune")])==AIR_HYDROGEN_METHANE);
        assert(atmosphere_gases(&s.planets[solar_body_index(&s,"Jupiter")])==AIR_HYDROGEN_HELIUM);
        body_facts(&s,pluto,&facts); assert(strstr(facts.air,"99% N2") && strstr(facts.air,"CO"));
        body_facts(&s,titan,&facts); assert(strstr(facts.air,"N2") && facts.pressure>100000);
    }
    game_init(&g,123); setup_system(&g,sol); g.target=-1; g.target_body=eris; g.docked=-1;
    g.frame_body=g.body=eris; g.pos=add(scale(g.system.planets[eris].center,FP,1),vec(0,(g.system.planets[eris].radius+200000LL)*FP,0));
    { Vec origin; int64_t extent; int parent;
      g.target_body=earth;g.map_local=1;
      ui_orbit_geometry(&g,&origin,&extent,&parent);
      assert(parent==earth && extent>384400000 && extent<500000000);
      g.target_body=eris;g.map_local=0; }
    g.vel=vec(0,0,0); g.waypoint=catalog_sector(star("Sirius"));g.waypoint_set=1;
    /* Exercise the second survey word and long orbital periods through saves. */
    g.body=solar_body_index(&g.system,"Charon"); record_exploration(&g);
    assert(g.solar_visited[1]&2U);
    assert(save_game(&g,"galaxy-test.sav")); assert(load_game(&h,"galaxy-test.sav"));
    assert(h.waypoint_set && same_sector(h.waypoint,g.waypoint) && h.target_body==eris);
    assert(!memcmp(g.solar_visited,h.solar_visited,sizeof(g.solar_visited)));
    g.solar_visited[15]|=1U<<31;assert(!save_game(&g,"galaxy-test.sav"));g.solar_visited[15]&=~(1U<<31);
    remove("galaxy-test.sav");
    { Frame f={320,200,0,NULL,{0}}; Controls controls={0};
      f.pixels=malloc(320*200);assert(f.pixels && render_init(&f));
      for(i=0;i<3;i++) {
          int body=i==0?earth:i==1?moon:eris;
          g.frame_body=g.body=body; g.target=-1;g.target_body=body;g.page=FLIGHT;
          g.pos=add(scale(g.system.planets[body].center,FP,1),vec(0,(g.system.planets[body].radius+500000LL)*FP,0));
          g.vel=vec(0,0,0);g.pitch=-200;
          game_tick(&g,controls);render_game(&g,&f);assert(!g.dead);
      }
      free(f.pixels);render_shutdown(); }

    puts("PASS Sol 13 planets/dwarfs, 21 major moons, ellipses, long periods and v9 saves");
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
    click(f,GAL_OVERVIEW); assert(g.galaxy_extent==14000000);
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
    assert(jump(&g,g.selected)); assert(!strcmp(g.system.name,"Sol") && g.system.count==34 && g.fuel==before-2);
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
    population();catalog();solar();migration();interaction(&f);f.h=256;interaction(&f);f.w=640;f.h=480;f.enhanced=1;interaction(&f);jumping();return 0;
}
