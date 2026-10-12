/* SPDX-License-Identifier: Unlicense
   Native low-resolution display layout; never changes the OS video standard. */
#ifndef SPACE_NATIVE_VIDEO_H
#define SPACE_NATIVE_VIDEO_H
typedef struct {
    int height, start, stop, rate;
} NativeVideo;
static inline NativeVideo native_video(int pal, int lowres) {
    NativeVideo v;
    v.height = pal && !lowres ? 256 : 200;
    v.start = 44 + (pal ? (256 - v.height) / 2 : 0);
    v.stop = v.start + v.height;
    v.rate = pal ? 50 : 60;
    return v;
}
#endif
