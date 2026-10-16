/* SPDX-License-Identifier: Unlicense */
#include "solar.h"
#include <string.h>
#include <stdio.h>
typedef struct {
    const char *name;
    int parent;
    int64_t orbit;
    uint64_t period;
    int radius,gravity,type,climate,atmosphere,inclination,phase,eccentricity,node,periapsis;
    uint32_t spin;
    int known;
    int32_t basis[6];
} SolarBody;
#include "solar_catalog.inc"
int solar_body_count(void) { return sizeof(solar_bodies)/sizeof(solar_bodies[0]); }
int solar_body_index(const System *s,const char *name) {
    int i; for(i=0;i<s->count;i++) if(!strcmp(s->planets[i].name,name)) return i;
    return -1;
}
void solar_system(System *s) {
    int i;
    memset(s->planets,0,sizeof(s->planets));
    s->count=solar_body_count();
    for(i=0;i<s->count;i++) {
        const SolarBody *d=&solar_bodies[i]; Planet *p=&s->planets[i];
        p->seed=hash32(0x534f4c00U+(unsigned)i); p->parent=d->parent;
        p->radius=d->radius; p->gravity=d->gravity; p->type=(BodyType)d->type;
        p->climate=(AtmosphereType)d->climate; p->atmosphere=d->atmosphere;
        p->orbit=d->orbit; p->period=d->period; p->phase=d->phase;
        p->inclination=d->inclination; p->eccentricity=d->eccentricity;
        p->ascending_node=d->node; p->periapsis=d->periapsis;
        p->spin_period=d->spin; p->catalog=d->known;
        memcpy(p->orbit_basis,d->basis,sizeof(p->orbit_basis));
        p->influence=p->parent>=0?p->orbit/5:p->orbit/100;
        p->rings=i>=4 && i<=7;
        /* Fictional ports only; the celestial catalog remains real. */
        p->settled=i==2 || i==3 || !strcmp(d->name,"Moon");
        snprintf(p->name,sizeof(p->name),"%s",d->name);
    }
}
