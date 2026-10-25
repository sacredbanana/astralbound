/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_H
#define ASTRALBOUND_H
#include <stdint.h>
#include <stddef.h>
#define FP 256
#define HZ 32
#define MAX_WARP 4096
/* Q8 drive thrust exceeds INT32_MAX; thrust paths carry int64_t. */
#define SYSTEM_DRIVE_ACCEL 20000000LL
#define LIGHT_SPEED 299792458LL
#define SYSTEM_DRIVE_SPEED (30LL * LIGHT_SPEED)
#define ATMOSPHERE_SPEED 1200
/* An entry from space arms a brake that halts the descent above this floor. */
#define ENTRY_BRAKE_FLOOR 10000
#define ENTRY_BRAKE_ACCEL 30
#define MAX_FRAME_STEPS 128
#define TERRAIN_MAX_HEIGHT 8000
#define TERRAIN_DETAIL_ALTITUDE (12000 + TERRAIN_MAX_HEIGHT)
#define ANGLE 1024
#define MAX_NPC 12
#define MAX_BLASTS 16
#define MAX_SMOKE 96
#define BLAST_TICKS (HZ * 7)
#define SMOKE_TICKS (HZ * 5)
#define MAX_MISSIONS 8
#define GOODS 8
#define MAX_PLANETS 512
#define GENERATED_PLANETS 12
#define PORTS_PER_PLANET 4
#define MAX_PORTS (GENERATED_PLANETS * PORTS_PER_PLANET)
#define GENERATOR_VERSION 3
#define MAX_EXPLORED 128
#define MAX_MARKETS 128
#define MARKET_INTERVAL (HZ * 3600U)
#define SAVE_VERSION 9
#define SHIPS 6
#define FACTIONS 3
#define MAX_PODS 8
#define MAX_MISSILES 8
#define MAX_WEAPON_BOLTS 32
#define MAX_RIGS 4
#define MAX_DEPOSITS 32
#define ASTEROIDS 8
#define AU_METRES 149597870700LL

typedef struct {
    int64_t x, y, z;
} Vec;
typedef struct {
    int32_t x, y, z;
} Sector;
typedef enum { ROCKY, OCEAN, DESERT, ICE, GAS_GIANT, MOON, BODY_TYPES } BodyType;
typedef enum { VACUUM, TEMPERATE, THIN, DENSE, FROZEN, HYDROGEN } AtmosphereType;
typedef enum { ST_O, ST_B, ST_A, ST_F, ST_G, ST_K, ST_M, ST_GIANT,
    ST_WHITE_DWARF, ST_BROWN_DWARF, ST_NEUTRON, ST_BLACK_HOLE, ST_RED_GIANT, ST_TYPES } StellarType;
typedef struct {
    int64_t radius;
    StellarType type;
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
    uint64_t period;
    uint32_t spin_period; /* optional catalog rotation period in seconds */
    int32_t orbit_basis[6]; /* catalog orbital plane, Q14 periapsis/tangent vectors */
    int eccentricity, ascending_node, periapsis; /* catalog ellipse; e in millionths */
    int catalog; /* 0 generated, 1 measured radius, 2 unknown radius (proxy geometry) */
    Vec orbit_start, orbit_end, velocity;
    Vec cruise_velocity; /* transient averaged Q8 system velocity */
    int32_t spin_sin, spin_cos; /* transient Q30 rotation about body Z */
    Vec center; /* metres in the system frame; ship coordinates are Q8 metres */
    char name[24];
} Planet;
typedef struct {
    uint32_t seed;
    Sector sector;
    char name[24];
    int economy, government, count, stars, density, world_ready;
    uint32_t orbit_tick;
    Star star[6];
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
    PASSENGER, ESCORT, RESCUE, ASSASSINATION, RECON, INSTALLATION, MULTILEG,
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
    int faction, required_rep, passengers, legs, leg, negotiated;
} Mission;
typedef struct {
    Vec pos, vel;
    int hull, active, hostile, mission;
    uint32_t cooldown;
} Npc;
typedef enum { FLIGHT, MAP, BOARD, MARKET, SHIPYARD, LOG, SYSTEM_MAP, HELP, EQUIPMENT, CAREER, SERVICES } Page;
typedef struct {
    Sector sector;
    uint32_t visited, surveyed;
    uint32_t first_tick, last_tick;
} Exploration;
typedef struct {
    Sector sector;
    int port;
    uint32_t last_tick;
    int stock[GOODS];
} Market;
typedef struct {
    const char *name;
    int mass, acceleration, capacity, crew, mounts, missiles, width, depth, height, cost;
} ShipSpec;
typedef enum { CABIN, MINING, SCOOP, MISSILE_RACK, COUNTERMEASURE, DORSAL, VENTRAL, ESCAPE_POD, EQUIP_TYPES } Equipment;
typedef enum { RAIDER, POLICE, CIVILIAN, INSTALLATION_TARGET } NpcRole;
/* Police are heavily armoured interceptors: hull, damage divisor and hull
   repaired per second; then shot damage, interval and range against the commander. */
#define POLICE_HULL 1000
#define POLICE_ARMOUR 10
#define POLICE_REPAIR 15
#define POLICE_DAMAGE 15
#define POLICE_INTERVAL (HZ / 2)
#define POLICE_RANGE 5000
/* Hyperspace sequence in real ticks: charge in the departure system, the
   jump itself at the tunnel boundary, then arrival in the new system. */
#define HYPER_CHARGE (HZ * 3)
#define HYPER_TUNNEL (HZ * 11 / 2)
#define HYPER_EXIT (HZ * 3 / 2)
#define HYPER_TOTAL (HYPER_CHARGE + HYPER_TUNNEL + HYPER_EXIT)
/* Parking. DOCK flies to and settles on a port within DOCK_RANGE metres;
   elsewhere below LAND_CEILING metres above solid ground, LAND descends in
   place. Landed on a surface, the commander may walk WALK_RANGE metres from
   the ship at WALK_SPEED m/s and use ground sites within SITE_REACH metres. */
#define DOCK_RANGE 20000
#define LAND_CEILING 30000
#define WALK_SPEED 3
#define WALK_RANGE 1500
#define EYE_HEIGHT 2
#define SITE_REACH 12
#define SITE_CELL 384
#define GROUND_SITES 9
#define GROUND_CLAIMS 16
enum { PARK_NONE, PARK_DOCK, PARK_LAND };
typedef enum { SITE_OUTCROP, SITE_WRECKAGE, SITE_BEACON, SITE_KINDS } SiteKind;
/* A point of interest on the ground: body-fixed Q8 base and a stable id. */
typedef struct { Vec anchor; uint32_t id; int body, kind; } GroundSite;
typedef struct { Vec pos, vel; int active, good, quantity, mission; } CargoPod;
typedef struct { Vec pos, vel; int active, target, ttl; } Missile;
/* Transient laser presentation: fixed-tick travel between Q8 endpoints.
   Damage remains on the existing weapon path; drift follows the local frame. */
typedef struct { Vec start, end, drift; int age, ticks, colour; } WeaponBolt;
typedef struct { Sector sector; int body, stock; Vec anchor; uint32_t last_tick; } MiningRig;
typedef struct { Sector sector; int ore[ASTEROIDS]; } Deposit;
/* Transient destruction presentation. Body >= 0 uses body-fixed Q8 coordinates,
   so crashes, smoke and impact blasts stay attached to rotating terrain. */
typedef struct {
    Vec pos, vel;
    int active, age, body, size;
    uint32_t seed;
} Blast;
typedef struct { Vec pos, vel; int active, age, body, size; uint32_t seed; } Smoke;
typedef struct {
    Vec pos, vel;
    int state, age, body, ship, yaw, pitch, roll; /* 1 falling, 2 exploded */
} Wreck;
typedef struct {
    uint32_t seed, tick;
    System system;
    Port ports[MAX_PORTS];
    int nports, frame_body, clearance, dock_stage, target_body;
    Exploration explored[MAX_EXPLORED];
    int explored_count;
    Vec pos, vel;
    int yaw, pitch, target, docked, autopilot, assist, body;
    int roll; /* v7 commander bank, 0..1023 */
    int throttle, hull, shield, fuel, heat, credits, ship, upgrade, kills;
    int cargo[GOODS];
    Market markets[MAX_MARKETS];
    int market_count;
    Mission board[MAX_MISSIONS], contracts[MAX_MISSIONS];
    Npc npc[MAX_NPC];
    Sector selected;
    int selection, page, warp, effective_warp;
    int laser, dead, paused, camera_mode;
    /* Presentation state is transient; version 3/4 commander payloads stay compatible. */
    int turret_yaw, turret_pitch, combat_target, system_display, map_local;
    int roll_fraction, pitch_fraction;
    int drive_manual, drive_active; /* transient; player drive switch and available thrust, independent of AP */
    Vec ground_anchor; /* transient body-fixed Q8 landed position */
    uint32_t shots, fuel_burn;
    /* v6 careers extension; legacy commander prefix is serialized unchanged. */
    int crew, cabins, equipment, missiles, countermeasures, turret_upgrade[2];
    int reputation[FACTIONS], service[FACTIONS], medals[FACTIONS], wanted[FACTIONS];
    uint32_t wage_tick, encounter_tick;
    int npc_role[MAX_NPC], npc_ship[MAX_NPC];
    CargoPod pods[MAX_PODS];
    Missile projectiles[MAX_MISSILES];
    MiningRig rigs[MAX_RIGS];
    Deposit deposits[MAX_DEPOSITS];
    int rig_count, deposit_count;
    int service_selection, mission_detail, mission_tab, mission_map_return, mission_map_selection; /* transient */
    int board_page, black_market, resource_target; /* transient UI/navigation */
    int ui_x, ui_y, ui_visible, ui_down, ui_capture, ui_press_page, ui_fire, ui_weapons; /* transient */
    int ui_quit; /* transient modal: 0 closed, 1 open, 2 save failed */
    int npc_port[MAX_NPC]; /* transient: port each police ship patrols, refreshed from position */
    int police_alert; /* transient: 0 calm, 1 weapons fine issued, 2 police engaging */
    uint32_t police_warned, police_tick; /* transient: fine time; earliest patrol reinforcement */
    uint32_t solar_visited[16], solar_surveyed[16]; /* full Sol body journal */
    uint32_t ui_clock; /* transient real-time ticks for presentation animation */
    /* Transient 3D galaxy camera, search and independent waypoint. */
    Vec galaxy_center;
    int galaxy_extent, galaxy_yaw, galaxy_pitch, galaxy_ready;
    int galaxy_detail, galaxy_pan_mode;
    int galaxy_searching, galaxy_result, galaxy_dragged, galaxy_down_x, galaxy_down_y;
    char galaxy_query[32];
    Sector waypoint;
    int waypoint_set;
    int hyperspace; /* transient: 0 idle, else real ticks into the jump sequence */
    Sector hyperspace_destination; /* transient: sector the charging jump will reach */
    int entry_brake; /* transient: 1 + body whose entry brake is armed, 0 idle */
    Blast blasts[MAX_BLASTS];
    Smoke smoke[MAX_SMOKE];
    Wreck wrecks[MAX_NPC + 1]; /* slot 0 is reserved for the commander */
    uint32_t blast_serial, destruction_clock;
    int death_started, death_yaw, smoke_next;
    int cinematic; /* transient: the title sequence is staging this Game */
    WeaponBolt bolts[MAX_WEAPON_BOLTS];
    unsigned bolt_next, bolt_count;
    /* Transient parking: 1 + body of an automatic landing, 0 idle; the
       touchdown point is body-fixed Q8 at the landed rest height. */
    int landing;
    Vec land_anchor;
    /* Transient excursion on foot: body-fixed Q8 feet position, heading from
       local north toward east and look pitch (table angles), used sites. */
    int on_foot, walk_yaw, walk_pitch;
    Vec walker;
    uint32_t site_claims[GROUND_CLAIMS];
    int site_claim_next;
    char message[80];
} Game;
typedef struct {
    int pitch, roll, thrust, fire;
    int axis_roll, axis_pitch; /* calibrated analogue, -256..256 */
    int strafe, axis_walk, axis_strafe; /* on foot: sidestep, analogue walk/sidestep */
} Controls;
typedef struct {
    int w, h, enhanced;
    uint8_t *pixels;
    uint32_t palette[256];
} Frame;
extern const char *goods_names[GOODS], *mission_names[MISSION_TYPES];
extern const int ship_capacity[SHIPS];
extern const ShipSpec ship_specs[SHIPS];
extern const char *faction_names[FACTIONS], *equipment_names[EQUIP_TYPES];
extern const int equipment_cost[EQUIP_TYPES];
int system_faction(const Game *g);
const char *military_rank(const Game *g, int faction);
int passengers_used(const Game *g);
int equipped(const Game *g, Equipment item);
const char *ship_refusal(const Game *g, int ship);
int buy_ship(Game *g, int ship);
int buy_equipment(Game *g, int item);
int sell_equipment(Game *g, int item);
int upgrade_weapon(Game *g, int mount);
int recruit_crew(Game *g, int hire);
int negotiate_mission(Game *g, int index);
int illegal_good(const Game *g, int good);
int black_market_available(const Game *g);
void inspect_cargo(Game *g);
int settle_fine(Game *g);
int police_patrol(const Game *g, int index);
Vec patrol_point(const Game *g, int index);
void police_patrols(Game *g);
void police_discharge(Game *g, int target);
void career_missions(Game *g);
void career_time(Game *g);
void career_tick(Game *g, Vec old);
void career_system(Game *g);
void damage_npc(Game *g, int index, int damage);
Vec destruction_position(const Game *g, Vec pos, int body);
void destruction_reset(Game *g);
void destruction_npc(Game *g, int index);
void destruction_player(Game *g, int impact_body);
void destruction_tick(Game *g);
void destruction_rebase(Game *g, Vec delta, Vec velocity);
void weapons_reset(Game *g);
void weapons_tick(Game *g);
void weapons_rebase(Game *g, Vec delta, Vec velocity);
void weapon_bolt(Game *g, Vec start, Vec end, Vec drift, int colour);
void weapon_player(Game *g, Vec direction, int distance);
void weapon_npc(Game *g, int index);
int fire_missile(Game *g);
int countermeasure(Game *g);
int escape_pod(Game *g);
Vec asteroid_position(const Game *g, int index);
Vec rig_position(const Game *g, int index);
int mine_asteroid(Game *g);
int collect_cargo(Game *g);
int deploy_rig(Game *g);
int recover_rig(Game *g);
uint32_t hash32(uint32_t x);
uint32_t sector_seed(uint32_t seed, Sector s);
uint64_t isqrt64(uint64_t n);
Vec vec(int64_t x, int64_t y, int64_t z);
Vec add(Vec a, Vec b);
Vec sub(Vec a, Vec b);
Vec scale(Vec a, int64_t n, int64_t d);
int64_t length(Vec a);
Vec unit(Vec a, int64_t magnitude);
Vec radial(Vec direction, int64_t radius);
int sini(int a);
int cosi(int a);
Vec forward(int yaw, int pitch);
void aim_angles(Vec direction, int *yaw, int *pitch);
void generate_system(uint32_t seed, Sector s, System *out);
void setup_system(Game *g, Sector sector);
void update_world(Game *g);
Vec body_position(const System *s, int body, uint32_t tick);
Vec body_orbit_sample(const Planet *p, int phase);
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
void set_rotation(Planet *p, uint32_t tick);
uint32_t rotation_period(const Planet *p);
Vec body_rotate(const Planet *p, Vec v, int inverse);
Vec surface_velocity(const Planet *p, Vec local);
int surface_light(const System *s, const Planet *p, Vec local);
/* Shared deterministic weather: density 0..256, precipitation 0/1 rain/2 snow. */
int rayleigh_scattering(const System *s, const Planet *p, Vec local, Vec view);
/* Starlight extinction overhead (0 = clear vacuum) and twilight glow 0..256
   with the direction (Q14) of the star near the horizon that drives it. */
int sky_opacity(const Planet *p, int64_t altitude);
int twilight(const System *s, const Planet *p, Vec local, Vec *sun);
int cloud_density(const Planet *p, Vec local, uint32_t tick);
/* Cumulus field: at most one cloud per cell of a body-fixed cube-face lattice.
   Puff centres are metres in the cloud's tangent frame above its flat base. */
#define CLOUD_PUFFS 6
typedef struct { int16_t x, y, z, radius; } CloudPuff;
typedef struct {
    Vec base, east, north, up; /* body-fixed metres; Q14 tangent axes */
    int16_t puffs, strength, height, radius;
    CloudPuff puff[CLOUD_PUFFS];
} Cloud;
int cloud_spacing(const Planet *p);
int cloud_face_cell(const Planet *p, int face, Vec fixed, int64_t reach, int *u, int *v);
const Cloud *cloud_at(const Planet *p, int face, int u, int v, uint32_t tick);
int precipitation(const Planet *p, Vec local, uint32_t tick);
int terrain_elevation(const Planet *p, Vec relative_metres);
int terrain_material(const Planet *p, Vec relative_metres);
/* Signed reflectance detail at a body-fixed direction; independent of time,
   camera and cube face. Geometry/collision heights are unaffected. */
int terrain_albedo(const Planet *p, Vec fixed_direction, int material);
int terrain_regional_albedo(const Planet *p, Vec fixed_direction, int material);
/* The anchor is a full-precision body-relative direction; tangent axes are Q14. */
Vec terrain_vertex(const Planet *p, Vec anchor, Vec right, Vec front, int x, int y);
extern const char *body_names[BODY_TYPES];
int nearest_body(const Game *g, Vec pos);
int64_t altitude_at(const Game *g, Vec pos);
int terrain_height(const Planet *p, Vec relative_metres);
int64_t terrain_height_fine(const Planet *p, Vec relative_q8);
int terrain_impact(const Game *g, Vec from, Vec to, int body);
int port_count(const Game *g);
void game_init(Game *g, uint32_t seed);
void game_tick(Game *g, Controls c);
void game_frame(Game *g, Controls c, int ticks);
void game_action(Game *g, int key);
void view_angles(const Game *g, int *yaw, int *pitch);
/* Integer atan2 in table angles: sine component x, cosine component z. */
int attitude_angle(int64_t x, int64_t z);
/* Parking console: PARK_DOCK (with the port), PARK_LAND or PARK_NONE. */
int park_mode(const Game *g, int *port);
const char *land_refusal(const Game *g);
/* The DOCK/LAND/USE button's label and state for the flight console. */
const char *park_label(const Game *g, int *enabled, int *active);
/* On foot (ground.c). Positions are Q8 in the system frame. */
extern const char *site_names[SITE_KINDS];
const char *disembark_refusal(const Game *g);
int disembark(Game *g);
int embark(Game *g);
int64_t walker_ship_distance(const Game *g);
int board_reach(const Game *g);
void ground_tick(Game *g, Controls c);
Vec walker_position(const Game *g);
void ground_view(const Game *g, Vec *eye, int *yaw, int *pitch, int *roll);
int ground_sites(const Game *g, Vec around, GroundSite *out, int max);
Vec site_position(const Game *g, const GroundSite *s);
int nearest_site(const Game *g, GroundSite *site, int64_t *metres);
int use_site(Game *g);
void ground_interact(Game *g);
int combat_target(const Game *g);
int safe_warp(const Game *g);
Vec avoidance(Vec relative_position, Vec relative_velocity, int64_t acceleration, int radius);
uint64_t isqrt_product(uint64_t a, uint64_t b);
int64_t muldiv(int64_t a, int64_t n, int64_t d);
Vec guidance(Vec pos, Vec velocity, Vec target, int64_t acceleration, int64_t maxspeed);
Vec guidance_step(Vec pos, Vec velocity, Vec target, int64_t acceleration, int64_t maxspeed, int ticks);
int swept_sphere(Vec start, Vec end, Vec center, int64_t radius);
int market_stock(const Game *g, int good);
int market_index(const Game *g, Sector sector, int port);
int price(const Game *g, int good);
int sell_price(const Game *g, int good);
int cargo_used(const Game *g);
int trade(Game *g, int good, int buy);
int accept_mission(Game *g, int index);
int jump(Game *g, Sector destination);
const char *jump_refusal(const Game *g, Sector destination);
int drive_available(const Game *g);
void refresh_board(Game *g);
int save_game(const Game *g, const char *path);
int load_game(Game *g, const char *path);
void render_palette(Frame *f);
int render_init(Frame *f);
void render_shutdown(void);
void render_game(const Game *g, Frame *f);
void render_set_pump(void (*pump)(void));
void c2p(const uint8_t *src, uint8_t *dst, int width, int height, int depth);
#endif
