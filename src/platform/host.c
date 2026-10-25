/* SPDX-License-Identifier: Unlicense; SDL is used only by the host harness. */
#include "astralbound.h"
#include "flight_input.h"
#include "ui.h"
#include "audio.h"
#include "music.h"
#include "controller.h"
#include "intro.h"
#include "launcher.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static Game g;
static Intro intro;
static Settings preferences;
static Performance performance;
static int testing, test_scene=-1;
static SpaceAudio sound;
static SDL_AudioDeviceID audio_device;
static void audio_callback(void *userdata, Uint8 *out, int size) {
    audio_mix((SpaceAudio *)userdata, (int8_t *)out, (unsigned)size);
}
static void ppm(Frame *f, const char *path) {
    FILE *o = fopen(path, "wb");
    int i;
    if (!o)
        return;
    fprintf(o, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc(c >> 16, o);
        fputc(c >> 8, o);
        fputc(c, o);
    }
    fclose(o);
}
static int platform_request(int request) {
    if (request == UI_EXIT) return 1;
    if (request == UI_SAVE || request == UI_SAVE_EXIT) {
        int saved = save_game(&g, "astralbound.sav");
        snprintf(g.message, sizeof(g.message), "%s", saved ? "SAVED" : "SAVE FAILED");
        if (request == UI_SAVE_EXIT) {
            if (saved) return 1;
            g.ui_quit = 2;
        }
    }
    if (request == UI_LOAD && !load_game(&g, "astralbound.sav"))
        snprintf(g.message, sizeof(g.message), "LOAD FAILED");
    return 0;
}
static int host_run(int argc, char **argv) {
    Frame f = {320, 256, 1, 0, {0}};
    SDL_Window *window=NULL;
    SDL_Renderer *renderer=NULL;
    SDL_Texture *texture=NULL;
    SDL_Joystick *joystick = NULL;
    SDL_GameController *gamepad = NULL;
    Controller pad = {0};
    controller_calibrate(&pad, 128, 128);
    MouseStick mouse = {0};
    int mouse_right = 0;
    uint32_t *rgb=NULL;
    Uint64 last, now, freq;
    uint64_t accumulator = 0;
    int quit = 0, i, radar_preview = 0, muted = 0, show_intro = 1, result=0;
    audio_device=0;
    settings_dimensions(&preferences,1,&f.w,&f.h);
    f.enhanced=preferences.mode!=VIDEO_ECS;
    render_graphics(&preferences.gfx);
    render_renderer(preferences.renderer);
    render_bezier(preferences.bezier);
    muted=!preferences.sound; show_intro=preferences.intro;
    memset(&intro,0,sizeof(intro));
    game_init(&g, 0x53504143);
    for (i = 1; i < argc; i++) {
        /* Preview flags stage a scene the title sequence would overwrite. */
        static const char *staged[] = {"--city-overhead", "--market", "--board", "--equipment",
            "--shipyard", "--destinations", "--weapons", "--system-map", "--system-data",
            "--local-map", "--turret", "--radar", "--orbit", "--surface"};
        size_t k;
        for (k = 0; k < sizeof(staged) / sizeof(*staged); k++)
            if (!strcmp(argv[i], staged[k])) show_intro = 0;



        if (!strcmp(argv[i], "--city-overhead")) {
            const Port *port = &g.ports[0];
            g.docked = -1;
            g.pos = add(port->pos, scale(port->normal, 1232LL * FP, 16384));
            aim_angles(sub(port->pos, g.pos), &g.yaw, &g.pitch);
        }
        if (!strcmp(argv[i], "--market")) g.page = MARKET;
        if (!strcmp(argv[i], "--board")) g.page = BOARD;
        if (!strcmp(argv[i], "--equipment")) g.page = EQUIPMENT;
        if (!strcmp(argv[i], "--shipyard")) g.page = SHIPYARD;
        if (!strcmp(argv[i], "--destinations")) { g.page = SYSTEM_MAP; g.system_display = 0; }
        if (!strcmp(argv[i], "--weapons")) g.ui_weapons = 1;
        if (!strcmp(argv[i], "--system-map")) { g.page = SYSTEM_MAP; g.system_display = 1; }
        if (!strcmp(argv[i], "--system-data")) { g.page = SYSTEM_MAP; g.system_display = 2; }
        if (!strcmp(argv[i], "--local-map")) { g.page = SYSTEM_MAP; g.system_display = 1; g.map_local = 1; }
        if (!strcmp(argv[i], "--turret")) { g.camera_mode = 3; g.turret_yaw = g.yaw; g.turret_pitch = 128; }
        if (!strcmp(argv[i], "--radar"))
            radar_preview = 1;

        if (!strcmp(argv[i], "--orbit")) {
            g.docked = -1;
            g.pos = add(
                scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + g.system.planets[0].atmosphere + 35000LL) * FP,
                    -60000LL * FP));
            g.pitch = -70;
        }
        if (!strcmp(argv[i], "--surface")) {
            g.docked = -1;
            g.pos = add(scale(g.system.planets[0].center, FP, 1),
                        vec(0, (g.system.planets[0].radius + 220LL) * FP, -900LL * FP));
            g.pitch = -35;
        }
    }
    if (radar_preview) {
        g.npc[0] = (Npc){add(g.pos, scale(forward(g.yaw, g.pitch), 2000 * FP, 16384)),
                        vec(0, 0, 0), 100, 1, 1, -1, 0};
        g.combat_target = 0;
    }
    f.pixels = malloc((size_t)f.w * f.h);
    rgb = malloc((size_t)f.w * f.h * 4);
    if (!f.pixels || !rgb || !render_init(&f)) { result=1; goto done; }
    if (argc > 2 && !strcmp(argv[1], "--render")) {
        render_game(&g, &f);
        ppm(&f, argv[2]);
        free(rgb);
        free(f.pixels);
        render_shutdown();
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "--render-intro")) {
        intro_start(&intro, &g, 0x53504143);
        intro_frame(&intro, &g, (int)(atof(argv[2]) * HZ));
        if (intro.running) render_intro(&intro, &g, &f);
        else render_game(&g, &f);
        ppm(&f, argv[3]);
        free(rgb);
        free(f.pixels);
        render_shutdown();
        return 0;
    }
    if(testing) { performance_begin(&performance,&g,&preferences,test_scene); performance.report.width=f.w; performance.report.height=f.h; }
    else if (show_intro) intro_start(&intro, &g, 0x53504143);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        result=1; goto done;
    }
    window = SDL_CreateWindow("ASTRALBOUND - Amiga development preview", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 960, 768, SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (window && !renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { result=1; goto done; }
    SDL_RenderSetLogicalSize(renderer, f.w, f.h);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                f.w, f.h);
    if (!texture) { result=1; goto done; }
    if (SDL_NumJoysticks() > 0) {
        if (SDL_IsGameController(0)) gamepad = SDL_GameControllerOpen(0);
        joystick = gamepad ? SDL_GameControllerGetJoystick(gamepad) : SDL_JoystickOpen(0);
    }
    audio_init(&sound, &g);
    if (!muted) {
        SDL_AudioSpec spec = {0};
        char path[1024];
        char *base = SDL_GetBasePath();
        int loaded = -1;
        if (base) {
            snprintf(path, sizeof(path), "%smusic", base);
            loaded = music_load(path, (uint32_t)SDL_GetPerformanceCounter());
            SDL_free(base);
        }
        if (loaded < 0) music_load("music", (uint32_t)SDL_GetPerformanceCounter());
        if(testing) performance_audio(&performance,&sound,&g);
        else audio_observe(&sound, &g);
        spec.freq = AUDIO_RATE; spec.format = AUDIO_S8; spec.channels = 1;
        spec.samples = 512; spec.callback = audio_callback; spec.userdata = &sound;
        audio_device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
        if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    }
    SDL_ShowCursor(SDL_DISABLE);
    ui_move(&g, &f, f.w / 2, f.h / 2);
    freq = SDL_GetPerformanceFrequency();
    last = SDL_GetPerformanceCounter();
    while (!quit) {
        SDL_Event e;
        Controls c = {0};
        const Uint8 *keys;
        Uint64 test_start=SDL_GetPerformanceCounter(), render_start=0, output_start=0;
        int skip = 0, staged = intro.running;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_JOYDEVICEADDED && !joystick) {
                if (SDL_IsGameController(e.jdevice.which)) gamepad = SDL_GameControllerOpen(e.jdevice.which);
                joystick = gamepad ? SDL_GameControllerGetJoystick(gamepad) : SDL_JoystickOpen(e.jdevice.which);
            }
            if (e.type == SDL_JOYDEVICEREMOVED && joystick &&
                SDL_JoystickInstanceID(joystick) == e.jdevice.which) {
                if (gamepad) SDL_GameControllerClose(gamepad); else SDL_JoystickClose(joystick);
                joystick = NULL; gamepad = NULL; pad.held = 0;
            }
            if(testing) {
                if(e.type==SDL_QUIT || (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) || e.type==SDL_JOYBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_MOUSEBUTTONDOWN) quit=1;
                continue;
            }
            if (intro.running) {
                /* Any key, button or click skips the title sequence. */
                if (e.type == SDL_QUIT) quit = 1;
                if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_MOUSEBUTTONDOWN ||
                    e.type == SDL_JOYBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONDOWN) skip = 1;
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                g.page == FLIGHT && !g.ui_quit) {
                mouse_right = 1;
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }
            if ((e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) ||
                (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
                mouse_right = 0;
                SDL_SetRelativeMouseMode(SDL_FALSE);
                mouse_stick(&mouse, 0, 0, 0);
            }
            if (e.type == SDL_MOUSEWHEEL && e.wheel.y && g.page==MAP && !g.ui_quit) game_action(&g,e.wheel.y>0?'+':'-');
            if (e.type == SDL_MOUSEMOTION) {
                if (!mouse_right) {
                    ui_move(&g, &f, e.motion.x, e.motion.y);
                    ui_pointer(&g, &f, e.motion.x, e.motion.y, g.ui_down);
                }
                mouse_stick(&mouse, e.motion.xrel, e.motion.yrel,
                            mouse_right && g.page == FLIGHT && !g.paused);
            }
            if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.button == SDL_BUTTON_LEFT) {
                int request = ui_pointer(&g, &f, mouse_right ? g.ui_x : e.button.x,
                                         mouse_right ? g.ui_y : e.button.y, e.type == SDL_MOUSEBUTTONDOWN);
                if (platform_request(request)) quit = 1;
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_cancel(&g);
            if (e.type == SDL_QUIT && !g.ui_quit)
                ui_quit_key(&g, 27);
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                int k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE || g.ui_quit) {
                    if (platform_request(ui_quit_key(&g, k))) quit = 1;
                }
                else if (k == SDLK_F5)
                    snprintf(g.message, sizeof(g.message), "%s",
                             save_game(&g, "astralbound.sav") ? "SAVED" : "SAVE FAILED");
                else if (k == SDLK_F9) {
                    if (!load_game(&g, "astralbound.sav"))
                        snprintf(g.message, sizeof(g.message), "LOAD FAILED");
                } else if (k == SDLK_UP)
                    game_action(&g, 1001);
                else if (k == SDLK_DOWN)
                    game_action(&g, 1002);
                else if (k == SDLK_LEFT)
                    game_action(&g, 1003);
                else if (k == SDLK_RIGHT)
                    game_action(&g, 1004);
                else if (k == SDLK_EQUALS)
                    game_action(&g, '+');
                else
                    game_action(&g, k);
            }
        }
        if (intro.running) {
            if (skip && intro_skippable(&intro)) intro_finish(&intro, &g);
        } else if(!testing) ui_pointer(&g, &f, g.ui_x, g.ui_y, g.ui_down);
        keys = SDL_GetKeyboardState(0);
        if (g.page != FLIGHT || g.paused || g.ui_quit) {
            mouse_right = 0;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            mouse_stick(&mouse, 0, 0, 0);
        }
        if (!testing && !intro.running && !g.ui_quit && g.page == FLIGHT && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
            c.pitch = keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP];
            c.roll = keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT];
            c.thrust = keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S];
            int roll = 0, pitch = 0, fire = 0;
            if (joystick && !gamepad) {
                Uint8 hat = SDL_JoystickNumHats(joystick) ? SDL_JoystickGetHat(joystick, 0) : 0;
                if (SDL_JoystickNumAxes(joystick) > 0)
                    roll = flight_axis(SDL_JoystickGetAxis(joystick, 0), 8000);
                if (SDL_JoystickNumAxes(joystick) > 1)
                    pitch = flight_axis(SDL_JoystickGetAxis(joystick, 1), 8000);
                if (hat & SDL_HAT_RIGHT) roll = 1;
                if (hat & SDL_HAT_LEFT) roll = -1;
                if (hat & SDL_HAT_UP) pitch = -1;
                if (hat & SDL_HAT_DOWN) pitch = 1;
                if (SDL_JoystickNumButtons(joystick)) fire = SDL_JoystickGetButton(joystick, 0);
            }
            c.fire = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
            flight_input(&c, &mouse, roll, pitch,
                         fire || ui_fire(&g, &f));
        }
        if (!testing && !intro.running && !g.ui_quit && gamepad && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
            unsigned buttons = 0;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A)) buttons |= PAD_FIRE;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B)) buttons |= PAD_BRAKE;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X)) buttons |= PAD_TARGET;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y)) buttons |= PAD_AUTO;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= PAD_THRUST;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) buttons |= PAD_REVERSE;
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START)) buttons |= PAD_PAUSE;
            controller_apply(&pad, &g, &c, buttons,
                             ((int)SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX) + 32768) / 256,
                             ((int)SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY) + 32768) / 256, 1);
        } else pad.held = 0;
        now = SDL_GetPerformanceCounter();
        accumulator += (now - last) * HZ;
        last = now;
        if (accumulator > freq * 8)
            accumulator = freq * 8;
        {
            int due = (int)(accumulator / freq);
            accumulator -= (uint64_t)due * freq;
            if(testing) performance_animate(&performance,&g);
            else if (intro.running) intro_frame(&intro, &g, due);
            else game_frame(&g, c, due);
            /* The commander's Game replaced the stage: resynchronise sound
               silently instead of hearing the switch as a jump and a dock. */
            if (staged && !intro.running) {
                if (audio_device) SDL_LockAudioDevice(audio_device);
                audio_init(&sound, &g);
                if (audio_device) SDL_UnlockAudioDevice(audio_device);
            }
        }
        if (audio_device) SDL_LockAudioDevice(audio_device);
        if(testing) performance_audio(&performance,&sound,&g);
        else audio_observe(&sound, &g);
        if (audio_device) SDL_UnlockAudioDevice(audio_device);
        g.ui_visible = !intro.running && !mouse_right && !!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
        render_start=SDL_GetPerformanceCounter();
        if (intro.running) render_intro(&intro, &g, &f);
        else if(testing) render_performance_scene(&g,&f);
        else render_game(&g, &f);
        if(testing) render_performance(&performance,&f);
        output_start=SDL_GetPerformanceCounter();
        for (i = 0; i < f.w * f.h; i++)
            rgb[i] = 0xff000000U | f.palette[f.pixels[i]];
        SDL_UpdateTexture(texture, 0, rgb, f.w * 4);
        SDL_RenderCopy(renderer, texture, 0, 0);
        SDL_RenderPresent(renderer);
        if(testing && !quit) {
            Uint64 end=SDL_GetPerformanceCounter();
            if(performance_record(&performance,&g,(output_start-render_start)*1000000/freq,
                 (end-output_start)*1000000/freq,(end-test_start)*1000000/freq)==1) quit=1;
        }
    }
done:
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    audio_device=0;
    music_close();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (gamepad) SDL_GameControllerClose(gamepad); else if (joystick) SDL_JoystickClose(joystick);
    SDL_Quit();
    free(rgb);
    free(f.pixels);
    render_shutdown();
    if(testing && !result) performance_print(stdout,&performance.report);
    return result;
}

/* A flat launch window is cheap even when the selected flight renderer is Ultra. */
static int host_launcher(Launcher *m, const char *path) {
    SDL_Window *w=NULL; SDL_Renderer *r=NULL; SDL_Texture *t=NULL; SDL_Joystick *joy=NULL; SDL_GameController *controller=NULL;
    Frame f={320,200,0,NULL,{0}}; uint32_t rgb[320*200]; int action=0,xaxis=0,yaxis=0,i;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)) return LAUNCH_QUIT;
    w=SDL_CreateWindow("Astralbound - launch options",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,SDL_WINDOW_RESIZABLE);
    if(w) r=SDL_CreateRenderer(w,-1,SDL_RENDERER_PRESENTVSYNC);
    if(w && !r) r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);
    if(r) { SDL_RenderSetLogicalSize(r,320,200); t=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,200); }
    f.pixels=malloc(320*200);
    if(!t || !f.pixels) { action=LAUNCH_QUIT; goto done; }
    render_palette(&f);
    if(SDL_NumJoysticks()) {
        if(SDL_IsGameController(0)) controller=SDL_GameControllerOpen(0);
        joy=controller ? SDL_GameControllerGetJoystick(controller) : SDL_JoystickOpen(0);
    }
    SDL_ShowCursor(SDL_ENABLE);
    while(!action) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            int key=0;
            if(e.type==SDL_QUIT) action=LAUNCH_QUIT;
            if(e.type==SDL_KEYDOWN) {
                key=e.key.keysym.sym;
                if(key==SDLK_UP) key=LAUNCH_UP;
                if(key==SDLK_DOWN) key=LAUNCH_DOWN;
                if(key==SDLK_LEFT) key=LAUNCH_LEFT;
                if(key==SDLK_RIGHT) key=LAUNCH_RIGHT;
            }
            if(e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP) {
                if(e.button.button==SDL_BUTTON_LEFT) action=launcher_pointer(m,e.button.x,e.button.y,e.type==SDL_MOUSEBUTTONDOWN);
            }
            if(e.type==SDL_CONTROLLERBUTTONDOWN) {
                switch(e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: key=LAUNCH_UP; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key=LAUNCH_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key=LAUNCH_LEFT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key=LAUNCH_RIGHT; break;
                case SDL_CONTROLLER_BUTTON_B: key=27; break;
                case SDL_CONTROLLER_BUTTON_A: key='\r'; break;
                }
            }
            if(e.type==SDL_JOYBUTTONDOWN && !controller) key='\r';
            if(e.type==SDL_JOYHATMOTION) {
                if(e.jhat.value&SDL_HAT_UP) key=LAUNCH_UP;
                if(e.jhat.value&SDL_HAT_DOWN) key=LAUNCH_DOWN;
                if(e.jhat.value&SDL_HAT_LEFT) key=LAUNCH_LEFT;
                if(e.jhat.value&SDL_HAT_RIGHT) key=LAUNCH_RIGHT;
            }
            if(e.type==SDL_JOYAXISMOTION && e.jaxis.axis<2) {
                int axis=flight_axis(e.jaxis.value,16000),*old=e.jaxis.axis ? &yaxis : &xaxis;
                if(axis && axis!=*old) key=e.jaxis.axis ? (axis>0 ? LAUNCH_DOWN : LAUNCH_UP) : (axis>0 ? LAUNCH_RIGHT : LAUNCH_LEFT);
                *old=axis;
            }
            if(key) action=launcher_key(m,key);
            if(action==4) { snprintf(m->message,sizeof(m->message),"%s",settings_save(&m->settings,path) ? "SETTINGS SAVED" : "SAVE FAILED - CHECK DISK"); action=0; }
            if(action) break;
        }
        render_launcher(m,&f);
        for(i=0;i<320*200;i++) rgb[i]=0xff000000U|f.palette[f.pixels[i]];
        SDL_UpdateTexture(t,NULL,rgb,320*4); SDL_RenderCopy(r,t,NULL,NULL); SDL_RenderPresent(r); SDL_Delay(10);
    }
done:
    if(controller) SDL_GameControllerClose(controller); else if(joy) SDL_JoystickClose(joy);
    free(f.pixels); render_shutdown();
    if(t) SDL_DestroyTexture(t);
    if(r) SDL_DestroyRenderer(r);
    if(w) SDL_DestroyWindow(w);
    SDL_Quit(); return action;
}
int main(int argc, char **argv) {
    Launcher menu; const char *path="astralbound.cfg"; int i,skip_menu=0,preview=0,action,result;
    for(i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--config") && i+1<argc) path=argv[++i];
        if(!strcmp(argv[i],"--render") || !strcmp(argv[i],"--render-intro") || !strcmp(argv[i],"--render-menu")) preview=1;
        if(!strcmp(argv[i],"--nomenu")) skip_menu=1;
        if(!strcmp(argv[i],"--benchmark")) { testing=1; skip_menu=1; }
    }
    settings_defaults(&preferences,0);
    if(!preview) settings_load(&preferences,path);
    for(i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--ecs")) preferences.mode=VIDEO_ECS;
        if(!strcmp(argv[i],"--aga")) preferences.mode=VIDEO_AGA;
        if(!strcmp(argv[i],"--rtg")) preferences.mode=VIDEO_RTG;
        if(!strcmp(argv[i],"--rtg16")) preferences.mode=VIDEO_RTG16;
        if(!strcmp(argv[i],"--lowres") || !strcmp(argv[i],"--ntsc")) preferences.compact=1;
        if(!strcmp(argv[i],"--nosound")) preferences.sound=0;
        if(!strcmp(argv[i],"--nointro")) preferences.intro=0;
        if(!strcmp(argv[i],"--classic")) preferences.renderer=RENDERER_CLASSIC;
        if(!strcmp(argv[i],"--enhanced")) preferences.renderer=RENDERER_ENHANCED;
        if(!strcmp(argv[i],"--no-bezier")) preferences.bezier=0;
        if(!strcmp(argv[i],"--preset") && i+1<argc) {
            int k; i++; for(k=0;k<5;k++) if(!strcasecmp(argv[i],quality_names[k])) settings_preset(&preferences,k);
        }
    }
    if(preview && argc>2 && !strcmp(argv[1],"--render-menu")) {
        Frame f={320,200,0,NULL,{0}};
        launcher_init(&menu,&preferences,1,1);
        if(argc>3 && !strcmp(argv[3],"--custom")) { settings_preset(&menu.settings,QUALITY_CUSTOM); menu.page=1; }
        f.pixels=malloc(320*200); if(!f.pixels || !render_init(&f)) return 1;
        render_launcher(&menu,&f); ppm(&f,argv[2]); free(f.pixels); render_shutdown(); return 0;
    }
    if(preview || skip_menu) return host_run(argc,argv);
    launcher_init(&menu,&preferences,1,1);
    for(;;) {
        action=host_launcher(&menu,path);
        if(!settings_save(&menu.settings,path)) fprintf(stderr,"Unable to save graphics settings to %s\n",path);
        if(action==LAUNCH_QUIT) return 0;
        preferences=menu.settings; testing=action==LAUNCH_TEST; test_scene=menu.test_scene;
        result=host_run(argc,argv);
        if(result) { snprintf(menu.message,sizeof(menu.message),"DISPLAY FAILED - LOWER OPTIONS OR CHANGE VIDEO"); menu.page=0; menu.selected=1; continue; }
        if(!testing) return result;
        launcher_results(&menu,&performance.report);
    }
}
