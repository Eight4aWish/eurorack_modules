# patchbank -- an AMYboard app: browse a curated bank of pad/drone + bass
# patches with the rotary encoder and play them from CV/Gate.
#
#   * Turn encoder  -> move the selection up/down the list
#   * Press encoder -> load the highlighted patch (and audition one note)
#   * CV / Gate in  -> CV0 = V/Oct pitch, CV1 = gate (rising edge plays a note)
#
# Ported to the *headless Eurorack* Tulip on the shorepine AMYboard. That Tulip
# has no graphics/keyboard API, so the display (SSD1327 OLED, I2C 0x3D) and the
# rotary encoder (Adafruit Seesaw, I2C 0x36) are driven directly over the front
# I2C accessory bus (SDA=GPIO17, SCL=GPIO18, 400 kHz). CV comes from
# amyboard.cv_in(); sound from amy. Bank data is pbdata.py (renamed from
# patches.py to avoid clashing with Tulip's built-in `patches`).
#
# Tulip runs a sketch by executing it once (setup) then calling its global
# loop() every frame. So this module exposes setup() + loop(); the autostart
# shim is sketch.py. run() is a convenience for driving it from the REPL.

import time
import struct
import framebuf
from machine import Pin, I2C

import amy
import amyboard
import pbdata as bank

# --- AMY voice/slot configuration ---------------------------------------
SYNTH = 1                 # AMY synth id used for note management
AUDITION_NOTE = 45        # note played when you press to load (bass-ish)
AUDITION_MS = 700         # how long the audition note holds
_SLOTS = (1024, 1025)     # alternate user-patch slots so AMY reloads the voice

# --- CV calibration (AMYboard CV in is in volts) ------------------------
# Matches the board's working convention: CV0 = 1V/oct pitch, CV1 = gate.
PITCH_CH = 0
GATE_CH = 1
GATE_VOLTS = 1.0          # gate considered "high" at/above this many volts
CV_NOTE_BASE = 60         # MIDI note at 0 V on the pitch CV
SEMIS_PER_VOLT = 12.0     # 1 V/oct

# --- I2C bus + device addresses -----------------------------------------
I2C_SCL = 18
I2C_SDA = 17
OLED_ADDR = 0x3D
ENC_ADDR = 0x36

# --- display layout (128x128, 4-bit grayscale) --------------------------
W = 128
H = 128
WHITE = 15
GRAY = 9
DIM = 5
SELBAR = 4
ROW_H = 13
LIST_Y = 16
VISIBLE = 7
FOOT_Y = 110              # footer text baseline (region redrawn on gate)

# --- macro editing ------------------------------------------------------
MACRO_STEP = 0.04         # normalized change per encoder detent
LONG_MS = 450             # press >= this is a long press (go up a level)

_i2c = None
_buf = None
_fb = None

_state = {
    "sel": 0,             # highlighted patch index
    "loaded": -1,         # currently loaded patch index
    "top": 0,             # first visible row (scroll offset)
    "slot": 0,            # index into _SLOTS for the next load
    "note": None,         # currently sounding note, or None
    "gate": False,        # last gate state, for edge detection
    "cv_note": CV_NOTE_BASE,  # note last set by CV pitch while gated
    "audition_off": 0,    # ticks_ms deadline to release the audition note
    "enc": 0,             # last encoder position
    "btn": False,         # last button state
    "page": "list",       # "list" or "macro"
    "mfield": 0,          # selected macro index on the macro pages
    "macros": [],         # loaded patch's macro metadata
    "mvals": [],          # normalized 0..1 value per macro
    "btn_t": 0,           # ticks_ms at button-down (for short/long detect)
    "btn_long": False,    # long press already fired this hold
}


# ========================================================================
# SSD1327 OLED driver (128x128, 4bpp, I2C)
# ========================================================================
_INIT = (
    (0xFD, b"\x12"), (0xAE, b""), (0xA8, b"\x7f"), (0xA1, b"\x00"),
    (0xA2, b"\x00"), (0xA0, b"\x51"), (0xAB, b"\x01"), (0x81, b"\x53"),
    (0xB1, b"\x51"), (0xB3, b"\x01"), (0xB9, b""), (0xBC, b"\x08"),
    (0xBE, b"\x07"), (0xB6, b"\x01"), (0xD5, b"\x62"), (0xA4, b""), (0xAF, b""),
)


def _cmd(c, params=b""):
    _i2c.writeto(OLED_ADDR, bytes([0x00, c]) + params)


def _oled_init():
    for c, p in _INIT:
        _cmd(c, p)
        time.sleep_ms(2)


def _show(y0=0, y1=127):
    # Blit framebuffer rows y0..y1 in small chunks (a single 8 KB transaction
    # browns the board out; 256-byte chunks are safe).
    _cmd(0x15, b"\x00\x3f")                 # columns 0..63 (2 px per byte)
    _cmd(0x75, bytes([y0, y1]))             # row window
    mv = memoryview(_buf)
    i = y0 * 64
    end = (y1 + 1) * 64
    while i < end:
        _i2c.writeto(OLED_ADDR, b"\x40" + bytes(mv[i:i + 256]))
        i += 256


# ========================================================================
# Adafruit Seesaw rotary encoder driver (position + push switch on GPIO24)
# ========================================================================
_SS_GPIO = 0x01
_SS_ENC = 0x11
_BTN_MASK = b"\x01\x00\x00\x00"             # 1 << 24, big-endian


def _enc_init():
    _i2c.writeto(ENC_ADDR, bytes([_SS_GPIO, 0x03]) + _BTN_MASK)   # DIRCLR -> input
    _i2c.writeto(ENC_ADDR, bytes([_SS_GPIO, 0x0B]) + _BTN_MASK)   # PULLENSET
    _i2c.writeto(ENC_ADDR, bytes([_SS_GPIO, 0x05]) + _BTN_MASK)   # BULK_SET -> pull up


def _enc_pos():
    _i2c.writeto(ENC_ADDR, bytes([_SS_ENC, 0x30]))
    time.sleep_us(300)
    return struct.unpack(">i", _i2c.readfrom(ENC_ADDR, 4))[0]


def _enc_btn():
    _i2c.writeto(ENC_ADDR, bytes([_SS_GPIO, 0x04]))
    time.sleep_us(300)
    v = struct.unpack(">I", _i2c.readfrom(ENC_ADDR, 4))[0]
    return ((v >> 24) & 1) == 0             # active-low: pressed when bit clear


# ========================================================================
# CV / gate
# ========================================================================
def _cv_to_note(volts):
    n = int(round(CV_NOTE_BASE + volts * SEMIS_PER_VOLT))
    return max(0, min(127, n))


def read_cv_gate():
    """Return (gate_high: bool, note: int) from the CV inputs."""
    try:
        pitch_v = amyboard.cv_in(PITCH_CH)
        gate_v = amyboard.cv_in(GATE_CH)
    except Exception:
        return (False, _state["cv_note"])
    return (gate_v >= GATE_VOLTS, _cv_to_note(pitch_v))


# ========================================================================
# Patch loading + playback
# ========================================================================
def _all_notes_off():
    if _state["note"] is not None:
        amy.send(synth=SYNTH, note=_state["note"], vel=0)
        _state["note"] = None


def _note_on(n, vel=0.9):
    # always release whatever is currently sounding first, so a gate arriving
    # mid-audition (or a retrigger) never leaves an orphaned note droning
    _all_notes_off()
    amy.send(synth=SYNTH, note=n, vel=vel)
    _state["note"] = n


def load_selected():
    p = bank.PATCHES[_state["sel"]]
    _all_notes_off()
    # tear the synth down and reload into a *different* slot, so AMY actually
    # rebuilds the voice (re-storing the same slot number is treated as a no-op)
    amy.send(synth=SYNTH, num_voices=0)
    _state["slot"] ^= 1
    slot = _SLOTS[_state["slot"]]
    bank.store_patch(amy, p, slot)
    amy.send(synth=SYNTH, num_voices=p["voices"], patch=slot)
    amy.send(synth=SYNTH, grab_midi_notes=0)
    bank.apply_fx(amy, p)
    _state["loaded"] = _state["sel"]
    # set up this patch's macros (values shown but NOT applied -- the patch
    # plays exactly as designed until you actually turn a macro)
    _state["macros"] = p.get("macros", [])
    _state["mvals"] = [m["init"] for m in _state["macros"]]
    _state["mfield"] = 0
    _state["page"] = "macro"
    # audition so you hear it the moment you press
    _note_on(AUDITION_NOTE)
    _state["audition_off"] = time.ticks_add(time.ticks_ms(), AUDITION_MS)


# ========================================================================
# Drawing
# ========================================================================
def _draw_list():
    _fb.fill(0)
    sel = _state["sel"]
    cat = bank.PATCHES[sel]["cat"]
    _fb.text("BANK " + cat, 2, 1, WHITE)
    _fb.hline(0, 11, W, DIM)

    if sel < _state["top"]:
        _state["top"] = sel
    elif sel >= _state["top"] + VISIBLE:
        _state["top"] = sel - VISIBLE + 1

    y = LIST_Y
    last = min(len(bank.PATCHES), _state["top"] + VISIBLE)
    for i in range(_state["top"], last):
        p = bank.PATCHES[i]
        if i == sel:
            _fb.fill_rect(0, y - 2, W, ROW_H, SELBAR)
        mark = "*" if i == _state["loaded"] else " "
        col = WHITE if i == sel else GRAY
        _fb.text(mark + p["name"], 2, y, col)
        y += ROW_H
    _draw_footer()
    _show()


def _draw_footer():
    _fb.fill_rect(0, FOOT_Y - 3, W, H - (FOOT_Y - 3), 0)
    _fb.hline(0, FOOT_Y - 4, W, DIM)
    if _state["loaded"] >= 0:
        name = bank.PATCHES[_state["loaded"]]["name"]
    else:
        name = "press to load"
    play = chr(0x10) if _state["note"] is not None else " "
    _fb.text(play + " " + name, 2, FOOT_Y, GRAY)


def _show_footer():
    _draw_footer()
    _show(FOOT_Y - 4, H - 1)


def _draw_macros():
    _fb.fill(0)
    name = bank.PATCHES[_state["loaded"]]["name"] if _state["loaded"] >= 0 else "--"
    _fb.text(name, 2, 1, WHITE)
    _fb.hline(0, 11, W, DIM)

    macros = _state["macros"]
    mvals = _state["mvals"]
    sel = _state["mfield"]
    pg = sel // 2
    y = 24
    for i in range(pg * 2, min(len(macros), pg * 2 + 2)):
        is_sel = (i == sel)
        if is_sel:
            _fb.fill_rect(0, y - 4, W, 36, SELBAR)
        col = WHITE if is_sel else GRAY
        _fb.text(macros[i]["name"], 6, y, col)
        bx, by, bw, bh = 6, y + 14, W - 12, 9
        _fb.rect(bx, by, bw, bh, col)
        fillw = int((bw - 2) * mvals[i])
        if fillw > 0:
            _fb.fill_rect(bx + 1, by + 1, fillw, bh - 2, col)
        y += 44

    _fb.hline(0, H - 14, W, DIM)
    npages = (len(macros) + 1) // 2
    _fb.text("P%d/%d  hold=up" % (pg + 1, npages), 2, H - 11, GRAY)
    _show()


# ========================================================================
# Lifecycle: setup() once, loop() every frame (Tulip sketch model)
# ========================================================================
def _hw_init():
    global _i2c, _buf, _fb
    _i2c = I2C(0, scl=Pin(I2C_SCL), sda=Pin(I2C_SDA), freq=400000)
    _buf = bytearray(W * H // 2)
    _fb = framebuf.FrameBuffer(_buf, W, H, framebuf.GS4_HMSB)
    _oled_init()
    _enc_init()


def setup():
    _hw_init()
    amy.send(volume=getattr(bank, "VOLUME", 1.0))
    _state["enc"] = _enc_pos()
    _state["btn"] = _enc_btn()
    _state["gate"] = False
    _draw_list()


def loop():
    n = len(bank.PATCHES)
    relist = False
    refoot = False
    remac = False

    try:
        p = _enc_pos()
        b = _enc_btn()
    except Exception:
        return
    now = time.ticks_ms()

    # encoder turn: scroll the list, or adjust the selected macro live
    d = p - _state["enc"]
    if d:
        _state["enc"] = p
        if _state["page"] == "list":
            _state["sel"] = (_state["sel"] + d) % n
            relist = True
        elif _state["macros"]:
            i = _state["mfield"]
            v = _state["mvals"][i] + d * MACRO_STEP
            v = 0.0 if v < 0 else 1.0 if v > 1 else v
            _state["mvals"][i] = v
            bank.apply_macro(amy, bank.PATCHES[_state["loaded"]], SYNTH,
                             _state["macros"][i], v)
            remac = True

    # button: short press = down / tab forward, long press = up a level
    if b and not _state["btn"]:
        _state["btn_t"] = now
        _state["btn_long"] = False
    if (b and not _state["btn_long"]
            and time.ticks_diff(now, _state["btn_t"]) >= LONG_MS):
        _state["btn_long"] = True
        if _state["page"] == "macro":
            _state["page"] = "list"
            relist = True
    if (not b) and _state["btn"]:
        if not _state["btn_long"]:
            if _state["page"] == "list":
                load_selected()                    # loads + descends to macros
                remac = True
            elif _state["macros"]:
                _state["mfield"] = (_state["mfield"] + 1) % len(_state["macros"])
                remac = True
    _state["btn"] = b

    # release the audition note once its timer expires (unless a gate holds one)
    if (_state["audition_off"] and not _state["gate"]
            and time.ticks_diff(time.ticks_ms(), _state["audition_off"]) >= 0):
        _state["audition_off"] = 0
        if _state["note"] == AUDITION_NOTE:
            _all_notes_off()
            refoot = True

    # CV/gate playing on the loaded patch
    if _state["loaded"] >= 0:
        gate, note = read_cv_gate()
        if gate and not _state["gate"]:                   # rising edge
            _state["audition_off"] = 0                     # cancel pending audition
            _note_on(note)                                 # releases audition/old note
            _state["cv_note"] = note
            refoot = True
        elif gate and note != _state["cv_note"]:          # pitch moved while held
            _note_on(note)
            _state["cv_note"] = note
        elif not gate and _state["gate"]:                 # falling edge
            _all_notes_off()
            refoot = True
        _state["gate"] = gate

    # render the active page
    if _state["page"] == "list":
        if relist:
            _draw_list()
        elif refoot:
            _show_footer()
    elif remac or relist:
        _draw_macros()


def run():
    """Drive the app from the REPL (blocking). Ctrl-C to stop. The board's
    autostart path uses setup()+loop() via sketch.py instead."""
    setup()
    print("patchbank running -- turn/press the encoder; Ctrl-C to stop")
    try:
        while True:
            loop()
            time.sleep_ms(12)
    except KeyboardInterrupt:
        _all_notes_off()
        print("patchbank stopped")
