# Teensy 4.1 — `teensy_move`

A Teensy 4.1-based modular synth controller and sound source. Two on-board CV/Gate channels, two expander channels via a 74HCT595 + two MCP4822 DACs, four drum triggers, an on-board 4-voice chord drone synth, and SGTL5000 line passthrough.

## Operating Modes

The OLED has four pages. Short-press the front button to cycle through them.

| Page | Mode | Output |
|------|------|--------|
| 0 | CV — Channels 1–2 | MIDI ch 1–2 → main-board Gate/Pitch/Mod |
| 1 | CV — Channels 3–4 | MIDI ch 3–4 → expander Gate/Pitch/Mod |
| 2 | Chord | MIDI ch 6 white keys → 4-voice chord on all Pitch/Gate outputs + on-board drone synth |
| 3 | Drone | Standalone 4-voice drone synth (set waveform / attack / release / volume) |

Long-press behaviour depends on page: CV pages emit a Reset pulse on `PIN_RESET`; Chord and Drone pages toggle the on-board drone on/off.

Drum triggers (MIDI ch 10, notes 36–39 → Q2..Q5) work on every page.

## I/O Mapping

- Shared SPI bus: `MOSI=11`, `SCK=13`.
- Expander 74HCT595 latch: `PIN_595_LATCH=32`.
- On-board DAC chip-selects: `PIN_CS_DAC1=33`, `PIN_CS_DAC2=34`.
- Gates on main board: `PIN_GATE1=40`, `PIN_GATE2=38` (through 74HCT14 — firmware uses inverted writes).
- Clock / Reset: `PIN_CLOCK=39`, `PIN_RESET=37` (HCT14 inverted writes).
- Front button: `PIN_BTN=2` (active-low, debounced 30 ms; long-press threshold 600 ms).

### Analog Pots

- Pot 1: `A3` (numeric 17)
- Pot 2: `A2` (numeric 16)
- Pot 3: `A1` (numeric 15)
- Pot 4: `A0` (numeric 14)

Pots are inverted in software (CW = max).

### 74HCT595 Bit Map (Expander)

- Q0 → Gate2 (Channel 4)
- Q1 → Gate1 (Channel 3)
- Q2 → Drum1
- Q3 → Drum2
- Q4 → Drum3
- Q5 → Drum4
- Q6 → Mod DAC chip-select (active-low)
- Q7 → Pitch DAC chip-select (active-low)

Notes:
- Gates pass through 74HCT14 inverters — a LOW at the 595 asserts the jack gate.
- DAC chip-select lines (Q6/Q7) feed the DACs directly (no inversion). Both CS lines idle HIGH.

## DAC Channel Mapping (Expander)

Configured in [include/teensy_move/pins.h](../include/teensy_move/pins.h):

- Mod DAC (CS via Q6):
  - Channel B → Mod3
  - Channel A → Mod4
- Pitch DAC (CS via Q7):
  - Channel B → Pitch3
  - Channel A → Pitch4

(Override `EXP_*_CH_IDX` build flags if your hardware swaps channels.)

Both DACs run with MCP4822 2× gain enabled (GA=0). On-board DAC1 channel B → Pitch1 / channel A → Mod1; DAC2 channel B → Pitch2 / channel A → Mod2.

## CV Mode — MIDI Behavior

- Channel 1 → Gate1 (main), Mod1 (DAC1.A), Pitch1 (DAC1.B)
- Channel 2 → Gate2 (main), Mod2 (DAC2.A), Pitch2 (DAC2.B)
- Channel 3 → Gate1 (expander Q1), Mod3 (Q6.B), Pitch3 (Q7.B)
- Channel 4 → Gate2 (expander Q0), Mod4 (Q6.A), Pitch4 (Q7.A)
- Channel 10 (drums): notes 36..39 (C1..D#1) → Q2..Q5 short pulses (~500 µs)

Pitch CV: V/Oct, base note `MIDI 36` = `0 V`. Pitch bend: ±2 semitones on channels 1–4. Mod CV is driven from note velocity. Control change messages are currently ignored.

## Chord Mode (Page 2)

MIDI input on **channel 6**. White keys trigger chords 1–7; an octave-up C triggers chord 8. Black keys snap to the nearest white key below. All four chord voices play simultaneously on Pitch1–4 / Gate1–4.

Pots while on the Chord page:

- **Pot 1**: Root note (C, C#, D, … B)
- **Pot 2**: Category (Pop, Jazz, EDM, Cinematic, LoFi)
- **Pot 3**: Progression within category
- **Pot 4**: Voicing — Root, Inv1, Inv2, Drop-2, Spread

Chord library: 40 progressions across 5 categories. Defined in [include/teensy_move/chord_library.h](../include/teensy_move/chord_library.h). Real-time chord-name detection ("Am7", "CM7", "Dm" …) shown on row 2 of the OLED.

## Drone Mode (Page 3)

A 4-voice on-board synth (8 sawtooth/square/triangle/sine/pulse oscillators, two detuned per voice) routed through per-voice envelopes and a master amp into the SGTL5000 output mix. Pitched from the active chord voices, so it tracks chord-mode triggers as well as standalone use.

Pots while on the Drone page:

- **Pot 1**: Waveform (SAW / SQR / TRI / SIN / PUL)
- **Pot 2**: Envelope attack (10 ms – 2000 ms)
- **Pot 3**: Envelope release (50 ms – 3000 ms)
- **Pot 4**: Master volume (0 – ~1.5×)

Long-press the front button on the Chord or Drone page to toggle the drone on/off.

## Audio

USB mode: `USB_MIDI_SERIAL` (composite MIDI + Serial; no USB Audio class).

Signal chain:

- I2S in (SGTL5000) → output mixer (passthrough channel)
- Drone synth (4 voices × 2 detuned oscs → per-voice envelope → master amp) → output mixer (drone channel)
- Output mixer → I2S out (SGTL5000)

Audio is sample-rate 44.1 kHz via the standard Teensy Audio Library.

## MIDI Clock

- Input: 24 PPQN
- Output: quarter-note pulses on `PIN_CLOCK` (5 ms default pulse width)
- Start: emits a short Reset pulse on `PIN_RESET` and resets the clock counter
- Stop / Continue: counter reset; Stop also clears all gates

## OLED

128×32 SSD1306, 4 rows × ~21 chars. Refresh rate 150 ms with row caching and partial updates to keep MIDI timing tight.

Page layouts (4 rows each):

```
Page 0 — CV Channels 1–2          Page 1 — CV Channels 3–4
  CV MODE  G1:#  G2:#                CV MODE  G3:#  G4:#
  P1:+1.23V  P2:-0.45V               P3:+1.23V  P4:-0.45V
  Drums:#### CLK:#                   Drums:#### RST:#
  MIDI ch:1 n:60 v:100               MIDI ch:1 n:60 v:100

Page 2 — Chord                    Page 3 — Drone
  CHORD C Pop P:1                    DRONE  [ON]  SAW
  V:Root -> CM7                      Wave: SAW
  Drone:ON  Vol:67%                  A:350ms R:600ms
  G:#### D:----                      Volume: 67%
```

## Calibration

Per-channel linear fits stored in [include/teensy_move/calib_static.h](../include/teensy_move/calib_static.h):

```
volts = m * DAC_code + c    →    code = (volts − c) / m
```

Constants for the four Mod (M1..M4) and four Pitch (P1..P4) channels were measured with a DMM and Excel-fitted. Update those arrays if you re-trim the hardware. Defaults produce roughly:

- Mod: −5 V to +5 V
- Pitch: −3 V to +7 V

`USE_STATIC_CALIB` is asserted by the build env, so [src/teensy_move/main.cpp](../src/teensy_move/main.cpp) calls `pitchVoltsToCode_ch()` / `modVoltsToCode_ch()` from this header for all DAC writes.

## Build & Upload

```sh
pio run -e teensy41
pio run -e teensy41 -t upload
pio device monitor -b 115200
```

Upload protocol is `teensy-cli`.

## Wiring Notes

- 74HCT595: tie `SRCLR` HIGH, `OE` LOW; decouple VCC; keep latch traces short.
- 74HCT14: verify inversion orientation — input from Q0/Q1 / `PIN_GATE*` / `PIN_CLOCK` / `PIN_RESET`, output to gate jacks.
- DAC logic levels: if DACs run from 5 V VDD, ensure the 3.3 V `MOSI` / `SCK` levels meet VIH, or buffer through a 74HCT.
- Grounding: keep analog and digital grounds low-impedance and tie at a quiet point near the DACs / output amps.

## Troubleshooting

- **Gates not asserting** — Check 74HCT14 wiring and confirm LOW on Q0/Q1 (or main-board gate pin) during gate-on.
- **DACs not responding** — Verify Q6/Q7 CS polarity (active-low) and shared SPI continuity.
- **Drum triggers missing** — Confirm Channel 10, note range 36..39.
- **Chord mode silent** — Send notes on MIDI **channel 6** specifically; other channels do not trigger chords.
- **Drone won't sound** — Long-press the front button on the Chord or Drone page to toggle it on; check Pot 4 (volume) is non-zero in Drone mode.
