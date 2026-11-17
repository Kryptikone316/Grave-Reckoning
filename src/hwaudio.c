#include "hwaudio.h"
#include "sfxmix.h"

#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RING_FRAMES HWAUDIO_RING_FRAMES
#define RING_MASK (RING_FRAMES - 1)

typedef int (*sound_init_fn)(void *, int, int);
typedef int (*sound_play_fn)(const void *, int);
typedef int (*sound_deinit_fn)(void);

static void *driver_handle;
static sound_init_fn sound_init;
static sound_play_fn sound_play;
static sound_deinit_fn sound_deinit;
static int audio_active;
static volatile int audio_thread_running;
static pthread_t audio_thread;
static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static short ring[RING_FRAMES * 2];
static unsigned ring_write;
static unsigned ring_read;
static volatile unsigned underrun_count;
static volatile unsigned dropped_frames;
static short last_left, last_right;
static int was_underrun;

/* Feeds the driver one 10 ms chunk at a time, on a fixed schedule measured on
 * the monotonic clock (so lateness never accumulates). When the ring runs dry
 * the last sample is faded out instead of cutting to zero, and the music fades
 * back in when it returns: a hard step is a click, and clicks are what made
 * the soundtrack sound scratchy. */
static void *audio_thread_main(void *unused) {
    (void)unused;
    const long period_ns = (long)((long long)HWAUDIO_CHUNK * 1000000000LL / HWAUDIO_RATE);
    struct timespec next_wake;
    short chunk[HWAUDIO_CHUNK * 2];
    clock_gettime(CLOCK_MONOTONIC, &next_wake);

    while (audio_thread_running) {
        next_wake.tv_nsec += period_ns;
        if (next_wake.tv_nsec >= 1000000000L) {
            next_wake.tv_nsec -= 1000000000L;
            next_wake.tv_sec += 1;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);

        pthread_mutex_lock(&ring_mutex);
        if (ring_write - ring_read >= HWAUDIO_CHUNK) {
            for (int i = 0; i < HWAUDIO_CHUNK; ++i) {
                const unsigned index = (ring_read + (unsigned)i) & RING_MASK;
                chunk[i * 2] = ring[index * 2];
                chunk[i * 2 + 1] = ring[index * 2 + 1];
            }
            ring_read += HWAUDIO_CHUNK;
            if (was_underrun) {                       /* coming back from a gap: fade in */
                for (int i = 0; i < HWAUDIO_CHUNK; ++i) {
                    const int scale = (i * 256) / HWAUDIO_CHUNK;
                    chunk[i * 2] = (short)((chunk[i * 2] * scale) >> 8);
                    chunk[i * 2 + 1] = (short)((chunk[i * 2 + 1] * scale) >> 8);
                }
                was_underrun = 0;
            }
            last_left = chunk[(HWAUDIO_CHUNK - 1) * 2];
            last_right = chunk[(HWAUDIO_CHUNK - 1) * 2 + 1];
        } else {                                      /* nothing to play: fade the last sample out */
            for (int i = 0; i < HWAUDIO_CHUNK; ++i) {
                const int scale = 256 - (i * 256) / HWAUDIO_CHUNK;
                chunk[i * 2] = (short)((last_left * scale) >> 8);
                chunk[i * 2 + 1] = (short)((last_right * scale) >> 8);
            }
            was_underrun = 1;
            last_left = last_right = 0;
            ++underrun_count;
        }
        pthread_mutex_unlock(&ring_mutex);
        sfxmix_into(chunk, HWAUDIO_CHUNK);        /* sound effects ride on top of the music */
        if (sound_play) sound_play(chunk, HWAUDIO_CHUNK);
    }
    return NULL;
}

static int try_sound_init(void) {
    int result = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
        result = sound_init(NULL, HWAUDIO_RATE, 2);
        if (result >= 0) return result;
        usleep(100000);
    }
    return result;
}

int hwaudio_init(void) {
    if (audio_active) return 0;
    driver_handle = dlopen("/mnt/sdcard/cubegm/driver_sf3500.so", RTLD_NOW | RTLD_GLOBAL);
    if (!driver_handle) {
        fprintf(stderr, "hwaudio: dlopen failed: %s\n", dlerror());
        return -1;
    }
    sound_init = (sound_init_fn)dlsym(driver_handle, "sound_driver_init");
    sound_play = (sound_play_fn)dlsym(driver_handle, "sound_driver_playframe");
    sound_deinit = (sound_deinit_fn)dlsym(driver_handle, "sound_driver_deinit");
    if (!sound_init || !sound_play) {
        fprintf(stderr, "hwaudio: sound symbols unavailable\n");
        dlclose(driver_handle);
        driver_handle = NULL;
        return -1;
    }
    if (try_sound_init() < 0) {
        fprintf(stderr, "hwaudio: sound_driver_init failed\n");
        dlclose(driver_handle);
        driver_handle = NULL;
        return -1;
    }
    if (sound_deinit) sound_deinit();
    usleep(100000);
    if (try_sound_init() < 0) {
        fprintf(stderr, "hwaudio: second sound_driver_init failed\n");
        dlclose(driver_handle);
        driver_handle = NULL;
        return -1;
    }

    ring_write = ring_read = 0;
    underrun_count = dropped_frames = 0;
    last_left = last_right = 0;
    was_underrun = 0;
    audio_thread_running = 1;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attributes, SCHED_FIFO);
    struct sched_param priority;
    priority.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
    pthread_attr_setschedparam(&attributes, &priority);
    int created = pthread_create(&audio_thread, &attributes, audio_thread_main, NULL);
    pthread_attr_destroy(&attributes);
    if (created != 0) created = pthread_create(&audio_thread, NULL, audio_thread_main, NULL);
    if (created != 0) {
        fprintf(stderr, "hwaudio: audio thread creation failed (%d)\n", created);
        audio_thread_running = 0;
        if (sound_deinit) sound_deinit();
        dlclose(driver_handle);
        driver_handle = NULL;
        return -1;
    }
    audio_active = 1;
    fprintf(stderr, "hwaudio: active at %d Hz\n", HWAUDIO_RATE);
    return 0;
}

int hwaudio_active(void) { return audio_active; }

int hwaudio_write(const short *samples, int frames) {
    if (!audio_active || !samples || frames <= 0) return 0;
    int accepted = 0;
    pthread_mutex_lock(&ring_mutex);
    for (; accepted < frames; ++accepted) {
        if (ring_write - ring_read >= HWAUDIO_RING_SOFT_CAP) break;   /* plenty queued already */
        const unsigned index = ring_write & RING_MASK;
        ring[index * 2] = samples[accepted * 2];
        ring[index * 2 + 1] = samples[accepted * 2 + 1];
        ++ring_write;
    }
    if (accepted < frames) dropped_frames += (unsigned)(frames - accepted);
    pthread_mutex_unlock(&ring_mutex);
    return accepted;
}

void hwaudio_get_stats(unsigned *underruns, unsigned *dropped, unsigned *fill) {
    pthread_mutex_lock(&ring_mutex);
    if (underruns) *underruns = underrun_count;
    if (dropped) *dropped = dropped_frames;
    if (fill) *fill = ring_write - ring_read;
    pthread_mutex_unlock(&ring_mutex);
}

void hwaudio_deinit(void) {
    if (!audio_active) return;
    fprintf(stderr, "hwaudio: closing: %u underrun(s), %u frame(s) not accepted\n", underrun_count, dropped_frames);
    audio_thread_running = 0;
    pthread_join(audio_thread, NULL);
    if (sound_deinit) sound_deinit();
    if (driver_handle) dlclose(driver_handle);
    driver_handle = NULL;
    sound_init = NULL;
    sound_play = NULL;
    sound_deinit = NULL;
    audio_active = 0;
}
