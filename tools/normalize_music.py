#!/usr/bin/env python3
"""Album-normalises the soundtrack without re-encoding it.

Measures every .ogg under assets/Music with ffmpeg's EBU R128 meter, then writes
ReplayGain tags (stream copy: the audio data is untouched) so that every track
plays at about -15 LUFS integrated with a true peak at or below -1 dBTP. The game
applies REPLAYGAIN_TRACK_GAIN at playback; album tags are written for other players.

Run:  python3 tools/normalize_music.py [music-dir]     (needs ffmpeg)
Idempotent: it measures the audio, not the tags, so running it twice is harmless.
"""
import glob
import math
import os
import re
import subprocess
import sys

TARGET = -15.0
TP_LIMIT = -1.0


def measure(path):
    out = subprocess.run(["ffmpeg", "-nostats", "-hide_banner", "-i", path, "-af", "ebur128=peak=true",
                          "-f", "null", "-"], capture_output=True, text=True).stderr
    s = out[out.rfind("Summary:"):]
    loudness = float(re.search(r"I:\s+(-?[\d.]+) LUFS", s).group(1))
    peak = float(re.search(r"Peak:\s+(-?[\d.]+) dBFS", s[s.find("True peak"):]).group(1))
    return loudness, peak


def main(root):
    files = sorted(glob.glob(os.path.join(root, "**", "*.ogg"), recursive=True))
    rows = [(f,) + measure(f) for f in files]
    album_power = sum(10 ** (i / 10) for _, i, _ in rows) / len(rows)
    album_i = 10 * math.log10(album_power)
    album_tp = max(tp for _, _, tp in rows)
    album_gain = min(TARGET - album_i, TP_LIMIT - album_tp)
    for f, i, tp in rows:
        gain = min(TARGET - i, TP_LIMIT - tp)
        tmp = f + ".tmp.ogg"
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-y", "-i", f, "-map", "0:a", "-c", "copy",
                        "-map_metadata", "0",
                        "-metadata", "REPLAYGAIN_TRACK_GAIN=%.2f dB" % gain,
                        "-metadata", "REPLAYGAIN_TRACK_PEAK=%.6f" % 10 ** (tp / 20),
                        "-metadata", "REPLAYGAIN_ALBUM_GAIN=%.2f dB" % album_gain,
                        "-metadata", "REPLAYGAIN_ALBUM_PEAK=%.6f" % 10 ** (album_tp / 20),
                        "-metadata", "REPLAYGAIN_REFERENCE_LOUDNESS=%.1f LUFS" % TARGET, tmp], check=True)
        os.replace(tmp, f)
        print("%-60s %6.1f LUFS  TP %5.1f  gain %+5.2f dB" % (os.path.relpath(f, root), i, tp, gain))
    print("album: %.2f LUFS, max true peak %.2f dBTP, album gain %+.2f dB" % (album_i, album_tp, album_gain))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/Music")
