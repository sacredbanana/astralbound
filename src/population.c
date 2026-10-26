/* SPDX-License-Identifier: Unlicense */
#include "galaxy.h"
#define CELL_WIDTH 2000
#define CELL_HEIGHT 60
#define CELL_VOLUME 240000000U
#define PIN_CAPACITY 128
typedef struct { uint32_t first,count; } PopulationCell;
#include "population.inc"
typedef struct { int cell; uint32_t rank; } PinRank;
static PinRank pin_ranks[PIN_CAPACITY];
static int pins_count,prepared;
static uint32_t prepared_seed;
static Sector pin_sector(int i) {
    if(i<star_catalog_count) return catalog_sector(i);
    i-=star_catalog_count;
    return (Sector){i%3-1,0,i/3-1};
}
static int pinned(Sector s) {
    return catalog_index(s)>=0 || (s.y==0 && s.x>=-1 && s.x<=1 && s.z>=-1 && s.z<=1);
}
static int cell_of(Sector s) {
    if(s.x< -GALAXY_RADIUS || s.x>=GALAXY_RADIUS || s.z< -GALAXY_RADIUS || s.z>=GALAXY_RADIUS || s.y< -GALAXY_HEIGHT || s.y>=GALAXY_HEIGHT) return -1;
    return ((s.y+GALAXY_HEIGHT)/CELL_HEIGHT*10+(s.z+GALAXY_RADIUS)/CELL_WIDTH)*10+(s.x+GALAXY_RADIUS)/CELL_WIDTH;
}
static Sector cell_origin(int cell) {
    return (Sector){cell%10*CELL_WIDTH-GALAXY_RADIUS,cell/100*CELL_HEIGHT-GALAXY_HEIGHT,cell/10%10*CELL_WIDTH-GALAXY_RADIUS};
}
/* Three reversible shears permute every integer sector in a cell bijectively.
   All arithmetic fits 32 bits, including on a 68000. */
static uint32_t mix(uint32_t seed,int cell,int a,int b,int stage) {
    return hash32(seed^hash32((unsigned)cell*173U+(unsigned)stage)^hash32((unsigned)a*2003U+(unsigned)b));
}
static uint32_t sector_rank(uint32_t seed,int cell,Sector s) {
    Sector o=cell_origin(cell);
    int x=s.x-o.x,y=s.y-o.y,z=s.z-o.z;
    x=(x+(int)(mix(seed,cell,y,z,1)%CELL_WIDTH))%CELL_WIDTH;
    z=(z+(int)(mix(seed,cell,x,y,2)%CELL_WIDTH))%CELL_WIDTH;
    y=(y+(int)(mix(seed,cell,x,z,3)%CELL_HEIGHT))%CELL_HEIGHT;
    return ((uint32_t)y*CELL_WIDTH+(unsigned)z)*CELL_WIDTH+(unsigned)x;
}
static Sector rank_sector(uint32_t seed,int cell,uint32_t rank) {
    Sector o=cell_origin(cell);
    int x=(int)(rank%CELL_WIDTH),z=(int)(rank/CELL_WIDTH%CELL_WIDTH),y=(int)(rank/(CELL_WIDTH*CELL_WIDTH));
    y=(y+CELL_HEIGHT-(int)(mix(seed,cell,x,z,3)%CELL_HEIGHT))%CELL_HEIGHT;
    z=(z+CELL_WIDTH-(int)(mix(seed,cell,x,y,2)%CELL_WIDTH))%CELL_WIDTH;
    x=(x+CELL_WIDTH-(int)(mix(seed,cell,y,z,1)%CELL_WIDTH))%CELL_WIDTH;
    return (Sector){o.x+x,o.y+y,o.z+z};
}
static void prepare(uint32_t seed) {
    int i,j;
    if(prepared && prepared_seed==seed) return;
    pins_count=0;
    for(i=0;i<star_catalog_count+9;i++) {
        Sector s=pin_sector(i);int cell=cell_of(s);PinRank p;
        if(cell<0 || !population_cells[cell].count) continue;
        p.cell=cell;p.rank=sector_rank(seed,cell,s);
        j=pins_count++;
        while(j && (pin_ranks[j-1].cell>p.cell || (pin_ranks[j-1].cell==p.cell && pin_ranks[j-1].rank>p.rank))) { pin_ranks[j]=pin_ranks[j-1];j--; }
        pin_ranks[j]=p;
    }
    prepared_seed=seed;prepared=1;
}
uint32_t galaxy_system_count(void) {
    return population_cells[999].first+population_cells[999].count+(unsigned)star_catalog_count+9U;
}
int galaxy_exists(uint32_t seed,Sector s) {
    int i,cell;uint32_t end,rank;
    if(pinned(s)) return 1;
    cell=cell_of(s);
    if(cell<0 || !population_cells[cell].count) return 0;
    prepare(seed);rank=sector_rank(seed,cell,s);end=population_cells[cell].count;
    /* Reserved stars consume their own IDs. Skip overlapping raw slots so
       the procedural quota plus all reserved entries stays exact. */
    for(i=0;i<pins_count;i++) if(pin_ranks[i].cell==cell && pin_ranks[i].rank<end) end++;
    return rank<end;
}
/* The inverse of galaxy_system_at: a system's dense ID from its sector. */
int galaxy_system_id(uint32_t seed,Sector s,uint32_t *id) {
    int i,cell,c=catalog_index(s);uint32_t rank,below=0;
    if(c>=0) { *id=(uint32_t)c;return 1; }
    if(pinned(s)) { *id=(unsigned)star_catalog_count+(unsigned)((s.z+1)*3+s.x+1);return 1; }
    cell=cell_of(s);
    if(cell<0 || !population_cells[cell].count) return 0;
    prepare(seed);rank=sector_rank(seed,cell,s);
    for(i=0;i<pins_count;i++) if(pin_ranks[i].cell==cell && pin_ranks[i].rank<rank) below++;
    if(rank-below>=population_cells[cell].count) return 0;
    *id=(unsigned)star_catalog_count+9U+population_cells[cell].first+rank-below;
    return 1;
}
int galaxy_system_at(uint32_t seed,uint32_t id,Sector *s) {
    int lo=0,hi=1000,i;uint32_t rank;
    if(id>=galaxy_system_count()) return 0;
    if(id<(unsigned)star_catalog_count+9U) { *s=pin_sector((int)id);return 1; }
    id-=(unsigned)star_catalog_count+9U;
    while(lo<hi) { int mid=(lo+hi)/2;
        if(population_cells[mid].first+population_cells[mid].count<=id) lo=mid+1;else hi=mid; }
    if(lo>=1000) return 0;
    prepare(seed);rank=id-population_cells[lo].first;
    for(i=0;i<pins_count;i++) if(pin_ranks[i].cell==lo && pin_ranks[i].rank<=rank) rank++;
    if(rank>=CELL_VOLUME) return 0;
    *s=rank_sector(seed,lo,rank);return 1;
}
