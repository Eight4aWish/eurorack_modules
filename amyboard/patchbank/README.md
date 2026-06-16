# AMYboard Patch Bank

A small Tulip app that turns the AMYboard into a **browse-and-play synth voice**:
spin the rotary encoder through a curated bank of **12 pads/drones + 12 basses**
on the 128×128 screen, press to load, and play from **CV/Gate** (Gate triggers
notes, V/Oct sets pitch). No menu diving — good sounds are one click away.

```
amyboard/patchbank/
  patches.py         the bank: 24 patches as portable data (no deps)
  patchbank.py       the Tulip app: encoder browser + CV/gate playing
  render_patches.py  desktop harness: renders every patch to WAV + metrics
  README.md
```

## What's verified vs. what to confirm on hardware

The **sounds are validated**. Every patch was rendered with the desktop `amy`
build and checked for level, brightness, stereo width and clean note-off
release — all 24 pass (`render_patches.py` reports `0 flagged`). `patches.py`
is pure data and runs identically on desktop CPython and Tulip MicroPython.

The **UI/IO layer** (`patchbank.py`) is written against the documented Tulip
API but couldn't be exercised without a board. Two clearly-isolated hooks are
the only places that touch board-specific APIs, so on-device tweaks (if any)
are contained:

- `read_encoder()` / `_on_key()` — maps encoder turn/press to actions. If the
  browser doesn't move, adjust the key codes to match your encoder.
- `read_cv_gate()` — reads the Gate and V/Oct jacks. Until you point it at your
  firmware's CV call, it returns "no gate" so the app is still fully usable
  from the encoder (press loads *and* auditions a note).

Also panel-dependent: `FONT` index and the grayscale palette constants at the
top — bump `FONT` if text doesn't fit 128 px.

## Try the sounds now (desktop, no board)

```sh
git clone https://github.com/shorepine/amy && (cd amy && pip install .)
cd amyboard/patchbank
python3 render_patches.py      # writes out/*.wav and prints a metrics table
```

## Run on the AMYboard

Copy this folder to the board (e.g. `/apps/patchbank`) and launch it from the
Tulip launcher, or:

```python
import tulip, patchbank
patchbank.run(tulip.UIScreen())
```

## Controls

| Action | Result |
|---|---|
| Turn encoder | Move selection up/down the list |
| Press encoder | Load highlighted patch + audition one note (`*` marks loaded) |
| Gate in (rising) | Note on at the V/Oct pitch |
| Gate in (falling) | Note off |

CV calibration constants (`CV_NOTE_BASE`, `CV_VOLTS_PER_OCT`, `GATE_VOLTS`) are
at the top of `patchbank.py`.

## How a patch works

Each entry stores a few oscillators into an AMY user-patch slot (1024+) and
plays them polyphonically. Control parameters use AMY's dict-form coefficients
whose sources are `const, note, vel, eg0, eg1, mod, bend, ext0, ext1` — note
that **`ext0`/`ext1` are the two CV inputs**, so any parameter can be
CV-modulated (e.g. the `Growl` bass routes `ext0` into its filter cutoff).
`bp0` is the amplitude envelope and `bp1` the filter envelope; in both, the
**last `time,value` pair is the release**, triggered on note-off.

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

Append it to `PATCHES`, run `render_patches.py` to check it isn't flagged
`SILENT`/`CLIP`, audition the WAV, then reload on the board.

> FM/`ALGO` patches aren't in the bank yet — AMY's operator wiring needs more
> setup than the subtractive/additive patches here. They're a clean follow-up.
