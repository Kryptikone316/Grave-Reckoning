#include "sfxmix.h"

#include <pthread.h>
#include <stddef.h>

struct voice {
    const short *data;
    int length;
    int position;
    int gain;
    unsigned started;     /* for choosing which voice to cut */
};

static struct voice voices[SFXMIX_VOICES];
static unsigned serial;
static pthread_mutex_t mix_mutex = PTHREAD_MUTEX_INITIALIZER;

void sfxmix_play(const short *mono, int frames, int gain_q8) {
    if (!mono || frames <= 0 || gain_q8 <= 0) return;
    pthread_mutex_lock(&mix_mutex);
    int slot = -1;
    for (int i = 0; i < SFXMIX_VOICES; ++i)
        if (!voices[i].data) { slot = i; break; }
    if (slot < 0) {                                   /* all busy: take the oldest */
        slot = 0;
        for (int i = 1; i < SFXMIX_VOICES; ++i)
            if (voices[i].started < voices[slot].started) slot = i;
    }
    voices[slot].data = mono;
    voices[slot].length = frames;
    voices[slot].position = 0;
    voices[slot].gain = gain_q8;
    voices[slot].started = ++serial;
    pthread_mutex_unlock(&mix_mutex);
}

void sfxmix_into(short *stereo, int frames) {
    pthread_mutex_lock(&mix_mutex);
    for (int v = 0; v < SFXMIX_VOICES; ++v) {
        struct voice *voice = &voices[v];
        if (!voice->data) continue;
        int count = voice->length - voice->position;
        if (count > frames) count = frames;
        for (int i = 0; i < count; ++i) {
            const int s = (voice->data[voice->position + i] * voice->gain) >> 8;
            int left = stereo[i * 2] + s;
            int right = stereo[i * 2 + 1] + s;
            if (left > 32767) left = 32767; else if (left < -32768) left = -32768;
            if (right > 32767) right = 32767; else if (right < -32768) right = -32768;
            stereo[i * 2] = (short)left;
            stereo[i * 2 + 1] = (short)right;
        }
        voice->position += count;
        if (voice->position >= voice->length) voice->data = NULL;
    }
    pthread_mutex_unlock(&mix_mutex);
}

void sfxmix_reset(void) {
    pthread_mutex_lock(&mix_mutex);
    for (int i = 0; i < SFXMIX_VOICES; ++i) voices[i].data = NULL;
    pthread_mutex_unlock(&mix_mutex);
}
