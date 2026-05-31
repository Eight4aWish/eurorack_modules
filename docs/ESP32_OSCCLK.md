# ESP32 Oscillator/Clock — MCP4728

This module runs on an ESP32-Dev board and drives an MCP4728 quad DAC. A
hardware ON-OFF-ON switch selects one of two modes (oscillator or clock),
and a pot adjusts either waveform/frequency or clock rate depending on the
active mode.

## Channel map

| DAC channel | Function |
|---|---|
| A | Clock — 0 V idle, ~+5 V trigger pulses (CLOCK mode only) |
| B | Reset — single ~+5 V pulse fired on entry to CLOCK mode |
| C | Oscillator — wavetable audio output (OSC mode only) |
| D | unused (not wired) |

The output stages are non-inverting unipolar with gain ~2, so DAC 0 → ~0 V
at the jack and DAC 2048 → ~+5 V. The clock and reset use these levels
directly; the oscillator runs the full 0–4095 DAC range (0–10 V) and is
intended for AC-coupled audio destinations.

## Modes

- **Oscillator mode** (switch DOWN): Channel C produces audio.
  - Pot position selects waveform: Sine, Triangle, Rising Sawtooth, 50% Square.
  - CV input selects frequency from a lookup table (musical note frequencies).
  - Engine: 512-entry wavetable per shape (all four pre-computed at boot),
    phase accumulator with linear interpolation, ~10.25 kHz sample rate.
  - Switching waveform is glitch-free — no regeneration in the audio loop.

- **Clock mode** (switch UP): Channel A pulses; Channel B fires one reset
  pulse on mode entry.
  - Pot sets the clock period. Range roughly 90–500 ms (2–11 Hz) across the
    sweep; constraint clamps to a usable musical range.
  - The reset pulse precedes the first clock tick so downstream sequencers
    restart in sync.
  - Each time the switch transitions from OFF or OSC to CLOCK, a fresh
    reset pulse fires.

- **Centre position** (switch OFF): all trigger outputs idle at 0 V; the
  oscillator output holds its last sample value.

## Pins

- ADC inputs: `pot=GPIO32`, `cv=GPIO33`.
- Input-only pins as mode-switch thresholds: `switchUp=GPIO35`, `switchDown=GPIO34`.
- I2C: SDA/SCL default ESP32-Dev pins, MCP4728 at `0x60`, bus clocked at 1 MHz.
- Output: MCP4728 VOUTA/VOUTB/VOUTC → channel A/B/C jacks via per-channel
  non-inverting buffer stages (TL074 quad op-amp).

## Build & upload

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor -b 115200
```

PlatformIO sometimes auto-detects the wrong serial port (e.g. the macOS
Bluetooth serial). Pass `--upload-port` explicitly if so.

## Tuning

Constants at the top of [src/esp32oscclk/main.cpp](../src/esp32oscclk/main.cpp):

- `clock_high` / `clock_low` — DAC values for trigger pulses. The defaults
  (2048 / 0) give ~5 V / ~0 V at the jack with the current output stage.
- `RESET_PULSE_MS` — width of the reset pulse on mode entry (default 10 ms).
- `TARGET_SAMPLE_RATE_HZ` — oscillator sample rate. Higher = better fidelity
  at higher pitches, at the cost of CPU/I2C headroom.
- `delay_value` formula in `loop()` — controls clock-period mapping from pot.

If I2C errors occur with long cabling, lower `Wire.setClock(...)` to `400000`.

The CV-to-frequency `lookupTable` can be edited to remap the musical scale
or change the pitch range.

## Bench notes

- The output stages are non-inverting unipolar with gain ~2 (confirmed by
  scoping VOUTB vs the B jack: DAC mid 2048 → 2.5 V at pin → 5 V at jack).
- The Intellijel sequencer's reset/clock inputs trigger reliably on the
  ~5 V pulse and idle dark at 0 V.
- Channel D is wired-out only on the MCP4728; no jack is connected on this
  panel build.
