# AMYboard Patch Bank

A Tulip app that turns the AMYboard into a **browse-and-play synth voice**:
spin the rotary encoder through a curated bank of **10 pads/drones + 10 basses**
on the 128×128 OLED, press to load, tweak with macros, and play from **CV/Gate**
or **TRS MIDI**. No menu diving — good sounds are one click away.

```
src/amyboard-patchbank/
  pbdata.py          the bank: 20 patches + macro defs as portable data (no deps)
  patchbank.py       the app: OLED + encoder UI, CV/gate + MIDI playing, macros
  sketch.py          Tulip autostart shim (the "current sketch")
  render_patches.py  desktop harness: renders every patch to WAV + metrics
  README.md
```

> **Ported to real hardware (2026-06).** This was originally written against the
> graphical *Tulip CC* API (`tulip.color`, `UIScreen`, `bg_str`, …). The
> AMYboard runs the *headless Eurorack* Tulip, which has none of that, so the
> whole UI/IO layer was rewritten to drive the hardware directly. The data file
> was renamed `patches.py` → `pbdata.py` because Tulip ships its own built-in
> `patches` module that shadowed it.

## How it runs on the AMYboard

The board's `tulip` module has no graphics or keyboard API, so `patchbank.py`
talks to the front-panel I2C accessory bus itself (**SDA=GPIO17, SCL=GPIO18,
400 kHz**):

- **OLED** — Adafruit 1.5" 128×128 grayscale SSD1327 at **0x3D**, driven via a
  small inline 4-bit driver + MicroPython `framebuf` (`GS4_HMSB`). The
  framebuffer is blitted in 256-byte chunks; a single 8 KB transaction browns
  the board out.
- **Encoder** — Adafruit Seesaw rotary at **0x36**: position from the encoder
  register, push-switch on Seesaw GPIO24 (active-low).
- **CV** — `amyboard.cv_in()`: **CV0 = 1 V/oct pitch, CV1 = gate** (matching the
  board's convention). `tulip.cv_in()` is a *different* source — don't use it.
- **Sound** — `amy`. All patches are pre-stored into distinct permanent slots at
  boot (patch *i* → slot `1024+i`); loading just points the synth at that slot.
  (Re-storing an AMY slot is fragile — it can capture an empty patch — and a
  distinct slot number forces a clean voice reload.)

Tulip's sketch model: it runs the current sketch top-to-bottom once (setup) then
calls its global `loop()` every frame. So the app exposes **`setup()` + `loop()`**
rather than a blocking loop — a blocking `while` starves the USB/REPL and locks
the board. `sketch.py` is the autostart shim; `run()` is a convenience for the
REPL.

## Controls

Two screens, navigated with the one encoder (**short press = down / tab forward,
long press = up**):

**Patch list**
| Action | Result |
|---|---|
| Turn | Scroll the patch list |
| Short press | Load the highlighted patch + audition a note, and drop into its macro page |

**Macro page** (per loaded patch — all macros on one screen)
| Action | Result |
|---|---|
| Turn | Adjust the highlighted macro, live |
| Short press | Move the highlight to the next macro |
| Long press (~½ s) | Back up to the patch list |

CV/gate keeps playing on both screens, so you tweak while it sounds.

| CV jack | Result |
|---|---|
| Gate in (CV1, rising/falling) | Note on at the V/Oct pitch / note off |
| V/Oct (CV0) | Pitch (1 V/oct, base note 60 at 0 V) |

**TRS MIDI in** also plays the loaded voice (omni, polyphonic), alongside CV/gate.
It's deliberately featherweight: the firmware parses MIDI and a tiny callback
(`_midi_cb`) only does `amy.send` note on/off (+ flush on Stop / All-Notes-Off) —
no draw, no I2C. We register it with a bare `midi.add_callback()` and **avoid
`midi.setup()`** on purpose (setup installs Tulip's per-channel router + a voices
app that fights the sketch). `MIDI_ENABLED = False` disables it. (Two TRS-MIDI
wiring standards exist; Type A is current — use a matching adapter.)

> **⚠️ Do not cascade MIDI *clock* into the board.** Tulip slaves its frame clock
> to incoming MIDI clock; if a device sends clock (e.g. Ableton Move "MIDI sync
> out"), the whole UI **freezes** whenever that clock pauses, and stays latched to
> external clock until a restart. `setup()` calls `external_midi_sync(0)` as a
> best effort, but it does not fully detach on this firmware. Keep MIDI clock
> **off** at the source and sync via **Ableton Link** instead — MIDI *notes* are
> unaffected.

## Macros

Each patch exposes up to 4 encoder-tweakable macros (auto-derived in
`_derive_macros()`), stored as normalized 0..1 values and sent live by
`apply_macro()`:

- **Filtered patches:** `TONE` (cutoff) · `RES` (resonance) · `SPACE` (reverb
  send) · `MOVE` (chorus or echo).
- **Filterless patches** (pure sine/triangle): `SPACE` · `AIR` (chorus) · `ECHO`.

Macros are **not** applied on load — the patch plays exactly as designed until you
turn one. Note the **TONE** caveat: AMY ignores a synth-level `filter_freq` that
carries a non-zero envelope (`eg1`), so turning TONE takes *manual* control of the
cutoff and replaces the patch's filter-envelope sweep (basses lose some "snap").
`RES`/`SPACE`/`MOVE` update cleanly. Both CV ins are used by pitch+gate, so macros
are encoder-only.

CV calibration constants (`CV_NOTE_BASE`, `SEMIS_PER_VOLT`, `GATE_VOLTS`,
`PITCH_CH`, `GATE_CH`) and macro tuning (`MACRO_STEP`, `LONG_MS`) are at the top of
`patchbank.py`.

## Deploying to the board

Tulip on the AMYboard is MicroPython over a native USB-CDC REPL. `mpremote`
fights this board (it toggles the reset lines on connect/close and drops the
CDC), so copy files over the raw REPL instead, into `/user`:

```
/user/pbdata.py
/user/patchbank.py
/user/current/sketch.py   # makes patchbank the boot app
```

On boot Tulip runs `/user/current/sketch.py`, which adds `/user` to the path,
calls `patchbank.setup()`, and hands `loop` to Tulip.

## Try the sounds now (desktop, no board)

```sh
git clone https://github.com/shorepine/amy && (cd amy && pip install .)
cd src/amyboard-patchbank
python3 render_patches.py      # writes out/*.wav and prints a metrics table
```

All 20 patches are pre-validated (level, brightness, stereo width, clean
note-off release); `render_patches.py` reports `0 flagged`. `pbdata.py` is pure
data and runs identically on desktop CPython and Tulip MicroPython.

## How a patch works

Each entry is a few oscillators played polyphonically (stored as an AMY user
patch). Control parameters use AMY's dict-form coefficients whose
sources are `const, note, vel, eg0, eg1, mod, bend, ext0, ext1` — note that
**`ext0`/`ext1` are the two CV inputs**, so any parameter can be CV-modulated
(e.g. the `Growl` bass routes `ext0` into its filter cutoff). `bp0` is the
amplitude envelope and `bp1` the filter envelope; in both, the **last
`time,value` pair is the release**, triggered on note-off.

### Add your own

```python
{"name": "My Pad", "cat": PAD, "voices": 4,
 "oscs": [
   {"wave": SAW_DOWN, "freq": {"note": 1.0},   "amp": _amp(0.7),
    "filter_type": LPF, "resonance": 4,
    "filter_freq": {"const": 400, "eg1": 1500},   # eg1 opens the filter
    "bp0": "0,0,600,1,500,0",                      # slow attack, 500ms release
    "bp1": "0,0,1200,1,800,0", "pan": 0.3},
   # detuned second layer for width: freq note 1.004, pan 0.7 ...
 ],
 "fx": {"chorus": [0.5, 320, 0.4, 0.4], "reverb": [0.3, 0.7, 0.4, 0.5]}},
```

`_amp(g)` is the velocity·envelope amplitude at gain `g`; macros are auto-derived
from each patch (`_derive_macros`), so a new entry gets TONE/RES/SPACE/MOVE for
free. Append it to `PATCHES` in `pbdata.py`, run `render_patches.py` to check it
isn't flagged `SILENT`/`CLIP`, audition the WAV, then reload on the board.

> FM/`ALGO` patches aren't in the bank yet — AMY's operator wiring needs more
> setup than the subtractive/additive patches here. They're a clean follow-up.
