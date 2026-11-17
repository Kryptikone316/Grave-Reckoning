#ifndef GRAVERECKONING_SFX_H
#define GRAVERECKONING_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

/* The effects, loaded from <directory>/<name>.wav (mono, 16-bit, 48 kHz; tools/make_sfx.py). */
enum SfxId {
    SFX_TICK, SFX_SELECT, SFX_BACK, SFX_PAGE,
    SFX_PUSH, SFX_SEAL, SFX_EXTINGUISH, SFX_BELL, SFX_CHIME, SFX_FALL,
    SFX_GRAVE, SFX_TOLL, SFX_SETTLED, SFX_WAVE,
    SFX_COUNT
};

/* Returns the number of effects loaded (0 if the folder is missing). Needs the audio output
 * (music_init) to be running to be heard. */
int sfx_init(const char *directory);
void sfx_deinit(void);
void sfx_set_enabled(int enabled);
void sfx_play(int id);

#ifdef __cplusplus
}
#endif

#endif
