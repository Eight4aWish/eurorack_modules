# Ksoloti Big Genes — Elements

A port of [Mutable Instruments Elements](https://mutable-instruments.net/modules/elements/) (modal synthesis voice) to the [Ksoloti Big Genes](https://ksoloti.github.io/7-big_genes.html) Eurorack module.

## Hardware

- **MCU**: STM32F429 @ 168 MHz, Cortex-M4F with hardware FPU
- **Codec**: ADAU1961, 32 kHz sample rate (Elements native), I2S master via SAI1
- **MCLK**: 8 MHz HSE routed via MCO1 (PA8)
- **DMA**: Double-buffered, 16-sample blocks (~500 us per callback)
- **Display**: SH1106 128x64 OLED (I2C1, PB8/PB9), 5x7 font, 1 KB framebuffer
- **Resources**: RAM 69.4%, Flash 19.7%

## Controls

### Pots 1-4 (top row, CV P1-P4 summed in hardware) — Resonator

| Pot | Parameter | Range |
|-----|-----------|-------|
| POT1 | resonator_geometry | 0-1 |
| POT2 | resonator_brightness | 0-1 |
| POT3 | resonator_damping | 0-1 |
| POT4 | resonator_position | 0-1 |

### Pots 5-8 (bottom row) — Dual Mode

S4 toggles P5-8 between two modes. Pot pickup prevents parameter jumps on mode switch.

**Mode 1 — Levels (default):**

| Pot | Parameter | Range |
|-----|-----------|-------|
| POT5 | exciter_bow_level | 0-1 |
| POT6 | exciter_blow_level | 0-1 |
| POT7 | exciter_strike_level | 0-1 |
| POT8 | space | 0-2 (>1 = increasing reverb, >1.5 = frozen) |

**Mode 2 — Timbres:**

| Pot | Parameter | Range |
|-----|-----------|-------|
| POT5 | exciter_blow_timbre | 0-1 |
| POT6 | exciter_blow_meta (Flow) | 0-1 |
| POT7 | exciter_strike_meta (Mallet) | 0-1 |
| POT8 | exciter_strike_timbre | 0-1 |

### CV Inputs

CV A-C are assignable via S2 (select CV) + E2 (cycle target). Default assignments shown.

| Jack | Default Target | Notes |
|------|----------------|-------|
| CV A (PA6) | Flow (blow_meta) | Modulates ±0.5 around base value |
| CV B (PA7) | Mallet (strike_meta) | Modulates ±0.5 around base value |
| CV C (PB0) | Unassigned | Assignable to any parameter via S2/E2 |
| CV D (PB1) | Gate + strength | >0.2V = gate on, voltage = velocity (0-1) |
| CV X (PC1) | V/Oct pitch | Centered at middle C (MIDI 60). Trimmable |
| CV Y (PC4) | FM modulation | Bipolar. 0 when unpatched |
| CV P1-P4 | Summed with pots 1-4 | Hardware summing, no separate ADC |

**Assignable CV targets:** Flow, Mallet, Contour, Bow Timbre, Blow Timbre, Strike Timbre, Signature, Mod Frequency, Mod Offset, Reverb Diffusion, Reverb LP, None.

### Buttons & Encoders

| Control | Function |
|---------|----------|
| S1 / ENC1 push (PB5) | Cycle resonator model: modal -> string -> chords |
| ENC1 rotate (PG11/PG12) | Mode 1: contour (envelope shape). Mode 2: bow timbre |
| S2 / ENC2 push (PA10) | Cycle selected CV for assignment (A -> B -> C) |
| ENC2 rotate (PG10/PA15) | Cycle CV target parameter for selected CV |
| S3 (PB12) | Play — manual gate (fixed strength 0.7). CV D takes priority when patched |
| S4 (PB13) | Toggle pot mode (levels / timbres) |

Note: S2 and S3 are active-high (no internal pull-up). S1 and S4 are active-low with internal pull-up.

### LEDs

| LED | Function |
|-----|----------|
| LED1 green (PG6) | Gate active |
| LED2 red (PC6) | CPU overload (>95% of 500 us budget) |
| LED4 green (PB6) | On for modal or chords model |
| LED4 red (PB7) | On for string or chords model |

LED4 encoding: green only = modal, red only = string, both = chords.

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
3. **Chords** — Polyphonic chord voicing from the resonator

## OLED Display

SH1106 128x64 on I2C1 (PB8 SCL, PB9 SDA, 400 kHz, addr 0x3C).

Single-page layout with six rows:

```
S1 Mod E1 Con SE2 Cv       <- model, E1 param, CV config reminder
─────────────────────
P1-4 Geo Brt Dmp Pos       <- resonator pots (always active)
P5-8 Bow Blw Stk Spc       <- mode 1: levels (underlined when active)
P5-8 BlT Flw Mal StT       <- mode 2: timbres (underlined when active)
CvAD Flw Mal --- Gte       <- CV A-C assignments + gate (active CV underlined)
S34 PyPge CvXY VO FM       <- button/CV reference (or active param + value)
```

The bottom line shows parameter name and 0-100 value when pots or E1 are being adjusted (up to two simultaneous controls), reverting to the static reference after 2 seconds.

Page-at-a-time refresh: 1 of 8 pages sent per main loop tick (~1 ms each). Full frame refresh every 8 ms. No measurable audio impact.

## Hidden Parameters

These parameters have sensible defaults and are accessible only via CV assignment (S2/E2):

| Parameter | Default | Effect |
|-----------|---------|--------|
| Signature (Sig) | 0.5 | Adds imperfection/character to exciters |
| Mod Frequency (MFr) | 0.5 | Resonator internal vibrato rate |
| Mod Offset (MOf) | 0.5 | Resonator vibrato depth |
| Reverb Diffusion (RvD) | 0.7 | Reverb density |
| Reverb LP (RvL) | 0.8 | Reverb brightness |

## Install (pre-built binary — no coding required)

You just need the firmware `.bin` file and a free tool called `dfu-util` to flash it to your Big Genes module over USB.

### Step 1: Download

Download `ksoloti-elements.bin` from the [Releases](https://github.com/Eight4aWish/eurorack_modules/releases) page.

### Step 2: Install dfu-util

| OS | Install command |
|----|-----------------|
| macOS | `brew install dfu-util` |
| Windows | Download from [dfu-util.sourceforge.net](http://dfu-util.sourceforge.net) and add to PATH |
| Linux | `sudo apt install dfu-util` |

### Step 3: Put Big Genes in DFU mode

1. Connect Big Genes to your computer via USB
2. Hold the **BOOT0** button (small button on the Ksoloti Core board)
3. While holding BOOT0, press and release **RESET**
4. Release BOOT0

The module's LEDs and screen will be off — this is normal. Your computer should now detect a DFU device.

### Step 4: Flash

Open a terminal and run:

```sh
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D ksoloti-elements.bin
```

You should see a progress bar. When it says "File downloaded successfully", the module will automatically restart and begin running Elements.

### Step 5: Verify

- The OLED should display "S1 Mod E1 Con SE2 Cv" on the top line
- LED1 (green) should light when you press S3 or send a gate to CV D
- Sound should come from the audio outputs when a gate is active and pots are turned up

### Troubleshooting

- **"No DFU capable USB device available"** — The board isn't in DFU mode. Repeat Step 3, making sure you hold BOOT0 *before* pressing RESET.
- **No sound** — Make sure POT5 (Bow), POT6 (Blow), or POT7 (Strike) is turned up in levels mode. At least one exciter level must be non-zero.
- **To restore original Ksoloti firmware** — Flash the original `.bin` file from [ksoloti.github.io](https://ksoloti.github.io) using the same DFU process.

## Build from Source (for developers)

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
- V/Oct tracking (CV X) needs calibration with board trimmer
- No MIDI input yet (USART6 on PG9 available)
- Hidden parameters (Sig, MFr, MOf, RvD, RvL) only accessible via CV assignment — no direct knob control

## License

The Elements DSP code is by [Emilie Gillet](https://github.com/pichenettes) under the MIT License. See `third_party/eurorack/stmlib/LICENSE`.
