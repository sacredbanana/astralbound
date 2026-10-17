/* SPDX-License-Identifier: Unlicense. Allocate Paula through audio.device
   before takeover. Never call the device while interrupts are disabled. */
#include "audio.h"
#include "music.h"
#include <devices/audio.h>
#define STREAM_SAMPLES 2048
static SpaceAudio sound;
static struct MsgPort *audio_port;
static struct IOAudio *audio_requests[4];
static int audio_opened, audio_pending[4], audio_muted;
static BYTE *engine_pcm, *effect_pcm[SOUND_COUNT], *stream_pcm[2];
static int stream_next, stream_oldest;
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
    int line;
    if (!audio_opened) return;
    hw->adkcon = 0x00ff; /* clear modulation on the four channels we own */
    audio_channel(0, stream_pcm[0], STREAM_SAMPLES, 48);
    audio_channel(1, stream_pcm[0], STREAM_SAMPLES, 48);
    /* Let Paula latch the initial DMA block (and its startup interrupt), then
       queue the next block. Interrupts stay disabled; sound_pump polls completion. */
    for (line = 0; line < 16; line++) {
        UWORD beam = hw->vhposr;
        while ((hw->vhposr & 0xff00) == (beam & 0xff00)) {}
    }
    hw->aud[0].ac_ptr = hw->aud[1].ac_ptr = (UWORD *)stream_pcm[1];
    hw->aud[0].ac_len = hw->aud[1].ac_len = STREAM_SAMPLES / 2;
    hw->intreq = 0x0180;
    stream_next = 0;
    audio_channel(3, engine_pcm, 256, 0);
    effect_until = 0;
}
static void audio_submit(int buffer) {
    int channel;
    audio_mix(&sound, (int8_t *)stream_pcm[buffer], STREAM_SAMPLES);
    for (channel = 0; channel < 2; channel++) {
        int n = buffer * 2 + channel;
        struct IOAudio *r = audio_requests[n];
        r->ioa_Request.io_Command = CMD_WRITE;
        r->ioa_Request.io_Flags = ADIOF_PERVOL;
        r->ioa_Request.io_Error = 0;
        r->ioa_Request.io_Unit = (struct Unit *)(1UL << channel);
        r->ioa_Data = (UBYTE *)stream_pcm[buffer];
        r->ioa_Length = STREAM_SAMPLES;
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
        if (stream_pcm[n]) FreeMem(stream_pcm[n], STREAM_SAMPLES);
        stream_pcm[n] = NULL;
    }
    if (audio_port) DeleteMsgPort(audio_port);
    audio_port = NULL;
    if (engine_pcm) FreeMem(engine_pcm, 256);
    engine_pcm = NULL;
    for (n = 1; n < SOUND_COUNT; n++) {
        if (effect_pcm[n]) FreeMem(effect_pcm[n], audio_effect_length(n));
        effect_pcm[n] = NULL;
    }
}
static int sound_open(void) {
    static UBYTE channels[] = {15};
    int n, t;
    audio_init(&sound, &g);
    if (audio_muted) return 1;
    audio_observe(&sound, &g);
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
    for (n = 0; n < 2; n++) {
        stream_pcm[n] = AllocMem(STREAM_SAMPLES, MEMF_CHIP);
        if (!stream_pcm[n]) return 0;
    }
    stream_oldest = 0;
    if (rtg) {
        audio_submit(0); audio_submit(1);
    } else {
        engine_pcm = AllocMem(256, MEMF_CHIP);
        if (!engine_pcm) return 0;
        music_mix((int8_t *)stream_pcm[0], STREAM_SAMPLES);
        music_mix((int8_t *)stream_pcm[1], STREAM_SAMPLES);
        for (t = 0; t < 256; t++) engine_pcm[t] = audio_engine((unsigned)t);
        for (n = 1; n < SOUND_COUNT; n++) {
            effect_pcm[n] = AllocMem(audio_effect_length(n), MEMF_CHIP);
            if (!effect_pcm[n]) return 0;
            for (t = 0; t < (int)audio_effect_length(n); t++) effect_pcm[n][t] = audio_effect(n, (unsigned)t);
        }
    }
    return 1;
}
static void sound_pump(void) {
    if (audio_opened && !rtg && (hw->intreqr & 0x0180) == 0x0180) {
        /* The queued block is now playing. Refill the consumed block and set
           its reload pointer for the following boundary. No DOS/device calls.
           Slow frames may repeat a block, but never free or overwrite DMA's
           active block. Clear after queuing to discard repeats during mixing. */
        music_mix((int8_t *)stream_pcm[stream_next], STREAM_SAMPLES);
        hw->aud[0].ac_ptr = hw->aud[1].ac_ptr = (UWORD *)stream_pcm[stream_next];
        hw->aud[0].ac_len = hw->aud[1].ac_len = STREAM_SAMPLES / 2;
        hw->intreq = 0x0180;
        stream_next = 1 - stream_next;
    }
    if (audio_opened && !rtg && effect_until &&
        ((ULONG)clock_now() - effect_until) % 0x1000000UL < 0x800000UL) {
        hw->dmacon = 4; hw->aud[2].ac_vol = 0; effect_until = 0;
    }
}
static void sound_update(void) {
    int n, changed=testing && performance.audio_scene!=performance.scene;
    int effect=testing ? performance_audio(&performance,&sound,&g) : audio_observe(&sound,&g);
    if (!audio_opened) return;
    if(rtg && changed) {
        /* Driver buffers can contain half a second of the preceding effect.
           Flush them under the running OS before submitting the new scene. */
        for(n=0;n<4;n++) if(audio_pending[n]) {
            AbortIO((struct IORequest *)audio_requests[n]);
            WaitIO((struct IORequest *)audio_requests[n]);
            audio_pending[n]=0;
        }
        stream_oldest=0;
        audio_submit(0); audio_submit(1);
        return;
    }
    if (rtg) {
        /* Service in submission order to preserve PCM time when both finish. */
        for (n = 0; n < 2; n++) {
            int b = stream_oldest;
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
            stream_oldest = 1 - stream_oldest;
        }
    } else {
        hw->aud[0].ac_vol = hw->aud[1].ac_vol = sound.paused ? 0 : 48;
        hw->aud[3].ac_vol = sound.active && !sound.paused ? sound.throttle * 18 / 4000 : 0;
        if (effect && !sound.paused) {
            unsigned size = audio_effect_length(effect);
            audio_channel(2, effect_pcm[effect], size, 48);
            effect_until = ((ULONG)clock_now() + (size * clock_rate + AUDIO_RATE - 1) / AUDIO_RATE) & 0xffffffUL;
            if (!effect_until) effect_until = 1;
        }
        if (sound.paused || (!sound.effect && effect_until)) {
            hw->dmacon = 4; hw->aud[2].ac_vol = 0; effect_until = 0;
        }
        sound_pump();
    }
}
