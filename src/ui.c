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
static void orbit_geometry(const Game *g, int local, Vec *origin, int64_t *extent, int *parent) {
    int i, b=g->target>=0?g->ports[g->target].body:g->target_body;
    *origin=vec(0,0,0); *extent=1; *parent=-1;
    if(local) {
        *parent=g->system.planets[b].parent;
        if(*parent<0) *parent=b;
        *origin=g->system.planets[*parent].center;
        *extent=(int64_t)g->system.planets[*parent].radius*4;
    }
    for(i=0;i<g->system.count;i++) {
        const Planet *p=&g->system.planets[i];
        int64_t d=local?(p->parent==*parent?p->orbit:0):p->orbit+(p->parent>=0?g->system.planets[p->parent].orbit:0);
        if(p->catalog && (!local || p->parent==*parent)) {
            d+=p->orbit*p->eccentricity/1000000;
            if(!local && p->parent>=0) d+=g->system.planets[p->parent].orbit*g->system.planets[p->parent].eccentricity/1000000;
        }
        if(d>*extent) *extent=d;
    }
    if(local) for(i=0;i<g->nports;i++) if(g->ports[i].body==*parent) {
        int64_t d=length(sub(scale(g->ports[i].pos,1,FP),*origin));
        if(d>*extent) *extent=d;
    }
    *extent=*extent*11/10;
}
void ui_orbit_geometry(const Game *g, Vec *origin, int64_t *extent, int *parent) {
    orbit_geometry(g,g->map_local,origin,extent,parent);
}
/* ------------------------------------------------------------ system map
   A 3D camera like the galaxy map's: perspective from three half-widths
   back, yaw and pitch in table angles (negative pitch looks down on the
   system plane). Until a control sets it for this system, the map shows
   the whole system from above at a slant, so it needs no setup and
   recentres itself after a jump. */
#define SYSMAP_PITCH (-200)
static int sysmap_set(const Game *g) { return g->sysmap_extent > 0 && g->sysmap_system == g->system.seed; }
static Vec sysmap_follow_point(const Game *g, int follow, Vec otherwise) {
    if(follow>=0 && follow<g->system.count) return g->system.planets[follow].center;
    if(follow==-2) return scale(g->pos,1,FP);
    if(follow<=-3 && -3-follow<g->nports) return scale(g->ports[-3-follow].pos,1,FP);
    return otherwise;
}
void ui_sysmap_view(const Game *g, Vec *centre, int64_t *extent, int *yaw, int *pitch) {
    if(!sysmap_set(g)) {
        int parent;
        orbit_geometry(g,0,centre,extent,&parent);
        *yaw=0; *pitch=SYSMAP_PITCH;
        return;
    }
    *centre=sysmap_follow_point(g,g->sysmap_follow,g->sysmap_center);
    *extent=g->sysmap_extent; *yaw=g->sysmap_yaw; *pitch=g->sysmap_pitch;
}
static void sysmap_take(Game *g) {
    Vec centre; int64_t extent; int yaw,pitch;
    if(sysmap_set(g)) return;
    ui_sysmap_view(g,&centre,&extent,&yaw,&pitch);
    g->sysmap_center=centre; g->sysmap_extent=extent; g->sysmap_yaw=yaw; g->sysmap_pitch=pitch;
    g->sysmap_follow=-1; g->sysmap_pan_mode=0; g->sysmap_system=g->system.seed;
}
int ui_sysmap_scale(int h) {
    int half=(SYSMAP_BOTTOM(h)-SYSMAP_TOP)/2;
    return half<(SYSMAP_RIGHT-SYSMAP_LEFT)/2 ? half : (SYSMAP_RIGHT-SYSMAP_LEFT)/2;
}
int ui_sysmap_project(const Game *g, int h, Vec p, int *x, int *y, int *depth) {
    Vec centre, v; int64_t e,xx,yy,zz,d,k=3LL*ui_sysmap_scale(h); int yaw,pitch,top=SYSMAP_TOP,bottom=SYSMAP_BOTTOM(h);
    ui_sysmap_view(g,&centre,&e,&yaw,&pitch);
    if(e<1) e=1;
    v=sub(p,centre);
    xx=(v.x*cosi(yaw)-v.z*sini(yaw))/16384;
    zz=(v.x*sini(yaw)+v.z*cosi(yaw))/16384;
    yy=(v.y*cosi(pitch)-zz*sini(pitch))/16384;
    zz=(v.y*sini(pitch)+zz*cosi(pitch))/16384;
    d=3*e+zz;
    if(d<e/4) return -1;
    xx=160+xx*k/d; yy=(top+bottom)/2-yy*k/d;
    *x=(int)(xx<-30000?-30000:xx>30000?30000:xx);
    *y=(int)(yy<-30000?-30000:yy>30000?30000:yy);
    *depth=(int)(zz*100/e<-1000000?-1000000:zz*100/e>1000000?1000000:zz*100/e);
    return *x>=SYSMAP_LEFT && *x<SYSMAP_RIGHT && *y>=top && *y<bottom;
}
/* Ties go to planets, then moons, then ports, then the nearer: at system
   scale a click on a planet takes the planet, not a moon or port sharing
   its pixel; zoomed in, those separate and take their own clicks. */
int ui_sysmap_pick(const Game *g, int h, int x, int y, int *port) {
    int i,best=50,rank=3,depth=1000000,found=-1,px,py,pd;
    *port=0;
    for(i=0;i<g->system.count+g->nports;i++) {
        int is_port=i>=g->system.count, k=is_port?i-g->system.count:i, d, r;
        Vec p=is_port?scale(g->ports[k].pos,1,FP):g->system.planets[k].center;
        if(ui_sysmap_project(g,h,p,&px,&py,&pd)!=1) continue;
        d=(px-x)*(px-x)+(py-y)*(py-y);
        r=is_port?2:g->system.planets[k].parent>=0;
        if(d<best || (d==best && (r<rank || (r==rank && pd<depth)))) {
            best=d; rank=r; depth=pd; found=k; *port=is_port;
        }
    }
    return found;
}
/* Whole system, or the neighbourhood of the selected body's primary. */
static void sysmap_frame(Game *g,int local) {
    Vec origin; int64_t extent; int parent;
    sysmap_take(g);
    orbit_geometry(g,local,&origin,&extent,&parent);
    g->sysmap_center=origin; g->sysmap_extent=extent; g->sysmap_follow=local?parent:-1;
    g->map_local=local;
}
int ui_sysmap_key(Game *g, int key) {
    int64_t whole; Vec origin; int parent, b=g->target>=0?g->ports[g->target].body:g->target_body;
    if(g->page!=SYSTEM_MAP || g->system_display!=1) return 0;
    switch(key) {
    case SYS_LEFT: case 1003: sysmap_take(g); g->sysmap_yaw=(g->sysmap_yaw-24)&1023; return 1;
    case SYS_RIGHT: case 1004: sysmap_take(g); g->sysmap_yaw=(g->sysmap_yaw+24)&1023; return 1;
    case SYS_UP: case SYS_DOWN:
        sysmap_take(g);
        g->sysmap_pitch+=key==SYS_UP?-24:24;
        if(g->sysmap_pitch< -250) g->sysmap_pitch=-250;
        if(g->sysmap_pitch>250) g->sysmap_pitch=250;
        return 1;
    case SYS_IN: case '+': case '=':
        sysmap_take(g);
        g->sysmap_extent=g->sysmap_extent*2/3;
        if(g->sysmap_extent<200000) g->sysmap_extent=200000; /* 200 km: a port and its pad apart */
        return 1;
    case SYS_OUT: case '-':
        sysmap_take(g);
        orbit_geometry(g,0,&origin,&whole,&parent);
        g->sysmap_extent=g->sysmap_extent*3/2;
        if(g->sysmap_extent>whole*4) g->sysmap_extent=whole*4;
        return 1;
    case SYS_PAN: sysmap_take(g); g->sysmap_pan_mode=!g->sysmap_pan_mode; return 1;
    case SYS_FOCUS: case 'f':
        /* Centre on the target and follow it, closing to its neighbourhood. */
        sysmap_take(g);
        g->sysmap_follow=g->target>=0?-3-g->target:b;
        orbit_geometry(g,1,&origin,&whole,&parent);
        if(g->sysmap_extent>whole) g->sysmap_extent=whole;
        return 1;
    case SYS_SHIP: case 'e': sysmap_take(g); g->sysmap_follow=-2; return 1;
    case SYS_WHOLE: sysmap_frame(g,0); return 1;
    case SYS_LOCAL: sysmap_frame(g,!g->map_local); return 1;
    default: return 0;
    }
}
/* Dragging turns the view, or in PAN mode moves it with the pointer. */
void ui_sysmap_drag(Game *g, int h, int dx, int dy) {
    Vec centre; int64_t e; int yaw,pitch;
    sysmap_take(g);
    if(!g->sysmap_pan_mode) {
        g->sysmap_yaw=(g->sysmap_yaw+dx*3)&1023;
        g->sysmap_pitch+=dy*3;
        if(g->sysmap_pitch< -250) g->sysmap_pitch=-250;
        if(g->sysmap_pitch>250) g->sysmap_pitch=250;
        return;
    }
    ui_sysmap_view(g,&centre,&e,&yaw,&pitch);
    {
        int64_t sy=sini(yaw),cy=cosi(yaw),sp=sini(pitch),cp=cosi(pitch);
        Vec right=vec(cy,0,-sy), up=vec(-sp*sy/16384,cp,-sp*cy/16384);
        int k=ui_sysmap_scale(h);
        centre=sub(centre,scale(right,(int64_t)dx*e/k,16384));
        centre=add(centre,scale(up,(int64_t)dy*e/k,16384));
    }
    g->sysmap_center=centre; g->sysmap_follow=-1;
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
        button(v,&n,222,y+17,47,13,park_key(g),park,park_on,park_active);
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
        /* The map itself: drag to turn (or pan), click a body or port to
           target it. The camera row sits between it and the page buttons. */
        int y=h-77;
        button(v,&n,SYSMAP_LEFT,SYSMAP_TOP,SYSMAP_RIGHT-SYSMAP_LEFT,SYSMAP_BOTTOM(h)-SYSMAP_TOP,SYS_DRAG,"",1,0);
        button(v,&n,6,y,20,13,SYS_LEFT,"<",1,0);
        button(v,&n,28,y,20,13,SYS_RIGHT,">",1,0);
        button(v,&n,50,y,20,13,SYS_UP,"UP",1,0);
        button(v,&n,72,y,20,13,SYS_DOWN,"DN",1,0);
        button(v,&n,94,y,20,13,SYS_IN,"+",1,0);
        button(v,&n,116,y,20,13,SYS_OUT,"-",1,0);
        button(v,&n,138,y,36,13,SYS_PAN,"PAN",1,g->sysmap_pan_mode && g->sysmap_system==g->system.seed);
        button(v,&n,176,y,50,13,SYS_FOCUS,"FOCUS",1,0);
        button(v,&n,228,y,40,13,SYS_SHIP,"SHIP",1,0);
        button(v,&n,270,y,44,13,SYS_WHOLE,"ALL",1,0);
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
        button(v,&n,191,y,59,14,'c',"LOCAL",1,g->map_local && g->sysmap_system==g->system.seed);
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
    if(g->ui_down && ((g->page==MAP && g->ui_capture==GAL_DRAG) ||
                      (g->page==SYSTEM_MAP && g->ui_capture==SYS_DRAG))) {
        int s=ui_scale(f),dx=(x-g->ui_x)/s,dy=(y-g->ui_y)/s;
        int tx=x/s-g->galaxy_down_x,ty=y/s-g->galaxy_down_y;
        if(tx*tx+ty*ty>16) g->galaxy_dragged=1;
        if(g->galaxy_dragged) {
            if(g->page==MAP) galaxy_drag(g,dx,dy);
            else ui_sysmap_drag(g,f->h/s,dx,dy);
        }
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
    if(action>SYS_DRAG && action<=SYS_LOCAL) { ui_sysmap_key(g,action); return; }
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
        if(g->page==MAP || g->page==SYSTEM_MAP) { g->galaxy_down_x=x/ui_scale(f); g->galaxy_down_y=y/ui_scale(f); g->galaxy_dragged=0; }
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
            } else if(action==SYS_DRAG) {
                /* A click, not a drag, targets the body or port under it. */
                int port,k=g->galaxy_dragged ? -1 : ui_sysmap_pick(g,f->h/ui_scale(f),g->ui_x/ui_scale(f),g->ui_y/ui_scale(f),&port);
                if(k>=0) activate(g,(port?UI_PORT:UI_BODY)+k,0);
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
