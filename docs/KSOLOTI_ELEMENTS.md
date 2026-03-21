# Ksoloti Big Genes — Elements

A port of [Mutable Instruments Elements](https://mutable-instruments.net/modules/elements/) (modal synthesis voice) to the [Ksoloti Big Genes](https://ksoloti.github.io/7-big_genes.html) Eurorack module.

## Hardware

- **MCU**: STM32F429 @ 168 MHz, Cortex-M4F with hardware FPU
- **Codec**: ADAU1961, 32 kHz sample rate (Elements native), I2S master via SAI1
- **MCLK**: 8 MHz HSE routed via MCO1 (PA8)
- **DMA**: Double-buffered, 16-sample blocks (~500 us per callback)
- **Display**: SH1106 128x64 OLED (I2C1, PB8/PB9), 5x7 font, 1 KB framebuffer
- **Resources**: RAM 69.4%, Flash 19.5%

## Controls

### Pots 1-4 (top row, CV P1-P4 summed in hardware) — Resonator

| Pot | Parameter | Range |
|-----|-----------|-------|
| POT1 | resonator_geometry | 0-1 |
| POT2 | resonator_brightness | 0-1 |
| POT3 | resonator_damping | 0-1 |
| POT4 | resonator_position | 0-1 |

### Pots 5-8 (bottom row, standalone) — Exciter + Space

| Pot | Parameter | Range |
|-----|-----------|-------|
| POT5 | exciter_bow_level | 0-1 |
| POT6 | exciter_blow_level | 0-1 |
| POT7 | exciter_strike_level | 0-1 |
| POT8 | space | 0-2 (>1 = increasing reverb, >1.5 = frozen) |

### CV Inputs

| Jack | Function | Notes |
|------|----------|-------|
| CV A (PA6) | exciter_blow_meta | Noise/granular model select. 0.5 when unpatched |
| CV B (PA7) | exciter_strike_meta | Mallet/particles select. 0.5 when unpatched |
| CV C (PB0) | exciter_envelope_shape | Contour. 0.5 when unpatched |
| CV D (PB1) | Gate + strength | >1V = gate on, voltage = velocity (0-1) |
| CV X (PC1) | V/Oct pitch | Centered at middle C (MIDI 60). Trimmable |
| CV Y (PC4) | FM modulation | Bipolar. 0 when unpatched |
| CV P1-P4 | Summed with pots 1-4 | Hardware summing, no separate ADC |

### Buttons

| Button | Function |
|--------|----------|
| S3 (PB12) | Manual gate (fixed strength 0.7). CV D takes priority when patched |
| ENC1 push (PB5) | Cycle resonator model: modal -> string -> strings |

### LEDs

| LED | Function |
|-----|----------|
| LED1 green (PG6) | Gate active |
| LED2 red (PC6) | CPU overload (>95% of 500 us budget) |
| LED4 green (PB6) | On for modal or strings model |
| LED4 red (PB7) | On for string or strings model |

LED4 encoding: green only = modal, red only = string, both = strings.

### Outputs

| Output | Signal |
|--------|--------|
| Audio L | Main output |
| Audio R | Aux (reverb) |
| Gate1 (PD3) | Gate echo (0-10.3V) for chaining |

## Resonator Models

Selectable via ENC1 push button (cycles through all three):

1. **Modal** (default) — Tuned resonant modes, like a struck or bowed physical object
2. **String** — Sympathetic string model
3. **Strings** — Polyphonic chord voicing from the resonator

### OLED Display

SH1106 128x64 on I2C1 (PB8 SCL, PB9 SDA, 400 kHz, addr 0x3C).

Displays:
- Resonator model name (MODAL / STRING / STRGS)
- Current note and octave (e.g. C4)
- Gate state indicator
- Pot function labels (Geo, Brt, Dmp, Pos / Bow, Blw, Str, Spc)
- Secondary parameter names and values (planned)

Page-at-a-time refresh: 1 of 8 pages sent per main loop tick (~1 ms each). Full frame refresh every 8 ms. No measurable audio impact.

## Secondary Parameters (fixed defaults, encoder UI planned)

These are currently set to sensible defaults. They will be adjustable via the encoder UI in a future update:

| Parameter | Default | Notes |
|-----------|---------|-------|
| exciter_bow_timbre | 0.5 | Bow brightness |
| exciter_blow_timbre | 0.5 | Blow brightness |
| exciter_strike_timbre | 0.5 | Strike brightness |
| exciter_signature | 0.5 | Adds imperfection/character |
| resonator_modulation_frequency | 0.5 | Internal vibrato rate |
| resonator_modulation_offset | 0.5 | Vibrato depth |
| reverb_diffusion | 0.7 | Reverb density |
| reverb_lp | 0.8 | Reverb brightness |
| modulation_frequency | 0.5 | Master modulation rate |

## Build and Flash

```sh
# Initialise submodules (first time only)
git submodule update --init --recursive

# Apply resonator resolution patch
cd third_party/eurorack
git apply ../../patches/ksoloti-elements-resonator-resolution.patch
cd ../..

# Build
pio run -e ksoloti-elements

# Flash via DFU (hold BOOT0 + reset to enter DFU mode)
pio run -e ksoloti-elements -t upload
```

DFU device serial: `305D355B3233`

## Architecture

```
src/ksoloti-elements/
  main.cc              — Entry point, DSP integration, control loop, parameter mapping
  adc.cc / adc.h       — ADC1 DMA (10ch) + ADC3 polled (4ch) + button GPIO
  codec.cc / codec.h   — SAI1 + ADAU1961 driver (I2C2, DMA double-buffer, 32 kHz)
  oled.cc / oled.h     — SH1106 128x64 OLED driver (I2C1, page-at-a-time update)
  font5x7.h            — 5x7 bitmap font (ASCII 32-126)
  elements/drivers/
    debug_pin.h        — Local shim replacing Mutable's hardware-dependent debug pins

scripts/
  elements_build.py    — PlatformIO build script (FPU flags, Elements source dirs)

patches/
  ksoloti-elements-resonator-resolution.patch  — Reduces resonator from 52 to 36 modes

third_party/eurorack/  — Git submodule: pichenettes/eurorack (MIT license)
  elements/dsp/        — Elements DSP core
  stmlib/              — Mutable Instruments DSP/utility library
```

## ADC Configuration

- **ADC1** (DMA2 Stream0, continuous scan, 10 channels): PA0-3 (pots 1-4), PA6-7 (CV A-B), PB0-1 (CV C-D), PC1 (CV X), PC4 (CV Y)
- **ADC3** (polled, 4 channels): PF6-9 (pots 5-8)
- **Clock**: APB2/4 = 21 MHz, 144-cycle sample time
- **Scaling**: Pots inverted by op-amp (full CW = 0V). CVs bipolar via inverting op-amp.

## Known Limitations

- Resonator resolution reduced from 52 to 36 modes to fit CPU budget at 168 MHz
- CV A-C (blow_meta, strike_meta, envelope_shape) have no associated pots — defaults to 0.5 when unpatched, adjustable via OLED menu (planned)
- V/Oct tracking (CV X) needs calibration with board trimmer
- No MIDI input yet (USART6 on PG9 available)

## License

The Elements DSP code is by [Emilie Gillet](https://github.com/pichenettes) under the MIT License. See `third_party/eurorack/stmlib/LICENSE`.
