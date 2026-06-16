# patchbank -- a Tulip app for the AMYboard: browse a curated bank of
# pad/drone + bass patches with the rotary encoder and play them from CV/gate.
#
#   * Turn encoder  -> move the selection up/down the list
#   * Press encoder -> load the highlighted patch (and audition one note)
#   * CV/Gate in    -> Gate triggers notes, V/Oct sets pitch on the loaded patch
#
# The *sounds* (patches.py) are validated on desktop AMY. The pieces that can
# only be confirmed on real hardware are deliberately isolated in the two
# hooks `read_encoder()` and `read_cv_gate()` below -- if your encoder sends
# different key codes, or CV is exposed under a different call, you only edit
# those two spots.
#
# Drop this folder on the AMYboard (e.g. /apps/patchbank) and run it from the
# Tulip launcher, or `import patchbank; patchbank.run(tulip.UIScreen())`.

import tulip
import amy

import patches as bank

# --- AMY voice/slot configuration ---------------------------------------
WORK_SLOT = 1024          # user-patch slot we restore each selection into
SYNTH = 1                 # AMY synth id used for note management
AUDITION_NOTE = 45        # note played when you press to load (bass-ish)

# --- CV calibration (AMYboard CV in is -10..+10 V) ----------------------
GATE_VOLTS = 1.0          # gate considered "high" above this many volts
CV_NOTE_BASE = 36         # MIDI note at 0 V on the pitch CV
CV_VOLTS_PER_OCT = 1.0    # 1 V/oct

# --- display palette (grayscale) ----------------------------------------
BLACK = tulip.color(0, 0, 0)
DIM = tulip.color(90, 90, 90)
MID = tulip.color(160, 160, 160)
WHITE = tulip.color(255, 255, 255)
FONT = 5                  # small readable font; bump if it doesn't fit 128px

# rows of the list that fit on the 128x128 panel below the header
VISIBLE = 8
ROW_H = 12

_state = {
    "sel": 0,            # highlighted patch index
    "loaded": -1,        # currently loaded patch index
    "top": 0,            # first visible row (scroll offset)
    "note": None,        # currently sounding note (from gate), or None
    "gate": False,       # last gate state, for edge detection
}


# ========================================================================
# Hardware hooks -- the only two functions that touch board-specific APIs.
# ========================================================================
def read_encoder():
    """Return one of: 'up', 'down', 'press', or None for this frame.

    Tulip delivers encoder turns/press as keyboard events. The default codes
    below are the common mapping; adjust to match the encoder you wired if
    the browser doesn't move. (See tulip docs: keyboard_callback / m5_8encoder.)
    """
    ev = _state.pop("_key", None)
    return ev


def read_cv_gate():
    """Return (gate_high: bool, note: float).

    AMYboard routes its two CV jacks to AMY's ext0/ext1 coefficients; here we
    also read the raw value to drive gate/pitch in Python. Replace the body
    with the real AMYboard CV read for your firmware build. Until then this
    returns (False, base) so the app is fully usable from the encoder alone.
    """
    try:
        # Expected on AMYboard firmware; guarded so the app still runs if absent.
        gate_v = tulip.cv_in(0)      # gate / trigger jack, volts
        pitch_v = tulip.cv_in(1)     # V/Oct jack, volts
    except Exception:
        return (False, CV_NOTE_BASE)
    note = CV_NOTE_BASE + 12.0 * (pitch_v / CV_VOLTS_PER_OCT)
    return (gate_v > GATE_VOLTS, note)


def _on_key(k):
    # Map raw key codes to encoder actions. Tweak to match your encoder.
    if k in (259, ord("k"), ord("w")):       # up / CCW
        _state["_key"] = "up"
    elif k in (258, ord("j"), ord("s")):     # down / CW
        _state["_key"] = "down"
    elif k in (10, 13, 32, ord("\r")):       # enter / button press
        _state["_key"] = "press"


# ========================================================================
# Patch loading + playback
# ========================================================================
def load_selected():
    p = bank.PATCHES[_state["sel"]]
    # stop anything sounding on the old voice
    if _state["note"] is not None:
        amy.send(synth=SYNTH, note=_state["note"], vel=0)
        _state["note"] = None
    bank.store_patch(amy, p, WORK_SLOT)
    amy.send(synth=SYNTH, num_voices=p["voices"], patch=WORK_SLOT)
    bank.apply_fx(amy, p)
    _state["loaded"] = _state["sel"]
    # audition so you hear it the moment you press
    amy.send(synth=SYNTH, note=AUDITION_NOTE, vel=0.9)
    tulip.defer(_audition_off, AUDITION_NOTE, 700)


def _audition_off(note):
    # only release the audition note if the gate isn't actively holding one
    if not _state["gate"]:
        amy.send(synth=SYNTH, note=note, vel=0)


# ========================================================================
# Drawing
# ========================================================================
def draw():
    w, h = tulip.screen_size()
    tulip.bg_clear(BLACK)
    sel = _state["sel"]
    cat = bank.PATCHES[sel]["cat"]
    tulip.bg_str("PATCH BANK  " + cat, 4, 2, MID, FONT)
    tulip.bg_rect(0, 14, w, 1, DIM, 1)

    # keep the selection in view
    if sel < _state["top"]:
        _state["top"] = sel
    elif sel >= _state["top"] + VISIBLE:
        _state["top"] = sel - VISIBLE + 1

    y = 18
    for i in range(_state["top"], min(len(bank.PATCHES), _state["top"] + VISIBLE)):
        p = bank.PATCHES[i]
        if i == sel:
            tulip.bg_rect(0, y - 1, w, ROW_H, DIM, 1)
        mark = "*" if i == _state["loaded"] else " "
        col = WHITE if i == sel else MID
        tulip.bg_str(mark + p["name"], 4, y, col, FONT)
        y += ROW_H

    # footer: loaded patch + play indicator
    tulip.bg_rect(0, h - 14, w, 1, DIM, 1)
    if _state["loaded"] >= 0:
        name = bank.PATCHES[_state["loaded"]]["name"]
    else:
        name = "(press to load)"
    play = chr(0x10) if _state["note"] is not None else " "
    tulip.bg_str(play + " " + name, 4, h - 11, MID, FONT)


# ========================================================================
# Per-frame update: encoder + CV/gate + redraw
# ========================================================================
def tick(_data):
    act = read_encoder()
    if act == "up":
        _state["sel"] = (_state["sel"] - 1) % len(bank.PATCHES)
    elif act == "down":
        _state["sel"] = (_state["sel"] + 1) % len(bank.PATCHES)
    elif act == "press":
        load_selected()

    # CV/gate playing on the loaded patch
    if _state["loaded"] >= 0:
        gate, note = read_cv_gate()
        if gate and not _state["gate"]:                  # rising edge: note on
            n = int(round(note))
            amy.send(synth=SYNTH, note=n, vel=0.9)
            _state["note"] = n
        elif not gate and _state["gate"]:                # falling edge: note off
            if _state["note"] is not None:
                amy.send(synth=SYNTH, note=_state["note"], vel=0)
                _state["note"] = None
        _state["gate"] = gate

    draw()


# ========================================================================
# App lifecycle
# ========================================================================
def _activate():
    tulip.keyboard_callback(_on_key)
    tulip.frame_callback(tick, {})
    draw()


def _deactivate():
    tulip.frame_callback()          # stop updating when switched away
    tulip.keyboard_callback()


def _quit():
    if _state["note"] is not None:
        amy.send(synth=SYNTH, note=_state["note"], vel=0)
    tulip.frame_callback()
    tulip.keyboard_callback()


def run(screen):
    screen.bg_color = BLACK
    screen.activate_callback = _activate
    screen.deactivate_callback = _deactivate
    screen.quit_callback = _quit
    screen.present()
    _activate()
