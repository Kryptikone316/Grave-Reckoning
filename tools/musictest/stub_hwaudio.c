#include "hwaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* Stand-in for the device audio. It consumes frames in real time like the speaker would,
 * so the queue length (the cushion) can be watched, and reports the loudest sample per
 * quarter second plus how many frames were waiting. */
static long written = 0, window_frames = 0;
static int window_peak = 0;
static struct timespec t0;
static long consumed(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(((now.tv_sec - t0.tv_sec) * 1000000000LL + (now.tv_nsec - t0.tv_nsec)) * HWAUDIO_RATE / 1000000000LL);
}
int hwaudio_init(void) { clock_gettime(CLOCK_MONOTONIC, &t0); return 0; }
int hwaudio_active(void) { return 1; }
void hwaudio_deinit(void) {}
void hwaudio_get_stats(unsigned *underruns, unsigned *dropped, unsigned *fill) {
    long queued = written - consumed();
    if (underruns) *underruns = queued < 0 ? 1 : 0;
    if (dropped) *dropped = 0;
    if (fill) *fill = queued < 0 ? 0 : (unsigned)queued;
}
int hwaudio_write(const short *samples, int frames) {
    for (int i = 0; i < frames * 2; ++i) { int v = abs(samples[i]); if (v > window_peak) window_peak = v; }
    written += frames; window_frames += frames;
    if (window_frames >= HWAUDIO_RATE / 4) {
        fprintf(stderr, "  t=%5.2fs peak=%5d queued=%5ld frames (%3ld ms)\n", (double)consumed() / HWAUDIO_RATE, window_peak,
                written - consumed(), (written - consumed()) * 1000 / HWAUDIO_RATE);
        window_frames = 0; window_peak = 0;
    }
    return frames;
}
