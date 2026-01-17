# Daisy MFX — Banked Reverb/Delay

This module runs on Electrosmith Daisy (Patch variant) using DaisyDuino. It provides two banks of effects with a clean OLED UI, CV takeover, tap-tempo for delays, and safety features to avoid clicks on patch changes.

## Overview

- Banks:
  - Reverb (A): Classic, Plate (with predelay), Tank (light modulation), Shimmer (+12 semitone pitch shifter with warm-up).
  - Delay (B): Ping, Tape (LP feedback and slow wow), MultiTap (3 taps with width), EchoVerb (delay feeding a reverb macro).
- Audio: 48 kHz. Per-patch wet fade on change; DC-block and gentle LPF on outputs.
- UI pacing: Faster while active; sleeps OLED after inactivity; wakes on interaction.

## Controls

- Button (D1):
  - Short: In Patch level, switch patch (1→4). In Bank menu, flip preview bank.
  - Long: Toggle between Bank menu and Patch view. Selecting a bank resets to patch 1.
- Pots: `P1=Mix`, `P2=Decay/Predelay/Time`, `P3=Tone/Feedback/Macro` depending on patch.
- CV takeover: CV1 can take over `P2`, CV2 can take over `P3` using hysteresis thresholds to avoid flicker. Indicators invert the bars on OLED when CV is active.
- Tap Tempo (Delays only): On CV2. Rising edges above ~1.5 V arm and capture interval; auto time-out (~1.8 s) returns control to pot.

## OLED UI

- Bank menu with two cells (A: Revb, B: Dely).
- Patch UI with four tiles: Button, P1, P2, P3 (bars). Title shows patch short name (e.g., A2 Plate).
- Low-contrast scheme to reduce OLED wear; configurable frame pacing for responsiveness.

## Hardware

- I2C OLED: `SCL=D11`, `SDA=D12`, address `0x3C`.
- Button: `D1` (INPUT_PULLUP). LED: `D13` heartbeat.
- Pots: `A5` (P1), `A3` (P2), `A2` (P3).
- CV inputs: `A1` (CV1), `A0` (CV2). Internal mapping converts ADC 0..1 to approximate volts.

## Build & Upload

```sh
pio run -e daisy-mfx
pio run -e daisy-mfx -t upload   # DFU (daisy platform)
```

## Notes

- Shimmer warms up its pitch shifter for ~170 ms to avoid artifacts.
- Predelays and modulation lines are pre-cleared on patch reset to avoid stale content.
- Output LPF is set around 14.5 kHz for a gentle roll-off.
