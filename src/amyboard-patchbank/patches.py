# AMYboard patch bank -- curated pads/drones + basses.
#
# Portable on purpose: this module is pure data plus two tiny helper
# functions, with no imports. It runs unchanged under desktop CPython (with
# the `amy` pip build) and under Tulip's MicroPython on the AMYboard.
#
# Each patch is built from a few oscillators (relative indices 0..n-1) that
# are stored as an AMY "user patch" (numbers 1024-1055) and then played
# polyphonically by a synth voice. Control coefficients use AMY's dict form
# whose valid source keys are:
#     const, note, vel, eg0, eg1, mod, bend, ext0, ext1
# ext0/ext1 are the two CV inputs, so any patch can be CV-modulated.
#
# Envelope (bp0/bp1) strings are "time_ms,value" pairs; the LAST pair is the
# release segment, triggered on note-off. bp0 drives amplitude (eg0), bp1 is
# a free envelope usually routed to the filter (eg1).

# --- wave constants (mirror amy.* so this file needs no amy import) ---
SINE = 0
PULSE = 1
SAW_DOWN = 2
SAW_UP = 3
TRIANGLE = 4
NOISE = 5
KS = 6
PCM = 7
ALGO = 8
PARTIAL = 9

# --- filter constants ---
LPF = 1
BPF = 2
HPF = 3

PAD = "PAD"
BASS = "BASS"

# Standard amplitude coefficient: velocity * envelope-0.
A = {"vel": 1, "eg0": 1}


def _saw_pad(det, pan, cutoff, env_a, env_r, sweep, q=3):
    """One detuned saw layer of a pad."""
    return {
        "wave": SAW_DOWN,
        "freq": {"note": 1.0 + det},
        "amp": A,
        "filter_type": LPF,
        "resonance": q,
        "filter_freq": {"const": cutoff, "eg1": sweep},
        "bp0": "0,0," + str(env_a) + ",1," + str(env_r) + ",0",
        "bp1": "0,0,1500,1,800,0",
        "pan": pan,
    }


# Each entry: name, category, num voices, oscillator list, optional bus FX.
PATCHES = [
    # ---------------- PADS / DRONES ----------------
    {"name": "Warm Saw Pad", "cat": PAD, "voices": 4,
     "oscs": [_saw_pad(0.0, 0.22, 380, 700, 600, 1100),
              _saw_pad(0.004, 0.78, 380, 760, 640, 1100)],
     "fx": {"chorus": [0.5, 320, 0.4, 0.4], "reverb": [0.3, 0.7, 0.4, 0.5]}},

    {"name": "Wide Unison", "cat": PAD, "voices": 3,
     "oscs": [_saw_pad(-0.006, 0.10, 600, 500, 500, 900),
              _saw_pad(0.0, 0.5, 600, 540, 520, 900),
              _saw_pad(0.006, 0.90, 600, 560, 540, 900)],
     "fx": {"chorus": [0.6, 380, 0.5, 0.5], "reverb": [0.25, 0.6, 0.4, 0.5]}},

    {"name": "Dark Drone", "cat": PAD, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 0.5}, "amp": A, "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 220, "eg1": 300},
               "bp0": "0,0,1200,1,1500,0", "bp1": "0,0,3000,1,0,0", "pan": 0.4},
              {"wave": SAW_DOWN, "freq": {"note": 0.501}, "amp": A, "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 220, "eg1": 300},
               "bp0": "0,0,1300,1,1500,0", "bp1": "0,0,3000,1,0,0", "pan": 0.6}],
     "fx": {"reverb": [0.45, 0.8, 0.3, 0.5]}},

    {"name": "Glass Pad", "cat": PAD, "voices": 4,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 4, "filter_freq": {"const": 1400, "eg1": 1800},
               "bp0": "0,0,400,1,500,0", "bp1": "0,0,900,1,700,0", "pan": 0.3},
              {"wave": SINE, "freq": {"note": 2.0}, "amp": {"vel": 0.5, "eg0": 1},
               "bp0": "0,0,600,1,500,0", "pan": 0.7}],
     "fx": {"chorus": [0.4, 300, 0.6, 0.4], "reverb": [0.4, 0.7, 0.35, 0.5]}},

    {"name": "String Ens", "cat": PAD, "voices": 4,
     "oscs": [_saw_pad(0.0, 0.2, 900, 250, 400, 1500, q=2),
              _saw_pad(0.005, 0.8, 900, 280, 420, 1500, q=2)],
     "fx": {"chorus": [0.7, 360, 0.7, 0.5], "reverb": [0.35, 0.7, 0.4, 0.5]}},

    {"name": "Choir Aah", "cat": PAD, "voices": 4,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 6, "filter_freq": {"const": 700, "eg1": 1300},
               "bp0": "0,0,450,1,500,0", "bp1": "0,0,900,1,0,0", "pan": 0.3},
              {"wave": SAW_DOWN, "freq": {"note": 2.004}, "amp": {"vel": 0.4, "eg0": 1},
               "filter_type": LPF, "resonance": 6, "filter_freq": {"const": 1500, "eg1": 800},
               "bp0": "0,0,480,1,520,0", "bp1": "0,0,900,1,0,0", "pan": 0.7}],
     "fx": {"chorus": [0.5, 340, 0.5, 0.5], "reverb": [0.4, 0.75, 0.4, 0.5]}},

    {"name": "Sweep Pad", "cat": PAD, "voices": 3,
     "oscs": [_saw_pad(0.0, 0.25, 200, 600, 600, 3000, q=6),
              _saw_pad(0.004, 0.75, 200, 640, 640, 3000, q=6)],
     "fx": {"chorus": [0.4, 320, 0.4, 0.4], "reverb": [0.3, 0.7, 0.4, 0.5]}},

    {"name": "Octave Drone", "cat": PAD, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 0.5}, "amp": A, "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 500, "eg1": 700},
               "bp0": "0,0,900,1,1200,0", "bp1": "0,0,2500,1,0,0", "pan": 0.5},
              {"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": {"vel": 0.6, "eg0": 1},
               "filter_type": LPF, "resonance": 3, "filter_freq": {"const": 500, "eg1": 700},
               "bp0": "0,0,1100,1,1200,0", "bp1": "0,0,2500,1,0,0", "pan": 0.5}],
     "fx": {"reverb": [0.4, 0.8, 0.35, 0.5]}},

    {"name": "Brass Pad", "cat": PAD, "voices": 4,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 4, "filter_freq": {"const": 400, "eg1": 2200},
               "bp0": "0,0,150,1,300,0", "bp1": "0,0,300,1,400,0", "pan": 0.35},
              {"wave": SAW_DOWN, "freq": {"note": 1.005}, "amp": A, "filter_type": LPF,
               "resonance": 4, "filter_freq": {"const": 400, "eg1": 2200},
               "bp0": "0,0,170,1,320,0", "bp1": "0,0,300,1,400,0", "pan": 0.65}],
     "fx": {"chorus": [0.3, 300, 0.4, 0.3], "reverb": [0.25, 0.6, 0.4, 0.5]}},

    {"name": "Soft Sine Pad", "cat": PAD, "voices": 4,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": A,
               "bp0": "0,0,700,1,700,0", "pan": 0.3},
              {"wave": SINE, "freq": {"note": 2.003}, "amp": {"vel": 0.35, "eg0": 1},
               "bp0": "0,0,900,1,700,0", "pan": 0.7}],
     "fx": {"chorus": [0.4, 320, 0.4, 0.4], "reverb": [0.45, 0.8, 0.35, 0.5]}},

    {"name": "Reso Sweep", "cat": PAD, "voices": 3,
     "oscs": [_saw_pad(0.0, 0.3, 160, 400, 700, 2600, q=10),
              _saw_pad(0.004, 0.7, 160, 440, 720, 2600, q=10)],
     "fx": {"reverb": [0.35, 0.7, 0.4, 0.5]}},

    {"name": "Evolving", "cat": PAD, "voices": 3,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 6, "filter_freq": {"const": 250, "eg1": 1800},
               "bp0": "0,0,1500,1,1200,0", "bp1": "0,0,4000,1,0,0", "pan": 0.25},
              {"wave": PULSE, "freq": {"note": 1.003}, "duty": 0.3, "amp": A, "filter_type": LPF,
               "resonance": 6, "filter_freq": {"const": 250, "eg1": 1800},
               "bp0": "0,0,1700,1,1200,0", "bp1": "0,0,4000,1,0,0", "pan": 0.75}],
     "fx": {"chorus": [0.5, 360, 0.3, 0.5], "reverb": [0.4, 0.8, 0.35, 0.5]}},

    # ---------------- BASSES ----------------
    {"name": "Sub Sine", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": A,
               "bp0": "0,0,8,1,120,0.8,90,0", "pan": 0.5}]},

    {"name": "Deep Sub", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 0.5}, "amp": A,
               "bp0": "0,0,10,1,200,0.9,120,0", "pan": 0.5}]},

    {"name": "Mono Saw", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 4, "filter_freq": {"const": 180, "eg1": 1400},
               "bp0": "0,0,5,1,150,0.7,80,0", "bp1": "0,1,200,0.2,80,0", "pan": 0.5}]},

    {"name": "Reese", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 600, "eg1": 200},
               "bp0": "0,0,5,1,40,0", "bp1": "0,1,2000,1,0,0", "pan": 0.3},
              {"wave": SAW_DOWN, "freq": {"note": 1.012}, "amp": A, "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 600, "eg1": 200},
               "bp0": "0,0,5,1,40,0", "bp1": "0,1,2000,1,0,0", "pan": 0.7}],
     "fx": {"chorus": [0.3, 300, 0.3, 0.4]}},

    {"name": "Acid 303", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 14, "filter_freq": {"const": 150, "eg1": 2600},
               "bp0": "0,0,5,1,300,0.4,60,0", "bp1": "0,1,250,0,60,0", "pan": 0.5}]},

    {"name": "Square Bass", "cat": BASS, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.5, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 3, "filter_freq": {"const": 220, "eg1": 1200},
               "bp0": "0,0,5,1,180,0.6,80,0", "bp1": "0,1,180,0.1,80,0", "pan": 0.5}]},

    {"name": "Pluck Bass", "cat": BASS, "voices": 2,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 5, "filter_freq": {"const": 300, "eg1": 2000},
               "bp0": "0,0,3,1,250,0,120,0", "bp1": "0,1,180,0,0,0", "pan": 0.5}]},

    {"name": "Growl", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 9, "filter_freq": {"const": 200, "eg1": 900, "ext0": 1500},
               "bp0": "0,0,5,1,400,0.6,90,0", "bp1": "0,1,600,0.3,90,0", "pan": 0.5}]},

    {"name": "Punch Bass", "cat": BASS, "voices": 1,
     "oscs": [{"wave": SINE, "freq": {"note": 1.0}, "amp": A,
               "bp0": "0,0,5,1,180,0.7,90,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 2.0}, "amp": {"vel": 0.6, "eg0": 1},
               "bp0": "0,0,3,1,60,0,40,0", "pan": 0.5},
              {"wave": SINE, "freq": {"note": 3.0}, "amp": {"vel": 0.3, "eg0": 1},
               "bp0": "0,0,2,1,35,0,30,0", "pan": 0.5}]},

    {"name": "Round Tri", "cat": BASS, "voices": 1,
     "oscs": [{"wave": TRIANGLE, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 2, "filter_freq": {"const": 400, "eg1": 600},
               "bp0": "0,0,8,1,160,0.7,90,0", "bp1": "0,1,160,0.3,80,0", "pan": 0.5}]},

    {"name": "Stab Bass", "cat": BASS, "voices": 2,
     "oscs": [{"wave": SAW_DOWN, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 7, "filter_freq": {"const": 250, "eg1": 2200},
               "bp0": "0,0,4,1,120,0,60,0", "bp1": "0,1,110,0,0,0", "pan": 0.4},
              {"wave": SAW_DOWN, "freq": {"note": 0.5}, "amp": {"vel": 0.7, "eg0": 1},
               "bp0": "0,0,4,1,120,0,60,0", "pan": 0.6}]},

    {"name": "Hollow Bass", "cat": BASS, "voices": 1,
     "oscs": [{"wave": PULSE, "duty": 0.25, "freq": {"note": 1.0}, "amp": A, "filter_type": LPF,
               "resonance": 4, "filter_freq": {"const": 240, "eg1": 1000},
               "bp0": "0,0,6,1,200,0.6,90,0", "bp1": "0,1,200,0.15,80,0", "pan": 0.5}]},
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
