#ifndef GRAVERECKONING_MUSIC_H
#define GRAVERECKONING_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

int music_init(const char *directory);
void music_deinit(void);
void music_set_volume(int percent);
int music_get_volume(void);
void music_set_muted(int muted);
int music_is_muted(void);
/* 0 = title and menus, 1..6 = the chapters; the player fades to that group's tracks. */
void music_set_group(int group);
/* 0 = blend left and right on the speakers (they do not play the left channel), 1 = always stereo. */
void music_set_speaker_mix(int mode);

#ifdef __cplusplus
}
#endif

#endif
