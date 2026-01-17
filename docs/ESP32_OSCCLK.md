# ESP32 Oscillator/Clock — MCP4728

This module runs on an ESP32-Dev board and drives an MCP4728 quad DAC to provide two functions: a simple clock generator (Channels A/B) and a quantized oscillator (Channel C). Mode is selected by two input thresholds.

## Overview

- Oscillator Mode:
  - Waveforms: Sine, Triangle, Rising Saw, 50% Square (selected by pot position).
  - Frequency: From CV input via a lookup table mapping ADC ranges to musical note frequencies.
  - Engine: 512-sample wavetable, ~10.25 kHz update rate (timed by `micros()`), phase-accumulator.
  - Output: DAC Channel C.

- Clock Mode:
  - Delay: Pot sets tick delay (5..250 ms, clamped). Internally splits into two 1/8 duty steps.
  - Outputs: Channel B pulses every tick; Channel A every 8th tick.

- I2C: MCP4728 at `0x60`, bus clocked up to 1 MHz (reduce if cabling is long).

## Pins

- ADC inputs: `pot=GPIO32`, `cv=GPIO33`.
- Input-only pins as thresholds: `switchUp=GPIO35`, `switchDown=GPIO34`.
- DAC channels: A/B for clocks, C for oscillator.

## Operation

- Mode selection:
  - If `switchDown` ADC > ~500 → Oscillator mode active.
  - If `switchUp` ADC > ~500 → Clock mode active.
  - Both can be wired to switches or comparators to set thresholds.
- Waveform changes reset phase to minimize clicks.
- DACs A/B are set low at startup.

## Build & Upload

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

## Tuning

- Adjust `TARGET_SAMPLE_RATE_HZ` for oscillator fidelity vs CPU.
- If I2C errors occur, lower `Wire.setClock` to `400000`.
- The lookup table can be edited to change musical mapping or scale.
