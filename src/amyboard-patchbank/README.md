# AMYboard Patch Bank

A Tulip app that turns the AMYboard into a **browse-and-play synth voice**:
spin the rotary encoder through a curated bank of **12 pads/drones + 12 basses**
on the 128×128 OLED, press to load, and play from **CV/Gate**. No menu diving —
good sounds are one click away.

```
src/amyboard-patchbank/
  pbdata.py          the bank: 24 patches as portable data (no deps)
  patchbank.py       the app: OLED + encoder browser + CV/gate playing
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
- **Sound** — `amy`. Each patch is stored into an AMY user-patch slot and the
  synth is rebuilt on load.

Tulip's sketch model: it runs the current sketch top-to-bottom once (setup) then
calls its global `loop()` every frame. So the app exposes **`setup()` + `loop()`**
rather than a blocking loop — a blocking `while` starves the USB/REPL and locks
the board. `sketch.py` is the autostart shim; `run()` is a convenience for the
REPL.

## Controls

| Action | Result |
|---|---|
| Turn encoder | Move selection up/down the list |
| Press encoder | Load highlighted patch + audition one note (`*` marks loaded) |
| Gate in (CV1, rising) | Note on at the V/Oct pitch |
| Gate in (CV1, falling) | Note off |
| V/Oct (CV0) | Sets pitch (1 V/oct, base note 60 at 0 V) |

CV calibration constants (`CV_NOTE_BASE`, `SEMIS_PER_VOLT`, `GATE_VOLTS`,
`PITCH_CH`, `GATE_CH`) are at the top of `patchbank.py`.

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

All 24 patches are pre-validated (level, brightness, stereo width, clean
note-off release); `render_patches.py` reports `0 flagged`. `pbdata.py` is pure
data and runs identically on desktop CPython and Tulip MicroPython.

## How a patch works

Each entry stores a few oscillators into an AMY user-patch slot and plays them
polyphonically. Control parameters use AMY's dict-form coefficients whose
sources are `const, note, vel, eg0, eg1, mod, bend, ext0, ext1` — note that
**`ext0`/`ext1` are the two CV inputs**, so any parameter can be CV-modulated
(e.g. the `Growl` bass routes `ext0` into its filter cutoff). `bp0` is the
amplitude envelope and `bp1` the filter envelope; in both, the **last
`time,value` pair is the release**, triggered on note-off.

### Add your own

```python
{"name": "My Pad", "cat": PAD, "voices": 4,
 "oscs": [
   {"wave": SAW_DOWN, "freq": {"note": 1.0},   "amp": A,
    "filter_type": LPF, "resonance": 4,
    "filter_freq": {"const": 400, "eg1": 1500},   # eg1 opens the filter
    "bp0": "0,0,600,1,500,0",                      # slow attack, 500ms release
    "bp1": "0,0,1200,1,800,0", "pan": 0.3},
   # detuned second layer for width: freq note 1.004, pan 0.7 ...
 ],
 "fx": {"chorus": [0.5, 320, 0.4, 0.4], "reverb": [0.3, 0.7, 0.4, 0.5]}},
```

Append it to `PATCHES` in `pbdata.py`, run `render_patches.py` to check it isn't
flagged `SILENT`/`CLIP`, audition the WAV, then reload on the board.

> FM/`ALGO` patches aren't in the bank yet — AMY's operator wiring needs more
> setup than the subtractive/additive patches here. They're a clean follow-up.
