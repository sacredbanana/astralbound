/* SPDX-License-Identifier: Unlicense. Allocate Paula through audio.device
   before takeover. Never call the device while interrupts are disabled. */
#include "audio.h"
#include <devices/audio.h>
static SpaceAudio sound;
static struct MsgPort *audio_port;
static struct IOAudio *audio_requests[4];
static int audio_opened, audio_pending[4], audio_muted;
static BYTE *music_pcm, *engine_pcm, *effect_pcm[SOUND_COUNT], *stream_pcm[2];
static ULONG effect_until;
static UWORD audio_period;
static void audio_native_stop(void) {
    int ch;
    if (!audio_opened) return;
    hw->dmacon = 15;
    for (ch = 0; ch < 4; ch++) hw->aud[ch].ac_vol = 0;
    hw->intreq = 0x0780;
}
static void audio_channel(int ch, BYTE *pcm, ULONG size, int volume) {
    hw->dmacon = (UWORD)(1 << ch);
    hw->aud[ch].ac_ptr = (UWORD *)pcm;
    hw->aud[ch].ac_len = (UWORD)(size / 2);
    hw->aud[ch].ac_per = audio_period;
    hw->aud[ch].ac_vol = (UWORD)volume;
    hw->dmacon = (UWORD)(0x8200 | (1 << ch));
}
static void audio_native_start(void) {
    if (!audio_opened) return;
    hw->adkcon = 0x00ff; /* clear modulation on the four channels we own */
    audio_channel(0, music_pcm, MUSIC_SAMPLES, 24);
    audio_channel(1, music_pcm, MUSIC_SAMPLES, 24);
    audio_channel(3, engine_pcm, 256, 0);
    effect_until = 0;
}
static void audio_submit(int buffer) {
    int channel;
    audio_mix(&sound, (int8_t *)stream_pcm[buffer], 2048);
    for (channel = 0; channel < 2; channel++) {
        int n = buffer * 2 + channel;
        struct IOAudio *r = audio_requests[n];
        r->ioa_Request.io_Command = CMD_WRITE;
        r->ioa_Request.io_Flags = ADIOF_PERVOL;
        r->ioa_Request.io_Unit = (struct Unit *)(1UL << channel);
        r->ioa_Data = (UBYTE *)stream_pcm[buffer];
        r->ioa_Length = 2048;
        r->ioa_Period = audio_period;
        r->ioa_Volume = 48;
        r->ioa_Cycles = 1;
        SendIO((struct IORequest *)r);
        audio_pending[n] = 1;
    }
}
static void sound_close(void) {
    int n;
    for (n = 0; n < 4; n++) {
        if (audio_pending[n]) {
            AbortIO((struct IORequest *)audio_requests[n]);
            WaitIO((struct IORequest *)audio_requests[n]);
            audio_pending[n] = 0;
        }
    }
    if (audio_opened) CloseDevice((struct IORequest *)audio_requests[0]);
    audio_opened = 0;
    for (n = 0; n < 4; n++) {
        if (audio_requests[n]) DeleteIORequest((struct IORequest *)audio_requests[n]);
        audio_requests[n] = NULL;
    }
    for (n = 0; n < 2; n++) {
        if (stream_pcm[n]) FreeMem(stream_pcm[n], 2048);
        stream_pcm[n] = NULL;
    }
    if (audio_port) DeleteMsgPort(audio_port);
    audio_port = NULL;
    if (music_pcm) FreeMem(music_pcm, MUSIC_SAMPLES);
    if (engine_pcm) FreeMem(engine_pcm, 256);
    music_pcm = engine_pcm = NULL;
    for (n = 1; n < SOUND_COUNT; n++) {
        if (effect_pcm[n]) FreeMem(effect_pcm[n], EFFECT_SAMPLES);
        effect_pcm[n] = NULL;
    }
}
static int sound_open(void) {
    static UBYTE channels[] = {15};
    int n, t;
    audio_init(&sound, &g);
    if (audio_muted) return 1;
    audio_period = (UWORD)(((GfxBase->DisplayFlags & PAL) ? 3546895UL : 3579545UL) / AUDIO_RATE);
    audio_port = CreateMsgPort();
    if (!audio_port) return 0;
    for (n = 0; n < 4; n++) {
        audio_requests[n] = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
        if (!audio_requests[n]) return 0;
    }
    channels[0] = rtg ? 3 : 15;
    audio_requests[0]->ioa_Request.io_Message.mn_Node.ln_Pri = -128;
    audio_requests[0]->ioa_Data = channels;
    audio_requests[0]->ioa_Length = sizeof(channels);
    if (OpenDevice((CONST_STRPTR)"audio.device", 0,
                   (struct IORequest *)audio_requests[0], 0)) return 0;
    audio_opened = 1;
    for (n = 1; n < 4; n++) {
        audio_requests[n]->ioa_Request.io_Device = audio_requests[0]->ioa_Request.io_Device;
        audio_requests[n]->ioa_AllocKey = audio_requests[0]->ioa_AllocKey;
    }
    if (rtg) {
        for (n = 0; n < 2; n++) {
            stream_pcm[n] = AllocMem(2048, MEMF_CHIP);
            if (!stream_pcm[n]) return 0;
        }
        audio_submit(0); audio_submit(1);
    } else {
        music_pcm = AllocMem(MUSIC_SAMPLES, MEMF_CHIP);
        engine_pcm = AllocMem(256, MEMF_CHIP);
        if (!music_pcm || !engine_pcm) return 0;
        for (t = 0; t < MUSIC_SAMPLES; t++) music_pcm[t] = audio_music((unsigned)t);
        for (t = 0; t < 256; t++) engine_pcm[t] = audio_engine((unsigned)t);
        for (n = 1; n < SOUND_COUNT; n++) {
            effect_pcm[n] = AllocMem(EFFECT_SAMPLES, MEMF_CHIP);
            if (!effect_pcm[n]) return 0;
            for (t = 0; t < EFFECT_SAMPLES; t++) effect_pcm[n][t] = audio_effect(n, (unsigned)t);
        }
    }
    return 1;
}
static void sound_pump(void) {
    if (audio_opened && !rtg && effect_until &&
        ((ULONG)clock_now() - effect_until) % 0x1000000UL < 0x800000UL) {
        hw->dmacon = 4; hw->aud[2].ac_vol = 0; effect_until = 0;
    }
}
static void sound_update(void) {
    int n, effect = audio_observe(&sound, &g);
    if (!audio_opened) return;
    if (rtg) {
        /* Service in submission order to preserve PCM time when both finish. */
        static int oldest;
        for (n = 0; n < 2; n++) {
            int b = oldest;
            if (!CheckIO((struct IORequest *)audio_requests[b * 2]) ||
                !CheckIO((struct IORequest *)audio_requests[b * 2 + 1])) break;
            WaitIO((struct IORequest *)audio_requests[b * 2]);
            WaitIO((struct IORequest *)audio_requests[b * 2 + 1]);
            audio_pending[b * 2] = audio_pending[b * 2 + 1] = 0;
            if (audio_requests[b * 2]->ioa_Request.io_Error ||
                audio_requests[b * 2 + 1]->ioa_Request.io_Error) {
                sound_close(); return; /* a stolen channel is no longer ours */
            }
            audio_submit(b);
            oldest = 1 - oldest;
        }
    } else {
        hw->aud[0].ac_vol = hw->aud[1].ac_vol = sound.paused ? 0 : 24;
        hw->aud[3].ac_vol = sound.active && !sound.paused ? sound.throttle * 18 / 4000 : 0;
        if (effect && !sound.paused) {
            unsigned size = effect == SOUND_LASER ? 1200 : EFFECT_SAMPLES;
            audio_channel(2, effect_pcm[effect], size, 48);
            effect_until = ((ULONG)clock_now() + (size * clock_rate + AUDIO_RATE - 1) / AUDIO_RATE) & 0xffffffUL;
            if (!effect_until) effect_until = 1;
        }
        if (sound.paused) { hw->dmacon = 4; effect_until = 0; }
        sound_pump();
    }
}
