/* SPDX-License-Identifier: Unlicense */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static Game g;
unsigned long __stack=262144;
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
static void workflow(Frame *f) {
    UiWidget v[UI_MAX_WIDGETS]; int n,i,credits,cargo;
    game_init(&g,0x53504143);
    click(f,UI_NAV+'b'); assert(g.page==BOARD);
    click(f,UI_NAV+'m'); assert(g.page==MARKET);
    click(f,UI_ROW+7); assert(g.selection==7);
    click(f,UI_ROW); credits=g.credits; cargo=g.cargo[0];
    click(f,'+'); assert(g.cargo[0]==cargo+1 && g.credits<credits);
    click(f,'-'); assert(g.cargo[0]==cargo);
    click(f,UI_NAV+'y'); assert(g.page==SYSTEM_MAP && !g.system_display);
    click(f,'q'); assert(g.target<0);
    click(f,UI_ROW+1); assert(g.target_body==1 && g.clearance==-1);
    click(f,UI_NAV+'z'); assert(g.system_display==1);
    /* Pick an isolated orbital marker through the same projection as rendering. */
    n=ui_widgets(&g,f,v);
    for(i=0;i<n;i++) if(v[i].action>=UI_BODY && v[i].action<UI_BODY+MAX_PLANETS) {
        int j,overlap=0;
        for(j=i+1;j<n;j++) if(v[j].action>=UI_BODY &&
            abs(v[i].x-v[j].x)<9 && abs(v[i].y-v[j].y)<9) overlap=1;
        if(!overlap) { click(f,v[i].action); assert(g.target_body==v[i].action-UI_BODY && g.target==-1); break; }
    }
    assert(click(f,UI_SAVE)==UI_SAVE); assert(click(f,UI_LOAD)==UI_LOAD);
    assert(save_game(&g, "ui-roundtrip.sav"));
    g.ui_x=31; g.ui_y=41; g.ui_down=g.ui_fire=g.ui_weapons=1;
    assert(load_game(&g,"ui-roundtrip.sav"));
    assert(g.ui_x==31 && g.ui_y==41 && !g.ui_down && !g.ui_fire && !g.ui_weapons);
    remove("ui-roundtrip.sav");
    click(f,UI_NAV+'v'); assert(g.page==FLIGHT && !g.camera_mode);
    click(f,'l'); assert(g.docked==-1);
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
    assert(click(f,UI_EXIT)==UI_EXIT);
    click(f,UI_RESTART); assert(!g.dead && g.hull==100 && g.docked>=0);
    puts("PASS mouse trade/navigation/launch/auto/warp/pause/throttle and click/fire isolation");
}
int main(void) {
    Frame f={320,200,0,NULL,{0}};
    workflow(&f); f.h=256; workflow(&f); f.w=640; f.h=480; workflow(&f);
    return 0;
}
