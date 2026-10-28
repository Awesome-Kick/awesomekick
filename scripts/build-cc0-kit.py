#!/usr/bin/env python3
"""
Build the "Rusty Kit" one-shot samples from Karoryfer Samples' Big Rusty Drums (CC0-1.0).

    python3 scripts/build-cc0-kit.py <output folder>

Source: https://github.com/sfzinstruments/karoryfer.big-rusty-drums (CC0-1.0, public domain).
For every pad the script downloads one strong hit (second-highest velocity layer, first
round robin) from each microphone, mixes the microphones, trims the silent tail, fades out
and normalises the peak. Result: 16 mono 44.1 kHz 16-bit WAV files, "01 Kick.wav" … for
pads 1–16 of bank A. Needs ffmpeg and numpy.

SPDX-License-Identifier: AGPL-3.0-or-later (this script; the samples stay CC0-1.0)
"""
import os
import re
import subprocess
import sys
import urllib.request
import wave

import numpy as np

REPO = "sfzinstruments/karoryfer.big-rusty-drums"
RAW = "https://raw.githubusercontent.com/%s/main/" % REPO
RATE = 44100

# pad: name, articulation folder, {microphone: gain}, peak (dBFS)
PADS = [
    ("Kick",          "kick_24/kick",        {"kick": 1.0, "oh": 0.5},            -1),
    ("Snare",         "snare_14/center",     {"top": 1.0, "btm": 0.35, "oh": 0.5}, -1),
    ("Rimshot",       "snare_14/rimshot",    {"top": 1.0, "btm": 0.35, "oh": 0.5}, -1),
    ("Sidestick",     "snare_14/sidestick",  {"top": 1.0, "oh": 0.5},              -3),
    ("Hihat Closed",  "hihat_14/cl",         {"cl": 1.0, "oh": 0.5},               -6),
    ("Hihat Pedal",   "hihat_14/chik",       {"cl": 1.0, "oh": 0.5},               -8),
    ("Hihat Open",    "hihat_14/open",       {"cl": 1.0, "oh": 0.6},               -6),
    ("Ride",          "ride_22/rd",          {"cl": 0.8, "oh": 1.0},               -7),
    ("Tom High",      "tom_14/center",       {"cl": 1.0, "oh": 0.5},               -2),
    ("Tom Mid",       "tom_15/center",       {"cl": 1.0, "oh": 0.5},               -2),
    ("Tom Low",       "tom_18/center",       {"cl": 1.0, "oh": 0.5},               -2),
    ("Floor Tom",     "tom_22/center",       {"cl": 1.0, "oh": 0.5},               -2),
    ("Ride Bell",     "ride_22/bl",          {"cl": 0.8, "oh": 1.0},               -6),
    ("Crash",         "crash_17/cr",         {"cl": 0.7, "oh": 1.0},               -5),
    ("Stack",         "stack_3_layer/mid",   {"cl": 0.8, "oh": 1.0},               -6),
    ("Snare Edge",    "snare_14/edge",       {"top": 1.0, "btm": 0.35, "oh": 0.5}, -2),
]


def tree():
    out = subprocess.run(["gh", "api", "repos/%s/git/trees/main?recursive=1" % REPO, "--jq",
                          '.tree[] | select(.type=="blob") | .path'], capture_output=True, text=True, check=True)
    return out.stdout.split("\n")


def decode(url):
    """Download and decode to float32 mono at RATE with ffmpeg."""
    data = urllib.request.urlopen(url, timeout=60).read()
    pcm = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
                          "-ac", "1", "-ar", str(RATE), "-f", "f32le", "pipe:1"],
                         input=data, capture_output=True, check=True).stdout
    return np.frombuffer(pcm, dtype=np.float32)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    files = tree()
    for n, (name, art, mics, peak_db) in enumerate(PADS, 1):
        first_mic = next(iter(mics))
        pattern = re.compile(r"^Samples/%s/%s/(.+_vl(\d+)_rr1\.flac)$" % (re.escape(art), re.escape(first_mic)))
        layers = sorted(((int(m.group(2)), m.group(1)) for m in map(pattern.match, files) if m))
        if not layers:
            sys.exit("no samples for %s/%s" % (art, first_mic))
        vl, fname = layers[-2] if len(layers) > 1 else layers[-1]
        mix = None
        for mic, gain in mics.items():
            sig = decode(RAW + "Samples/%s/%s/%s" % (art, mic, fname)) * gain
            if mix is None:
                mix = sig
            else:
                length = max(len(mix), len(sig))
                mix = np.pad(mix, (0, length - len(mix))) + np.pad(sig, (0, length - len(sig)))
        # Trim the tail below -60 dB of the peak, then a 20 ms fade out.
        level = np.abs(mix)
        above = np.nonzero(level > level.max() * 10 ** (-60 / 20))[0]
        mix = mix[: above[-1] + 1 if len(above) else len(mix)]
        fade = min(len(mix), int(RATE * 0.02))
        mix[-fade:] *= np.linspace(1.0, 0.0, fade)
        mix *= 10 ** (peak_db / 20) / np.abs(mix).max()
        path = os.path.join(out_dir, "%02d %s.wav" % (n, name))
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes((np.clip(mix, -1, 1) * 32767).astype("<i2").tobytes())
        print("%-24s %-20s vl%-3d %.2f s  (%s)" % (os.path.basename(path), art, vl, len(mix) / RATE, " + ".join(mics)))


if __name__ == "__main__":
    main()
