# Teensy 4.1 — `teensy_move`: Boy

Boy on the analogue board Rev A (Teensy 4.1, `teensy_core_midi`), in a firmware with modes. It took
over `teensy_move` on 2026-11-01.

The old Boy, on its hand-built hardware with the Teensy Expander, is now
[`teensy_move_old`](TEENSY_MOVE_OLD.md). Once the new Boy has taken over, the old one is
decommissioned and its code archived or deleted, when David says.

The board, its pin map, the chips and the start-up rules are in
[`eurorack_electronics/docs/analogue_board_reva_firmware.md`](https://github.com/Eight4aWish/eurorack_electronics/blob/main/docs/analogue_board_reva_firmware.md).
Read it before changing `board.cpp`.

**Status, 2026-11-01:** mode 1 is on Boy and works on the bench (David): voices, drums, clock
and reset, the encoder and the chooser. TRS MIDI in is the one thing not yet tried: see
[First flash](#first-flash).

## Modes

The mode is chosen at power-on and holds for the session, because modes set the six CV jacks as
inputs or outputs, and that shouldn't change under a patch.

- **The chooser** starts on the last mode. Turn the encoder to pick another and press to start.
  Untouched, it starts the last mode after 3 s, so a rack powering up comes back as it was.
- **Back to the chooser without a power cycle:** hold the encoder's push for 2 s in any mode. The
  board goes to its power-on state (jack switches off, gate outs low and off, DAC at 0 V) and the
  Teensy restarts into the chooser, which then waits for a press. A restart means every mode starts
  from a clean Teensy and none has to undo another's audio or MIDI set-up.

| Mode | What | Status |
|---|---|---|
| 1 | **Move companion** | works on the bench, 2026-11-01; TRS MIDI in not yet tried |
| 2 | **Warps**, a straight port of Mutable's source (`third_party/eurorack/warps`, MIT) | to come |
| 3 | **Weirder two-input processing**: spectral morphing and sidechain ducking first | to come |

## Mode 1, the Move companion

| MIDI | Out |
|---|---|
| ch 1–3 notes | GATE1–3 (+5 V) and PITCH1–3, 1 V/oct, note 36 at 0 V (the old Boy's law); notes above about 99 clip |
| ch 1–3 pitch bend | ±2 semitones |
| ch 10 note 36, note 37 | DRM1, DRM2: 500 µs pulses |
| clock | CLK: a 5 ms pulse every 16th (24 ppqn ÷ 6) |
| Start | RST: an 8 ms pulse; Stop drops every gate |

- **MIDI comes in on USB** (the Move) **and the TRS input** (Type A), merged. If both send clock,
  CLK runs at the sum.
- **Encoder:** a long press (0.6 s) sends a reset pulse; hold 2 s for the chooser.
- **Screen:**
  - row 1: the gates, as note names while they're on, then the drums and `CLK` while clock arrives;
  - row 2: the three pitch voltages;
  - row 3: the last MIDI event and its input for a second.
- **The audio outs** sit at 0 V: the codec runs, sending silence.

**What changed from the old Boy:**
- three voices, not four; no mod outs, so no velocity or CC#42;
- two drums, not four;
- no FX;
- no pots, and the encoder replaces the button;
- TRS MIDI in.

Fixed in passing:
- **Pitch bend.** Teensy's `usbMIDI` already centres the bend at 0, and the old Boy subtracted
  8192 again, so a centred bend left a voice two semitones flat.
- **The screen's I2C clock.** Adafruit's library sets the bus back to 100 kHz after each of its
  transfers unless told otherwise (`clkAfter`), and the old Boy's `oled.begin()` and `oled.dim()`
  left it there. So each row it pushed took about 12 ms, not the 3 ms its comment counts on, and
  MIDI, clock and drum pulses waited that long. Boy keeps 400 kHz.
- **Screen stalls.** Boy also sends a changed row in four 32-byte pieces, one per pass, so the loop
  never waits more than about 1 ms on the screen.

**Pitch calibration** is one measured gain for all six CV outs. Every jack read 1.8% under the
design formula with a meter. Per-channel calibration in the EEPROM (spec requirement 10), and the
board's load servo (requirement 9), aren't written yet.

## Files

- `src/teensy_move/board.{h,cpp}`: the board. EEPROM probe, the expander, the DAC start-up, the
  gate word, the encoder and its push, settings in the EEPROM (page 0x1FC0), the screen.
- `src/teensy_move/main.cpp`: the mode table and the chooser.
- `src/teensy_move/move_mode.{h,cpp}`: mode 1.

## Building and flashing

```sh
pio run -e teensy41              # build
pio run -e teensy41 -t upload    # flash: in the foreground, never in the background
```

This Mac is arm64 with no Rosetta:
- `scripts/arm64_toolchain.py` swaps in PlatformIO's arm64 GCC;
- `teensy-gui` (the Teensy Loader app) replaces `teensy-cli`.

The Teensy has VIN–VUSB cut, so the rack has to be on. Uploads often report `Error -1` and still
program the board: check before retrying.

## First flash

**Run 2026-11-01:** everything works except step 5, TRS, which hasn't been tried yet.

With nothing patched, then patching one thing at a time:

1. **Boot.**
   - The chooser shows `> 1 MOVE companion` and starts it after 3 s.
   - The screen then shows `MOVE   hold: menu`.
   - The Move still sees the module as before.
2. **Voices.** Play ch 1, 2 and 3 from the Move.
   - Each GATE goes to +5 V while a note is held.
   - Each PITCH steps 1 V an octave: meter C1 (note 36) at 0 V and C2 at 1 V.
   - The screen names the notes.
3. **Drums.** Ch 10 notes 36 and 37 should fire DRM1 and DRM2. A drum module or the ES-10 shows a
   pulse that a meter misses.
4. **Clock.** With Move playing, CLK pulses every 16th, and `CLK` shows on screen. Start pulses RST.
5. **TRS.** The same from a MIDI source on the TRS jack (Type A).
6. **Encoder.**
   - A long press pulses RST.
   - Hold 2 s: the chooser comes back and waits for a press, without restarting the rack.
7. **Audio outs** at 0 V DC.
