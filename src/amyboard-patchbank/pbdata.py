# AMYboard patch bank -- curated pads/drones + basses.
#
# Portable on purpose: data plus small helpers (only `math`, available on both
# CPython and MicroPython). Runs unchanged under desktop CPython (with the `amy`
# pip build) and Tulip MicroPython.
#
# Each patch is a few oscillators (relative indices 0..n-1) stored as an AMY
# "user patch" and played by a synth voice. Control coefficients use AMY's dict
# form whose source keys are: const, note, vel, eg0, eg1, mod, bend, ext0, ext1
# (ext0/ext1 are the two CV inputs). Envelope (bp0/bp1) strings are "time_ms,
# value" pairs; the LAST pair is the release, triggered on note-off. bp0 drives
# amplitude (eg0); bp1 is a free envelope usually routed to the filter (eg1).
#
# Loudness: the app/harness set a global VOLUME; patches are gain-staged so a
# single held note peaks comfortably below clipping with headroom for the FX.

import math

# --- wave constants (mirror amy.*) ---
SINE = 0
PULSE = 1
SAW_DOWN = 2
SAW_UP = 3
TRIANGLE = 4
NOISE = 5

# --- filter constants ---
LPF = 1
BPF = 2
HPF = 3

# --- banks (the patch list is organised into these; display order) ---
BASS = "BASS"
LEAD = "LEAD"
PAD = "PAD"
DRONE = "DRONE"
DRUM = "DRUM"
PERC = "PERC"
BANKS = [BASS, LEAD, PAD, DRONE, DRUM, PERC]

# Global AMY volume the bank is voiced against (app + render harness use this).
VOLUME = 3.0

# Velocity*envelope amplitude at a given gain.
def _amp(g=1.0):
    return {"vel": g, "eg0": g}


def _saw_stack(n, detune, pan_lo, pan_hi, cutoff, atk, rel, sweep, q, gain):
    """n detuned saws spread evenly across the stereo field -- the core of a
    rich analog pad. `detune` is the max ratio offset of the outer voices."""
    oscs = []
    for i in range(n):
        f = (i / (n - 1)) if n > 1 else 0.5          # 0..1
        det = (f - 0.5) * 2.0 * detune               # -detune..+detune
        pan = pan_lo + (pan_hi - pan_lo) * f
        oscs.append({
            "wave": SAW_DOWN, "freq": {"note": 1.0 + det}, "amp": _amp(gain),
            "filter_type": LPF, "resonance": q,
            "filter_freq": {"const": cutoff, "eg1": sweep},
            "bp0": "0,0,%d,1,%d,0" % (atk, rel),
            "bp1": "0,0,%d,1,%d,0" % (max(atk, 150), rel),
            "pan": round(pan, 3),
        })
    return oscs


# Each entry: name, category, num voices, oscillator list, optional bus FX.
PATCHES = [
    # ======================= PADS / DRONES =======================
    {"name": "Warm Analog", "cat": PAD, "voices": 3,
     "oscs": _saw_stack(3, 0.006, 0.2, 0.8, 520, 320, 600, 1400, 2, 0.7),
     "fx": {"chorus": [0.6, 340, 0.4, 0.5], "reverb": [0.35, 0.7, 0.4, 0.5]}},

    {"name": "Super Strings", "cat": PAD, "voices": 3,
     "oscs": _saw_stack(5, 0.010, 0.05, 0.95, 760, 380, 360, 1600, 2, 0.5),
     "fx": {"chorus": [0.8, 380, 0.6, 0.6], "reverb": [0.4, 0.78, 0.4, 0.5]}},

    {"name": "Glass Bells", "cat": PAD, "voices": 4,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "bp0": "0,0,4,1,900,0", "pan": 0.35},
              {"wave": SINE, "freq": {"note": 2.01}, "amp": {"vel": 0.5, "eg0": 1},
               "bp0": "0,0,4,1,600,0", "pan": 0.65},
              {"wave": TRIANGLE, "freq": {"note": 3.0}, "amp": {"vel": 0.25, "eg0": 1},
               "bp0": "0,0,4,1,350,0", "pan": 0.5}],
     "fx": {"chorus": [0.3, 300, 0.5, 0.4], "reverb": [0.5, 0.82, 0.3, 0.5]}},

    {"name": "Dark Drone", "cat": DRONE, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 0.5}, "amp": _amp(0.8),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 240, "eg1": 500},
               "bp0": "0,0,900,1,900,0", "bp1": "0,0,2600,1,600,0", "pan": 0.35},
              {"wave": SAW_DOWN, "freq": {"note": 0.504}, "amp": _amp(0.8),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 240, "eg1": 500},
               "bp0": "0,0,1000,1,900,0", "bp1": "0,0,2600,1,600,0", "pan": 0.65}],
     "fx": {"reverb": [0.5, 0.85, 0.3, 0.5]}},

    {"name": "Vox Choir", "cat": PAD, "voices": 4,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(0.8),
               "filter_type": LPF, "resonance": 6, "filter_freq": {"const": 800, "eg1": 1200},
               "bp0": "0,0,360,1,500,0", "bp1": "0,0,800,1,500,0", "pan": 0.3},
              {"wave": SAW_DOWN, "freq": {"note": 2.004}, "amp": {"vel": 0.4, "eg0": 1},
               "filter_type": LPF, "resonance": 6, "filter_freq": {"const": 1600, "eg1": 700},
               "bp0": "0,0,380,1,520,0", "bp1": "0,0,800,1,500,0", "pan": 0.7}],
     "fx": {"chorus": [0.6, 340, 0.5, 0.5], "reverb": [0.45, 0.78, 0.35, 0.5]}},

    {"name": "Soft Keys", "cat": PAD, "voices": 4,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "filter_type": LPF, "resonance": 2, "filter_freq": {"const": 1200, "eg1": 900},
               "bp0": "0,0,8,1,500,0.4,400,0", "bp1": "0,1,400,0.2,400,0", "pan": 0.4},
              {"wave": SINE, "freq": {"note": 2.0}, "amp": {"vel": 0.3, "eg0": 1},
               "bp0": "0,0,8,1,300,0,250,0", "pan": 0.6}],
     "fx": {"chorus": [0.4, 320, 0.4, 0.4], "reverb": [0.35, 0.7, 0.4, 0.5]}},

    {"name": "Brass Section", "cat": PAD, "voices": 3,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(0.7),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 350, "eg1": 2400},
               "bp0": "0,0,90,1,250,0.8,300,0", "bp1": "0,0,120,1,400,0.5,300,0", "pan": 0.35},
              {"wave": SAW_DOWN, "freq": {"note": 1.006}, "amp": _amp(0.7),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 350, "eg1": 2400},
               "bp0": "0,0,110,1,260,0.8,300,0", "bp1": "0,0,120,1,400,0.5,300,0", "pan": 0.65}],
     "fx": {"reverb": [0.3, 0.65, 0.4, 0.5]}},

    {"name": "Reso Sweep", "cat": DRONE, "voices": 3,
     "oscs": _saw_stack(3, 0.005, 0.3, 0.7, 180, 500, 700, 3200, 9, 0.7),
     "fx": {"reverb": [0.4, 0.75, 0.4, 0.5]}},

    {"name": "Air Pad", "cat": DRONE, "voices": 4,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "bp0": "0,0,800,1,900,0", "pan": 0.3},
              {"wave": TRIANGLE, "freq": {"note": 2.002}, "amp": {"vel": 0.3, "eg0": 1},
               "bp0": "0,0,1000,1,800,0", "pan": 0.7}],
     "fx": {"chorus": [0.5, 360, 0.3, 0.5], "reverb": [0.6, 0.88, 0.25, 0.5]}},

    {"name": "PWM Pad", "cat": PAD, "voices": 3,
     "oscs": [{"wave": PULSE, "duty": 0.35, "freq": {"note": 1.0}, "amp": _amp(0.7),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 600, "eg1": 1200},
               "bp0": "0,0,300,1,600,0", "bp1": "0,0,1400,1,600,0", "pan": 0.3},
              {"wave": PULSE, "duty": 0.5, "freq": {"note": 1.004}, "amp": _amp(0.7),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 600, "eg1": 1200},
               "bp0": "0,0,320,1,620,0", "bp1": "0,0,1400,1,600,0", "pan": 0.7}],
     "fx": {"chorus": [0.7, 360, 0.5, 0.5], "reverb": [0.35, 0.72, 0.4, 0.5]}},

    # ========================= BASSES =========================
    {"name": "Sub", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(1.4),
               "bp0": "0,0,6,1,160,0.85,90,0", "pan": 0.5}]},

    {"name": "Deep Sub", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 0.5}, "amp": _amp(1.5),
               "bp0": "0,0,8,1,220,0.9,120,0", "pan": 0.5}]},

    {"name": "Analog Bass", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.1),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 200, "eg1": 1600},
               "bp0": "0,0,5,1,160,0.7,90,0", "bp1": "0,1,180,0.15,90,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 0.5}, "amp": {"vel": 0.6, "eg0": 1},
               "bp0": "0,0,5,1,160,0.7,90,0", "pan": 0.5}]},

    {"name": "Reese", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 520, "eg1": 300},
               "bp0": "0,0,5,1,60,0", "bp1": "0,1,1500,1,80,0", "pan": 0.3},
              {"wave": SAW_DOWN, "freq": {"note": 1.013}, "amp": _amp(0.9),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 520, "eg1": 300},
               "bp0": "0,0,5,1,60,0", "bp1": "0,1,1500,1,80,0", "pan": 0.7}],
     "fx": {"chorus": [0.3, 300, 0.3, 0.4]}},

    {"name": "Acid", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 15, "filter_freq": {"const": 160, "eg1": 2800},
               "bp0": "0,0,5,1,280,0.3,70,0", "bp1": "0,1,220,0,70,0", "pan": 0.5}]},

    {"name": "FM Punch", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(1.3),
               "bp0": "0,0,4,1,150,0.7,80,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 2.0}, "amp": {"vel": 0.7, "eg0": 1},
               "bp0": "0,0,3,1,60,0,40,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 3.01}, "amp": {"vel": 0.35, "eg0": 1},
               "bp0": "0,0,2,1,35,0,30,0", "pan": 0.5}]},

    {"name": "Pluck", "cat": BASS, "voices": 2,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": _amp(1.2),
               "filter_type": LPF, "resonance": 5, "filter_freq": {"const": 320, "eg1": 2200},
               "bp0": "0,0,3,1,200,0,90,0", "bp1": "0,1,160,0,0,0", "pan": 0.5}]},

    # ======================== LEADS ========================
    {"name": "Saw Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 5, "filter_freq": {"const": 600, "eg1": 2500},
               "bp0": "0,0,4,1,200,0.8,150,0", "bp1": "0,1,300,0.4,150,0", "pan": 0.5}],
     "fx": {"reverb": [0.2, 0.7, 0.4, 0.5]}},

    {"name": "Square Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.5, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 800, "eg1": 1800},
               "bp0": "0,0,4,1,180,0.85,140,0", "bp1": "0,1,250,0.5,140,0", "pan": 0.5}],
     "fx": {"reverb": [0.2, 0.7, 0.4, 0.5]}},

    {"name": "Supersaw Lead", "cat": LEAD, "voices": 1,
     "oscs": _saw_stack(5, 0.008, 0.2, 0.8, 1200, 6, 220, 2000, 3, 0.55),
     "fx": {"chorus": [0.6, 340, 0.5, 0.5], "reverb": [0.25, 0.7, 0.4, 0.5]}},

    {"name": "Pluck Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.1),
               "filter_type": LPF, "resonance": 6, "filter_freq": {"const": 400, "eg1": 3000},
               "bp0": "0,0,3,1,250,0,150,0", "bp1": "0,1,150,0,0,0", "pan": 0.5}],
     "fx": {"reverb": [0.3, 0.75, 0.35, 0.5]}},

    {"name": "Sync Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "filter_type": LPF, "resonance": 8, "filter_freq": {"const": 1500, "eg1": 2500},
               "bp0": "0,0,4,1,200,0.8,120,0", "bp1": "0,1,200,0.6,120,0", "pan": 0.5},
              {"wave": PULSE, "duty": 0.2, "freq": {"note": 2.01}, "amp": {"vel": 0.5, "eg0": 1},
               "filter_type": LPF, "resonance": 8, "filter_freq": {"const": 2000, "eg1": 1500},
               "bp0": "0,0,4,1,200,0.8,120,0", "pan": 0.5}],
     "fx": {"reverb": [0.2, 0.7, 0.4, 0.5]}},

    {"name": "Sine Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(1.3),
               "bp0": "0,0,20,1,300,0.9,200,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 3.0}, "amp": {"vel": 0.2, "eg0": 1},
               "bp0": "0,0,30,1,200,0.5,150,0", "pan": 0.5}],
     "fx": {"reverb": [0.35, 0.78, 0.35, 0.5]}},

    {"name": "Acid Lead", "cat": LEAD, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 15, "filter_freq": {"const": 300, "eg1": 3000},
               "bp0": "0,0,4,1,250,0.4,90,0", "bp1": "0,1,220,0,90,0", "pan": 0.5}],
     "fx": {"reverb": [0.2, 0.7, 0.4, 0.5]}},

    # ==================== DRONES (top-up) ====================
    {"name": "Deep Space", "cat": DRONE, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 0.5}, "amp": _amp(0.8), "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 200, "eg1": 400},
               "bp0": "0,0,1500,1,1200,0", "bp1": "0,0,4000,1,800,0", "pan": 0.3},
              {"wave": SAW_DOWN, "freq": {"note": 0.503}, "amp": _amp(0.8), "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 200, "eg1": 400},
               "bp0": "0,0,1600,1,1200,0", "bp1": "0,0,4000,1,800,0", "pan": 0.7}],
     "fx": {"reverb": [0.55, 0.88, 0.25, 0.5]}},

    {"name": "Harmonic Drift", "cat": DRONE, "voices": 3,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "bp0": "0,0,1200,1,1000,0", "pan": 0.4},
              {"wave": SINE, "freq": {"note": 1.5}, "amp": {"vel": 0.5, "eg0": 1},
               "bp0": "0,0,1600,1,1000,0", "pan": 0.6},
              {"wave": SINE, "freq": {"note": 2.002}, "amp": {"vel": 0.35, "eg0": 1},
               "bp0": "0,0,2000,1,900,0", "pan": 0.5}],
     "fx": {"chorus": [0.4, 360, 0.3, 0.4], "reverb": [0.5, 0.85, 0.3, 0.5]}},

    {"name": "Glass Drone", "cat": DRONE, "voices": 3,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": _amp(0.8),
               "bp0": "0,0,900,1,1000,0", "pan": 0.35},
              {"wave": SINE, "freq": {"note": 3.01}, "amp": {"vel": 0.3, "eg0": 1},
               "bp0": "0,0,1200,1,800,0", "pan": 0.65}],
     "fx": {"chorus": [0.5, 320, 0.5, 0.4], "reverb": [0.55, 0.88, 0.3, 0.5]}},

    {"name": "Sub Drone", "cat": DRONE, "voices": 2,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 0.5}, "amp": _amp(1.0), "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 300, "eg1": 200},
               "bp0": "0,0,1000,1,1100,0", "bp1": "0,0,3000,1,0,0", "pan": 0.5}],
     "fx": {"reverb": [0.45, 0.82, 0.3, 0.5]}},

    # ============== DRUMS (synth, one-shot) ==============
    # bp0 "0,0,a,1,d,0,3,0" = attack a / decay-to-0 d / release -> a true hit,
    # not a held note. Pitch is freq {"note": ratio} (absolute Hz goes silent in
    # a synth voice on this board); ratio sets the drum's pitch at the played note.
    {"name": "Kick", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 0.7}, "amp": _amp(1.8), "filter_type": LPF,
               "resonance": 1, "filter_freq": {"const": 2000}, "bp0": "0,0,2,1,130,0,3,0", "pan": 0.5},
              {"wave": NOISE, "amp": {"vel": 0.5, "eg0": 1}, "filter_type": HPF, "resonance": 1,
               "filter_freq": {"const": 3000}, "bp0": "0,0,1,1,8,0,3,0", "pan": 0.5}]},

    {"name": "Snare", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(1.0), "filter_type": HPF, "resonance": 2,
               "filter_freq": {"const": 1200}, "bp0": "0,0,1,1,160,0,3,0", "pan": 0.5},
              {"wave": TRIANGLE, "freq": {"note": 2.8}, "amp": {"vel": 0.6, "eg0": 1},
               "bp0": "0,0,1,1,90,0,3,0", "pan": 0.5}]},

    {"name": "Closed Hat", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(0.8), "filter_type": HPF, "resonance": 2,
               "filter_freq": {"const": 8000}, "bp0": "0,0,1,1,45,0,3,0", "pan": 0.5}]},

    {"name": "Open Hat", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(0.8), "filter_type": HPF, "resonance": 2,
               "filter_freq": {"const": 8000}, "bp0": "0,0,1,1,350,0,3,0", "pan": 0.5}]},

    {"name": "Clap", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(1.0), "filter_type": BPF, "resonance": 3,
               "filter_freq": {"const": 1600}, "bp0": "0,0,1,1,130,0,3,0", "pan": 0.5}],
     "fx": {"reverb": [0.25, 0.7, 0.4, 0.5]}},

    {"name": "Tom", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.8}, "amp": _amp(1.4), "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 1500}, "bp0": "0,0,2,1,220,0,3,0", "pan": 0.5}]},

    {"name": "Rim", "cat": DRUM, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.4, "freq": {"note": 6.7}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 4, "filter_freq": {"const": 3000},
               "bp0": "0,0,1,1,40,0,3,0", "pan": 0.5}]},

    # ============ PERCUSSION (synth, one-shot) ============
    {"name": "Conga", "cat": PERC, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 3.4}, "amp": _amp(1.3), "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 2000}, "bp0": "0,0,2,1,160,0,3,0", "pan": 0.4}]},

    {"name": "Bongo", "cat": PERC, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 5.8}, "amp": _amp(1.2), "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 2600}, "bp0": "0,0,2,1,110,0,3,0", "pan": 0.6}]},

    {"name": "Woodblock", "cat": PERC, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.4, "freq": {"note": 17}, "amp": _amp(1.3),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 3500},
               "bp0": "0,0,1,1,45,0,3,0", "pan": 0.5}]},

    {"name": "Shaker", "cat": PERC, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(0.7), "filter_type": HPF, "resonance": 1,
               "filter_freq": {"const": 7000}, "bp0": "0,0,1,1,50,0,3,0", "pan": 0.5}]},

    {"name": "Tambourine", "cat": PERC, "voices": 1,
     "oscs": [{"wave": NOISE, "amp": _amp(0.8), "filter_type": BPF, "resonance": 3,
               "filter_freq": {"const": 6000}, "bp0": "0,0,1,1,130,0,3,0", "pan": 0.5}]},

    {"name": "Clave", "cat": PERC, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.5, "freq": {"note": 36}, "amp": _amp(1.4), "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 4500}, "bp0": "0,0,1,1,40,0,3,0", "pan": 0.5}]},

    {"name": "Cowbell", "cat": PERC, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.5, "freq": {"note": 8.6}, "amp": _amp(0.9),
               "filter_type": BPF, "resonance": 3, "filter_freq": {"const": 2500},
               "bp0": "0,0,1,1,200,0,3,0", "pan": 0.5},
              {"wave": PULSE, "duty": 0.5, "freq": {"note": 13}, "amp": {"vel": 0.7, "eg0": 1},
               "filter_type": BPF, "resonance": 3, "filter_freq": {"const": 2500},
               "bp0": "0,0,1,1,200,0,3,0", "pan": 0.5}]},
]


def store_patch(amy, patch, patch_number):
    """Store one bank entry as AMY user patch `patch_number` (1024-1055)."""
    amy.start_store_patch()
    for i, osc in enumerate(patch["oscs"]):
        amy.send(osc=i, **osc)
    amy.stop_store_patch(patch_number)


def apply_fx(amy, patch):
    """Apply (or clear) this patch's bus effects. Call after selecting a voice."""
    fx = patch.get("fx", {})
    amy.send(chorus=fx.get("chorus", _FX_DEFAULT["chorus"]))
    amy.send(reverb=fx.get("reverb", _FX_DEFAULT["reverb"]))
    amy.send(echo=fx.get("echo", _FX_DEFAULT["echo"]))


# ======================================================================
# Macros -- up to 4 live, encoder-tweakable parameters per patch.
#
# A macro is metadata: name, kind, value range, and an `init` (the normalized
# 0..1 start that reproduces the patch's stored value). The app stores values
# normalized; apply_macro() maps to the real value and sends it live.
#   kind "cutoff" -> synth filter cutoff (log-scaled). NB: AMY ignores a synth
#                    filter_freq with a non-zero eg1, so this takes *manual*
#                    control of the cutoff (replacing the patch's env sweep)
#                    once you turn it -- so macros are NOT applied on load.
#   kind "res"    -> synth resonance (linear)
#   kind "fx"     -> rides a bus-effect send level (reverb/chorus/echo)
# Filtered patches get TONE+RES+SPACE+MOVE; filterless ones get FX-only macros.
# ======================================================================
_FX_DEFAULT = {
    "reverb": [0.0, 0.8, 0.35, 0.5],     # [level, liveness, damping, xover]
    "chorus": [0.0, 340, 0.5, 0.5],      # [level, max_delay, lfo_freq, depth]
    "echo": [0.0, 250, 500, 0.4, 0.0],   # [level, delay, max_delay, feedback, ...]
}


def _scale(macro, norm):
    lo, hi = macro["min"], macro["max"]
    if norm < 0:
        norm = 0.0
    elif norm > 1:
        norm = 1.0
    if macro["kind"] == "cutoff":
        return lo * (hi / lo) ** norm        # log -> musical
    return lo + (hi - lo) * norm             # linear


def _norm_for(lo, hi, value, log=False):
    """The normalized 0..1 that _scale maps back to `value` (for init)."""
    value = min(max(value, lo), hi)
    if log:
        return math.log(value / lo) / math.log(hi / lo)
    return (value - lo) / (hi - lo) if hi > lo else 0.0


def apply_macro(amy, patch, synth, macro, norm):
    """Send a macro's value live to the playing voice / bus."""
    v = _scale(macro, norm)
    k = macro["kind"]
    if k == "cutoff":
        amy.send(synth=synth, filter_freq={"const": v, "eg1": 0})
    elif k == "res":
        amy.send(synth=synth, resonance=v)
    elif k == "decay":
        amy.send(synth=synth, bp0="0,0,2,1,%d,0,3,0" % int(v))   # one-shot decay (next hit)
    elif k == "fx":
        # drive the whole effect from dry to very wet (not just the send level)
        # so the AMYboard's modest bus FX actually move the sound
        name = macro["fx"]
        if name == "reverb":                         # [level, liveness, damping, xover]
            amy.send(reverb=[v, 0.4 + 0.6 * v, 0.6 - 0.4 * v, 0.5])
        elif name == "chorus":                       # [level, max_delay, lfo, depth]
            amy.send(chorus=[v, 340, 0.6, 0.15 + 0.8 * v])
        elif name == "echo":                         # [level, delay, max, feedback, ...]
            amy.send(echo=[v, 270, 500, 0.15 + 0.65 * v, 0.0])
        else:
            base = patch.get("fx", {}).get(name)
            arr = list(base) if base else list(_FX_DEFAULT[name])
            arr[0] = v
            amy.send(**{name: arr})


def _fx_macro(patch, name, label):
    lvl = patch.get("fx", {}).get(name, _FX_DEFAULT[name])[0]
    return {"name": label, "kind": "fx", "fx": name,
            "min": 0.0, "max": 1.0, "init": _norm_for(0.0, 1.0, lvl)}


def _bp0_decay(bp0):
    try:
        return int(bp0.split(",")[4])   # decay time of the post-attack segment (ms)
    except Exception:
        return 150


def _drum_macros(p, osc0):
    cutoff = osc0.get("filter_freq", {}).get("const", 2000)
    dec = _bp0_decay(osc0.get("bp0", "0,0,2,1,150,0,3,0"))
    return [
        {"name": "DECAY", "kind": "decay", "min": 20, "max": 700,
         "init": _norm_for(20, 700, min(max(dec, 20), 700))},
        {"name": "TONE", "kind": "cutoff", "min": 200, "max": 9000,
         "init": _norm_for(200, 9000, min(max(cutoff, 200), 9000), log=True)},
        {"name": "SNAP", "kind": "res", "min": 0.7, "max": 12,
         "init": _norm_for(0.7, 12, osc0.get("resonance", 2))},
        _fx_macro(p, "reverb", "SPACE"),
    ]


def _derive_macros(p):
    osc0 = p["oscs"][0]
    if p["cat"] in (DRUM, PERC):
        return _drum_macros(p, osc0)
    ff = osc0.get("filter_freq")
    if ff is not None and "filter_type" in osc0:
        cutoff = ff.get("const", 800)
        lo, hi = 200, 6000
        res = osc0.get("resonance", 2)
        move = "chorus" if "chorus" in p.get("fx", {}) else "echo"
        return [
            {"name": "TONE", "kind": "cutoff", "min": lo, "max": hi,
             "init": _norm_for(lo, hi, min(max(cutoff, lo), hi), log=True)},
            {"name": "RES", "kind": "res", "min": 0.7, "max": 14,
             "init": _norm_for(0.7, 14, res)},
            _fx_macro(p, "reverb", "SPACE"),
            _fx_macro(p, move, "MOVE"),
        ]
    # filterless (pure sine/triangle) -- FX macros only
    return [_fx_macro(p, "reverb", "SPACE"),
            _fx_macro(p, "chorus", "AIR"),
            _fx_macro(p, "echo", "ECHO")]


for _p in PATCHES:
    if "macros" not in _p:
        _p["macros"] = _derive_macros(_p)


def patches_in(bank):
    """Global PATCHES indices in `bank`, sorted alphabetically by patch name."""
    idx = [i for i, p in enumerate(PATCHES) if p["cat"] == bank]
    idx.sort(key=lambda i: PATCHES[i]["name"].lower())
    return idx


# bank -> [patch indices]; banks with no patches yet (LEAD/DRUM/PERC) map to []
BANK_INDEX = {b: patches_in(b) for b in BANKS}
