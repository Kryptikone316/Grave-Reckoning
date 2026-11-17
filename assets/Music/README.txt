Grave Reckoning's soundtrack. Each chapter has its own folder, named "Ch<N> - <name>":

  Ch0  the title, menus, Journal and Options
  Ch1  The Forgotten Grave          Ch4  The Family Crypt
  Ch2  The Old Cemetery             Ch5  The Chapel Grounds
  Ch3  The Ossuary                  Ch6  The Ancestral Mausoleum

The game plays a chapter's tracks while you are in one of its graves and on its story
pages, in random order, with a half-second fade when the music changes. A chapter with no
tracks falls back to the Ch0 music; loose .ogg files in this folder join Ch0. With no
tracks at all the game runs silently.

Format: Vorbis, 48 kHz stereo (the SF3500 audio path runs at 48 kHz, so nothing is
resampled at playback). Each track carries ReplayGain tags (REPLAYGAIN_TRACK_GAIN, written
without re-encoding the audio) that the player applies at playback. The soundtrack is
levelled as an album: every track is brought to about -15 LUFS integrated (EBU R128) with
a true peak at or below -1 dBTP. To add or replace a track, measure it, tag it the same
way and drop it in the chapter's folder (tools/normalize_music.py does exactly this).
