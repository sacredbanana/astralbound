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
typedef char catalog_fits[sizeof(star_catalog)/sizeof(star_catalog[0])<=STAR_CATALOG_MAX?1:-1];
static int sector_order(Sector a,Sector b) {
    return a.x!=b.x ? (a.x<b.x?-1:1) : a.y!=b.y ? (a.y<b.y?-1:1) : a.z!=b.z ? (a.z<b.z?-1:1) : 0;
}
/* Every sector test asks this, so it is a binary search of the sector-sorted index. */
int catalog_index(Sector s) {
    int lo=0,hi=star_catalog_count;
    while(lo<hi) {
        int mid=(lo+hi)/2,c=sector_order(catalog_sector(star_catalog_order[mid]),s);
        if(!c) return star_catalog_order[mid];
        if(c<0) lo=mid+1; else hi=mid;
    }
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
/* The original two-syllable names: HETHA at home keeps its own. */
static void legacy_name(uint32_t seed,Sector s,char name[24]) {
    static const char *first[]={"VE","KA","OR","SE","TA","NI","LU","AE","MI","RU","DA","IO","ZE","UL","BA","HE"};
    static const char *last[]={"LORA","NARA","VEL","SEN","RIA","THA","MIN","RIS","RON","LIS","DRA","ZEN","VAR","TIS","LAN","MAR"};
    uint32_t r=sector_seed(seed,s),a,b;
    r=hash32(r+0x9e3779b9U);a=r%16;r=hash32(r+0x9e3779b9U);b=r%16;
    snprintf(name,24,"%s%s",first[a],last[b]);
}
/* Unique names. Two syllables from sixteen each named only 256 systems in a
   galaxy of a billion, so every other system spells its dense ID instead:
   a seed-keyed 30-bit permutation, read four bits of onset, three of vowel
   and a flag (with three bits of coda when set) per syllable until all 30
   are spent - three or four syllables. Every syllable opens on a single
   consonant, so a name splits into syllables one way only and two IDs never
   share one. Catalog stars and the home system keep their names; an ID
   whose spelling would be one of those takes that fixed ID's spelling. */
#define NAME_BITS 30
#define NAME_MASK ((1U<<NAME_BITS)-1U)
static const char *name_onset[16]={"B","D","F","G","H","K","L","M","N","P","R","S","T","V","Y","Z"};
static const char *name_vowel[8]={"A","E","I","O","U","AE","IA","IO"};
static const char *name_coda[8]={"N","R","L","S","TH","X","M","K"};
#define NAME_A 0x2545f491U
#define NAME_B 0x9e3779b1U
static uint32_t odd_inverse(uint32_t a) {
    uint32_t x=a; int i;
    for(i=0;i<5;i++) x*=2U-a*x;
    return x;
}
static uint32_t name_scramble(uint32_t seed,uint32_t id) {
    uint32_t x=(id^hash32(seed^0x4e414d45U))&NAME_MASK;
    x=(x*NAME_A)&NAME_MASK; x^=x>>15;
    x=(x*NAME_B)&NAME_MASK; x^=x>>15;
    return x;
}
static uint32_t name_unscramble(uint32_t seed,uint32_t x) {
    x^=x>>15; x=(x*odd_inverse(NAME_B))&NAME_MASK;
    x^=x>>15; x=(x*odd_inverse(NAME_A))&NAME_MASK;
    return (x^hash32(seed^0x4e414d45U))&NAME_MASK;
}
static void name_spell(uint32_t value,char name[24]) {
    int bits=0,n=0;
    while(bits<NAME_BITS) {
        const char *part[3]; int k;
        part[0]=name_onset[value&15]; value>>=4;
        part[1]=name_vowel[value&7]; value>>=3;
        part[2]=value&1 ? name_coda[(value>>1)&7] : ""; bits+=value&1 ? 11 : 8; value>>=value&1 ? 4 : 1;
        for(k=0;k<3;k++) n+=snprintf(name+n,(size_t)(24-n),"%s",part[k]);
    }
}
static int name_match(const char *s,const char *table[],int count,size_t *length) {
    int i,best=-1; size_t n=0;
    for(i=0;i<count;i++) {
        size_t l=strlen(table[i]);
        if(l>n && !strncmp(s,table[i],l)) { best=i; n=l; }
    }
    *length=n;
    return best;
}
/* The ID spelled by a name of this scheme, or 0 when it is not one. */
int galaxy_name_value(const char *name,uint32_t *value) {
    char up[24], again[24]; uint32_t v=0; int bits=0; size_t i,l;
    for(i=0;name[i] && i<23;i++) up[i]=(char)toupper((unsigned char)name[i]);
    up[i]=0; i=0;
    while(up[i] && bits<NAME_BITS) {
        int o=name_match(up+i,name_onset,16,&l),vw,c;
        if(o<0 || l!=1) return 0;
        i+=l; vw=name_match(up+i,name_vowel,8,&l);
        if(vw<0) return 0;
        /* The longest vowel run that is a nucleus; a longer run is no name. */
        i+=l; if(up[i] && strchr("AEIOU",up[i])) return 0;
        if(bits<32) v|=(uint32_t)o<<bits;
        bits+=4;
        if(bits<32) v|=(uint32_t)vw<<bits;
        bits+=3;
        /* A coda is the consonant run less the next syllable's onset. */
        { size_t run=0; while(up[i+run] && !strchr("AEIOU",up[i+run])) run++;
          if(up[i+run]) run--;
          if(!run) { bits+=1; continue; }
          c=name_match(up+i,name_coda,8,&l);
          if(c<0 || l!=run) return 0;
          if(bits<32) v|=(uint32_t)(1+2*c)<<bits;
          bits+=4; i+=l; }
    }
    if(up[i] || bits<NAME_BITS) return 0;
    v&=NAME_MASK; name_spell(v,again);
    if(strcmp(again,up)) return 0;
    *value=v; return 1;
}
/* Fixed names: the catalog's, then home (0,0,0), pin ID catalog + 4. */
#define HOME_ID ((unsigned)star_catalog_count+4U)
static int fixed_name(uint32_t seed,uint32_t id,char name[24]) {
    Sector home={0,0,0};
    if(id<(unsigned)star_catalog_count) { snprintf(name,24,"%s",star_catalog[id].name); return 1; }
    if(id==HOME_ID) { legacy_name(seed,home,name); return 1; }
    return 0;
}
/* Generated IDs whose spelling is a fixed name, and the fixed ID each takes. */
static struct { uint32_t seed; int ready,count; uint32_t id[128],pin[128]; } name_swaps;
static void name_swaps_prepare(uint32_t seed) {
    uint32_t j,v,pins=(unsigned)star_catalog_count+9U; char name[24];
    if(name_swaps.ready && name_swaps.seed==seed) return;
    name_swaps.count=0;
    for(j=0;j<pins && name_swaps.count<128;j++) {
        if(!fixed_name(seed,j,name)) continue;
        if(!galaxy_name_value(name,&v)) continue;
        v=name_unscramble(seed,v);
        if(!fixed_name(seed,v,name) && v<galaxy_system_count()) { name_swaps.id[name_swaps.count]=v; name_swaps.pin[name_swaps.count++]=j; }
    }
    name_swaps.seed=seed; name_swaps.ready=1;
}
void galaxy_name(uint32_t seed,Sector s,char name[24]) {
    uint32_t id; int i;
    if(!galaxy_system_id(seed,s,&id)) { legacy_name(seed,s,name); return; }
    if(fixed_name(seed,id,name)) return;
    name_swaps_prepare(seed);
    for(i=0;i<name_swaps.count;i++) if(name_swaps.id[i]==id) { id=name_swaps.pin[i]; break; }
    name_spell(name_scramble(seed,id),name);
}
StellarType stellar_type(const char *s) {
    if(!strncmp(s,"PSR",3)) return ST_NEUTRON;
    if(!strncmp(s,"LBV",3)) return ST_GIANT;
    if(s[0]=='W') return ST_O; /* Wolf-Rayet */
    if(s[0]=='D') return ST_WHITE_DWARF;
    if(s[0]=='L'||s[0]=='T'||s[0]=='Y') return ST_BROWN_DWARF;
    if(strstr(s,"III") || strstr(s,"II") || strstr(s,"Ib") || strstr(s,"Ia")) return s[0]=='K' || s[0]=='M' || s[0]=='C' ? ST_RED_GIANT : ST_GIANT;
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
    case '+': case '=': if(g->galaxy_extent>GALAXY_ZOOM_MIN) g->galaxy_extent=g->galaxy_extent*2/3; if(g->galaxy_extent<GALAXY_ZOOM_MIN) g->galaxy_extent=GALAXY_ZOOM_MIN; break;
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
