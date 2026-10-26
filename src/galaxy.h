/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_GALAXY_H
#define ASTRALBOUND_GALAXY_H
#include "astralbound.h"
#define MAX_STARS 6
#define GALAXY_SYSTEMS 1000000000U
#define GALAXY_RADIUS 10000
#define GALAXY_HEIGHT 300
#define GALAXY_MARKERS 5100
#define GALAXY_SEARCH 256
#define GAL_SEARCH 12000
#define GAL_NEXT 12001
#define GAL_PREV 12002
#define GAL_FOCUS 12003
#define GAL_WAYPOINT 12004
#define GAL_CLEAR 12005
#define GAL_HOME 12006
#define GAL_OVERVIEW 12007
#define GAL_DRAG 12008
#define GAL_INFO 12009
#define GAL_PAN 12010
#define GAL_RESULT 12100
/* Coordinates are millilightyears; sector IDs remain integer ly save keys. */
typedef struct {
    const char *name, *aliases;
    Vec position;
    Sector sector;
    int count;
    const char *spectra[6];
} CatalogStar;
typedef struct {
    Sector sector;
    int x,y,depth,type,catalog;
} GalaxyMarker;
extern const CatalogStar star_catalog[];
extern const int star_catalog_count;
extern const char *stellar_names[];
int same_sector(Sector a, Sector b);
Sector catalog_sector(int i);
int catalog_index(Sector s);
Vec galaxy_position(Sector s);
int64_t galaxy_distance(Sector a, Sector b);
void galaxy_name(uint32_t seed, Sector s, char name[24]);
uint32_t galaxy_system_count(void);
int galaxy_system_at(uint32_t seed, uint32_t id, Sector *s);
int galaxy_system_id(uint32_t seed, Sector s, uint32_t *id);
int galaxy_name_value(const char *name, uint32_t *value);
int galaxy_exists(uint32_t seed, Sector s);
StellarType stellar_type(const char *s);
StellarType galaxy_type(uint32_t seed, Sector s);
int stellar_colour(StellarType type);
void galaxy_apply(System *s);
void galaxy_reset(Game *g);
void galaxy_focus(Game *g, Sector s);
int galaxy_key(Game *g, int key);
void galaxy_drag(Game *g, int dx, int dy);
void galaxy_pan(Game *g, int dx, int dy, int dz);
int galaxy_project(const Game *g, const Frame *f, Vec p, int *x, int *y, int *depth);
const GalaxyMarker *galaxy_markers(const Game *g, const Frame *f, int *n);
int galaxy_pick(const Game *g, const Frame *f, int x, int y, Sector *s);
int galaxy_search(const Game *g, Sector *results, int limit);
int galaxy_jump_cost(const Game *g, Sector s);
const char *galaxy_jump_status(const Game *g, Sector s);
#endif
