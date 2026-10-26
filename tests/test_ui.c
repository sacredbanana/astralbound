/* SPDX-License-Identifier: Unlicense */
#include "ui.h"
#include "services.h"
#include "galaxy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
unsigned long __stack=1048576;
static int click(Frame *f,int action) {
    UiWidget v[UI_MAX_WIDGETS]; int i,n=ui_widgets(&g,f,v),s=ui_scale(f);
    for(i=n-1;i>=0;i--) if(v[i].action==action) {
        int x=(v[i].x+v[i].w/2)*s,y=(v[i].y+v[i].h/2)*s;
        ui_pointer(&g,f,x,y,1);
        assert(!ui_fire(&g,f));
        return ui_pointer(&g,f,x,y,0);
    }
    assert(!"missing action"); return 0;
}
/* Press, optionally move, release at logical map points. */
static void map_gesture(Frame *f,int x,int y,int to_x,int to_y) {
    int s=ui_scale(f);
    ui_pointer(&g,f,x*s,y*s,1);
    if(to_x!=x || to_y!=y) { ui_pointer(&g,f,(x+to_x)/2*s,(y+to_y)/2*s,1); ui_pointer(&g,f,to_x*s,to_y*s,1); }
    ui_pointer(&g,f,to_x*s,to_y*s,0);
}
/* The orbits page: a 3D camera like the galaxy map's, whose clicks target
   bodies and ports through the projection rendering uses. */
static void system_map(Frame *f) {
    int h=f->h/ui_scale(f),i,x,y,d,port,yaw,pitch,picked=0;
    int64_t extent,before;
    Vec centre;
    ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch);
    before=extent;
    /* Click an isolated body: it becomes the target; a second, its data. */
    for(i=0;i<g.system.count && !picked;i++) {
        int k,clear=1;
        if(ui_sysmap_project(&g,h,g.system.planets[i].center,&x,&y,&d)!=1) continue;
        if(g.system.planets[i].parent>=0) continue;
        /* Its own moons and ports share its pixel and yield the click. */
        for(k=0;k<g.system.count+g.nports;k++) {
            int px,py,pd,own=k<g.system.count?g.system.planets[k].parent==i:g.ports[k-g.system.count].body==i;
            Vec p=k<g.system.count?g.system.planets[k].center:scale(g.ports[k-g.system.count].pos,1,FP);
            if(k!=i && !own && ui_sysmap_project(&g,h,p,&px,&py,&pd)==1 && abs(px-x)<9 && abs(py-y)<9) clear=0;
        }
        if(!clear) continue;
        assert(ui_sysmap_pick(&g,h,x,y,&port)==i && !port);
        map_gesture(f,x,y,x,y);
        assert(g.target_body==i && g.target==-1 && g.system_display==1);
        map_gesture(f,x,y,x,y);
        assert(g.system_display==2);
        click(f,UI_NAV+'z'); picked=1;
    }
    assert(picked);
    /* A drag turns the view and targets nothing. */
    i=g.target_body;
    map_gesture(f,60,60,100,80);
    ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch);
    assert(yaw!=0 && pitch!=-200 && g.target_body==i && g.system_display==1);
    /* Zoom by button and key, then ALL frames the system again. */
    click(f,SYS_IN); game_action(&g,'+');
    ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch); assert(extent<before*2/3);
    click(f,SYS_OUT); ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch); assert(extent<before && extent>before/3);
    click(f,SYS_WHOLE); ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch); assert(extent==before && !g.map_local);
    /* FOCUS follows the target as it moves; a port, once resolved, is
       clicked to target it. */
    g.target=0; g.target_body=-1;
    click(f,SYS_FOCUS);
    for(i=0;i<20;i++) click(f,SYS_IN);
    ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch);
    assert(length(sub(centre,scale(g.ports[0].pos,1,FP)))<2);
    assert(ui_sysmap_project(&g,h,scale(g.ports[0].pos,1,FP),&x,&y,&d)==1);
    g.target=-1; g.target_body=0;
    map_gesture(f,x,y,x,y);
    assert(g.target==0 && g.target_body==-1);
    { int b=g.ports[0].body; Vec was=g.system.planets[b].center;
      g.tick+=HZ*3600; update_world(&g);
      ui_sysmap_view(&g,&centre,&extent,&yaw,&pitch);
      assert(length(sub(g.system.planets[b].center,was))>0 && length(sub(centre,scale(g.ports[0].pos,1,FP)))<2); }
    /* PAN drags the view instead of turning it, and stops following. */
    { Vec was; ui_sysmap_view(&g,&was,&extent,&yaw,&pitch);
      click(f,SYS_PAN); map_gesture(f,100,60,140,70);
      { int yaw2,pitch2; ui_sysmap_view(&g,&centre,&extent,&yaw2,&pitch2);
        assert(length(sub(centre,was))>0 && yaw2==yaw && pitch2==pitch && g.sysmap_follow==-1); }
      click(f,SYS_PAN); }
    /* LOCAL (C) toggles the target's neighbourhood and the whole system. */
    game_action(&g,'c'); assert(g.map_local);
    game_action(&g,'c'); assert(!g.map_local);
    puts("PASS system map camera: drag turns, zoom, focus follows, clicks target bodies and ports");
}
static void workflow(Frame *f) {
    UiWidget v[UI_MAX_WIDGETS]; int n,i,credits,cargo;
    game_init(&g,0x53504143);
    click(f,SERVICE_DIRECTORY); assert(g.page==SERVICES);
    click(f,SERVICE_SELECT+1); click(f,SERVICE_CALL); assert(g.page==MARKET);
    click(f,UI_NAV+'m'); assert(g.page==MARKET);
    click(f,UI_ROW+7); assert(g.selection==7);
    click(f,UI_ROW); credits=g.credits; cargo=g.cargo[0];
    click(f,'+'); assert(g.cargo[0]==cargo+1 && g.credits<credits);
    click(f,'-'); assert(g.cargo[0]==cargo);
    click(f,UI_NAV+'y'); assert(g.page==SYSTEM_MAP && !g.system_display);
    click(f,'q'); assert(g.target<0);
    click(f,UI_ROW+1); assert(g.target_body==1 && g.clearance==-1 && !g.system_display);
    click(f,UI_ROW+1); assert(g.target_body==1 && g.system_display==2); /* Second click: data sheet. */
    click(f,UI_NAV+'z'); assert(g.system_display==1);
    system_map(f);
    assert(click(f,UI_SAVE)==UI_SAVE); assert(click(f,UI_LOAD)==UI_LOAD);
    assert(save_game(&g, "ui-roundtrip.sav"));
    g.ui_x=31; g.ui_y=41; g.ui_down=g.ui_fire=g.ui_weapons=1;
    assert(load_game(&g,"ui-roundtrip.sav"));
    assert(g.ui_x==31 && g.ui_y==41 && !g.ui_down && !g.ui_fire && !g.ui_weapons);
    remove("ui-roundtrip.sav");
    click(f,UI_NAV+'v'); assert(g.page==FLIGHT && !g.camera_mode);
    click(f,'l'); assert(g.docked==-1);
    /* The drive cannot run inside the departure port's exclusion zone. */
    n=ui_widgets(&g,f,v);
    for(i=0;i<n;i++) if(v[i].action=='a') assert(!v[i].enabled);
    click(f,'a'); assert(!g.drive_manual);
    g.frame_body=-1; g.pos=vec(0,AU_METRES*FP,0); g.vel=vec(0,0,0);
    click(f,'p'); assert(g.autopilot && !g.drive_manual);
    click(f,'a'); assert(g.autopilot && g.drive_manual);
    click(f,'p'); assert(!g.autopilot && g.drive_manual);
    click(f,'p'); assert(g.autopilot && g.drive_manual);
    click(f,'a'); assert(g.autopilot && !g.drive_manual);
    click(f,UI_WARP+6); assert(g.warp==4096);
    click(f,' '); assert(g.paused); click(f,' '); assert(!g.paused);
    click(f,UI_SCAN); assert(g.ui_weapons); click(f,UI_SCAN); assert(!g.ui_weapons);
    click(f,'p'); assert(!g.autopilot);
    click(f,UI_THROTTLE); assert(g.throttle>1800 && g.throttle<2200);
    click(f,UI_THROTTLE+1); assert(g.throttle==0);
    ui_pointer(&g,f,20,60,1); assert(ui_fire(&g,f));
    ui_pointer(&g,f,20,f->h-5,1); assert(!ui_fire(&g,f));
    ui_cancel(&g); assert(!g.ui_down && !ui_fire(&g,f));
    /* Release outside the pressed control cancels its operation. */
    n=ui_widgets(&g,f,v);
    for(i=0;i<n;i++) if(v[i].action=='p') {
        ui_pointer(&g,f,(v[i].x+2)*ui_scale(f),(v[i].y+2)*ui_scale(f),1);
        ui_pointer(&g,f,20,60,0); assert(!g.autopilot);
    }
    g.page=MARKET; g.docked=-1; cargo=g.cargo[0]; credits=g.credits;
    click(f,'+'); assert(g.cargo[0]==cargo && g.credits==credits);
    click(f,UI_NAV+'v');
    g.dead=1; g.throttle=0; click(f,UI_THROTTLE); assert(!g.throttle);
    n=ui_widgets(&g,f,v);
    for(i=0;i<n;i++) assert(v[i].action!=UI_EXIT && v[i].action!=UI_SAVE_EXIT);
    click(f,UI_RESTART); assert(!g.dead && g.hull==100 && g.docked>=0);
    puts("PASS mouse trade/navigation/launch/auto/warp/pause/throttle and click/fire isolation");
}
static void quit_dialog(Frame *f) {
    UiWidget v[UI_MAX_WIDGETS];
    int page, dead, paused, i, n;
    for (page=FLIGHT; page<=SERVICES; page++) for (dead=0; dead<2; dead++) {
        game_init(&g,0x53504143); g.page=page; g.dead=dead;
        n=ui_widgets(&g,f,v);
        for(i=0;i<n;i++) assert(v[i].action!=UI_EXIT && v[i].action!=UI_SAVE_EXIT);
        for(paused=0;paused<2;paused++) {
            uint32_t tick=g.tick;
            Controls c={0};
            g.paused=paused;
            assert(!ui_quit_key(&g,27) && g.ui_quit);
            assert(ui_widgets(&g,f,v)==3);
            game_action(&g,' '); game_action(&g,'r'); game_action(&g,'g');
            assert(g.paused==paused && g.dead==dead && g.page==page);
            c.fire=1; c.thrust=1;
            game_tick(&g,c); game_frame(&g,c,10);
            assert(g.tick==tick);
            /* Empty modal background cannot fire or activate the old UI. */
            ui_pointer(&g,f,20,40,1); assert(!ui_fire(&g,f));
            assert(!ui_pointer(&g,f,20,40,0));
            assert(click(f,UI_SAVE_EXIT)==UI_SAVE_EXIT && g.ui_quit);
            assert(click(f,UI_EXIT)==UI_EXIT && g.ui_quit);
            assert(ui_quit_key(&g,'s')==UI_SAVE_EXIT);
            assert(ui_quit_key(&g,'q')==UI_EXIT);
            /* Enter never chooses a destructive default. */
            assert(!ui_quit_key(&g,'\r') && g.ui_quit);
            click(f,UI_CANCEL_EXIT);
            assert(!g.ui_quit && g.paused==paused && g.dead==dead && g.page==page);
            ui_quit_key(&g,27); ui_quit_key(&g,27);
            assert(!g.ui_quit && g.paused==paused);
            ui_quit_key(&g,27); ui_quit_key(&g,'c'); assert(!g.ui_quit);
        }
    }
    game_init(&g,0x53504143); ui_quit_key(&g,27);
    assert(save_game(&g,"ui-quit.sav"));
    g.ui_quit=2;
    assert(load_game(&g,"ui-quit.sav") && !g.ui_quit && !g.paused);
    remove("ui-quit.sav");
    puts("PASS quit modal on every page, paused/dead states, keyboard/mouse isolation and save roundtrip");
}
static void station_workflow(Frame *f) {
    int i,credits; Mission offered; Sector old_waypoint={2,0,3}; char name[40],again[40];
    game_init(&g,0x53504143);
    game_action(&g,'s'); assert(g.page==SERVICES);
    contact_name(service_identity(&g,0),name,sizeof(name));
    contact_name(service_identity(&g,0),again,sizeof(again)); assert(!strcmp(name,again));
    for(i=0;i<PORT_SERVICES;i++) {
        click(f,SERVICE_DIRECTORY); click(f,SERVICE_SELECT+i);
        assert(g.service_selection==i); credits=g.credits;
        click(f,SERVICE_CALL);
        if(i==4) assert(g.page==SERVICES && g.credits==credits-100 && g.hull==100 && g.fuel==100);
        else assert((g.page==(int[]){BOARD,MARKET,SHIPYARD,EQUIPMENT,SERVICES,CAREER}[i]));
    }
    game_action(&g,'b'); assert(g.page==BOARD && !g.mission_detail);
    offered=g.board[0]; click(f,UI_ROW); assert(g.mission_detail && g.selection==0);
    for(i=0;i<3;i++) { click(f,MISSION_TAB+i); assert(g.mission_tab==i); }
    g.waypoint=old_waypoint; g.waypoint_set=1; g.galaxy_ready=1;
    g.galaxy_searching=1; snprintf(g.galaxy_query,sizeof(g.galaxy_query),"SOL");
    click(f,MISSION_MAP); assert(g.page==MAP && same_sector(g.selected,offered.destination));
    assert(!g.galaxy_searching && !g.galaxy_query[0] && !g.galaxy_detail);
    assert(g.galaxy_center.x==galaxy_position(offered.destination).x && g.galaxy_extent==12000);
    assert(g.waypoint_set && same_sector(g.waypoint,old_waypoint));
    click(f,GAL_WAYPOINT); assert(g.waypoint_set && same_sector(g.waypoint,offered.destination));
    click(f,MISSION_RETURN); assert(g.page==BOARD && g.mission_detail && g.selection==0 && g.mission_tab==2);
    assert(!mission_refusal(&g,0)); click(f,'\r'); assert(g.page==LOG && g.mission_detail && g.contracts[g.selection].id==offered.id);
    g.contracts[g.selection].destination=g.system.sector; g.contracts[g.selection].port=1;
    click(f,MISSION_MAP); assert(g.target==1 && g.clearance==-1 && !g.dock_stage);
    click(f,MISSION_RETURN); assert(g.page==LOG && g.mission_detail);
    click(f,MISSION_LIST); click(f,UI_ROW); assert(g.mission_detail);
    for(i=0;i<MISSION_TYPES;i++) { Mission m=offered; m.type=(MissionType)i; assert(strlen(mission_story(&m))>80 && strlen(mission_orders(&m))>70); }
    click(f,SERVICE_DIRECTORY); assert(save_game(&g,"station-ui.sav"));
    assert(load_game(&g,"station-ui.sav") && g.page==SERVICES && !g.mission_detail && !g.mission_map_return);
    remove("station-ui.sav");
    g.docked=-1; credits=g.credits;
    click(f,SERVICE_SELECT+4); click(f,SERVICE_CALL); assert(g.page==SERVICES && g.credits==credits);
    game_action(&g,'b'); click(f,UI_ROW); click(f,MISSION_MAP); assert(g.page==MAP);
    g.page=FLIGHT; game_action(&g,'s'); assert(g.page==FLIGHT); /* S remains reverse thrust in flight. */
    puts("PASS station calls, mission stories/terms/orders, acceptance, route focus/waypoint/return and saved directory");
}
int main(void) {
    Frame f={320,200,0,NULL,{0}};
    workflow(&f); station_workflow(&f); quit_dialog(&f); f.h=256; workflow(&f); station_workflow(&f); quit_dialog(&f); f.w=640; f.h=480; workflow(&f); station_workflow(&f); quit_dialog(&f);
    return 0;
}
