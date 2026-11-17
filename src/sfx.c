#include "sfx.h"

#include "hwaudio.h"
#include "sfxmix.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct effect {
    const char *name;
    int gain_q8;          /* relative loudness, 256 = as made */
    short *data;
    int frames;
};

/* Gains are a starting point for the handheld's speakers (the samples peak near full scale):
 * nothing should startle against the music, and the bells are the loudest thing in the game. */
static struct effect effects[SFX_COUNT] = {
    {"tick", 80, NULL, 0},     {"select", 100, NULL, 0}, {"back", 80, NULL, 0},     {"page", 90, NULL, 0},
    {"push", 120, NULL, 0},    {"seal", 140, NULL, 0},   {"extinguish", 110, NULL, 0}, {"bell", 120, NULL, 0},
    {"chime", 100, NULL, 0},   {"fall", 130, NULL, 0},   {"grave", 130, NULL, 0},   {"toll", 150, NULL, 0},
    {"settled", 120, NULL, 0}, {"wave", 90, NULL, 0}};
static volatile int enabled = 1;

static uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* A plain RIFF/WAVE reader: 16-bit PCM, one channel, 48 kHz. */
static int load_wav(const char *path, short **out, int *frames) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44 || size > 8 * 1024 * 1024) { fclose(f); return -1; }
    unsigned char *raw = (unsigned char *)malloc((size_t)size);
    if (!raw || fread(raw, 1, (size_t)size, f) != (size_t)size) { free(raw); fclose(f); return -1; }
    fclose(f);
    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) { free(raw); return -1; }
    int ok = 0;
    long pos = 12;
    while (pos + 8 <= size) {
        const uint32_t chunk = le32(raw + pos + 4);
        if (memcmp(raw + pos, "fmt ", 4) == 0 && chunk >= 16 && pos + 8 + 16 <= size) {
            ok = le16(raw + pos + 8) == 1 && le16(raw + pos + 10) == 1 && le32(raw + pos + 12) == HWAUDIO_RATE &&
                 le16(raw + pos + 22) == 16;
        } else if (memcmp(raw + pos, "data", 4) == 0) {
            if (!ok || pos + 8 + (long)chunk > size) break;
            const int n = (int)(chunk / 2);
            short *data = (short *)malloc((size_t)n * sizeof(short));
            if (!data) break;
            for (int i = 0; i < n; ++i) data[i] = (short)le16(raw + pos + 8 + 2 * i);
            free(raw);
            *out = data;
            *frames = n;
            return 0;
        }
        pos += 8 + (long)chunk + (long)(chunk & 1);
    }
    free(raw);
    return -1;
}

int sfx_init(const char *directory) {
    int loaded = 0;
    for (int i = 0; i < SFX_COUNT; ++i) {
        char path[600];
        snprintf(path, sizeof(path), "%s/%s.wav", directory, effects[i].name);
        free(effects[i].data);
        effects[i].data = NULL;
        effects[i].frames = 0;
        if (load_wav(path, &effects[i].data, &effects[i].frames) == 0) ++loaded;
        else fprintf(stderr, "sfx: could not load %s\n", path);
    }
    fprintf(stderr, "sfx: %d of %d effects loaded\n", loaded, SFX_COUNT);
    return loaded;
}

void sfx_deinit(void) {
    sfxmix_reset();
    for (int i = 0; i < SFX_COUNT; ++i) {
        free(effects[i].data);
        effects[i].data = NULL;
        effects[i].frames = 0;
    }
}

void sfx_set_enabled(int on) { enabled = on ? 1 : 0; }

void sfx_play(int id) {
    if (!enabled || id < 0 || id >= SFX_COUNT || !effects[id].data || !hwaudio_active()) return;
    sfxmix_play(effects[id].data, effects[id].frames, effects[id].gain_q8);
}
