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
#define MAX_PLANETS 6
#define PORTS_PER_PLANET 4
#define MAX_PORTS (MAX_PLANETS * PORTS_PER_PLANET)
#define GENERATOR_VERSION 2

typedef struct {
    int64_t x, y, z;
} Vec;
typedef struct {
    int32_t x, y, z;
} Sector;
typedef struct {
    uint32_t seed;
    int32_t radius, gravity, atmosphere;
    Vec center; /* metres in the system frame; ship coordinates are Q8 metres */
    char name[24];
} Planet;
typedef struct {
    uint32_t seed;
    Sector sector;
    char name[24];
    int economy, government, count;
    Planet planets[MAX_PLANETS];
} System;
typedef struct {
    Vec pos;
    char name[24];
    int surface, body;
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
    uint32_t seed, tick;
    System system;
    Port ports[MAX_PORTS];
    Vec pos, vel;
    int yaw, pitch, target, docked, gear, autopilot, assist, body;
    int throttle, hull, shield, fuel, heat, credits, ship, upgrade, kills;
    int cargo[GOODS], stock[GOODS];
    Mission board[MAX_MISSIONS], contracts[MAX_MISSIONS];
    Npc npc[MAX_NPC];
    Sector selected;
    int selection, page, warp, effective_warp;
    int laser, dead, paused, camera_mode;
    uint32_t shots;
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
int sini(int a);
int cosi(int a);
Vec forward(int yaw, int pitch);
void aim_angles(Vec direction, int *yaw, int *pitch);
void generate_system(uint32_t seed, Sector s, System *out);
void setup_system(Game *g, Sector sector);
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
