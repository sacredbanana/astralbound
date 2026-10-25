/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "audio.h"
#include "controller.h"
#include "intro.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
unsigned long __stack = 1048576;
static Game g;
#ifdef __AMIGAOS3__
#define DESTRUCTION_SAVE "astralbound-destruction.sav"
#else
#define DESTRUCTION_SAVE "build/destruction-test.sav"
#endif
static void sound_tests(void) {
    SpaceAudio a, b;
    int8_t whole[8192], chunks[8192];
    unsigned i, effect;
    int nonzero = 0;
    game_init(&g, 0x53504143);
    audio_init(&a, &g); b = a;
    assert(audio_observe(&a, &g) == SOUND_NONE);
    g.shots++;
    assert(audio_observe(&a, &g) == SOUND_LASER);
    assert(audio_observe(&a, &g) == SOUND_NONE);
    g.hull--;
    assert(audio_observe(&a, &g) == SOUND_HIT);
    g.kills++; g.blast_serial++;
    assert(audio_observe(&a, &g) == SOUND_EXPLOSION);
    g.docked = -1;
    assert(audio_observe(&a, &g) == SOUND_DOCK);
    g.system.seed++;
    assert(audio_observe(&a, &g) == SOUND_JUMP);
    b = a;
    audio_mix(&a, whole, sizeof(whole));
    for (i = 0; i < sizeof(chunks); i += 256) audio_mix(&b, chunks + i, 256);
    assert(!memcmp(whole, chunks, sizeof(whole)));
    assert(a.cursor == b.cursor && a.effect_pos == b.effect_pos);
    a.paused = 1;
    audio_mix(&a, whole, sizeof(whole));
    for (i = 0; i < sizeof(whole); i++) assert(whole[i] == 0);
    for (effect = 1; effect < SOUND_COUNT; effect++) {
        nonzero = 0;
        for (i = 0; i < audio_effect_length(effect); i++) nonzero |= audio_effect(effect, i) != 0;
        assert(nonzero && audio_effect(effect, audio_effect_length(effect)) == 0);
    }
    /* The charge and tunnel effects span their sequence phases. */
    assert(audio_effect_length(SOUND_HYPERSPACE) == AUDIO_RATE * HYPER_CHARGE / HZ);
    assert(audio_effect_length(SOUND_JUMP) == AUDIO_RATE * HYPER_TUNNEL / HZ);
    game_init(&g, 0x53504143);
    audio_init(&a, &g);
    g.hyperspace = 1;
    assert(audio_observe(&a, &g) == SOUND_HYPERSPACE);
    g.hyperspace = 0; /* aborted charge */
    assert(audio_observe(&a, &g) == SOUND_NONE && a.effect == SOUND_NONE);
    g.docked = -1; g.page = FLIGHT; g.throttle = 4000; g.fuel = 0;
    audio_observe(&a, &g);
    assert(a.active && !a.throttle); /* a dry tank has no engine note */
    puts("PASS procedural effects, event priority, chunk continuity, pause, hyperspace and dry tank");
}
static void controller_tests(void) {
    Controller p = {0};
    Controls c = {0};
    int i, roll, yaw;
    game_init(&g, 0x53504143);
    assert(analogue_axis(128,128) == 0 && analogue_axis(140,128) == 0);
    assert(analogue_axis(0,128) == -256 && analogue_axis(255,128) == 256);
    assert(analogue_axis(0,0) == 0 && analogue_axis(255,255) == 0);
    controller_calibrate(&p, 100, 150);
    g.docked = -1; g.pos = add(g.pos, vec(0, 18000 * FP, 0)); g.page = FLIGHT;
    controller_apply(&p, &g, &c, PAD_FIRE | PAD_THRUST, 255, 0, 1, 128, 128);
    assert(c.axis_roll == 256 && c.axis_pitch == -256 && c.fire && c.thrust == 1);
    c = (Controls){0};
    controller_apply(&p, &g, &c, 0, 100, 255, 1, 128, 128);
    assert(!c.axis_roll && c.axis_pitch == 256);
    game_tick(&g, c);
    assert(g.pitch == 3); /* Pushing down raises the nose. */
    c = (Controls){0}; c.axis_roll = 64;
    roll = g.roll; yaw = g.yaw;
    for (i = 0; i < 4; i++) game_tick(&g, c);
    assert(g.roll == ((roll + 3) & 1023) && g.yaw == yaw);
    c = (Controls){0}; c.roll = -1; c.axis_roll = 256;
    game_tick(&g, c);
    assert(g.roll == roll && g.yaw == yaw); /* digital wins */
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0, 128, 128); assert(g.paused);
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0, 128, 128); assert(g.paused);
    controller_apply(&p, &g, &c, 0, 0, 0, 0, 128, 128);
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0, 128, 128); assert(!g.paused);
    g.throttle = 4000;
    controller_apply(&p, &g, &c, PAD_BRAKE, 0, 0, 0, 128, 128); assert(!g.throttle);
    g.page = MARKET; c = (Controls){0};
    controller_apply(&p, &g, &c, PAD_FIRE | PAD_THRUST, 255, 0, 1, 128, 128);
    assert(!c.fire && !c.thrust && !c.axis_roll);
    puts("PASS analogue calibration/rates, digital precedence, button edges and menu isolation");
}
static int active_blasts(const Game *g) {
    int i, n = 0;
    for (i = 0; i < MAX_BLASTS; i++) n += g->blasts[i].active != 0;
    return n;
}
static void destruction_tests(void) {
    static Game reference;
    SpaceAudio audio;
    Controls fire = {0};
    Vec up, local, start;
    uint32_t tick, serial;
    int i, frames;
    game_init(&g, 0x53504143);
    g.docked = g.frame_body = -1;
    g.pos = vec(0, AU_METRES * FP, 0); g.vel = vec(400 * FP, 0, 0);
    g.page = MAP; g.camera_mode = 4; g.autopilot = g.drive_manual = 1;
    g.ui_fire = 1; g.warp = MAX_WARP; g.hull = 0;
    game_frame(&g, (Controls){0}, 1);
    assert(g.dead && g.death_started && g.page == FLIGHT && g.camera_mode == 2);
    assert(!g.autopilot && !g.drive_manual && !g.ui_fire && g.warp == 1);
    assert(g.wrecks[0].state == 2 && active_blasts(&g) == 1);
    tick = g.tick; serial = g.blast_serial;
    reference = g;
    fire.fire = fire.thrust = fire.roll = 1;
    game_frame(&g, fire, HZ);
    for (i = 0; i < HZ; i++) game_frame(&reference, fire, 1);
    assert(!memcmp(&g, &reference, sizeof(g))); /* frame partition invariant */
    assert(g.tick == tick && g.blast_serial == serial && !g.shots);
    assert(length(sub(g.pos, destruction_position(&g, g.blasts[0].pos, -1))) < 2 * FP);
    game_frame(&g, fire, BLAST_TICKS + SMOKE_TICKS);
    assert(!active_blasts(&g));
    for (i = 0; i < MAX_SMOKE; i++) assert(!g.smoke[i].active);
    game_action(&g, 'r');
    assert(!g.dead && !g.death_started && !active_blasts(&g));

    /* Shot down above real terrain: external view immediately, burst only on
       swept terrain contact, regardless of held fire or the old warp. */
    game_init(&g, 0x53504143);
    g.docked = -1; g.frame_body = g.body = 0;
    up = g.ports[0].normal;
    g.pos = add(g.ports[0].pos, scale(up, 900 * FP, 16384));
    local = sub(g.pos, scale(g.system.planets[0].center, FP, 1));
    g.vel = add(surface_velocity(&g.system.planets[0], scale(local, 1, FP)),
                scale(forward(0, 0), 80 * FP, 16384));
    start = g.pos;
    g.hull = 1; g.shield = 0; g.camera_mode = 4;
    g.npc[0] = (Npc){add(g.pos, vec(0,0,1200 * FP)), g.vel, 100, 1, 1, -1, 0};
    g.npc_role[0] = RAIDER;
    audio_init(&audio, &g);
    game_tick(&g, (Controls){0});
    assert(g.dead && g.hull == 0);
    assert(g.wrecks[0].state == 1 && !active_blasts(&g));
    assert(audio_observe(&audio, &g) == SOUND_HIT);
    assert(g.camera_mode == 2 && g.page == FLIGHT);
    tick = g.tick;
    game_frame(&g, fire, HZ * 2);
    assert(g.wrecks[0].state == 1 && !active_blasts(&g));
    assert(length(sub(g.pos, start)) > 20 * FP);
    frames = 0;
    while (g.wrecks[0].state == 1 && frames++ < HZ * 120) game_frame(&g, fire, 1);
    assert(g.wrecks[0].state == 2 && g.tick == tick && g.blast_serial == 1);
    assert(llabs(altitude_at(&g, g.pos) - 10) < 4);
    assert(audio_observe(&audio, &g) == SOUND_EXPLOSION);
    assert(audio_observe(&audio, &g) == SOUND_NONE);
    g.blast_serial = 0; /* loading/changing systems clears transient events */
    assert(audio_observe(&audio, &g) == SOUND_NONE);
    g.blast_serial = 1;
    assert(save_game(&g, DESTRUCTION_SAVE));
    assert(load_game(&reference, DESTRUCTION_SAVE));
    assert(!reference.death_started && !active_blasts(&reference));
    game_frame(&reference, fire, 1);
    assert(reference.dead && reference.death_started && reference.camera_mode == 2);
    remove(DESTRUCTION_SAVE); remove(DESTRUCTION_SAVE ".bak");

    game_init(&g, 0x53504143);
    g.frame_body = -1; g.pos = vec(0, AU_METRES * FP, 0);
    g.npc[0] = (Npc){add(g.pos, vec(0,0,600 * FP)), vec(0,0,0), 1, 1, 1, -1, 0};
    g.npc_role[0] = RAIDER;
    damage_npc(&g, 0, 2);
    assert(!g.npc[0].active && g.kills == 1 && active_blasts(&g) == 1);
    serial = g.blast_serial; damage_npc(&g, 0, 2); assert(g.blast_serial == serial);
    game_init(&reference, 0x53504143);
    reference.frame_body = 0;
    reference.npc[0] = (Npc){add(reference.ports[0].pos,
        scale(reference.ports[0].normal, 200 * FP, 16384)), vec(0,0,0), 1, 1, 1, -1, 0};
    reference.npc_role[0] = RAIDER;
    reference.npc[0].vel = surface_velocity(&reference.system.planets[0],
        sub(scale(reference.npc[0].pos, 1, FP), reference.system.planets[0].center));
    audio_init(&audio, &reference);
    damage_npc(&reference, 0, 2);
    assert(reference.wrecks[1].state == 1 && !active_blasts(&reference));
    assert(audio_observe(&audio, &reference) == SOUND_NONE); /* kill credit is not impact audio */
    for (i = 0; i < 120 * HZ && reference.wrecks[1].state; i++) destruction_tick(&reference);
    assert(!reference.wrecks[1].state && active_blasts(&reference) == 1);
    assert(audio_observe(&audio, &reference) == SOUND_EXPLOSION);
    /* Fixed capacity survives simultaneous destruction without allocation. */
    for (i = 0; i < MAX_BLASTS * 2; i++) {
        g.npc[0].active = 1; g.npc[0].hull = 1; damage_npc(&g, 0, 2);
    }
    assert(active_blasts(&g) == MAX_BLASTS);
    setup_system(&g, g.system.sector);
    assert(!active_blasts(&g) && !g.death_started);
    puts("PASS external death camera, ballistic smoke/terrain impact, real-time playback, blast lifetime, NPC destruction and transient saves");
}
/* The title sequence is a fixed script on a borrowed Game: same frames on
   every run, cameras never underground, every story beat audible, and an
   ending or skip that leaves exactly a fresh commander. */
static int active_bolts(const Game *game) {
    int i,n=0;
    for(i=0;i<MAX_WEAPON_BOLTS;i++) n+=game->bolts[i].ticks>0;
    assert((unsigned)n==game->bolt_count);
    return n;
}
static void weapon_stage(void) {
    game_init(&g,0x53504143);
    g.docked=g.frame_body=-1; g.page=FLIGHT;
    g.pos=vec(0,AU_METRES*FP,0); g.vel=vec(0,0,0);
    memset(g.npc,0,sizeof(g.npc));
    g.yaw=g.pitch=g.roll=0; g.tick=5;
}
static void weapon_tests(void) {
    static Game loaded;
    WeaponBolt before;
    Vec delta=vec(100*FP,-80*FP,30*FP),transfer=vec(3*FP,5*FP,-7*FP);
    int i,mode,role;
    /* Real fire controls, including turrets, keep hitscan damage and aim. */
    for(mode=0;mode<=4;mode++) {
        Vec direction=forward(mode>=3 ? 256 : 0,0);
        weapon_stage(); g.camera_mode=mode; g.turret_yaw=256;
        g.npc[0]=(Npc){add(g.pos,scale(direction,2000*FP,16384)),vec(0,0,0),100,1,0,-1,0};
        game_tick(&g,(Controls){.fire=1});
        assert(g.shots==1 && g.heat==4 && g.npc[0].hull==80 && active_bolts(&g)==2);
        for(i=0;i<2;i++) {
            Vec path=unit(sub(g.bolts[i].end,g.pos),16384);
            assert(length(sub(path,direction))<4 && g.bolts[i].colour==12);
        }
    }
    /* Banking moves the actual hardpoints; unsupported turrets cannot fire. */
    weapon_stage(); g.roll=256; game_tick(&g,(Controls){.fire=1});
    assert(active_bolts(&g)==2 && llabs(g.bolts[0].start.y-g.bolts[1].start.y)>10*FP);
    for(i=0;i<SHIPS;i++) {
        weapon_stage(); g.ship=i; weapon_player(&g,forward(0,0),1);
        assert(g.bolts[0].end.z>g.bolts[0].start.z); /* close targets never reverse a bolt */
    }
    weapon_stage(); g.equipment=0; g.camera_mode=3;
    game_tick(&g,(Controls){.fire=1}); assert(!active_bolts(&g) && !g.shots);
    weapon_stage(); g.heat=95; game_tick(&g,(Controls){.fire=1}); assert(!active_bolts(&g));
    weapon_stage(); g.equipment|=1<<MINING;
    g.pos=sub(asteroid_position(&g,0),vec(0,0,1000*FP));
    assert(mine_asteroid(&g) && active_bolts(&g)==2 && g.shots==1);
    for(role=RAIDER;role<=POLICE;role++) {
        weapon_stage(); g.shield=100;
        g.npc[0]=(Npc){add(g.pos,vec(600*FP,100*FP,1500*FP)),vec(0,0,0),100,1,1,-1,0};
        g.npc_role[0]=role; g.npc_port[0]=-1;
        game_tick(&g,(Controls){0});
        assert(active_bolts(&g)==1 && g.shield==100-(role==POLICE ? POLICE_DAMAGE : 8));
        assert(g.bolts[0].colour==(role==POLICE ? 13 : 11));
        assert(length(sub(g.bolts[0].end,g.pos))==0);
        game_tick(&g,(Controls){0}); assert(active_bolts(&g)==1); /* cooldown */
    }
    weapon_stage(); g.vel=vec(100*FP,40*FP,-80*FP);
    weapon_player(&g,forward(0,0),5000); before=g.bolts[0];
    weapons_tick(&g);
    assert(g.bolts[0].age==1 && length(sub(sub(g.bolts[0].start,before.start),scale(g.vel,1,HZ)))==0);
    before=g.bolts[0]; weapons_rebase(&g,delta,transfer);
    assert(length(sub(sub(g.bolts[0].start,before.start),delta))==0);
    assert(length(sub(sub(g.bolts[0].end,before.end),delta))==0);
    assert(length(sub(sub(g.bolts[0].drift,before.drift),transfer))==0);
    before=g.bolts[0]; g.paused=1; game_frame(&g,(Controls){0},8);
    assert(!memcmp(&before,&g.bolts[0],sizeof(before))); g.paused=0;
    g.ui_quit=1; game_frame(&g,(Controls){0},8); assert(g.bolts[0].age==before.age); g.ui_quit=0;
    /* Transient visuals never enter a commander payload. */
    assert(save_game(&g,DESTRUCTION_SAVE) && load_game(&loaded,DESTRUCTION_SAVE));
    assert(!active_bolts(&loaded)); remove(DESTRUCTION_SAVE);
    destruction_player(&g,-1); game_frame(&g,(Controls){0},HZ);
    assert(!active_bolts(&g));
    weapon_stage();
    for(i=0;i<MAX_WEAPON_BOLTS*5;i++) weapon_player(&g,forward(i&1023,0),5000);
    assert(active_bolts(&g)==MAX_WEAPON_BOLTS && g.bolt_next<MAX_WEAPON_BOLTS*2);
    setup_system(&g,g.system.sector); assert(!active_bolts(&g));
    puts("PASS player/turret/banked hardpoints, raider/police fire, unchanged damage, bolt bounds, drift/rebase, pause, death and transient saves");
}
static void intro_tests(void) {
    static Game fresh, stage;
    static Intro first, second;
    SpaceAudio audio;
    int heard[SOUND_COUNT] = {0}, t, chunk = 7, jumped = 0;
    uint32_t home;
    assert(INTRO_TICKS >= HZ * 190 && INTRO_TICKS <= HZ * 200);
    game_init(&fresh, 0x53504143);
    home = fresh.system.seed;
    intro_start(&first, &g, 0x53504143);
    assert(first.running && g.cinematic && g.docked == -1 && g.ship == 3);
    {
        /* The escort and the rain: some ticks show the ally hull, and the
           rendezvous is flown in rain under the cloud base. */
        int escorted = 0, rain = 0, k;
        intro_start(&second, &stage, 0x53504143);
        for (k = 0; k < INTRO_TICKS - HZ; k += HZ / 2) {
            intro_frame(&second, &stage, k - second.tick);
            escorted += second.escort;
            if (second.escort) assert(second.ally.ship == 5 && length(second.ally.front) > 16000);
            rain |= precipitation(&stage.system.planets[stage.body],
                scale(sub(second.eye, scale(stage.system.planets[stage.body].center, FP, 1)), 1, FP), stage.tick) == 1;
        }
        assert(escorted > 150 && rain);
    }
    assert(!intro_skippable(&first));
    intro_start(&second, &stage, 0x53504143);
    audio_init(&audio, &g);
    for (t = 1; first.running; t += chunk) {
        int steps = t + chunk > INTRO_TICKS ? INTRO_TICKS - t + 1 : chunk, k;
        for (k = 0; k < steps && first.running; k++) {
            intro_frame(&first, &g, 1);
            if (!first.running) break;
            heard[audio_observe(&audio, &g)] = 1;
            if (g.system.seed != home) jumped = 1;
            /* Directed cameras stay above ground and inside the system. */
            if (!first.title && !(g.hyperspace > HYPER_CHARGE && g.hyperspace <= HYPER_CHARGE + HYPER_TUNNEL))
                assert(altitude_at(&g, first.eye) > 5);
            assert(first.beams <= INTRO_BEAMS && first.glows <= INTRO_GLOWS);
        }
        /* Chunked playback matches tick-by-tick playback exactly. */
        if (first.running) {
            intro_frame(&second, &stage, steps);
            assert(second.tick == first.tick && !memcmp(&second.eye, &first.eye, sizeof(Vec)));
            assert(!memcmp(&stage.pos, &g.pos, sizeof(Vec)) && stage.yaw == g.yaw && stage.roll == g.roll);
        }
    }
    assert(t >= INTRO_TICKS - chunk && jumped);
    assert(heard[SOUND_LASER] && heard[SOUND_HIT] && heard[SOUND_EXPLOSION]);
    assert(heard[SOUND_HYPERSPACE] && heard[SOUND_JUMP] && heard[SOUND_DOCK]);
    assert(!g.cinematic && !memcmp(&g, &fresh, sizeof(Game)));
    /* A skip, after the launch keypress grace period, gives the same Game. */
    intro_start(&first, &g, 0x53504143);
    intro_frame(&first, &g, HZ / 2);
    assert(intro_skippable(&first));
    intro_frame(&first, &g, HZ * 100);
    intro_finish(&first, &g);
    assert(!first.running && !intro_skippable(&first) && !memcmp(&g, &fresh, sizeof(Game)));
    puts("PASS title sequence length, determinism, camera clearance, story audio cues, skip and clean handover");
}
int main(void) { sound_tests(); controller_tests(); destruction_tests(); weapon_tests(); intro_tests(); return 0; }
