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
BASE_SLOT = 1024          # patch i lives in AMY user-patch slot BASE_SLOT + i

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
    "bank": 0,            # selected bank index (into bank.BANKS)
    "sel": 0,             # highlighted patch index WITHIN the current bank
    "loaded": -1,         # currently loaded patch (global PATCHES index)
    "top": 0,             # first visible row (scroll offset)
    "note": None,         # currently sounding note, or None
    "gate": False,        # last gate state, for edge detection
    "cv_note": CV_NOTE_BASE,  # note last set by CV pitch while gated
    "audition_off": 0,    # ticks_ms deadline to release the audition note
    "enc": 0,             # last encoder position
    "btn": False,         # last button state
    "page": "bank",       # "bank" | "list" | "macro"
    "mfield": 0,          # selected macro index on the macro pages
    "macros": [],         # loaded patch's macro metadata
    "mvals": [],          # normalized 0..1 value per macro
    "btn_t": 0,           # ticks_ms at button-down (for short/long detect)
    "btn_long": False,    # long press already fired this hold
    "load_sel": 0,        # selection latched at press-down (ignores press jitter)
    "cv_t": 0,            # ticks_ms of last CV poll (throttled)
}

# --- loop tuning --------------------------------------------------------
CV_POLL_MS = 6            # min ms between CV/gate reads (caps I2C traffic)


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
# TRS MIDI in -- plays the loaded voice (synth 1) polyphonically alongside
# CV/gate. The byte parsing is done in firmware; this callback only does fast
# amy.send() note calls (no draw / no I2C), so it can't stall the UI loop.
# ========================================================================
MIDI_ENABLED = True


def _flush_notes():
    """Release everything sounding (CV, audition, and any stuck MIDI notes)."""
    try:
        amy.reset(amy.RESET_ALL_NOTES)
    except Exception:
        pass
    _state["note"] = None
    _state["gate"] = False


def _midi_cb(m):
    if not m:
        return
    if m[0] == 0xFC:                                # MIDI Stop -> flush
        _flush_notes()
        return
    if len(m) < 3:
        return
    status = m[0] & 0xF0
    if status == 0x90 and m[2] > 0:                 # note on
        amy.send(synth=SYNTH, note=m[1] + _pitch_xpose(), vel=m[2] / 127.0)
    elif status == 0x80 or (status == 0x90 and m[2] == 0):   # note off
        amy.send(synth=SYNTH, note=m[1] + _pitch_xpose(), vel=0)
    elif status == 0xB0 and m[1] in (120, 123):     # all sound / all notes off
        _flush_notes()


def _midi_setup():
    if not MIDI_ENABLED:
        return
    try:
        import midi
        # The firmware MIDI input is armed at boot and dispatches to
        # midi.MIDI_CALLBACKS, so registering our handler is all that's needed.
        # We deliberately do NOT call midi.setup(): it installs Tulip's
        # per-channel router + "voices" app, which runs per message and starves
        # the sketch loop -- the encoder/UI freezes once MIDI flows. A bare
        # add_callback keeps MIDI featherweight and the UI responsive.
        midi.add_callback(_midi_cb)
    except Exception as e:
        print("patchbank: MIDI in unavailable:", e)


# ========================================================================
# Patch loading + playback
# ========================================================================
def _all_notes_off():
    if _state["note"] is not None:
        amy.send(synth=SYNTH, note=_state["note"], vel=0)
        _state["note"] = None


def _pitch_xpose():
    # semitone offset from the loaded patch's PITCH macro (drum/perc), else 0
    for i, m in enumerate(_state["macros"]):
        if m.get("kind") == "pitch":
            v = _state["mvals"][i]
            return int(round(m["min"] + (m["max"] - m["min"]) * v))
    return 0


def _note_on(n, vel=0.9):
    # always release whatever is currently sounding first, so a gate arriving
    # mid-audition (or a retrigger) never leaves an orphaned note droning
    _all_notes_off()
    n = n + _pitch_xpose()
    amy.send(synth=SYNTH, note=n, vel=vel)
    _state["note"] = n


def _bank_name():
    return bank.BANKS[_state["bank"]]


def _bank_patches():
    """Global PATCHES indices in the currently-selected bank (may be empty)."""
    return bank.BANK_INDEX[_bank_name()]


def load_selected():
    bp = _bank_patches()
    if not bp:
        return
    sel = _state["sel"]
    if sel >= len(bp):
        sel = len(bp) - 1
    g = bp[sel]                          # global patch index
    p = bank.PATCHES[g]
    _all_notes_off()
    # every patch is pre-stored in its own permanent slot at setup(); just point
    # the synth at this patch's slot. Re-storing slots is fragile in AMY (it can
    # capture an empty patch), and a distinct slot number forces a clean reload.
    amy.send(synth=SYNTH, num_voices=0)
    amy.send(synth=SYNTH, num_voices=p["voices"], patch=BASE_SLOT + g)
    amy.send(synth=SYNTH, grab_midi_notes=0)
    bank.apply_fx(amy, p)
    _state["loaded"] = g
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
def _draw_bank():
    _fb.fill(0)
    _fb.text("BANKS", 2, 1, WHITE)
    _fb.hline(0, 11, W, DIM)
    y = LIST_Y
    for i, bnk in enumerate(bank.BANKS):
        cnt = len(bank.BANK_INDEX[bnk])
        if i == _state["bank"]:
            _fb.fill_rect(0, y - 2, W, ROW_H, SELBAR)
        col = WHITE if i == _state["bank"] else (GRAY if cnt else DIM)
        _fb.text("%-9s %d" % (bnk, cnt), 2, y, col)
        y += ROW_H
    _fb.text("press = open", 2, H - 10, DIM)
    _show()


def _draw_list():
    _fb.fill(0)
    bp = _bank_patches()
    _fb.text(_bank_name(), 2, 1, WHITE)
    _fb.hline(0, 11, W, DIM)
    if not bp:
        _fb.text("(empty)", 4, LIST_Y, DIM)
        _draw_footer()
        _show()
        return

    sel = _state["sel"]
    if sel < _state["top"]:
        _state["top"] = sel
    elif sel >= _state["top"] + VISIBLE:
        _state["top"] = sel - VISIBLE + 1

    y = LIST_Y
    last = min(len(bp), _state["top"] + VISIBLE)
    for k in range(_state["top"], last):
        g = bp[k]
        p = bank.PATCHES[g]
        if k == sel:
            _fb.fill_rect(0, y - 2, W, ROW_H, SELBAR)
        mark = "*" if g == _state["loaded"] else " "
        col = WHITE if k == sel else GRAY
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
    _fb.text(name, 2, 2, WHITE)
    _fb.hline(0, 12, W, DIM)

    macros = _state["macros"]
    mvals = _state["mvals"]
    sel = _state["mfield"]
    # all macros on one screen: name on the left, value bar on the right
    y = 24
    for i in range(len(macros)):
        is_sel = (i == sel)
        if is_sel:
            _fb.fill_rect(0, y - 4, W, 18, SELBAR)
        col = WHITE if is_sel else GRAY
        _fb.text(macros[i]["name"], 4, y, col)
        bx, by, bw, bh = 50, y - 1, 72, 10
        _fb.rect(bx, by, bw, bh, col)
        fillw = int((bw - 2) * mvals[i])
        if fillw > 0:
            _fb.fill_rect(bx + 1, by + 1, fillw, bh - 2, col)
        y += 22

    _fb.text("hold = back", 2, H - 10, DIM)
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


def _free_run_clock():
    # Best-effort: ask Tulip not to slave its timebase to external MIDI clock.
    # NB: on this firmware it does NOT fully detach -- if a device sends MIDI
    # *clock* (e.g. Ableton "MIDI sync out"), Tulip locks its frame clock to it
    # and the whole UI freezes whenever that clock pauses. Workaround: don't
    # cascade MIDI clock into the board (drive sync via Ableton Link instead);
    # MIDI notes are unaffected. See README.
    try:
        import tulip
        tulip.external_midi_sync(0)
    except Exception:
        pass


def setup():
    _hw_init()
    amy.send(volume=getattr(bank, "VOLUME", 1.0))
    # pre-store every patch in its own permanent AMY slot (BASE_SLOT + index);
    # loading then just reassigns the synth to a slot (see load_selected)
    for i, p in enumerate(bank.PATCHES):
        bank.store_patch(amy, p, BASE_SLOT + i)
    _midi_setup()
    _free_run_clock()
    _state["enc"] = _enc_pos()
    _state["btn"] = _enc_btn()
    _state["gate"] = False
    _draw_bank()


def loop():
    relist = False
    refoot = False
    remac = False
    rebank = False

    try:
        p = _enc_pos()
        b = _enc_btn()
    except Exception:
        return
    now = time.ticks_ms()
    page = _state["page"]                 # page at the start of this frame
    prev_sel = _state["sel"]              # selection before this frame's turn

    # encoder turn: scroll banks / patches, or adjust the selected macro live
    d = p - _state["enc"]
    if d:
        _state["enc"] = p
        if page == "bank":
            _state["bank"] = (_state["bank"] + d) % len(bank.BANKS)
            rebank = True
        elif page == "list":
            bp = _bank_patches()
            if bp:
                _state["sel"] = (_state["sel"] + d) % len(bp)
                relist = True
        elif _state["macros"]:
            i = _state["mfield"]
            v = _state["mvals"][i] + d * MACRO_STEP
            v = 0.0 if v < 0 else 1.0 if v > 1 else v
            _state["mvals"][i] = v
            bank.apply_macro(amy, bank.PATCHES[_state["loaded"]], SYNTH,
                             _state["macros"][i], v)
            remac = True

    # button: short press = down / into, long press = up a level
    if b and not _state["btn"]:
        _state["btn_t"] = now
        _state["btn_long"] = False
        # latch the selection as it was BEFORE this frame -- pressing the encoder
        # shaft can nudge it a detent, which otherwise loads a neighbouring patch
        _state["load_sel"] = prev_sel
    if (b and not _state["btn_long"]
            and time.ticks_diff(now, _state["btn_t"]) >= LONG_MS):
        _state["btn_long"] = True
        if page == "macro":
            _state["page"] = "list"
            relist = True
        elif page == "list":
            _state["page"] = "bank"
            rebank = True
    if (not b) and _state["btn"]:
        if not _state["btn_long"]:
            if page == "bank":
                _state["page"] = "list"      # open the highlighted bank
                _state["sel"] = 0
                _state["top"] = 0
                relist = True
            elif page == "list":
                if _bank_patches():
                    _state["sel"] = _state["load_sel"]   # discard press-jitter
                    load_selected()                      # loads + descends to macros
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

    # CV/gate playing on the loaded patch (throttled to cap I2C traffic)
    if _state["loaded"] >= 0 and time.ticks_diff(now, _state["cv_t"]) >= CV_POLL_MS:
        _state["cv_t"] = now
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
    pg = _state["page"]
    if pg == "bank":
        if rebank:
            _draw_bank()
    elif pg == "list":
        if relist or rebank:
            _draw_list()
        elif refoot:
            _show_footer()
    elif pg == "macro":
        if remac:
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
