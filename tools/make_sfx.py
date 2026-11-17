#!/usr/bin/env python3
"""Synthesises the game's sound effects (CC0, made from scratch: no recordings, no samples).

Mono, 16-bit, 48 kHz WAVs into assets/Sfx/, one per name in src/sfx.c. Everything is plain
sines, decaying partials and filtered noise, so it can be tuned here and regenerated.

Run:  python3 tools/make_sfx.py [out-dir]       (needs numpy)
"""
import os
import sys
import wave

import numpy as np

SR = 48000
rng = np.random.RandomState(1264)


def time(d):
    return np.arange(int(d * SR)) / float(SR)


def band(x, lo, hi):
    """Keeps only lo..hi Hz of x (a smooth-edged mask on the spectrum)."""
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    mask = np.clip((f - lo) / max(1.0, lo * 0.4), 0, 1) * np.clip((hi - f) / max(1.0, hi * 0.4), 0, 1)
    return np.fft.irfft(spec * mask, len(x))


def noise(d, lo=80, hi=8000):
    return band(rng.randn(int(d * SR)), lo, hi)


def decay(t, tau):
    return np.exp(-t / tau)


def sweep(t, f0, f1, tau):
    """A sine that glides from f0 to f1 (exponentially, time constant tau)."""
    f = f1 + (f0 - f1) * np.exp(-t / tau)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def bell(f0, length, hum=1.0, bright=1.0):
    t = time(length)
    # partials of a church bell: hum, prime, minor third, fifth, nominal, then the shimmer
    parts = [(0.5, 0.40 * hum, 3.4), (1.0, 0.55, 2.6), (1.2, 0.34, 1.9), (1.5, 0.22, 1.4),
             (2.0, 0.26 * bright, 1.1), (2.51, 0.12 * bright, 0.8), (3.0, 0.10 * bright, 0.6),
             (4.2, 0.06 * bright, 0.4)]
    out = np.zeros_like(t)
    for ratio, amp, tau in parts:
        out += amp * np.sin(2 * np.pi * f0 * ratio * t + rng.rand() * 6.28) * decay(t, tau)
    strike = noise(0.012, 300, 3500)
    out[:len(strike)] += 0.5 * strike * np.linspace(1, 0, len(strike))
    return out


def knock(f, d=0.06, tau=0.012):
    t = time(d)
    return (np.sin(2 * np.pi * f * t) + 0.5 * np.sin(2 * np.pi * f * 1.6 * t)) * decay(t, tau)


def shape(x, peak=0.85, fade=0.004):
    n = int(fade * SR)
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    x[:n] *= np.linspace(0, 1, n)
    x[-n:] *= np.linspace(1, 0, n)
    return x


def pad(x, n):
    out = np.zeros(n)
    out[:len(x)] = x[:n]
    return out


def mix(*layers):
    n = max(off + len(x) for off, x in layers)
    out = np.zeros(n)
    for off, x in layers:
        out[off:off + len(x)] += x
    return out


def make():
    fx = {}
    fx["tick"] = knock(1100, 0.06, 0.010)
    fx["select"] = mix((0, knock(760, 0.08, 0.016)), (int(0.055 * SR), 0.8 * knock(1020, 0.09, 0.018)))
    fx["back"] = knock(520, 0.08, 0.016)

    t = time(0.30)
    bursts = np.zeros_like(t)
    for start in (0.0, 0.07, 0.15):
        i = int(start * SR)
        k = int(0.10 * SR)
        bursts[i:i + k] += np.hanning(k)[:len(bursts[i:i + k])] * rng.uniform(0.6, 1.0)
    fx["page"] = noise(0.30, 1800, 7500) * bursts

    t = time(0.32)
    rumble = noise(0.32, 90, 900)
    grind = rumble * (0.5 + 0.5 * np.sin(2 * np.pi * 38 * t)) * np.minimum(1, t / 0.03) * decay(t, 0.22)
    fx["push"] = mix((0, grind), (0, 0.7 * np.sin(2 * np.pi * 64 * t) * decay(t, 0.05)))

    t = time(0.40)
    fx["seal"] = mix((0, sweep(t, 120, 46, 0.05) * decay(t, 0.12)), (0, 0.5 * noise(0.40, 60, 500) * decay(t, 0.06)))

    t = time(0.24)
    fx["extinguish"] = mix((0, noise(0.24, 700, 4200) * np.minimum(1, t / 0.01) * decay(t, 0.06)),
                           (0, 0.25 * sweep(t, 900, 260, 0.08) * decay(t, 0.07)))

    fx["bell"] = bell(147, 2.6)
    t = time(0.8)
    fx["chime"] = (np.sin(2 * np.pi * 1318 * t) + 0.45 * np.sin(2 * np.pi * 1976 * t + 1.0) +
                   0.12 * np.sin(2 * np.pi * 3320 * t)) * decay(t, 0.16) * np.minimum(1, t / 0.004)

    t = time(0.55)
    fx["fall"] = mix((0, sweep(t, 360, 85, 0.12) * decay(t, 0.16)),
                     (int(0.2 * SR), np.concatenate([sweep(time(0.3), 90, 40, 0.04) * decay(time(0.3), 0.08)])),
                     (int(0.2 * SR), 0.4 * noise(0.2, 60, 600) * decay(time(0.2), 0.05)))

    t = time(0.40)
    fx["grave"] = mix((0, sweep(t, 100, 38, 0.06) * decay(t, 0.14)), (0, 0.5 * noise(0.40, 200, 1800) * decay(t, 0.09)))
    fx["toll"] = bell(98, 3.4, hum=1.6, bright=0.5)
    fx["settled"] = mix((0, 0.8 * bell(330, 1.8, hum=0.6, bright=0.6)), (int(0.30 * SR), bell(495, 2.0, hum=0.6, bright=0.5)))

    t = time(0.70)
    fx["wave"] = mix((0, 0.9 * sweep(t, 74, 52, 0.15) * decay(t, 0.22)), (0, 0.35 * noise(0.70, 50, 260) * decay(t, 0.12)))
    return fx


def main(out):
    os.makedirs(out, exist_ok=True)
    for name, x in make().items():
        data = (np.clip(shape(x), -1, 1) * 32767).astype("<i2")
        with wave.open(os.path.join(out, name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(data.tobytes())
        print("%-11s %5.2f s" % (name, len(data) / float(SR)))
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/Sfx")
