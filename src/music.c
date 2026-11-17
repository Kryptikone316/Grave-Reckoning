#include "music.h"
#include "hwaudio.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#include <tremor/ivorbisfile.h>

/* The soundtrack is split into groups: group 0 is the title and menus, groups
 * 1 to 6 are the six chapters. Each lives in a folder called "Ch<N> - <name>".
 * The game asks for a group and the player moves to it with a short fade. */
#define MAX_GROUPS 7
#define MAX_PER_GROUP 8
#define DECODE_FRAMES 4096
#define FADE_FRAMES (HWAUDIO_RATE / 2)   /* half a second */

static char tracks[MAX_GROUPS][MAX_PER_GROUP][512];
static int track_count[MAX_GROUPS];
static int last_track[MAX_GROUPS] = {-1, -1, -1, -1, -1, -1, -1};
static volatile int requested_group;
static volatile int music_running;
static volatile int music_muted;
static volatile int music_volume = 80;
static pthread_t music_thread;

static int is_ogg(const char *name) {
    const size_t length = strlen(name);
    return length > 4 && strcmp(name + length - 4, ".ogg") == 0;
}

/* Per-track gain from a REPLAYGAIN_TRACK_GAIN Vorbis comment, in 1/256ths.
 * The tags are written without touching the audio data, so levelling the
 * album costs no extra lossy re-encode. Gain is limited to +6 dB so the
 * integer maths below cannot overflow. */
#define GAIN_UNITY 256

static int replaygain_from_comments(vorbis_comment *comments) {
    static const char key[] = "REPLAYGAIN_TRACK_GAIN=";
    if (!comments) return GAIN_UNITY;
    for (int i = 0; i < comments->comments; ++i) {
        const char *text = comments->user_comments[i];
        if (comments->comment_lengths[i] > (int)sizeof(key) - 1 &&
            strncasecmp(text, key, sizeof(key) - 1) == 0) {
            const double db = atof(text + sizeof(key) - 1);
            int gain = (int)(pow(10.0, db / 20.0) * GAIN_UNITY + 0.5);
            if (gain < 1) gain = 1;
            if (gain > 2 * GAIN_UNITY) gain = 2 * GAIN_UNITY;
            return gain;
        }
    }
    return GAIN_UNITY;
}

static short scaled_sample(short sample, int gain) {
    const int scaled = (int)sample * gain * music_volume;
    const int divisor = GAIN_UNITY * 100;
    return (short)((scaled + (scaled >= 0 ? divisor / 2 : -divisor / 2)) / divisor);
}

/* Picks a track for the wanted group (falling back to the menu group, then to
 * any group that has music), never the same track twice running in a group.
 * Returns the group actually used, or -1 if there is no music at all. */
static int choose_track(int wanted, int *index) {
    int group = wanted;
    if (group < 0 || group >= MAX_GROUPS || track_count[group] == 0) group = 0;
    if (track_count[group] == 0) {
        for (int g = 1; g < MAX_GROUPS; ++g)
            if (track_count[g] > 0) { group = g; break; }
    }
    if (track_count[group] == 0) return -1;
    int selected = 0;
    if (track_count[group] > 1) {
        do {
            selected = rand() % track_count[group];
        } while (selected == last_track[group]);
    }
    last_track[group] = selected;
    *index = selected;
    return group;
}

/* Keeps about a quarter of a second of music queued ahead of the audio thread,
 * so a late frame in the game can never starve the speaker. (The old version
 * waited until each block was due, leaving nothing in reserve.) */
#define CUSHION_FRAMES (HWAUDIO_RATE / 4)

static void wait_for_room(void) {
    while (music_running) {
        unsigned fill = 0;
        hwaudio_get_stats(NULL, NULL, &fill);
        if (fill <= CUSHION_FRAMES) return;
        usleep(5000);
    }
}

/* ---- speakers or headphones -------------------------------------------------
 * The SF3500's speakers do not play the left channel (measured on the device;
 * the right channel plays through both). So on the speakers the two channels
 * are blended and nothing panned left is lost. The stock firmware drives one
 * GPIO pin from the headphone jack, named in the device tree; reading the
 * bank's level register (mapped read-only) tells which is in use. If it cannot
 * be read the music stays stereo. */
static volatile uint32_t *hp_level;
static int hp_pin = -1;
static volatile int speaker_mix_mode;     /* 0 = blend on the speakers, 1 = always stereo */

static void headphone_detector_init(void) {
    unsigned char raw[8];
    int fd = open("/proc/device-tree/panel/speaker-output", O_RDONLY);
    if (fd < 0) return;
    const int n = (int)read(fd, raw, sizeof(raw));
    close(fd);
    if (n < 4) return;
    uint32_t value;
    memcpy(&value, raw, 4);
    value = ntohl(value);
    if (value > 31) return;
    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) return;
    void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x18800000);
    close(fd);
    if (map == MAP_FAILED) return;
    hp_pin = (int)value;
    hp_level = (volatile uint32_t *)((char *)map + 0x44 + 4 * 4);
}

static int speakers_in_use(void) {
    return hp_level != NULL && ((*hp_level >> hp_pin) & 1u) == 0;
}

static void blend_channels(short *samples, int frames) {
    for (int i = 0; i < frames; ++i) {
        const short mixed = (short)(((int)samples[i * 2] + (int)samples[i * 2 + 1]) / 2);
        samples[i * 2] = mixed;
        samples[i * 2 + 1] = mixed;
    }
}

static void *music_thread_main(void *unused) {
    (void)unused;
    short samples[DECODE_FRAMES * 2];

    while (music_running) {
        const int wanted = requested_group;
        int index = 0;
        const int group = choose_track(wanted, &index);
        if (group < 0) { usleep(250000); continue; }
        const char *path = tracks[group][index];
        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "music: unable to open %s\n", path);
            usleep(250000);
            continue;
        }
        OggVorbis_File vorbis;
        if (ov_open(file, &vorbis, NULL, 0) < 0) {
            fprintf(stderr, "music: invalid OGG %s\n", path);
            fclose(file);
            usleep(250000);
            continue;
        }
        vorbis_info *info = ov_info(&vorbis, -1);
        if (!info || info->channels != 2 || info->rate != HWAUDIO_RATE)
            fprintf(stderr, "music: expected 48 kHz stereo in %s\n", path);
        const int gain = replaygain_from_comments(ov_comment(&vorbis, -1));
        fprintf(stderr, "music: group %d, playing %s (gain %d/256)\n", group, path, gain);

        int bitstream = 0;
        long fade_in_left = FADE_FRAMES;
        long fade_out_left = -1;          /* >= 0 once the group has changed */
        while (music_running) {
            const long bytes = ov_read(&vorbis, (char *)samples, sizeof(samples), &bitstream);
            if (bytes <= 0) break;
            const int frames = (int)(bytes / (2 * (long)sizeof(short)));
            if (speaker_mix_mode == 0 && speakers_in_use()) blend_channels(samples, frames);
            if (music_muted || music_volume <= 0) {
                memset(samples, 0, (size_t)frames * 2 * sizeof(short));
            } else if (music_volume < 100 || gain != GAIN_UNITY) {
                for (int i = 0; i < frames * 2; ++i) samples[i] = scaled_sample(samples[i], gain);
            }
            /* a different group was asked for: fade this track out, then move on */
            if (fade_out_left < 0 && requested_group != wanted && group != requested_group &&
                track_count[requested_group >= 0 && requested_group < MAX_GROUPS ? requested_group : 0] > 0)
                fade_out_left = FADE_FRAMES;
            if (fade_in_left > 0 || fade_out_left >= 0) {
                for (int i = 0; i < frames; ++i) {
                    int mul = 1024;
                    if (fade_in_left > 0) { mul = (int)(1024L * (FADE_FRAMES - fade_in_left) / FADE_FRAMES); --fade_in_left; }
                    if (fade_out_left >= 0) {
                        mul = (int)((long)mul * fade_out_left / FADE_FRAMES);
                        if (fade_out_left > 0) --fade_out_left;
                    }
                    samples[i * 2] = (short)((samples[i * 2] * mul) >> 10);
                    samples[i * 2 + 1] = (short)((samples[i * 2 + 1] * mul) >> 10);
                }
            }
            for (int done = 0; done < frames && music_running;) {
                const int accepted = hwaudio_write(samples + done * 2, frames - done);
                done += accepted;
                if (done < frames) usleep(3000);        /* ring is full enough: wait */
            }
            wait_for_room();
            if (fade_out_left == 0) break;
        }
        ov_clear(&vorbis);
    }
    return NULL;
}

/* Adds every .ogg in `path` to a group. */
static void scan_group(int group, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && track_count[group] < MAX_PER_GROUP) {
        if (!is_ogg(entry->d_name)) continue;
        snprintf(tracks[group][track_count[group]], sizeof(tracks[group][0]), "%s/%s", path, entry->d_name);
        ++track_count[group];
    }
    closedir(dir);
}

int music_init(const char *directory) {
    DIR *dir = opendir(directory);
    if (!dir) {
        fprintf(stderr, "music: unable to open %s\n", directory);
        return -1;
    }
    memset(track_count, 0, sizeof(track_count));
    struct dirent *entry;
    int total = 0;
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            /* "Ch3 - The Ossuary" is group 3; "Ch0 ..." is the title and menus */
            if (entry->d_name[0] == 'C' && entry->d_name[1] == 'h' && entry->d_name[2] >= '0' && entry->d_name[2] <= '9') {
                const int group = atoi(entry->d_name + 2);
                if (group >= 0 && group < MAX_GROUPS) scan_group(group, path);
            }
        } else if (is_ogg(entry->d_name) && track_count[0] < MAX_PER_GROUP) {
            /* loose tracks in the top folder join the menu group */
            snprintf(tracks[0][track_count[0]], sizeof(tracks[0][0]), "%s", path);
            ++track_count[0];
        }
    }
    closedir(dir);
    for (int g = 0; g < MAX_GROUPS; ++g) {
        total += track_count[g];
        if (track_count[g] > 0) fprintf(stderr, "music: group %d has %d track(s)\n", g, track_count[g]);
    }
    if (total == 0) {
        fprintf(stderr, "music: no OGG tracks in %s\n", directory);
        return -1;
    }
    if (hwaudio_init() != 0) return -1;
    headphone_detector_init();
    fprintf(stderr, "music: output is %s\n", hp_level ? (speakers_in_use() ? "the speakers" : "headphones") : "unknown (staying stereo)");
    srand((unsigned)time(NULL));
    requested_group = 0;
    music_running = 1;
    if (pthread_create(&music_thread, NULL, music_thread_main, NULL) != 0) {
        music_running = 0;
        hwaudio_deinit();
        return -1;
    }
    return 0;
}

void music_deinit(void) {
    if (!music_running) return;
    music_running = 0;
    pthread_join(music_thread, NULL);
    hwaudio_deinit();
}

void music_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    music_volume = percent;
}

int music_get_volume(void) { return music_volume; }

void music_set_muted(int muted) { music_muted = muted ? 1 : 0; }

int music_is_muted(void) { return music_muted; }

void music_set_group(int group) {
    if (group < 0 || group >= MAX_GROUPS) group = 0;
    requested_group = group;
}

void music_set_speaker_mix(int mode) { speaker_mix_mode = mode ? 1 : 0; }
