/* SPDX-License-Identifier: Unlicense */
#include "ui.h"
#include "galaxy.h"
#include "services.h"
#include <stdio.h>
#include <string.h>
int ui_scale(const Frame *f) { return f->w >= 640 ? 2 : 1; }
int ui_view_height(const Game *g, const Frame *f) {
    return g->page == FLIGHT ? f->h - (g->death_started ? 32 : 80) * ui_scale(f) : f->h;
}
void ui_orbit_geometry(const Game *g, Vec *origin, int64_t *extent, int *parent) {
    int i, b=g->target>=0?g->ports[g->target].body:g->target_body;
    *origin=vec(0,0,0); *extent=1; *parent=-1;
    if(g->map_local) {
        *parent=g->system.planets[b].parent;
        if(*parent<0) *parent=b;
        *origin=g->system.planets[*parent].center;
        *extent=(int64_t)g->system.planets[*parent].radius*4;
    }
    for(i=0;i<g->system.count;i++) {
        const Planet *p=&g->system.planets[i];
        int64_t d=g->map_local?(p->parent==*parent?p->orbit:0):p->orbit+(p->parent>=0?g->system.planets[p->parent].orbit:0);
        if(p->catalog && (!g->map_local || p->parent==*parent)) {
            d+=p->orbit*p->eccentricity/1000000;
            if(!g->map_local && p->parent>=0) d+=g->system.planets[p->parent].orbit*g->system.planets[p->parent].eccentricity/1000000;
        }
        if(d>*extent) *extent=d;
    }
    if(g->map_local) for(i=0;i<g->nports;i++) if(g->ports[i].body==*parent) {
        int64_t d=length(sub(scale(g->ports[i].pos,1,FP),*origin));
        if(d>*extent) *extent=d;
    }
    *extent=*extent*11/10;
}
void ui_map_point(Vec p,Vec origin,int64_t extent,int *x,int *y) {
    Vec r=scale(sub(p,origin),62,extent);
    /* Clamp astronomical projections before narrowing to host/native int. */
    int64_t xx=160+r.x, yy=86-r.z*46/62;
    *x=xx < -1000?-1000:xx>1000?1000:(int)xx;
    *y=yy < -1000?-1000:yy>1000?1000:(int)yy;
}
static void button(UiWidget *v, int *n, int x, int y, int w, int h,
                   int action, const char *label, int enabled, int active) {
    UiWidget *b;
    if(*n>=UI_MAX_WIDGETS) return;
    b = &v[(*n)++];
    *b = (UiWidget){x,y,w,h,action,enabled,active,{0}};
    snprintf(b->label, sizeof(b->label), "%s", label);
}
int ui_widgets(const Game *g, const Frame *f, UiWidget *v) {
    static const char *tabs[] = {"FLY", "GAL", "NAV", "PORT", "LOG", "SYS", "KIT", "CREW", "HLP"};
    static const int pages[] = {FLIGHT, MAP, SYSTEM_MAP, BOARD, LOG, SYSTEM_MAP, EQUIPMENT, CAREER, HELP};
    static const int keys[] = {'v','g','y','b','j','z','0','9','h'};
    int n = 0, i, h = f->h / ui_scale(f), dock = g->docked >= 0;
    if (g->ui_quit) {
        int y = h/2;
        button(v,&n,50,y-12,220,16,UI_SAVE_EXIT,"SAVE AND QUIT / S",1,0);
        button(v,&n,50,y+7,220,16,UI_EXIT,"QUIT WITHOUT SAVING / Q",1,0);
        button(v,&n,50,y+26,220,16,UI_CANCEL_EXIT,"CANCEL / ESC",1,0);
        return n;
    }
    if (g->death_started) {
        button(v,&n,230,h-25,80,18,UI_RESTART,"RESTART",1,0);
        return n;
    }
    for (i = 0; i < 9; i++)
        button(v,&n,2+i*35,h-16,34,15,i==3?SERVICE_DIRECTORY:UI_NAV+keys[i],tabs[i],1,
               (g->page == pages[i] || (i == 3 && (g->page == MARKET || g->page == SHIPYARD || g->page==SERVICES))) && (g->page != SYSTEM_MAP || g->system_display == (i == 5)));
    if (g->page == FLIGHT) {
        int y = h-80, park_on, park_active, foot = g->on_foot;
        const char *park = park_label(g,&park_on,&park_active);
        button(v,&n,2,y+2,47,13,'v',"VIEW",!foot,0);
        button(v,&n,50,y+2,47,13,'l',"LAUNCH",g->docked != -1 && !foot,0);
        button(v,&n,222,y+2,47,13,'p',"AUTO",g->docked == -1,g->autopilot);
        /* Armed stays clickable so the commander can always switch it off. */
        button(v,&n,270,y+2,47,13,'a',"DRIVE",g->drive_manual || drive_available(g),g->drive_manual);
        button(v,&n,2,y+17,47,13,'e',"ASSIST",!foot,g->assist && !foot);
        /* Landed on a surface the hatch opens; outside it reads BOARD. */
        button(v,&n,50,y+17,47,13,'k',foot?"BOARD":"EXIT",
               foot ? walker_ship_distance(g) <= board_reach(g) : !disembark_refusal(g),0);
        button(v,&n,222,y+17,47,13,foot?'e':'d',park,park_on,park_active);
        button(v,&n,270,y+17,47,13,'r',"LOCK",!foot,0);
        button(v,&n,101,y+2,117,38,UI_SCAN,"",1,g->ui_weapons);
        if (g->ui_weapons) {
            button(v,&n,104,y+14,54,13,',',"MISSILE",g->missiles > 0,0);
            button(v,&n,160,y+14,54,13,'.',"DECOY",g->countermeasures > 0,0);
        }
        button(v,&n,2,y+43,95,8,UI_THROTTLE,"",g->docked == -1,0);
        for (i=0;i<7;i++) {
            char s[12]; int warp=1 << (i*2);
            snprintf(s,sizeof(s),"%d",warp);
            button(v,&n,101+i*31,y+46,30,13,UI_WARP+i,s,1,g->warp==warp);
        }
        button(v,&n,2,y+53,47,10,' ',g->paused ? "PLAY" : "PAUSE",1,g->paused);
        button(v,&n,50,y+53,47,10,UI_THROTTLE+1,"STOP",1,0);
        if (g->dead) {
            button(v,&n,120,y/2-7,80,16,UI_RESTART,"RESTART",1,0);
        }
        return n;
    }
    button(v,&n,254,5,30,13,UI_SAVE,"SAVE",1,0);
    button(v,&n,286,5,30,13,UI_LOAD,"LOAD",1,0);
    if (g->page == MARKET || ((g->page == BOARD || g->page==LOG) && !g->mission_detail) || g->page == SHIPYARD || g->page == EQUIPMENT ||
        (g->page == SYSTEM_MAP && !g->system_display)) {
        int count = g->page == SHIPYARD ? SHIPS : 8;
        for (i=0;i<count;i++) {
            int enabled=1, active=g->selection==i;
            if(g->page==SYSTEM_MAP) {
                int first = g->target < 0 ? g->target_body/8*8 : g->target/8*8;
                enabled = first+i < (g->target < 0 ? g->system.count : g->nports);
                active = (g->target < 0 ? g->target_body : g->target) == first+i;
            }
            /* The shipyard keeps its hull list beside the rotating preview. */
            if(g->page==SHIPYARD) button(v,&n,8,36+i*13,110,12,UI_ROW+i,"",enabled,active);
            else button(v,&n,8,42+i*12,304,11,UI_ROW+i,"",enabled,active);
        }
    }
    if(g->page==MARKET || g->page==SHIPYARD) {
        button(v,&n,8,20,100,12,SERVICE_DIRECTORY,"SERVICES",1,0);
        button(v,&n,110,20,100,12,UI_NAV+'m',"MARKET",1,g->page==MARKET);
        button(v,&n,212,20,100,12,UI_NAV+'u',"SHIPYARD",1,g->page==SHIPYARD);
    }
    if(g->page==SERVICES) {
        for(i=0;i<PORT_SERVICES;i++) button(v,&n,8,40+i*15,176,14,SERVICE_SELECT+i,"",dock,g->service_selection==i);
        button(v,&n,8,h-45,176,14,SERVICE_CALL,g->service_selection==4?"AUTHORISE 100 CR":"CALL / ENTER",dock && (g->service_selection!=4 || g->credits>=100),0);
    }
    if(g->page==BOARD || g->page==LOG) {
        const Mission *m=selected_mission(g); int valid=m && m->id;
        if(g->mission_detail) {
            button(v,&n,8,21,56,12,MISSION_LIST,"LIST",1,0);
            for(i=0;i<3;i++) button(v,&n,66+i*82,21,80,12,MISSION_TAB+i,(const char*[]){"STORY","ORDERS","TERMS"}[i],valid,g->mission_tab==i);
        } else {
            button(v,&n,8,21,90,12,SERVICE_DIRECTORY,"SERVICES",1,0);
            button(v,&n,100,21,104,12,g->page==BOARD?'b':UI_NAV+'b',g->page==BOARD?"MORE NOTICES":"OFFERS",dock,0);
            button(v,&n,206,21,106,12,UI_NAV+'j',"MY CONTRACTS",1,g->page==LOG);
        }
        button(v,&n,8,h-45,98,14,g->mission_detail && g->page==BOARD?'\r':MISSION_OPEN,g->mission_detail && g->page==BOARD?"ACCEPT":"READ BRIEFING",valid && (g->page!=BOARD || !g->mission_detail || !mission_refusal(g,g->selection)),0);
        button(v,&n,108,h-45,98,14,MISSION_MAP,"DESTINATION",valid,0);
        button(v,&n,208,h-45,104,14,g->page==BOARD?'+':MISSION_LIST,g->page==BOARD?"NEGOTIATE":"LIST",valid && (g->page==LOG || (dock && m->state==OFFERED)),0);
    }
    if(g->page==EQUIPMENT || g->page==CAREER) button(v,&n,8,20,100,12,SERVICE_DIRECTORY,"SERVICES",1,0);
    if(g->page==MAP && g->mission_map_return) button(v,&n,210,h-34,104,12,MISSION_RETURN,"BRIEFING",1,0);
    if(g->page==SYSTEM_MAP && g->system_display==1) {
        Vec origin; int64_t extent; int parent,x,y;
        ui_orbit_geometry(g,&origin,&extent,&parent);
        for(i=0;i<g->system.count;i++) {
            if(g->map_local && i!=parent && g->system.planets[i].parent!=parent) continue;
            ui_map_point(g->system.planets[i].center,origin,extent,&x,&y);
            if(n<UI_MAX_WIDGETS-20 && x>=12 && x<308 && y>=34 && y<=136)
                button(v,&n,x-4,y-4,9,9,UI_BODY+i,g->system.planets[i].name,1,i==(g->target<0?g->target_body:g->ports[g->target].body));
        }
        for(i=0;i<g->nports;i++) {
            if(i!=g->target && (!g->map_local || g->ports[i].body!=parent)) continue;
            ui_map_point(scale(g->ports[i].pos,1,FP),origin,extent,&x,&y);
            if(n<UI_MAX_WIDGETS-20 && x>=12 && x<308 && y>=34 && y<=136)
                button(v,&n,x-4,y-4,9,9,UI_PORT+i,g->ports[i].name,1,i==g->target);
        }
    }
    { int y=h-45;
      switch(g->page) {
      case MARKET:
        button(v,&n,8,y,70,14,'+',"BUY 1T",dock,0);
        button(v,&n,80,y,70,14,'-',"SELL 1T",dock,0);
        button(v,&n,152,y,78,14,'b',"BLACK",dock && black_market_available(g),g->black_market);
        button(v,&n,232,y,80,14,'f',"SERVICE",dock,0); break;
      case SHIPYARD: case EQUIPMENT:
        button(v,&n,8,y,74,14,'\r',"BUY",dock,0);
        button(v,&n,84,y,74,14,g->page == EQUIPMENT ? '-' : 'f',g->page == EQUIPMENT ? "SELL" : "SERVICE",dock,0);
        button(v,&n,160,y,74,14,'o',"LASER",dock,0);
        button(v,&n,236,y,76,14,UI_NAV+(g->page==EQUIPMENT?'u':'0'),g->page==EQUIPMENT?"HULLS":"KIT",1,0); break;
      case CAREER:
        button(v,&n,8,y,100,14,'+',"HIRE",dock,0);
        button(v,&n,110,y,100,14,'-',"DISMISS",dock,0);
        button(v,&n,212,y,100,14,'f',"PAY FINE",dock,0); break;
      case MAP:
        button(v,&n,6,36,200,h-104,GAL_DRAG,"",1,0);
        button(v,&n,6,22,308,12,GAL_SEARCH,"",1,g->galaxy_searching);
        button(v,&n,6,h-64,22,13,1003,"<",1,0);
        button(v,&n,30,h-64,22,13,1004,">",1,0);
        button(v,&n,54,h-64,22,13,1001,"UP",1,0);
        button(v,&n,78,h-64,22,13,1002,"DN",1,0);
        button(v,&n,102,h-64,22,13,'+',"+",1,0);
        button(v,&n,126,h-64,22,13,'-',"-",1,0);
        button(v,&n,150,h-64,46,13,GAL_PAN,"PAN",1,g->galaxy_pan_mode);
        button(v,&n,198,h-64,54,13,GAL_HOME,"HOME",1,0);
        button(v,&n,254,h-64,60,13,GAL_OVERVIEW,"GALAXY",1,0);
        button(v,&n,6,h-49,56,13,GAL_FOCUS,"FOCUS",1,0);
        button(v,&n,64,h-49,74,13,GAL_WAYPOINT,"WAYPOINT",1,g->waypoint_set && same_sector(g->waypoint,g->selected));
        button(v,&n,140,h-49,50,13,GAL_INFO,"INFO",1,g->galaxy_detail);
        button(v,&n,192,h-49,50,13,GAL_CLEAR,"CLEAR",1,0);
        button(v,&n,244,h-49,70,13,'x',"JUMP",!strcmp(galaxy_jump_status(g,g->selected),"JUMP READY"),0);
        if(g->galaxy_searching || g->galaxy_query[0]) {
            Sector result[GALAXY_SEARCH]; int count=galaxy_search(g,result,GALAXY_SEARCH),first=g->galaxy_result/6*6;
            for(i=0;i<6 && first+i<count;i++)
                button(v,&n,210,38+i*12,104,11,GAL_RESULT+i,"",1,first+i==g->galaxy_result);
            button(v,&n,210,112,50,13,GAL_PREV,"PREV",count>6,0);
            button(v,&n,262,112,52,13,GAL_NEXT,"NEXT",count>6,0);
        }
        break;
      case SYSTEM_MAP:
        button(v,&n,8,y,59,14,'q',"BODY",1,0);
        button(v,&n,69,y,59,14,'t',"PORT",g->nports>0,0);
        button(v,&n,130,y,59,14,UI_TAB,"PAGE",1,0);
        button(v,&n,191,y,59,14,'c',"SCALE",1,g->map_local);
        button(v,&n,252,y,60,14,'p',"AUTO",g->docked==-1,g->autopilot);
        button(v,&n,8,20,74,12,UI_NAV+'y',"LIST",1,g->system_display==0);
        button(v,&n,84,20,74,12,UI_NAV+'z',"ORBITS",1,g->system_display==1);
        button(v,&n,160,20,74,12,UI_NAV+'i',"DATA",1,g->system_display==2); break;
      default: break;
      }
    }
    return n;
}
void ui_move(Game *g, const Frame *f, int x, int y) {
    if(g->page==MAP && g->ui_down && g->ui_capture==GAL_DRAG) {
        int s=ui_scale(f),dx=(x-g->ui_x)/s,dy=(y-g->ui_y)/s;
        int tx=x/s-g->galaxy_down_x,ty=y/s-g->galaxy_down_y;
        if(tx*tx+ty*ty>16) g->galaxy_dragged=1;
        if(g->galaxy_dragged) galaxy_drag(g,dx,dy);
    }
    g->ui_x = x<0?0:x>=f->w?f->w-1:x;
    g->ui_y = y<0?0:y>=f->h?f->h-1:y;
    g->ui_visible = 1;
}
static int hit(const Game *g,const Frame *f, UiWidget *b) {
    int s=ui_scale(f), x=g->ui_x/s,y=g->ui_y/s;
    return x>=b->x && x<b->x+b->w && y>=b->y && y<b->y+b->h;
}
static UiWidget *pick(const Game *g,const Frame *f,UiWidget *v,int n) {
    while(n--) if(hit(g,f,&v[n])) return &v[n];
    return NULL;
}
static void activate(Game *g,int action,int x) {
    if(g->hyperspace && action!=' ') return;
    if(action>=GAL_SEARCH && action<GAL_RESULT+6) { galaxy_key(g,action); return; }
    if(action==UI_RESTART) game_action(g,'r');
    else if(action>=UI_PORT && action<UI_PORT+MAX_PORTS) {
        g->target=action-UI_PORT; g->target_body=-1; g->resource_target=-1; g->clearance=-1; g->dock_stage=0;
    } else if(action>=UI_BODY && action<UI_BODY+MAX_PLANETS) {
        /* A second click on the selected body opens its data sheet. */
        if(g->target<0 && g->target_body==action-UI_BODY) g->system_display=2;
        g->target=-1; g->target_body=action-UI_BODY; g->resource_target=-1; g->clearance=-1; g->dock_stage=0;
    } else if(action==UI_TAB) {
        if(g->target<0) g->target_body=g->target_body/8*8+8<g->system.count?g->target_body/8*8+8:0;
        else if(g->nports) g->target=g->target/8*8+8<g->nports?g->target/8*8+8:0;
        g->resource_target=-1; g->clearance=-1; g->dock_stage=0;
    } else if(action==UI_SCAN) g->ui_weapons=!g->ui_weapons;
    else if(action==UI_THROTTLE || action==UI_THROTTLE+1) {
        g->throttle=action==UI_THROTTLE+1?0:(x-2)*4000/94;
    } else if(action>=UI_WARP && action<UI_WARP+7) g->warp=1<<((action-UI_WARP)*2);
    else if(action>=UI_NAV && action<UI_NAV+128) {
        int k=action-UI_NAV;
        g->galaxy_searching=0;
        if(k=='v') g->page=FLIGHT; else game_action(g,k);
    } else if(action>=UI_ROW && action<UI_ROW+8) {
        int i=action-UI_ROW;
        if(g->page==SYSTEM_MAP) {
            if(g->target<0 && g->target_body==g->target_body/8*8+i) g->system_display=2;
            if(g->target<0) g->target_body=g->target_body/8*8+i;
            else g->target=g->target/8*8+i;
            g->resource_target=-1; g->clearance=-1; g->dock_stage=0;
        } else { g->selection=i; if(g->page==BOARD || g->page==LOG) { g->mission_detail=1; g->mission_tab=0; } }
    } else game_action(g,action);
}
int ui_pointer(Game *g,const Frame *f,int x,int y,int down) {
    UiWidget v[UI_MAX_WIDGETS], *b;
    int n=ui_widgets(g,f,v), was=g->ui_down;
    ui_move(g,f,x,y);
    b=pick(g,f,v,n);
    if (g->page != g->ui_press_page || g->paused || g->dead) g->ui_fire=0;
    if (g->page != g->ui_press_page) g->ui_capture=-1;
    g->ui_down=down;
    if(down && !was) {
        g->ui_capture = b ? b->action : -1;
        g->ui_press_page=g->page;
        if(g->page==MAP) { g->galaxy_down_x=x/ui_scale(f); g->galaxy_down_y=y/ui_scale(f); g->galaxy_dragged=0; }
        g->ui_fire = !g->ui_quit && !b && g->page==FLIGHT && y<ui_view_height(g,f) && !g->paused && !g->dead;
        if(b && b->enabled && !g->dead && b->action==UI_THROTTLE) activate(g,b->action,g->ui_x/ui_scale(f));
    }
    if(down && b && b->enabled && !g->dead && b->action==UI_THROTTLE && g->ui_capture==UI_THROTTLE)
        activate(g,b->action,g->ui_x/ui_scale(f));
    if(!down && was) {
        int action=g->ui_capture;
        g->ui_capture=-1; g->ui_fire=0;
        if(b && b->enabled && b->action==action && g->page==g->ui_press_page && (!g->dead || g->ui_quit || action==UI_RESTART)) {
            if(action==UI_CANCEL_EXIT) { g->ui_quit=0; ui_cancel(g); return 0; }
            if(action==UI_SAVE || action==UI_LOAD || action==UI_EXIT || action==UI_SAVE_EXIT) return action;
            if(action==GAL_DRAG) {
                Sector selected;
                if(!g->galaxy_detail && !g->galaxy_dragged && galaxy_pick(g,f,g->ui_x/ui_scale(f),g->ui_y/ui_scale(f),&selected)) g->selected=selected;
            } else activate(g,action,g->ui_x/ui_scale(f));
        }
    }
    return 0;
}
int ui_fire(const Game *g,const Frame *f) {
    return !g->ui_quit && g->ui_down && g->ui_fire && g->page==FLIGHT && g->ui_y<ui_view_height(g,f) && !g->paused && !g->dead;
}
int ui_quit_key(Game *g, int key) {
    if(key==27 && !g->ui_quit && g->page==MAP && g->galaxy_searching) { g->galaxy_searching=0; return 0; }
    if (key == 27) {
        g->ui_quit = g->ui_quit ? 0 : 1;
        ui_cancel(g);
    } else if (g->ui_quit) {
        if (key == 's') return UI_SAVE_EXIT;
        if (key == 'q') return UI_EXIT;
        if (key == 'c') { g->ui_quit=0; ui_cancel(g); }
    }
    return 0;
}
void ui_cancel(Game *g) { g->ui_down=g->ui_fire=0; g->ui_capture=-1; g->ui_visible=0; }
