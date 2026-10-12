/* SPDX-License-Identifier: Unlicense; SDL is used only by the host harness. */
#include "space.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Game g;
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
int main(int argc, char **argv) {
    Frame f = {320, 256, 1, 0, {0}};
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *rgb;
    Uint64 last, now, freq;
    uint64_t accumulator = 0;
    int quit = 0, i;
    game_init(&g, 0x53504143);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rtg")) {
            f.w = 640;
            f.h = 480;
        }
        if (!strcmp(argv[i], "--city-overhead")) {
            const Port *port = &g.ports[0];
            g.docked = -1;
            g.pos = add(port->pos, scale(port->normal, 1232LL * FP, 16384));
            aim_angles(sub(port->pos, g.pos), &g.yaw, &g.pitch);
        }
        if (!strcmp(argv[i], "--ecs"))
            f.enhanced = 0;
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
    f.pixels = malloc((size_t)f.w * f.h);
    rgb = malloc((size_t)f.w * f.h * 4);
    if (!f.pixels || !rgb)
        return 1;
    if (!render_init(&f))
        return 1;
    if (argc > 2 && !strcmp(argv[1], "--render")) {
        render_game(&g, &f);
        ppm(&f, argv[2]);
        free(rgb);
        free(f.pixels);
        render_shutdown();
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("SPACE - Amiga development preview", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 960, 768, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer)
        return 1;
    SDL_RenderSetLogicalSize(renderer, f.w, f.h);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                f.w, f.h);
    if (!texture)
        return 1;
    freq = SDL_GetPerformanceFrequency();
    last = SDL_GetPerformanceCounter();
    while (!quit) {
        SDL_Event e;
        Controls c = {0};
        const Uint8 *keys;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                quit = 1;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                int k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE)
                    quit = 1;
                else if (k == SDLK_F5)
                    snprintf(g.message, sizeof(g.message), "%s",
                             save_game(&g, "space.sav") ? "SAVED" : "SAVE FAILED");
                else if (k == SDLK_F9) {
                    if (!load_game(&g, "space.sav"))
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
        keys = SDL_GetKeyboardState(0);
        if (g.page == FLIGHT) {
            c.pitch = keys[SDL_SCANCODE_UP] - keys[SDL_SCANCODE_DOWN];
            c.yaw = keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT];
            c.thrust = keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S];
            c.fire = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
        }
        now = SDL_GetPerformanceCounter();
        accumulator += (now - last) * HZ;
        last = now;
        if (accumulator > freq * 8)
            accumulator = freq * 8;
        while (accumulator >= freq) {
            game_frame(&g, c, 1);
            accumulator -= freq;
        }
        render_game(&g, &f);
        for (i = 0; i < f.w * f.h; i++)
            rgb[i] = 0xff000000U | f.palette[f.pixels[i]];
        SDL_UpdateTexture(texture, 0, rgb, f.w * 4);
        SDL_RenderCopy(renderer, texture, 0, 0);
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(rgb);
    free(f.pixels);
    render_shutdown();
    return 0;
}
