# AMYboard patch bank -- curated pads/drones + basses.
#
# Portable on purpose: pure data plus small helpers, no imports. Runs unchanged
# under desktop CPython (with the `amy` pip build) and Tulip MicroPython.
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

PAD = "PAD"
BASS = "BASS"

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

    {"name": "Dark Drone", "cat": PAD, "voices": 2,
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

    {"name": "Reso Sweep", "cat": PAD, "voices": 3,
     "oscs": _saw_stack(3, 0.005, 0.3, 0.7, 180, 500, 700, 3200, 9, 0.7),
     "fx": {"reverb": [0.4, 0.75, 0.4, 0.5]}},

    {"name": "Air Pad", "cat": PAD, "voices": 4,
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

    {"name": "Square Bass", "cat": BASS, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.5, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 240, "eg1": 1300},
               "bp0": "0,0,5,1,200,0.6,80,0", "bp1": "0,1,200,0.1,80,0", "pan": 0.5}]},

    {"name": "Pluck", "cat": BASS, "voices": 2,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": _amp(1.2),
               "filter_type": LPF, "resonance": 5, "filter_freq": {"const": 320, "eg1": 2200},
               "bp0": "0,0,3,1,200,0,90,0", "bp1": "0,1,160,0,0,0", "pan": 0.5}]},

    {"name": "Growl", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(1.0),
               "filter_type": LPF, "resonance": 10, "filter_freq": {"const": 200, "eg1": 900, "ext0": 1800},
               "bp0": "0,0,5,1,380,0.6,90,0", "bp1": "0,1,600,0.3,90,0", "pan": 0.5}]},

    {"name": "Stab", "cat": BASS, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": _amp(0.9),
               "filter_type": LPF, "resonance": 6, "filter_freq": {"const": 280, "eg1": 2400},
               "bp0": "0,0,4,1,140,0,70,0", "bp1": "0,1,130,0,0,0", "pan": 0.4},
              {"wave": SINE, "freq": {"note": 0.5}, "amp": {"vel": 0.7, "eg0": 1},
               "bp0": "0,0,4,1,140,0,70,0", "pan": 0.6}]},
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
    amy.send(chorus=fx.get("chorus", [0, 320, 0.5, 0.5]))
    amy.send(reverb=fx.get("reverb", [0, 0.5, 0.5, 0.5]))
    amy.send(echo=fx.get("echo", [0, 250, 500, 0.5, 0]))
