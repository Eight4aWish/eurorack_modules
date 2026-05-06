# Eurorack Firmware Monorepo

## Build

PlatformIO is used for building across targets. Pico 2 W is the default (`pico2w_oc`).

```sh
# Build default env
pio run

# Build & upload Pico 2 W
pio run -e pico2w_oc -t upload

# Monitor serial
pio device monitor -b 115200
```

## Teensy 4.1 — `teensy-move` / `teensy-move-v2`

This target implements a modular synth controller on Teensy 4.1 with two on-board channels and two expander channels driven via a 74HCT595. USB runs in composite mode (Audio + MIDI + Serial).

### V2 Features (`teensy41_v2` environment):

- **Two Operating Modes** (cycle with short button press):
  - **CV Mode** (Pages 0-1): MIDI channels 1-4 produce gates + Pitch CV + Mod CV (velocity-based). Channel 10 drum triggers.
  - **Chord Mode** (Page 2): One-finger chord progressions similar to Maschine/Ableton. MIDI channel 6 triggers 4-voice chords on all pitch/gate outputs.

- **Chord Mode Details**:
  - 40 chord progressions across 5 categories: Pop, Jazz, EDM, Cinematic, LoFi
  - Pot 1: Root note (C through B)
  - Pot 2: Category selection
  - Pot 3: Progression within category
  - Pot 4: Voicing (Root, Inv1, Inv2, Drop2, Spread)
  - White keys trigger chords 1-7, higher C triggers chord 8
  - Real-time chord name detection and display (e.g., "Am7", "CM7", "Dm")
  - Drum triggers (ch10) work in both modes

- **Optimized MIDI Timing**:
  - Reduced OLED refresh rate (150ms with row caching)
  - Partial display updates for minimal blocking
  - Rock-solid timing for live performance

- **Hardware**:
  - On-board channels (1-2): Gates `PIN_GATE1=40`, `PIN_GATE2=38`; DACs on `PIN_CS_DAC1=33`, `PIN_CS_DAC2=34`
  - Expander channels (3-4) via 74HCT595: Gates + Drums + two MCP4822 DACs
  - USB Audio passthrough: I2S input routed to both I2S out and USB out
  - MIDI clock: 24 PPQN divided to quarter-note pulses; Start emits Reset pulse

- Build & Upload:

```sh
# Teensy 4.1 V2 (recommended - with chord mode)
pio run -e teensy41_v2
pio run -e teensy41_v2 -t upload

# Teensy 4.1 V1 (original MIDI-to-CV only)
pio run -e teensy41
pio run -e teensy41 -t upload
```

See `docs/TEENSY_MOVE.md` for wiring notes, expander sequencing, and behavior details.

## Pico 2 W — `pico2w_oc`

This target implements a menu-driven multi-patch Eurorack utility on Raspberry Pi Pico 2 W with an SSD1306 OLED, ADS1115 ADC inputs, and an MCP4728 quad DAC for CV outputs.

- Features:
	- OLED UI with short/long press navigation (menu and in-patch controls).
	- Patches: Clock, Quant, Euclid, Env (dual envelopes), QuadLFO, Scope, Calib, Diag.
	- Inputs: Two analog inputs via ADS1115 plus an external clock input (`AD_EXT_CLOCK_CH`).
	- Outputs: Four CVs via MCP4728 (calibrated mapping for bipolar/unipolar where applicable). Timing patches use fixed gate codes for crisp edges.
	- Consistent grid-based UI layout for readability on 128x64 OLED.

- Hardware mapping:
	- See `include/pico2w_oc/pins.h` for physical macros: `CV0_DA_CH..CV3_DA_CH`, `AD0_CH`, `AD1_CH`, `AD_EXT_CLOCK_CH`.
	- External clock is detected on rising edges on `AD_EXT_CLOCK_CH` in Clock/Env patches.

- Build & Upload:

```sh
# Pico 2 W
pio run -e pico2w_oc
pio run -e pico2w_oc -t upload
pio device monitor -b 115200
```

See `docs/PICO2W_OC.md` for full UI behavior and patch-specific controls.

## Daisy Seed — `daisy-mfx`

This target implements a compact multi-FX for the Electrosmith Daisy Seed with two banks: Reverbs and Delays. It features CV takeover, tap-tempo on CV2 (Delays bank), wet-fade on patch change, shimmer warm-up, OLED sleep/wake, and output filtering.

- Features:
	- Banks: A) Reverb (Classic, Plate with predelay, Tank with light modulation, Shimmer); B) Delays (Ping, Tape with LP feedback, MultiTap, EchoVerb).
	- Controls: Button short cycles patch; long toggles Bank/Patch UI. `P1=Mix`, `P2=Decay/Predelay/Time`, `P3=Tone/Feedback/Macro`.
	- CV takeover: `CV1` can take over `P2`, `CV2` can take over `P3` with hysteresis; CV2 also supports tap-tempo in Delay bank.
	- OLED UI with low-contrast theme, active/idle frame pacing, sleep after inactivity.
	- Audio: 48 kHz; DC-block + gentle LPF on outputs to tame HF.

- Hardware mapping (from firmware):
	- I2C OLED: `SCL=D11`, `SDA=D12`, addr `0x3C`. Button `D1` (pullup), LED `D13`.
	- Pots: `A5`, `A3`, `A2`. CV inputs: `A1`, `A0` (mapped to volts in code).

- Build & Upload:

```sh
# Daisy (daisy-mfx)
pio run -e daisy-mfx
pio run -e daisy-mfx -t upload   # DFU
```

See `docs/DAISY_MFX.md` for patch details and CV/tap behavior.

## ESP32 Dev — `esp32oscclk`

This target provides a simple dual-function utility on ESP32 with an MCP4728 quad DAC: a quantized oscillator (Channel C) and a clock generator (Channels A/B). Mode is selected via two input thresholds.

- Features:
	- Oscillator mode: Select waveform via pot (Sine, Triangle, Saw, Square); frequency from CV input through a lookup table of musical notes; ~10.25 kHz update rate with 512-sample wavetable.
	- Clock mode: Pot maps to delay (5–250 ms). Channel B pulses every tick; Channel A pulses every 8th tick.
	- I2C at up to 1 MHz; initial DAC state forces low on all clock channels.

- Hardware mapping (from firmware):
	- ADC inputs: `pot=GPIO32`, `cv=GPIO33`, `switchUp=GPIO35`, `switchDown=GPIO34` (input-only pins).
	- MCP4728 channels: `A` and `B` used for clocks, `C` for oscillator output.

- Build & Upload:

```sh
# ESP32 Dev (esp32oscclk)
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

See `docs/ESP32_OSCCLK.md` for behavior, pin notes, and tuning.

## AI Module (CortHex) — `nanoesp32-corthex`

A Eurorack voice driven by an LLM. Talks to the user via three web pages, drives a Plaits + Swords + Four Play patch through six calibrated CV outputs, and uses panel buttons 1–6 as a 6-slot patch bank that an LLM populates with variations on a prompt.

- **Hardware**: Arduino Nano ESP32 (NORA-W106 / ESP32-S3, 1M-context-class flash). Six CV outputs through 3× MCP4822 + bipolar shift, audio listening tap on A0/A1, clock input on D9, 7 panel buttons + LEDs. Full schematic and panel layout in [docs/AI_MODULES_HARDWARE.md](docs/AI_MODULES_HARDWARE.md).
- **Three web pages** (all served by the firmware, on the module's LAN address):
  - `/` — diagnostics: live telemetry, panel buttons, audio level, gate input, CV output voltages, system info, four sample patches with trigger/release.
  - `/plaits` — Plaits-only control: 24-engine picker organised by bank (orange / green / red), three macro sliders for Timbre / Harmonics / Morph, per-engine OUT / AUX / macro reference text from the v1.2 manual, internal-LPG envelope explainer.
  - `/llm` — natural-language patch generation: chat with a Mac-side LLM proxy, view a 6-patch bank as cards, telemetry highlights the slot the user selects via the panel buttons.
- **Mac LLM proxy**: small FastAPI service at [tools/llm-proxy/](tools/llm-proxy/) that translates prompts into 6 distinct, named patches via Claude. The iPad page calls the proxy; the proxy forwards bank to the module. See its README for setup.
- **AR envelope engine**: every CV channel can run a gate-driven attack/release envelope using the `D9` gate input, so generated patches can have per-channel dynamics (filter sweeps, VCA envelopes) on top of Plaits' own internal LPG.

Build & upload:

```sh
# First flash via DFU (USB)
pio run -e nanoesp32-corthex -t upload

# Subsequent OTA flashes (over WiFi, mDNS hostname from secrets.h)
AI_MODULE_OTA_PASS='your-ota-password' pio run -e nanoesp32-corthex-ota -t upload
```

WiFi credentials and OTA password live in `src/nanoesp32-corthex/secrets.h` (gitignored — copy from `secrets.h.example`).

See [docs/NANOESP32_CORTHEX.md](docs/NANOESP32_CORTHEX.md) for the firmware architecture, HTTP API, and per-page usage notes.

## Ksoloti Big Genes — `ksoloti-elements`

A port of **Mutable Instruments Elements** (modal synthesis voice) to the [Ksoloti Big Genes](https://ksoloti.github.io/7-big_genes.html) Eurorack module (STM32F429 @ 168 MHz + ADAU1961 codec).

- **Status**: Fully playable with single-page OLED UI, dual pot mode, CV assignment, all buttons and encoders working.
- **Audio**: L in = blow exciter, R in = strike exciter, L out = main, R out = aux (reverb).
- **Resonator**: 36 modes (reduced from 52 to fit CPU budget at 168 MHz). Three models selectable via S1: modal, string, chords.
- **Controls**: POT1-4 = resonator (geometry/brightness/damping/position, CV-summable). S4 toggles POT5-8 between levels (bow/blow/strike/space) and timbres (blow timbre/flow/mallet/strike timbre). E1 = contour (levels mode) or bow timbre (timbres mode). CV A-C = assignable modulation (S2 selects, E2 assigns target). CV D = gate + velocity. CV X = V/Oct. CV Y = FM. S3 = play (manual gate).
- **Display**: Single-page OLED with control reference, active pot mode underline, CV assignments, and real-time parameter name + value on pot/encoder activity.
- **Indicators**: LED1 green = gate. LED2 red = CPU overload. LED4 dual = resonator model (green/red/both). Gate1 = gate echo output.
- **Resources**: RAM 69.4%, Flash 19.7%.

See `docs/KSOLOTI_ELEMENTS.md` for full control mapping, secondary parameters, and ADC details.

### Setup

```sh
# After cloning, initialise submodules
git submodule update --init --recursive

# Apply the resonator resolution patch
cd third_party/eurorack
git apply ../../patches/ksoloti-elements-resonator-resolution.patch
cd ../..

# Build
pio run -e ksoloti-elements

# Flash via DFU (board must be in DFU mode)
pio run -e ksoloti-elements -t upload
```

### Architecture

```
src/ksoloti-elements/
  main.cc              — Entry point, Elements DSP, control loop, parameter mapping
  adc.cc / adc.h       — ADC1 DMA (10ch) + ADC3 polled (4ch) + button GPIO
  codec.cc / codec.h   — SAI1 + ADAU1961 driver (I2C2, DMA double-buffer)
  oled.cc / oled.h     — SH1106 128x64 OLED driver (I2C1, page-at-a-time update)
  font5x7.h            — 5x7 bitmap font (ASCII 32-126)
  elements/drivers/
    debug_pin.h        — Local shim (empty stubs for hardware debug pins)

scripts/
  elements_build.py    — PlatformIO build script (FPU flags, extra source dirs)

patches/
  ksoloti-elements-resonator-resolution.patch  — Reduces resonator from 52 to 36 modes

third_party/eurorack/  — Git submodule: pichenettes/eurorack (MIT license)
  elements/dsp/        — Elements DSP core
  stmlib/              — Mutable Instruments DSP/utility library
```

## Libraries

- expander I/O: `libs/expander_io` — 74HC595 expander driver (`Expander595`) and MCP4822 helper (`Mcp4822Expander`). See `libs/expander_io/README.md` for API and wiring.

## Third-Party Licenses

### Mutable Instruments Eurorack Modules

`third_party/eurorack/` and `third_party/eurorack/stmlib/` are by [Emilie Gillet](https://github.com/pichenettes) and released under the **MIT License**:

> Copyright 2012-2015 Emilie Gillet.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.

The full license text is in `third_party/eurorack/stmlib/LICENSE`.

The `ksoloti-elements` firmware applies a local patch (`patches/ksoloti-elements-resonator-resolution.patch`) that reduces the resonator resolution for performance on the STM32F429. The original source is unmodified in the submodule.
