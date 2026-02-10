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

## Libraries

- expander I/O: `libs/expander_io` — 74HC595 expander driver (`Expander595`) and MCP4822 helper (`Mcp4822Expander`). See `libs/expander_io/README.md` for API and wiring.
