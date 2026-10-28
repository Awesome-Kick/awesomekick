#!/usr/bin/env python3
"""
Synthesize the metronome sounds the firmware expects in Kick/assets (they are missing from the public
release, so the click was silent). One file per STYLE setting, under the firmware's own file names.

    python3 scripts/make-metronome-sounds.py

The metronome plays the same sound on the bar (velocity 127) and on the beats (velocity 64).
Mono, 48 kHz, 16 bit, peak -3 dBFS. Made here from sine waves and noise: CC0, no third-party audio.

SPDX-License-Identifier: AGPL-3.0-or-later (this script; the generated sounds are CC0-1.0)
"""
import os
import wave

import numpy as np

RATE = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Kick", "assets")
rng = np.random.default_rng(0x4D45)


def t(seconds):
    return np.arange(int(RATE * seconds)) / RATE


def env(x, decay):
    return np.exp(-x / decay)


def tone(freqs, seconds, decay):
    x = t(seconds)
    return sum(a * np.sin(2 * np.pi * f * x) for f, a in freqs) * env(x, decay)


def noise(seconds, decay, highpass=0.85):
    x = t(seconds)
    n = rng.uniform(-1, 1, len(x))
    hp = np.zeros_like(n)
    for i in range(1, len(n)):  # one-pole high-pass
        hp[i] = highpass * (hp[i - 1] + n[i] - n[i - 1])
    return hp * env(x, decay)


def sweep(f0, f1, tau, seconds, decay):
    x = t(seconds)
    freq = f1 + (f0 - f1) * np.exp(-x / tau)
    return np.sin(2 * np.pi * np.cumsum(freq) / RATE) * env(x, decay)


SOUNDS = {
    "met-1-bar.wav":                tone([(1500, 1.0), (3000, 0.25)], 0.12, 0.018),   # classic click (default)
    "Perc_Glass_hi.wav":            tone([(2637, 1.0), (5274, 0.4), (7911, 0.15)], 0.35, 0.07),
    "Perc_Clap_hi.wav":             np.concatenate([noise(0.008, 0.004)] * 3 + [noise(0.15, 0.04)]),
    "Perc_Metal_hi.wav":            tone([(1790, 1.0), (2817, 0.7), (4140, 0.5), (5523, 0.3)], 0.3, 0.05),
    "Perc_MouthPop_hi.wav":         sweep(900, 250, 0.01, 0.08, 0.02),
    "Perc_MetronomeQuartz_hi.wav":  np.sign(np.sin(2 * np.pi * 2000 * t(0.03))) * 0.6 * env(t(0.03), 0.006),
    "Perc_Stick_hi.wav":            tone([(1250, 0.8), (2500, 0.3)], 0.06, 0.01) + noise(0.06, 0.005) * 0.6,
    "Synth_Bell_A_hi.wav":          tone([(880, 1.0), (1760, 0.5), (2640, 0.25), (3520, 0.12)], 0.8, 0.18),
}

for name, sig in SOUNDS.items():
    sig = np.asarray(sig, dtype=np.float64)
    fade = min(len(sig), int(RATE * 0.003))
    sig[-fade:] *= np.linspace(1, 0, fade)                    # no click at the end
    sig *= 10 ** (-3 / 20) / np.abs(sig).max()
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((sig * 32767).astype("<i2").tobytes())
    print("%-30s %.3f s" % (name, len(sig) / RATE))
