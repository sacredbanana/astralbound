/* SPDX-License-Identifier: Unlicense */
#include "galaxy.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "star_catalog.inc"
const char *stellar_names[] = {"O BLUE", "B BLUE", "A WHITE", "F WHITE", "G YELLOW", "K ORANGE", "M RED DWARF", "GIANT", "WHITE DWARF", "BROWN DWARF", "NEUTRON STAR", "BLACK HOLE", "RED GIANT"};
int same_sector(Sector a,Sector b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
Sector catalog_sector(int i) { return star_catalog[i].sector; }
int catalog_index(Sector s) {
    int i; for(i=0;i<star_catalog_count;i++) if(same_sector(s,catalog_sector(i))) return i;
    return -1;
}
Vec galaxy_position(Sector s) {
    int i=catalog_index(s);
    uint32_t h=sector_seed(0x47414c58U,s);
    if(i>=0) return star_catalog[i].position;
    if(!s.x && !s.y && !s.z) return vec(500,500,500);
    return vec((int64_t)s.x*1000+125+h%750,(int64_t)s.y*1000+125+hash32(h)%750,(int64_t)s.z*1000+125+hash32(h^123U)%750);
}
int64_t galaxy_distance(Sector a,Sector b) { return length(sub(galaxy_position(a),galaxy_position(b))); }
void galaxy_name(uint32_t seed,Sector s,char name[24]) {
    static const char *first[]={"VE","KA","OR","SE","TA","NI","LU","AE","MI","RU","DA","IO","ZE","UL","BA","HE"};
    static const char *last[]={"LORA","NARA","VEL","SEN","RIA","THA","MIN","RIS","RON","LIS","DRA","ZEN","VAR","TIS","LAN","MAR"};
    int c=catalog_index(s); uint32_t r=sector_seed(seed,s),a,b;
    if(c>=0) { snprintf(name,24,"%s",star_catalog[c].name);return; }
    r=hash32(r+0x9e3779b9U);a=r%16;r=hash32(r+0x9e3779b9U);b=r%16;
    snprintf(name,24,"%s%s",first[a],last[b]);
}
StellarType stellar_type(const char *s) {
    if(s[0]=='D') return ST_WHITE_DWARF;
    if(s[0]=='L'||s[0]=='T'||s[0]=='Y') return ST_BROWN_DWARF;
    if(strstr(s,"III") || strstr(s,"II") || strstr(s,"Ib") || strstr(s,"Ia")) return s[0]=='K' || s[0]=='M' ? ST_RED_GIANT : ST_GIANT;
    while(*s && !strchr("OBAFGKM",*s)) s++;
    if(*s) return (StellarType)(strchr("OBAFGKM",*s)-"OBAFGKM");
    return ST_M;
}
static StellarType generated_type(uint32_t seed) {
    unsigned r=hash32(seed^0x53504543U)%1000;
    return r<8?ST_BLACK_HOLE:r<20?ST_NEUTRON:r<85?ST_WHITE_DWARF:r<170?ST_BROWN_DWARF:
        r<220?ST_RED_GIANT:r<650?ST_M:r<800?ST_K:r<920?ST_G:r<965?ST_F:r<987?ST_A:r<998?ST_B:ST_O;
}
StellarType galaxy_type(uint32_t seed,Sector s) {
    int i=catalog_index(s);
    return i>=0?stellar_type(star_catalog[i].spectra[0]):(!s.x && !s.y && !s.z)?ST_G:generated_type(sector_seed(seed,s));
}
int stellar_colour(StellarType t) {
    static const int c[]={13,13,15,15,12,14,14,15,15,21,13,25,14};
    return c[t>=0 && t<ST_TYPES?t:ST_G];
}
static int64_t stellar_radius(StellarType t) {
    static const int64_t radii[]={7000000000LL,3000000000LL,1500000000,1000000000,696000000,500000000,150000000,20000000000LL,7000000,70000000,12000,30000,20000000000LL};
    return radii[t];
}
void galaxy_apply(System *s) {
    int i,c=catalog_index(s->sector);
    if(c>=0) {
        snprintf(s->name,sizeof(s->name),"%s",star_catalog[c].name);
        memset(s->star,0,sizeof(s->star));
        s->stars=star_catalog[c].count;
    }
    for(i=0;i<s->stars;i++) {
        Star *p=&s->star[i];
        p->type=c>=0?stellar_type(star_catalog[c].spectra[i]):i?ST_M:(!s->sector.x && !s->sector.y && !s->sector.z)?ST_G:generated_type(s->seed);
        if(c>=0 || s->sector.x || s->sector.y || s->sector.z) p->radius=stellar_radius(p->type);
        if(c>=0 && s->stars>1) {
            /* Bounded gameplay orbits, not an astrometric binary solution. */
            p->orbit=(int64_t)(i+1)*4000000000LL; p->period=500000; p->phase=i*1024/s->stars;
        }
    }
    /* Giant surfaces must not envelop generated planetary orbits. */
    if(s->stars && s->star[0].radius>AU_METRES/6) for(i=0;i<s->count;i++) if(s->planets[i].parent<0)
        s->planets[i].orbit+=s->star[0].radius*3;
}
void galaxy_reset(Game *g) {
    g->galaxy_extent=12000; g->galaxy_yaw=0; g->galaxy_pitch=100;
    g->galaxy_ready=1; g->galaxy_searching=0; g->galaxy_result=0;
    g->galaxy_query[0]=0; g->waypoint_set=0; g->galaxy_detail=0; g->galaxy_pan_mode=0;
    g->galaxy_center=galaxy_position(g->system.sector);
}
void galaxy_focus(Game *g,Sector s) { g->galaxy_center=galaxy_position(s); }
static void galaxy_rotate(Game *g,int dx,int dy) {
    g->galaxy_yaw=(g->galaxy_yaw+dx*3)&1023;
    g->galaxy_pitch+=dy*3;
    if(g->galaxy_pitch>240) g->galaxy_pitch=240;
    if(g->galaxy_pitch< -240) g->galaxy_pitch=-240;
}
void galaxy_drag(Game *g,int dx,int dy) {
    if(g->galaxy_pan_mode) {
        int64_t x=-(int64_t)dx*g->galaxy_extent/80, y=(int64_t)dy*g->galaxy_extent/80;
        g->galaxy_center=add(g->galaxy_center,vec(x*cosi(g->galaxy_yaw)/16384,y,-x*sini(g->galaxy_yaw)/16384));
        galaxy_pan(g,0,0,0); return;
    }
    galaxy_rotate(g,dx,dy);
}
void galaxy_pan(Game *g,int dx,int dy,int dz) {
    int64_t step=g->galaxy_extent/4;
    Vec d=vec((dx*cosi(g->galaxy_yaw)+dz*sini(g->galaxy_yaw))*step/16384,dy*step,
        (dz*cosi(g->galaxy_yaw)-dx*sini(g->galaxy_yaw))*step/16384);
    g->galaxy_center=add(g->galaxy_center,d);
    if(g->galaxy_center.x>1000000000) g->galaxy_center.x=1000000000;
    if(g->galaxy_center.x< -1000000000) g->galaxy_center.x= -1000000000;
    if(g->galaxy_center.y>1000000000) g->galaxy_center.y=1000000000;
    if(g->galaxy_center.y< -1000000000) g->galaxy_center.y= -1000000000;
    if(g->galaxy_center.z>1000000000) g->galaxy_center.z=1000000000;
    if(g->galaxy_center.z< -1000000000) g->galaxy_center.z= -1000000000;
}
int galaxy_project(const Game *g,const Frame *f,Vec p,int *x,int *y,int *depth) {
    int h=f->h/(f->w>=640?2:1),hh=h-104;
    int64_t e=g->galaxy_extent?g->galaxy_extent:12000,xx,yy,zz,d;
    Vec v=sub(p,g->galaxy_center);
    xx=(v.x*cosi(g->galaxy_yaw)-v.z*sini(g->galaxy_yaw))/16384;
    zz=(v.x*sini(g->galaxy_yaw)+v.z*cosi(g->galaxy_yaw))/16384;
    yy=(v.y*cosi(g->galaxy_pitch)-zz*sini(g->galaxy_pitch))/16384;
    zz=(v.y*sini(g->galaxy_pitch)+zz*cosi(g->galaxy_pitch))/16384;
    d=3*e+zz;
    if(d<e/4) return 0;
    xx=107+xx*240/d; yy=36+hh/2-yy*(hh*3/2)/d;
    if(xx<10 || xx>203 || yy<40 || yy>h-72) return 0;
    *x=(int)xx; *y=(int)yy; *depth=(int)(zz*100/e);
    return 1;
}
static GalaxyMarker markers[GALAXY_MARKERS];
typedef struct { Sector sector; Vec position; int type,catalog; } GalaxyCell;
static GalaxyCell cells[GALAXY_MARKERS];
static uint16_t cell_slots[16384];
static int cells_count,cache_ready,cache_extent;
static Vec cache_center;
static uint32_t cache_seed;
static void add_cell(Sector s) {
    unsigned slot=hash32((uint32_t)s.x^hash32((uint32_t)s.y)^hash32((uint32_t)s.z+123U))&16383;
    GalaxyCell *c;
    while(cell_slots[slot]) {
        if(same_sector(cells[cell_slots[slot]-1].sector,s)) return;
        slot=(slot+1)&16383;
    }
    if(cells_count>=GALAXY_MARKERS || !galaxy_exists(cache_seed,s)) return;
    c=&cells[cells_count];c->sector=s;c->position=galaxy_position(s);
    c->catalog=catalog_index(s);c->type=galaxy_type(cache_seed,s);
    cell_slots[slot]=(uint16_t)++cells_count;
}
const GalaxyMarker *galaxy_markers(const Game *g,const Frame *f,int *n) {
    int i,x,y,z,step=(g->galaxy_extent+7999)/8000,vertical;
    Sector base={(int32_t)(g->galaxy_center.x/1000),(int32_t)(g->galaxy_center.y/1000),(int32_t)(g->galaxy_center.z/1000)};
    if(step<1) step=1;
    vertical=step>100?step/12:step;
    if(!cache_ready || cache_extent!=g->galaxy_extent || cache_seed!=g->seed || memcmp(&cache_center,&g->galaxy_center,sizeof(Vec))) {
        cells_count=0; cache_seed=g->seed; memset(cell_slots,0,sizeof(cell_slots));
        for(i=0;i<star_catalog_count;i++) add_cell(catalog_sector(i));
        for(y=-8;y<=8;y++) for(z=-8;z<=8;z++) for(x=-8;x<=8;x++) {
            Sector s={base.x+x*step,base.y+y*vertical,base.z+z*step};
            if(step>1) {
                uint32_t jitter=sector_seed(g->seed,s);
                s.x+=(int32_t)(jitter%(unsigned)step)-step/2;
                s.z+=(int32_t)(hash32(jitter)%(unsigned)step)-step/2;
                s.y+=(int32_t)(hash32(jitter^123U)%(unsigned)vertical)-vertical/2;
            }
            if(abs(s.x)>1000000 || abs(s.y)>1000000 || abs(s.z)>1000000) continue;
            add_cell(s);
        }
        if(g->galaxy_extent>=1000000) {
            /* Overview samples actual indexed systems, rather than inventing
               markers or missing the now-sparse field by probing empty cells. */
            for(i=0;i<600;i++) {
                Sector sample;
                uint32_t id=(uint32_t)(((uint64_t)(unsigned)i*galaxy_system_count())/600U);
                if(galaxy_system_at(g->seed,id,&sample)) add_cell(sample);
            }
        }
        cache_center=g->galaxy_center; cache_extent=g->galaxy_extent; cache_seed=g->seed; cache_ready=1;
    }
    /* Navigation and journal entries are always represented, even in overview. */
    add_cell(g->system.sector); add_cell(g->selected);
    if(g->waypoint_set) add_cell(g->waypoint);
    for(i=0;i<g->explored_count;i++) add_cell(g->explored[i].sector);
    *n=0;
    for(i=0;i<cells_count;i++) {
        GalaxyMarker m; m.sector=cells[i].sector;
        if(!galaxy_project(g,f,cells[i].position,&m.x,&m.y,&m.depth)) continue;
        m.catalog=cells[i].catalog; m.type=cells[i].type;
        markers[(*n)++]=m;
    }
    return markers;
}
int galaxy_pick(const Game *g,const Frame *f,int x,int y,Sector *s) {
    int n,i,best=26,depth=100000; const GalaxyMarker *m=galaxy_markers(g,f,&n);
    for(i=0;i<n;i++) {
        int d=(m[i].x-x)*(m[i].x-x)+(m[i].y-y)*(m[i].y-y);
        if(d<best || (d==best && m[i].depth<depth)) { best=d; depth=m[i].depth; *s=m[i].sector; }
    }
    return best<26;
}
static int contains(const char *s,const char *query) {
    size_t i,n=strlen(query);
    do {
        for(i=0;i<n && s[i] && toupper((unsigned char)s[i])==toupper((unsigned char)query[i]);i++);
        if(i==n) return 1;
    } while(*s++);
    return 0;
}
int galaxy_search(const Game *g,Sector *out,int limit) {
    int i,n=0,j; char name[24];
    for(i=0;i<star_catalog_count && n<limit;i++) if(contains(star_catalog[i].name,g->galaxy_query) || contains(star_catalog[i].aliases,g->galaxy_query)) out[n++]=catalog_sector(i);
    /* Search every named system in the local scene and every visited system. */
    for(i=0;i<cells_count+g->explored_count && n<limit;i++) {
        Sector s=i<cells_count?cells[i].sector:g->explored[i-cells_count].sector;
        if(catalog_index(s)>=0) continue;
        galaxy_name(g->seed,s,name);
        if(!contains(name,g->galaxy_query)) continue;
        for(j=0;j<n;j++) if(same_sector(out[j],s)) break;
        if(j==n) out[n++]=s;
    }
    return n;
}
int galaxy_jump_cost(const Game *g,Sector s) { return (int)((galaxy_distance(g->system.sector,s)+499)/500); }
const char *galaxy_jump_status(const Game *g,Sector s) {
    int i;
    if(!galaxy_exists(g->seed,s)) return "EMPTY SECTOR";
    if(same_sector(g->system.sector,s)) return "CURRENT SYSTEM";
    if(galaxy_distance(g->system.sector,s)>6000) return "OUT OF RANGE";
    if(g->docked!=-1) return "LAUNCH FIRST";
    if(altitude_at(g,g->pos)<12000) return "CLIMB TO 12 KM";
    if(g->fuel<galaxy_jump_cost(g,s)) return "INSUFFICIENT FUEL";
    for(i=0;i<MAX_NPC;i++) if(g->npc[i].active && !police_patrol(g,i) && length(sub(g->npc[i].pos,g->pos))<20000*FP) return "CONTACT NEARBY";
    return "JUMP READY";
}
int galaxy_key(Game *g,int k) {
    Sector result[GALAXY_SEARCH]; int n;
    if(!g->galaxy_ready) galaxy_reset(g);
    if(k==GAL_SEARCH || (!g->galaxy_searching && k=='/')) { g->galaxy_searching=1; g->galaxy_result=0; g->galaxy_detail=0; return 1; }
    if(g->galaxy_searching && k<2000) {
        size_t len=strlen(g->galaxy_query);
        if(k==27) { g->galaxy_searching=0; return 1; }
        if(k==8 || k==127) { if(len) g->galaxy_query[len-1]=0; g->galaxy_result=0; return 1; }
        if(k=='\r') {
            n=galaxy_search(g,result,GALAXY_SEARCH);
            if(n) { g->selected=result[g->galaxy_result%n]; galaxy_focus(g,g->selected); }
            g->galaxy_searching=0; return 1;
        }
        if(k==1001 || k==1002) { n=galaxy_search(g,result,GALAXY_SEARCH); if(n) g->galaxy_result=(g->galaxy_result+n+(k==1001?-1:1))%n; return 1; }
        if(k>=32 && k<127 && len<sizeof(g->galaxy_query)-1) { g->galaxy_query[len]=(char)k; g->galaxy_query[len+1]=0; g->galaxy_result=0; }
        return 1;
    }
    if(k>=GAL_RESULT && k<GAL_RESULT+6) {
        n=galaxy_search(g,result,GALAXY_SEARCH); int index=g->galaxy_result/6*6+k-GAL_RESULT;
        if(index<n) { g->selected=result[index]; galaxy_focus(g,g->selected); g->galaxy_result=index; }
        return 1;
    }
    switch(k) {
    case GAL_INFO: g->galaxy_detail=!g->galaxy_detail; break;
    case GAL_PAN: g->galaxy_pan_mode=!g->galaxy_pan_mode; break;
    case GAL_NEXT: case GAL_PREV:
        n=galaxy_search(g,result,GALAXY_SEARCH); if(n) { int page=g->galaxy_result/6*6; g->galaxy_result=k==GAL_NEXT?(page+6>=n?0:page+6):(page? page-6:(n-1)/6*6); } break;
    case GAL_CLEAR: g->galaxy_query[0]=0; g->galaxy_searching=0; g->galaxy_result=0; break;
    case GAL_WAYPOINT: g->waypoint_set=!(g->waypoint_set && same_sector(g->waypoint,g->selected)); g->waypoint=g->selected; break;
    case GAL_FOCUS: case 'f': galaxy_focus(g,g->selected); break;
    case GAL_HOME: case 'c': galaxy_focus(g,g->system.sector); break;
    case GAL_OVERVIEW: g->galaxy_center=vec(0,0,0); g->galaxy_extent=14000000; g->galaxy_pitch=190; break;
    case '+': case '=': if(g->galaxy_extent>2000) g->galaxy_extent=g->galaxy_extent*2/3; if(g->galaxy_extent<2000) g->galaxy_extent=2000; break;
    case '-': if(g->galaxy_extent<60000000) g->galaxy_extent=g->galaxy_extent*3/2; if(g->galaxy_extent>60000000) g->galaxy_extent=60000000; break;
    case 1001: galaxy_rotate(g,0,-8); break;
    case 1002: galaxy_rotate(g,0,8); break;
    case 1003: galaxy_rotate(g,-8,0); break;
    case 1004: galaxy_rotate(g,8,0); break;
    case 'a': galaxy_pan(g,-1,0,0); break;
    case 'd': galaxy_pan(g,1,0,0); break;
    case 'w': galaxy_pan(g,0,0,1); break;
    case 's': galaxy_pan(g,0,0,-1); break;
    case 'q': galaxy_pan(g,0,1,0); break;
    case 'e': galaxy_pan(g,0,-1,0); break;
    case 'x': return 0; /* Shared game action starts the hyperspace sequence. */
    default: return 0;
    }
    return 1;
}
