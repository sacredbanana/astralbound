/* SPDX-License-Identifier: Unlicense */
#ifndef SPACE_H
#define SPACE_H
#include <stdint.h>
#include <stddef.h>
#define FP 256
#define HZ 32
#define ANGLE 1024
#define MAX_NPC 12
#define MAX_MISSIONS 8
#define GOODS 8
#define MAX_PLANETS 12
#define PORTS_PER_PLANET 4
#define MAX_PORTS (MAX_PLANETS * PORTS_PER_PLANET)
#define GENERATOR_VERSION 3
#define MAX_EXPLORED 128
#define AU_METRES 149597870700LL

typedef struct {
    int64_t x, y, z;
} Vec;
typedef struct {
    int32_t x, y, z;
} Sector;
typedef enum { ROCKY, OCEAN, DESERT, ICE, GAS_GIANT, MOON, BODY_TYPES } BodyType;
typedef enum { VACUUM, TEMPERATE, THIN, DENSE, FROZEN, HYDROGEN } AtmosphereType;
typedef struct {
    int32_t radius;
    Vec center;
    int64_t orbit;
    uint32_t period;
    int phase;
} Star;
typedef struct {
    uint32_t seed;
    int32_t radius, gravity, atmosphere;
    BodyType type;
    AtmosphereType climate;
    int parent, settled, rings, inclination, phase;
    int64_t orbit, influence;
    uint32_t period;
    Vec orbit_start, orbit_end, velocity;
    Vec center; /* metres in the system frame; ship coordinates are Q8 metres */
    char name[24];
} Planet;
typedef struct {
    uint32_t seed;
    Sector sector;
    char name[24];
    int economy, government, count, stars, density, world_ready;
    uint32_t orbit_tick;
    Star star[2];
    Planet planets[MAX_PLANETS];
} System;
typedef struct {
    Vec orbit_start, orbit_end;
    Vec pos, velocity, normal; /* Q8 system position/velocity; Q14 approach axis */
    char name[24];
    int surface, body, rotation;
    uint32_t busy_until;
} Port;
typedef enum {
    COURIER,
    FREIGHT,
    ORBIT_PATROL,
    AIR_PATROL,
    SURVEY,
    BOUNTY,
    MISSION_TYPES
} MissionType;
typedef enum { OFFERED, ACTIVE, COMPLETE, FAILED } MissionState;
typedef struct {
    uint32_t id;
    MissionType type;
    MissionState state;
    Sector destination;
    int port, quantity, good, reward, progress, spawned;
    uint32_t deadline;
} Mission;
typedef struct {
    Vec pos, vel;
    int hull, active, hostile, mission;
    uint32_t cooldown;
} Npc;
typedef enum { FLIGHT, MAP, BOARD, MARKET, SHIPYARD, LOG, SYSTEM_MAP, HELP } Page;
typedef struct {
    Sector sector;
    uint32_t visited, surveyed;
    uint32_t first_tick, last_tick;
} Exploration;
typedef struct {
    uint32_t seed, tick;
    System system;
    Port ports[MAX_PORTS];
    int nports, frame_body, clearance, dock_stage, target_body;
    Exploration explored[MAX_EXPLORED];
    int explored_count;
    Vec pos, vel;
    int yaw, pitch, target, docked, gear, autopilot, assist, body;
    int throttle, hull, shield, fuel, heat, credits, ship, upgrade, kills;
    int cargo[GOODS], stock[GOODS];
    Mission board[MAX_MISSIONS], contracts[MAX_MISSIONS];
    Npc npc[MAX_NPC];
    Sector selected;
    int selection, page, warp, effective_warp;
    int laser, dead, paused, camera_mode;
    uint32_t shots, fuel_burn;
    char message[80];
} Game;
typedef struct {
    int pitch, yaw, thrust, fire;
} Controls;
typedef struct {
    int w, h, enhanced;
    uint8_t *pixels;
    uint32_t palette[256];
} Frame;
extern const char *goods_names[GOODS], *mission_names[MISSION_TYPES];
extern const int ship_capacity[3];
uint32_t hash32(uint32_t x);
uint32_t sector_seed(uint32_t seed, Sector s);
uint64_t isqrt64(uint64_t n);
Vec vec(int64_t x, int64_t y, int64_t z);
Vec add(Vec a, Vec b);
Vec sub(Vec a, Vec b);
Vec scale(Vec a, int64_t n, int64_t d);
int64_t length(Vec a);
Vec unit(Vec a, int magnitude);
Vec radial(Vec direction, int64_t radius);
int sini(int a);
int cosi(int a);
Vec forward(int yaw, int pitch);
void aim_angles(Vec direction, int *yaw, int *pitch);
void generate_system(uint32_t seed, Sector s, System *out);
void setup_system(Game *g, Sector sector);
void update_world(Game *g);
Vec body_position(const System *s, int body, uint32_t tick);
Vec body_velocity(const System *s, int body, uint32_t tick);
Vec frame_velocity(const Game *g);
Vec port_relative_velocity(const Game *g, int port);
Vec navigation_position(const Game *g);
Vec navigation_velocity(const Game *g);
int system_port_count(const System *s);
int galaxy_density(Sector s);
void record_exploration(Game *g);
int exploration_index(const Game *g, Sector s);
int request_clearance(Game *g, int port);
int station_impact(const Game *g, Vec from, Vec to, int port);
int atmosphere_density(const Planet *p, int64_t altitude);
int terrain_elevation(const Planet *p, Vec relative_metres);
int terrain_material(const Planet *p, Vec relative_metres);
/* The anchor is a full-precision body-relative direction; tangent axes are Q14. */
Vec terrain_vertex(const Planet *p, Vec anchor, Vec right, Vec front, int x, int y);
extern const char *body_names[BODY_TYPES];
int nearest_body(const Game *g, Vec pos);
int64_t altitude_at(const Game *g, Vec pos);
int terrain_height(const Planet *p, Vec relative_metres);
int terrain_impact(const Game *g, Vec from, Vec to, int body);
int port_count(const Game *g);
void game_init(Game *g, uint32_t seed);
void game_tick(Game *g, Controls c);
void game_frame(Game *g, Controls c, int ticks);
void game_action(Game *g, int key);
int safe_warp(const Game *g);
Vec avoidance(Vec relative_position, Vec relative_velocity, int acceleration, int radius);
Vec guidance(Vec pos, Vec velocity, Vec target, int acceleration, int maxspeed);
int swept_sphere(Vec start, Vec end, Vec center, int radius);
int price(const Game *g, int good);
int cargo_used(const Game *g);
int trade(Game *g, int good, int buy);
int accept_mission(Game *g, int index);
int jump(Game *g, Sector destination);
void refresh_board(Game *g);
int save_game(const Game *g, const char *path);
int load_game(Game *g, const char *path);
void render_init(Frame *f);
void render_game(const Game *g, Frame *f);
void render_set_pump(void (*pump)(void));
void c2p(const uint8_t *src, uint8_t *dst, int width, int height, int depth);
#endif
