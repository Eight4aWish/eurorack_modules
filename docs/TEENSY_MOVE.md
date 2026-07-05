# Teensy 4.1 — `teensy_move`

An Ableton Move ↔ Eurorack bridge and audio processor. Two on-board CV/Gate channels, two expander channels via a 74HCT595 + two MCP4822 DACs, four drum triggers, a chord mode driving the CV outputs, and an SGTL5000 stereo line passthrough with a Filter → Delay → Reverb FX send.

> **v3** — this revision removed the on-board drone synth, redirected chord mode to CV-only, added the stereo FX send, fixed the audio-out DC offset (LINE OUT AC-coupling + ADC HPF freeze), and added OLED screen-sleep noise mitigation. The OLED switching noise was resolved with grounding (star/single-point return) + screen-sleep; a dedicated OLED supply LDO was tried and barely helped, which ruled out the supply rail (the coupling is ground/radiated). Possible v4 (software, optional): move gate/clock/drum edge timing into a timer ISR off the main loop (the original jitter concern), keeping the 74HC595 and arbitrating the shared SPI bus.

## Operating Modes

The OLED has four pages. Short-press the front button to cycle through them.

| Page | Mode | Output |
|------|------|--------|
| 0 | CV — Channels 1–2 | MIDI ch 1–2 → main-board Gate/Pitch/Mod |
| 1 | CV — Channels 3–4 | MIDI ch 3–4 → expander Gate/Pitch/Mod |
| 2 | Chord | MIDI ch 6 white keys → 4-voice chord on all Pitch/Gate outputs |
| 3 | FX | Pots control the stereo Filter → Delay → Reverb send on the audio passthrough |

The CV bridge (MIDI ch 1–4) runs on **all pages except Chord** — page 3 (FX) keeps the bridge live underneath, so you can adjust effects while playing. Chord mode is page 2 only. The audio FX send is **always-on** regardless of page (page 3 just gives the pots control of it).

Long-press the front button emits a Reset pulse on `PIN_RESET` (on every page).

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

Pitch CV: V/Oct, base note `MIDI 36` = `0 V`. Pitch bend: ±2 semitones on channels 1–4.

Mod CV source — velocity or **CC#42** (the live-coding mod CC standardised across our MIDI-to-CV modules):
- By default each channel's Mod output follows **note velocity** (0–5 V, held after note-off).
- The first **CC#42** received on a channel (1–4) **latches that channel's Mod to CC control**: CC#42 values drive it (0–5 V) and note velocity no longer writes it, so streamed notes can't stomp a CC sweep. The latch holds until power-off.
- CC#42 is accepted on any OLED page (the value flushes whenever the CV bridge next writes). No other CCs are mapped.

## Chord Mode (Page 2)

MIDI input on **channel 6**. White keys trigger chords 1–7; an octave-up C triggers chord 8. Black keys snap to the nearest white key below. All four chord voices play simultaneously on Pitch1–4 / Gate1–4.

Pots while on the Chord page:

- **Pot 1**: Root note (C, C#, D, … B)
- **Pot 2**: Category (Pop, Jazz, EDM, Cinematic, LoFi)
- **Pot 3**: Progression within category
- **Pot 4**: Voicing — Root, Inv1, Inv2, Drop-2, Spread

Chord library: 40 progressions across 5 categories. Defined in [include/teensy_move/chord_library.h](../include/teensy_move/chord_library.h). Real-time chord-name detection ("Am7", "CM7", "Dm" …) shown on row 2 of the OLED. Chord mode drives only the CV outputs (pitch/gate) — there is no on-board synth voice.

## FX Mode (Page 3)

A stereo **Filter → Delay → Reverb** send on the line passthrough, processing the Move's audio on its way to the Eurorack-level outputs. The FX runs always-on, concurrent with the CV bridge; page 3 just gives the pots control of it. Defaults are fully clean (open filter, no delay/reverb), so the passthrough stays a transparent level-shifted send until you dial FX in.

Pots while on the FX page:

- **Pot 1**: Filter cutoff (low-pass, ~80 Hz – 12 kHz, log)
- **Pot 2**: Delay time (~20 – 400 ms)
- **Pot 3**: Delay amount (wet level + feedback; 0 = no delay)
- **Pot 4**: Reverb mix (freeverb wet level)

The first time you visit the FX page the effects snap to the current pot positions (no soft-takeover). A wide pot deadband (~80 codes) keeps a noisy ADC from jittering the values.

## Audio

USB mode: `USB_MIDI_SERIAL` (composite MIDI + Serial; no USB Audio class).

Signal chain — a stereo FX send on the line passthrough:

- I2S in (SGTL5000 LINE IN) → per-channel state-variable low-pass filter
- → delay line with feedback → output mix (dry filtered + delay wet)
- + stereo reverb (freeverb) wet → I2S out (SGTL5000 LINE OUT)

44.1 kHz via the Teensy Audio Library; `AudioMemory(320)` sizes the pool for the two delay lines.

**DC handling.** The SGTL5000 ADC high-pass filter is *frozen* (`adcHighPassFilterFreeze`) to block input-side DC offset. The output side is a hardware fix: the SGTL LINE OUT sits on a ~1.5 V VAG bias, so the Eurorack output gain stage must be **AC-coupled** (series cap into the op-amp + a resistor to ground) or it amplifies that bias into a DC offset at the jack.

## MIDI Clock

- Input: 24 PPQN
- Output: quarter-note pulses on `PIN_CLOCK` (5 ms default pulse width)
- Start: emits a short Reset pulse on `PIN_RESET` and resets the clock counter
- Stop / Continue: counter reset; Stop also clears all gates

## OLED

128×32 SSD1306, 4 rows × ~21 chars. Refresh 250 ms with row caching and partial updates to keep MIDI timing tight.

**Screen sleep.** The panel powers down (`DISPLAYOFF`, which stops the charge-pump and the I2C refresh bursts) after 10 s of inactivity — this is the main software mitigation for OLED switching noise coupling into the audio. On the CV pages the **button** wakes it, so the screen stays dark and quiet while you play; on the Chord/FX pages a deliberate **pot move** also wakes/holds it so you keep the readout while editing. The first wake press only wakes (no page change). Combined with hardware grounding (star/single-point return) this brings the noise to an acceptable level; a dedicated OLED supply LDO was tried and barely helped, confirming the coupling is ground/radiated rather than the supply rail.

Page layouts (4 rows each):

```
Page 0 — CV Channels 1–2          Page 1 — CV Channels 3–4
  CV MODE  G1:#  G2:#                CV MODE  G3:#  G4:#
  P1:+1.23V  P2:-0.45V               P3:+1.23V  P4:-0.45V
  Drums:#### CLK:#                   Drums:#### RST:#
  MIDI ch:1 n:60 v:100               MIDI ch:1 n:60 v:100

Page 2 — Chord                    Page 3 — FX
  CHORD C Pop P:1                    FX  Filt>Dly>Verb
  V:Root -> CM7                      Cut:12000Hz Dly:250
  Trig note: 60                      Fb: 0% Verb: 0%
  G:#### D:----                      P1cut P2dly P3fb P4vb
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
- **FX not heard** — The passthrough defaults to clean; dial FX in on page 3 (Pot 1 cutoff / Pot 2 delay time / Pot 3 delay amount / Pot 4 reverb mix).
- **DC offset on the audio output** — The LINE OUT gain stage must be AC-coupled (see [Audio](#audio)); the SGTL LINE OUT carries a ~1.5 V VAG bias.
- **Audible noise that tracks the screen** — OLED charge-pump/I2C coupling; the screen sleeps after 10 s (button or pot to wake). Addressed with star/single-point grounding plus screen-sleep; supply-rail isolation made little difference (the coupling is ground/radiated).
