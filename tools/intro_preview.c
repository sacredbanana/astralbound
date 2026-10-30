/* SPDX-License-Identifier: Unlicense
   Renders the title sequence without a window: one frame, a contact sheet,
   or raw RGB24 frames on stdout plus an 8 kHz WAV (effects and music). The
   sequence is deterministic, so write the WAV first, then the pictures:
     intro_preview --frame 92.5 out.ppm
     intro_preview --sheet 3 sheet.ppm
     intro_preview --stream 25 sound.wav > /dev/null
     intro_preview --stream 25 | ffmpeg -f rawvideo -pixel_format rgb24
         -video_size 320x256 -framerate 25 -i - -i sound.wav intro.mp4
   --ecs, --ntsc and --rtg select the display layout.
   --classic or --enhanced selects the renderer; --preset selects detail.
   --tour renders the Solar System tour instead of the title sequence. */
#include "astralbound.h"
#include "settings.h"
#include "intro.h"
#include "tour.h"
#include "audio.h"
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static Game g;
static Intro intro;
static Tour tour;
static int touring;
/* The title sequence or the tour, behind one interface. */
static void start(void) {
    if (touring) tour_start(&tour, &g, 0x53504143); else intro_start(&intro, &g, 0x53504143);
}
static int running(void) { return touring ? tour.running : intro.running; }
static int position(void) { return touring ? tour.tick : intro.tick; }
static int total(void) { return touring ? tour.length : INTRO_TICKS; }
static void advance(int ticks) { if (touring) tour_frame(&tour, &g, ticks); else intro_frame(&intro, &g, ticks); }
static void draw(Frame *f) { if (touring) render_tour(&tour, &g, f); else render_intro(&intro, &g, f); }
static void put_ppm(FILE *o, const Frame *f) {
    int i;
    fprintf(o, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc((int)(c >> 16 & 255), o); fputc((int)(c >> 8 & 255), o); fputc((int)(c & 255), o);
    }
}
static void put_rgb(FILE *o, const Frame *f) {
    int i;
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc((int)(c >> 16 & 255), o); fputc((int)(c >> 8 & 255), o); fputc((int)(c & 255), o);
    }
}
static void put32(FILE *o, uint32_t v) { fputc((int)(v & 255), o); fputc((int)(v >> 8 & 255), o); fputc((int)(v >> 16 & 255), o); fputc((int)(v >> 24), o); }
/* Advance to the tick without rendering. */
static void seek(int tick) {
    if (!running() || position() > tick) start();
    if (tick > position()) advance(tick - position());
}
int main(int argc, char **argv) {
    Frame f = {320, 256, 1, NULL, {0}};
    Settings settings;
    int i = 1, preset = -1, renderer_override = -1, bezier_override = -1;
    settings_defaults(&settings, 0);
    for (; i < argc && !strncmp(argv[i], "--", 2); i++) {
        if (!strcmp(argv[i], "--ecs")) f.enhanced = 0;
        else if (!strcmp(argv[i], "--ntsc")) f.h = 200;
        else if (!strcmp(argv[i], "--rtg")) { f.w = 640; f.h = 480; }
        else if (!strcmp(argv[i], "--classic")) renderer_override = RENDERER_CLASSIC;
        else if (!strcmp(argv[i], "--enhanced")) renderer_override = RENDERER_ENHANCED;
        else if (!strcmp(argv[i], "--no-bezier")) bezier_override = 0;
        else if (!strcmp(argv[i], "--tour")) touring = 1;
        else if (!strcmp(argv[i], "--preset") && i + 1 < argc) {
            int k;
            i++;
            for (k = 0; k < QUALITY_CUSTOM; k++) if (!strcasecmp(argv[i], quality_names[k])) break;
            if (k == QUALITY_CUSTOM) { fputs("Unknown preset (use LOW, MEDIUM, HIGH or ULTRA)\n", stderr); return 1; }
            preset = k;
        }
        else break;
    }
    if (preset >= 0) settings_preset(&settings, preset);
    if (renderer_override >= 0) settings.renderer = renderer_override;
    if (bezier_override >= 0) settings.bezier = bezier_override;
    render_graphics(&settings.gfx);
    render_renderer(settings.renderer);
    render_bezier(settings.bezier);
    f.pixels = malloc((size_t)f.w * f.h);
    if (!f.pixels || !render_init(&f) || i + 1 >= argc ||
        (strcmp(argv[i], "--stream") && i + 2 >= argc)) {
        fputs("intro_preview [--ecs|--ntsc|--rtg] [--classic|--enhanced] [--no-bezier] [--preset NAME] [--tour]"
              " --frame SECONDS OUT.ppm | --sheet STEP OUT.ppm |"
              " --stream FPS [OUT.wav]\n", stderr);
        return 1;
    }
    if (!strcmp(argv[i], "--frame")) {
        FILE *o = fopen(argv[i + 2], "wb");
        seek((int)(atof(argv[i + 1]) * HZ));
        if (!o) return 1;
        draw(&f); put_ppm(o, &f); fclose(o);
    } else if (!strcmp(argv[i], "--sheet")) {
        /* Half-size thumbnails, ten to a row. */
        int step = (int)(atof(argv[i + 1]) * HZ), count, n, x, y;
        int tw = f.w / 2, th = f.h / 2, cols = 10, rows;
        uint32_t *sheet;
        start();
        count = (total() + step - 1) / step; rows = (count + cols - 1) / cols;
        sheet = calloc((size_t)tw * cols * th * rows, sizeof(uint32_t));
        FILE *o = fopen(argv[i + 2], "wb");
        if (!sheet || !o) return 1;
        for (n = 0; n < count; n++) {
            seek(n * step);
            draw(&f);
            for (y = 0; y < th; y++) for (x = 0; x < tw; x++)
                sheet[(size_t)(n / cols * th + y) * tw * cols + n % cols * tw + x] = f.palette[f.pixels[y * 2 * f.w + x * 2]];
        }
        fprintf(o, "P6\n%d %d\n255\n", tw * cols, th * rows);
        for (n = 0; n < tw * cols * th * rows; n++) {
            fputc((int)(sheet[n] >> 16 & 255), o); fputc((int)(sheet[n] >> 8 & 255), o); fputc((int)(sheet[n] & 255), o);
        }
        fclose(o); free(sheet);
    } else if (!strcmp(argv[i], "--stream")) {
        int fps = atoi(argv[i + 1]), frame_count, n, done = 0;
        FILE *wav = i + 2 < argc ? fopen(argv[i + 2], "wb") : NULL;
        SpaceAudio sound;
        int8_t pcm[AUDIO_RATE / HZ];
        if (fps <= 0) return 1;
        start();
        frame_count = total() * fps / HZ;
        if (wav) {
            uint32_t bytes = (uint32_t)total() * (AUDIO_RATE / HZ);
            music_load("music", 1);
            fputs("RIFF", wav); put32(wav, 36 + bytes); fputs("WAVEfmt ", wav); put32(wav, 16);
            put32(wav, 1 | 1 << 16); put32(wav, AUDIO_RATE); put32(wav, AUDIO_RATE); put32(wav, 1 | 8 << 16);
            fputs("data", wav); put32(wav, bytes);
        }
        audio_init(&sound, &g);
        for (n = 0; n < frame_count; n++) {
            int target = n * HZ / fps;
            while (done < target) {
                if (running()) advance(1);
                done++;
                audio_observe(&sound, &g);
                audio_mix(&sound, pcm, sizeof(pcm));
                if (wav) { int k; for (k = 0; k < (int)sizeof(pcm); k++) fputc(pcm[k] + 128, wav); }
            }
            if (!running()) break;
            draw(&f);
            put_rgb(stdout, &f);
        }
        if (wav) fclose(wav);
        music_close();
    }
    free(f.pixels);
    render_shutdown();
    return 0;
}
