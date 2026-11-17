#ifndef GRAVERECKONING_HWAUDIO_H
#define GRAVERECKONING_HWAUDIO_H

#define HWAUDIO_RATE 48000
#define HWAUDIO_CHUNK 480   /* frames (10 ms at 48 kHz), fixed by the driver */

/* The ring between the music thread and the audio thread. A power of two. */
#define HWAUDIO_RING_FRAMES 65536
#define HWAUDIO_RING_SOFT_CAP (HWAUDIO_RING_FRAMES * 3 / 4)

int hwaudio_init(void);
int hwaudio_active(void);
/* Non-blocking: copies what fits and returns how many frames were accepted. */
int hwaudio_write(const short *samples, int frames);
/* Frames waiting in the ring and how often the audio thread ran dry. */
void hwaudio_get_stats(unsigned *underruns, unsigned *dropped, unsigned *fill);
void hwaudio_deinit(void);

#endif
