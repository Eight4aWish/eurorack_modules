#!/usr/bin/env python3
"""Render every patch in the bank to a WAV and report objective metrics.

This validates the *sounds* without an AMYboard: it drives the desktop `amy`
build (pip install . from the amy repo), plays each patch, releases it, and
measures level / brightness / stereo width / release behaviour so regressions
or dead patches are obvious. Audition the WAVs in ./out/.

Usage:  python3 render_patches.py
"""
import os
import numpy as np
import soundfile as sf
import amy

import patches as bank

SR = 44100
OUT = os.path.join(os.path.dirname(__file__), "out")
PNUM = 1024  # AMY user-patch slot we reuse for each render


def seg(buf, t0, t1):
    return buf[int(t0 * SR):int(t1 * SR)]


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


def centroid(buf):
    x = buf[:, 0]
    X = np.abs(np.fft.rfft(x))
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    return float((f * X).sum() / (X.sum() + 1e-9))


def width(buf):
    L, R = buf[:, 0], buf[:, 1]
    return rms(L - R) / (rms(L) + rms(R) + 1e-9)


def render_one(p):
    is_pad = p["cat"] == bank.PAD
    note = 48 if is_pad else 36
    hold = 1.6 if is_pad else 0.8       # note-on duration
    tail = 1.8 if is_pad else 0.5       # render past note-off
    amy.restart()
    bank.store_patch(amy, p, PNUM)
    amy.send(synth=1, num_voices=p["voices"], patch=PNUM)
    bank.apply_fx(amy, p)
    amy.send(time=0, synth=1, note=note, vel=0.9)
    amy.send(time=int(hold * 1000), synth=1, note=note, vel=0)
    buf = np.array(amy.render(hold + tail))
    return buf, hold


def main():
    os.makedirs(OUT, exist_ok=True)
    print("%-14s %-4s  %6s %7s %7s %6s %6s  %s" %
          ("name", "cat", "peak", "sustain", "release", "cent", "width", "flags"))
    bad = 0
    for p in bank.PATCHES:
        buf, hold = render_one(p)
        peak = float(np.max(np.abs(buf)))
        sus = rms(seg(buf, hold * 0.5, hold * 0.9))
        rel = rms(seg(buf, hold + 0.25, hold + 0.45))
        cen = centroid(buf)
        wid = width(buf)
        flags = []
        if peak < 0.02:
            flags.append("SILENT")
        if peak > 0.99:
            flags.append("CLIP")
        if sus > 1e-4 and rel > sus * 0.6:
            flags.append("NO-RELEASE")
        if flags:
            bad += 1
        sf.write(os.path.join(OUT, p["name"].replace(" ", "_") + ".wav"),
                 buf, SR)
        print("%-14s %-4s  %6.3f %7.4f %7.4f %6.0f %6.3f  %s" %
              (p["name"], p["cat"], peak, sus, rel, cen, wid,
               ",".join(flags)))
    print("\n%d patches, %d flagged. WAVs in %s/" %
          (len(bank.PATCHES), bad, OUT))


if __name__ == "__main__":
    main()
