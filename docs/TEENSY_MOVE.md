# Teensy 4.1 — `teensy-move`

This document captures the features, wiring, and software behavior for the Teensy 4.1 target and its expander board.

## Overview

- USB composite: Audio + MIDI + Serial (`USB_MIDI_AUDIO_SERIAL`).
- Audio passthrough: SGTL5000 I2S input → I2S output + USB output.
- 4 channels total:
  - Channels 1–2: On-board (main board)
  - Channels 3–4: Expander (via 74HCT595 and two MCP4822 DACs)
- Drum triggers: MIDI channel 10 notes 36–39 → Q2..Q5 pulses (~15 ms).

## I/O Mapping

- Shared SPI bus: `MOSI=11`, `SCK=13`.
- Expander latch (74HCT595 `RCLK`): `PIN_595_LATCH=32`.
- On-board DAC CS: `PIN_CS_DAC1=33` (chip 1), `PIN_CS_DAC2=34` (chip 2).
- Gates on main board: `PIN_GATE1=40`, `PIN_GATE2=38` (through 74HCT14; firmware uses inverted writes).
- Clock/Reset: `PIN_CLOCK=39`, `PIN_RESET=37` (HCT14 inverted writes).

### Analog Pots

- Pot 1: `A3` (numeric 17)
- Pot 2: `A2` (numeric 16)
- Pot 3: `A1` (numeric 15)
- Pot 4: `A0` (numeric 14)

### 74HCT595 Bit Map (Expander)

- Q1 → Gate1 (Channel 3)
- Q0 → Gate2 (Channel 4)
- Q2 → Drum1
- Q3 → Drum2
- Q4 → Drum3
- Q5 → Drum4
- Q6 → Mod DAC chip-select (active-low)
- Q7 → Pitch DAC chip-select (active-low)

Notes:
- Gates pass through 74HCT14 inverters — a LOW at the 595 asserts the jack gate.
- DAC chip-select lines (Q6/Q7) feed DACs directly (no inversion). Both CS are held HIGH (inactive) when idle.

## DAC Channel Mapping (Expander)

- Mod DAC (CS via Q6):
  - Channel A → Mod3
  - Channel B → Mod4
- Pitch DAC (CS via Q7):
  - Channel A → Pitch3
  - Channel B → Pitch4

Both DACs run with MCP4822 2× gain enabled (GA=0), using a 4.096 V DAC full-scale reference in code ↔ volt conversions.

## MIDI Behavior

- Channel 1 → Gate1 (main board), Mod1 (DAC1.A), Pitch1 (DAC1.B)
- Channel 2 → Gate2 (main board), Mod2 (DAC2.A), Pitch2 (DAC2.B)
- Channel 3 → Gate1 (expander Q1), Mod3 (Q6.A), Pitch3 (Q7.A)
- Channel 4 → Gate2 (expander Q0), Mod4 (Q6.B), Pitch4 (Q7.B)
- Channel 10 (drums): Notes 36..39 (C1..D#1) → Q2..Q5 short pulses (~15 ms)

Pitch bend: ±2 semitones on channels 1–4.

Control change: Currently ignored.

## MIDI Clock

- Input: 24 PPQN
- Output: Quarter-note clock pulses (`PIN_CLOCK`) — 5 ms default pulse width
- Start: Short Reset pulse (`PIN_RESET`) and counter reset
- Stop/Continue: Counter reset; stop clears gates

## OLED

- Row 0: `THRU CLK:<#|-> G1:<#|-> G2:<#|-> R:<#|->`
- Row 1: `P1: <volts>  P2: <volts>` — expected analog outputs based on current DAC codes
- Row 2: `M1: <volts>  M2: <volts>` — expected analog outputs based on current DAC codes
- Row 3: Either last MIDI event overlay for ~1 s (`ch/note/vel`) or drum state `D1..D4` (`#` active)

## Calibration

Conversions from target volts to DAC codes use these constants (empirically derived):

- Pitch: `kPitchSlope`, `kPitchOffset`
- Mod: `kModSlope`, `kModOffset`

These produce target ranges of approximately:
- Mod: ±5 V
- Pitch: −3 V to +7 V

## Build & Upload

```sh
# Build Teensy 4.1 target
pio run -e teensy41

# Upload via CLI
pio run -e teensy41 -t upload
```

## Wiring Notes

- 74HCT595: `SRCLR` = HIGH, `OE` = LOW; decouple VCC and keep traces short for latch.
- 74HCT14: Verify inversion orientation — input from Q0/Q1, output to gate jacks.
- DAC logic levels: If DACs are at 5 V VDD, ensure 3.3 V `MOSI/SCK` meet VIH, or buffer via 74HCT.
- Grounding: Keep analog/digital grounds low-impedance; tie at a quiet point near DACs/amps.

## Troubleshooting

- Gates not asserting: Check HCT14 wiring and confirm LOW on Q0/Q1 during gate.
- DACs not responding: Verify Q6/Q7 CS polarity (active-low) and shared SPI wiring continuity.
- Drum triggers missing: Confirm Channel 10 note range (36..39).
