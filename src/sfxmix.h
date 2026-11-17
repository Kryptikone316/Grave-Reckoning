#ifndef GRAVERECKONING_SFXMIX_H
#define GRAVERECKONING_SFXMIX_H

/* A small voice mixer for sound effects. The audio thread calls sfxmix_into() once for every
 * 10 ms block it is about to play, so an effect starts within 10 ms of being asked for (the
 * music is queued a quarter of a second ahead, which is far too late for a footfall or a bell).
 * The samples are mono 16-bit at the output rate and must outlive the voice. Thread safe. */

#ifdef __cplusplus
extern "C" {
#endif

#define SFXMIX_VOICES 8

/* Starts a voice; `gain_q8` is 256 for unity. The oldest voice is cut if all are busy. */
void sfxmix_play(const short *mono, int frames, int gain_q8);
/* Adds every playing voice to a stereo block (saturating) and advances them. */
void sfxmix_into(short *stereo, int frames);
/* Stops everything. */
void sfxmix_reset(void);

#ifdef __cplusplus
}
#endif

#endif
