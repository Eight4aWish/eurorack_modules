# Eurorack Firmware Monorepo

Companion repo: [Eight4aWish/eurorack_electronics](https://github.com/Eight4aWish/eurorack_electronics) — analog breadboard layouts, drum-voice schematics, and a layout visualiser.

## Released modules

Firmwares offered as finished modules are named after the nursery rhyme
(*"one for sorrow, two for joy…"* — hence **Eight4aWish**). The first two, **Sorrow**
and **Joy**, live in [eurorack_daisy_patch_init](https://github.com/Eight4aWish/eurorack_daisy_patch_init);
the third lives here:

| Name | Firmware | Version | Based on | Licence |
| --- | --- | --- | --- | --- |
| **Girl** | [`ksoloti_elements`](src/ksoloti_elements/) | v1.2.2 | Mutable Instruments Elements | MIT |

These are independent community works. Product names of other makers are used only to
describe each firmware's origin — **not affiliated with, or endorsed by, Mutable
Instruments or Ksoloti.**

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

## Teensy 4.1 — `teensy_move`

A Teensy 4.1 Ableton Move ↔ Eurorack bridge and audio processor: two on-board CV/Gate channels, two expander channels via a 74HCT595 + two MCP4822 DACs, four drum triggers, manual mod-pot sweeps on the STATUS page, and an SGTL5000 stereo passthrough with a Filter → Delay → Reverb FX send. USB is composite MIDI + Serial (`USB_MIDI_SERIAL`).

- **Two OLED pages**, toggled by short-pressing the front button:
  - **Page 0 — STATUS**: all four channels on one screen (gates, drums, clock/reset, pitch + mod voltages, last MIDI event). MIDI ch 1–4 → Gate + Pitch CV (V/Oct) + Mod CV; ch 10 notes 36–39 → drum triggers. **Pot 1–4 manually sweep Mod 1–4** across −5…+5 V with soft-takeover; mod source per channel is velocity, **CC#42** (the live-coding mod CC), or pot — last actuator wins.
  - **Page 1 — FX**: Stereo Filter → Delay → Reverb send on the audio passthrough (Pot 1–4 = cutoff / delay time / delay amount / reverb mix). Always-on and clean by default; the CV bridge keeps running underneath.
- **Long-press**: emits a Reset pulse (on every page).
- **OLED sleeps** after 10 s (button or pot move wakes it; MIDI alone never does) to keep the panel's switching noise off the audio.
- **MIDI clock**: 24 PPQN → quarter-note pulses on `PIN_CLOCK`; Start emits a Reset pulse.
- **Per-channel calibration** (DMM-fitted slope/offset arrays in [include/teensy_move/calib_static.h](include/teensy_move/calib_static.h)).

Build & upload:

```sh
pio run -e teensy41
pio run -e teensy41 -t upload
```

See [docs/TEENSY_MOVE.md](docs/TEENSY_MOVE.md) for full pin map, OLED page layouts, mod-source rules, and troubleshooting.

## Teensy 4.1 — `teensy_chaos`

A 10 HP chaotic / fractal synthesis voice that exploits the Teensy 4.1's 600 MHz Cortex-M7 (hardware FPU, RK4 per audio sample) for stereo output via the Teensy Audio Shield (SGTL5000, I2S). Two attractor state variables drive the stereo audio out and a pair of MCP4822 CV outputs (X/Y); four CV inputs arrive through an ADS1115.

- **Algorithms**: the shipping firmware cycles **6 continuous-ODE attractors** — Rössler, Van der Pol, Lorenz, Chua, Duffing, Coupled Rössler — by short-pressing BTN. (A larger 14-algorithm suite across Melodic / Percussive / Texture groups is the design roadmap, not yet wired up.)
- **Controls**: CHAOS (bifurcation), RATE (integration step / frequency), CHAR (secondary parameter), DEPTH (mix / envelope decay). MOD adds bipolar modulation to CHAOS; ASGN is a menu-assignable mod target.
- **V/Oct by oversampling**: CLK pitch raises the integration sub-step count per sample rather than enlarging `dt`, so even stiff systems (Lorenz/Chua) track 1 V/oct over several octaves without diverging. The ceiling on those extra steps is **per algorithm** (`ChaosBase::maxStepsPerSecond`), scaled by each system's measured per-step cost so none can overrun the audio budget.
- **Peak CPU on screen**: top-right of the OLED shows peak audio-ISR load, reset on each algorithm change.
- **Portable DSP core**: the attractors live in [`libs/chaos_core/`](libs/chaos_core/) — `ChaosBase`, the six algorithms and the registry, depending on nothing but `<math.h>`. `main.cpp` is the Teensy platform layer (audio graph, ADS1115, MCP4822, OLED, control loop). The core builds unchanged for Cortex-M7, STM32H7/Daisy and host compilers.
- **Onboard AD envelope (VCA)**, off by default: long-press BTN opens the **ENV page** where CHAOS toggles it on/off and CHAR / DEPTH set Attack / Decay. When on, an RST rising edge fires a one-shot for a self-contained percussion voice; when off the module is a free-running drone.
- **Display**: SSD1306 128×64 OLED — algorithm name, real-time phase-space plot, and parameter bars.

Build & upload:

```sh
pio run -e teensy_chaos
pio run -e teensy_chaos -t upload
```

See [docs/TEENSY_CHAOS_V2.md](docs/TEENSY_CHAOS_V2.md) for the settled v2 control surface, platform analysis and hardware reference, and [docs/TEENSY_CHAOS.md](docs/TEENSY_CHAOS.md) for the full I/O map, per-algorithm control detail, audio architecture, and the algorithm-suite roadmap.

## Pico 2 W — `pico2w_oc`

This target implements a menu-driven multi-patch Eurorack utility on Raspberry Pi Pico 2 W with an SSD1306 OLED, ADS1115 ADC inputs, and an MCP4728 quad DAC for CV outputs.

- Features:
	- OLED UI: Pot1 scrolls the home menu, button selects (short = next, long = enter); in-patch, short press = page/mode, long press = back to menu.
	- Panel convention: columns are voices — column A = Pot2 + IN1 + OUT1/OUT3, column B = Pot3 + IN2 + OUT2/OUT4 (OUT1/OUT2 = pitch row, OUT3/OUT4 = gate row).
	- Patches: Clock, Quant, Euclid, Env (dual envelopes), QuadLFO, Scope, UsbMIDI, NetMIDI, Turing, Acid, Diag.
	- **UsbMIDI / NetMIDI**: MIDI-to-CV over USB and over WiFi (RTP-MIDI / AppleMIDI "Network MIDI") respectively, sharing one engine with two short-press-toggled modes — **DUO** (two independent gate/pitch voices on two MIDI channels: A = OUT1/OUT3, B = OUT2/OUT4) and **CLK** (a gate/pitch voice on OUT1/OUT3 plus MIDI clock/reset on OUT2/OUT4). Channels/division set via Pot2/Pot3. NetMIDI: connect from macOS Audio MIDI Setup (Network) or rtpMIDI on Windows via the module's IP (port 5004); WiFi credentials in `include/pico2w_oc/secrets.h` (gitignored — copy `secrets.h.example`).
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

## ESP32 Clk/Link — `esp32_clklink`

This target turns an ESP32-Dev + MCP4728 board into a Eurorack clock and reset generator that can run standalone or sync to an Ableton Link network on the same WiFi. ON-OFF-ON switch picks OFF / INTERNAL / LINK.

- Features:
	- INTERNAL mode: pot sets BPM (40–300), Channel A clocks, Channel B fires reset on entry and on any external CV trigger.
	- LINK mode: Channel A pulses on each beat (PPQN=1), Channel B fires reset on each bar boundary (quantum=4), Channel C tracks the pot as a manual CV (0–10 V).
	- WiFi credentials in `include/shared/secrets.h` (gitignored — copy `include/shared/secrets.h.example`).

- Build & Upload:

```sh
# ESP32 Clk/Link
pio run -e esp32_clklink
pio run -e esp32_clklink -t upload
pio device monitor -b 115200
```

See `docs/ESP32_CLKLINK.md` for the full channel map, LED status patterns, and tuning.

## ESP32 Clk/Link/Rec — `esp32_clklinkrec` (in development)

A successor to `esp32_clklink`. Combines the Link-synced clock generator with a **Recorder trigger**: pressing the Capture button on the front panel sends an HTTP POST to a Mac-side menu-bar app that saves the last N seconds of audio it was playing. Same Link sync behaviour, smaller BOM (no DAC, no op-amps), sharper Eurorack triggers via a 74HCT14 Schmitt trigger.

- **Hardware**: Seeed Studio XIAO ESP32-C5 (dual-band Wi-Fi 6, USB-C) + 74HCT14 hex inverting Schmitt trigger. 4 HP n8synth control board panel — two cells with LED + momentary button, four cells for jacks.
- **Outputs**: Clock, Reset, Running (all 0/+5 V triggers via the Schmitt trigger).
- **Inputs**: Reset In (external trigger to realign Link phase), Capture button, Link toggle.
- **Mac counterpart**: lives in a sibling repo at [`~/GitHub/seeed-recorder`](https://github.com/Eight4aWish/seeed-recorder).
- **Status**: hardware design and protocol locked. Firmware skeleton in place; implementation pending.

See [docs/ESP32_CLKLINKREC.md](docs/ESP32_CLKLINKREC.md) for the hardware design, pin allocation, and netlist. The wire protocol between the firmware and the Mac app lives in [docs/RECORDER_PROTOCOL.md](docs/RECORDER_PROTOCOL.md).

## AI Module (CortHex) — `nanoesp32_corthex`

A Eurorack voice driven by an LLM. Talks to the user via three web pages, drives a Plaits + Swords + T03 patch through six calibrated CV outputs, and uses panel buttons 1–6 as a 6-slot patch bank that an LLM populates with variations on a prompt.

- **Hardware**: Arduino Nano ESP32 (NORA-W106 / ESP32-S3, 1M-context-class flash). Six CV outputs through 3× MCP4822 + bipolar shift, audio listening tap on A0/A1, clock input on D9, 7 panel buttons + LEDs. Full schematic and panel layout in [docs/NANOESP32_CORTHEX_HARDWARE.md](docs/NANOESP32_CORTHEX_HARDWARE.md).
- **Three web pages** (all served by the firmware, on the module's LAN address):
  - `/` — diagnostics: live telemetry, panel buttons, audio level, gate input, CV output voltages, system info, four sample patches with trigger/release.
  - `/plaits` — Plaits-only control: 24-engine picker organised by bank (orange / green / red), three macro sliders for Timbre / Harmonics / Morph, per-engine OUT / AUX / macro reference text from the v1.2 manual, internal-LPG envelope explainer.
  - `/llm` — natural-language patch generation: chat with a Mac-side LLM proxy, view a 6-patch bank as cards, telemetry highlights the slot the user selects via the panel buttons.
- **Mac LLM proxy**: small FastAPI service at [tools/llm-proxy/](tools/llm-proxy/) that translates prompts into 6 distinct, named patches via Claude. The iPad page calls the proxy; the proxy forwards bank to the module. See its README for setup.
- **AR envelope engine**: every CV channel can run a gate-driven attack/release envelope using the `D9` gate input, so generated patches can have per-channel dynamics (filter sweeps, VCA envelopes) on top of Plaits' own internal LPG.

Build & upload:

```sh
# First flash via DFU (USB)
pio run -e nanoesp32_corthex -t upload

# Subsequent OTA flashes (over WiFi, mDNS hostname from secrets.h)
AI_MODULE_OTA_PASS='your-ota-password' pio run -e nanoesp32_corthex_ota -t upload
```

WiFi credentials and OTA password live in `src/nanoesp32_corthex/secrets.h` (gitignored — copy from `secrets.h.example`).

See [docs/NANOESP32_CORTHEX.md](docs/NANOESP32_CORTHEX.md) for the firmware architecture, HTTP API, and per-page usage notes.

## AMYboard — `amyboard_patchbank`

A **MicroPython / Tulip** app (not a PlatformIO env) for the shorepine AMYboard (ESP32-S3 + the AMY synth engine). Turns the board into a browse-and-play synth voice: spin the rotary encoder through a curated bank of **10 pads + 10 basses** on the 128×128 OLED, press to load, tweak via a one-screen macro page, and play from CV/Gate or TRS MIDI.

- **Hardware**: shorepine AMYboard (runs Tulip/MicroPython stock) + front-panel I2C accessories — Adafruit SSD1327 128×128 OLED (`0x3D`) and Seesaw rotary encoder (`0x36`) on the front Grove/STEMMA bus (SDA=GPIO17, SCL=GPIO18, 400 kHz).
- **Play**: CV0 = 1 V/oct pitch, CV1 = gate; TRS MIDI in (notes, omni + polyphonic).
- **Macros**: up to 4 encoder-tweakable parameters per patch (TONE / RES / SPACE / MOVE), all on one screen, applied live while it sounds.
- **⚠️ Don't cascade MIDI _clock_** into the board — Tulip slaves its frame clock to it and the UI freezes when the clock stops. Sync via Ableton Link instead; MIDI notes are unaffected.

Deploy is over the board's MicroPython REPL, not `pio`: copy `pbdata.py` + `patchbank.py` to `/user/`, and `sketch.py` to `/user/current/sketch.py` to make it the boot app (`mpremote` fights this board's USB-CDC — copy over the raw REPL).

See [src/amyboard_patchbank/README.md](src/amyboard_patchbank/README.md) for hardware bring-up, the macro/MIDI details, deployment, and the desktop sound-rendering harness.

## Girl (Ksoloti Big Genes) — `ksoloti_elements`

**Girl** — a modal synthesis voice for the [Ksoloti Big Genes](https://ksoloti.github.io/7-big_genes.html) Eurorack module (STM32F429 @ 168 MHz + ADAU1961 codec), the third in the rhyme-named family. Based on **Mutable Instruments Elements** by Émilie Gillet (MIT); *not affiliated with, or endorsed by, Mutable Instruments or Ksoloti.*

- **Status**: Fully playable with single-page OLED UI, three pot states, CV assignment, all buttons and encoders working.
- **Audio**: L in = blow exciter, R in = strike exciter, L out = main, R out = aux (reverb).
- **Resonator**: 40 modes (reduced from 52 to fit CPU budget at 168 MHz). Three models selectable via S1: modal, string, chords.
- **Controls**: POT1-4 = resonator (geometry/brightness/damping/position, CV-summable). POT5-7 are one exciter each - bow, blow, strike - and S4 cycles which of that exciter's parameters they hold: levels, then meta, then timbres. POT8 = space and E1 = contour in every state, so neither ever pages. CV A-C = assignable modulation (S2 selects, E2 assigns target). CV D = gate + velocity. CV X = V/Oct. CV Y = FM. S3 = play (manual gate).
- **Display**: Single-page OLED with control reference, CV assignments with the selected slot underlined, and real-time parameter name + value on pot/encoder activity.
- **Indicators**: LED1 green = gate. LED2 red = CPU overload. LED4 dual = resonator model (green/red/both). Gate1 = gate echo output.
- **Resources**: RAM 44.8%, Flash 19.7%.

See [docs/KSOLOTI_ELEMENTS.md](docs/KSOLOTI_ELEMENTS.md) for full control mapping, secondary parameters, ADC details, and the pre-built-binary install guide.

### Setup

```sh
# After cloning, initialise submodules
git submodule update --init --recursive

# Build — the resonator resolution is set into the vendored source automatically
pio run -e ksoloti_elements

# Flash via DFU (board must be in DFU mode)
pio run -e ksoloti_elements -t upload
```

### Architecture

```
src/ksoloti_elements/
  main.cc              — Entry point, Elements DSP, control loop, parameter mapping
  adc.cc / adc.h       — ADC1 DMA (10ch) + ADC3 polled (4ch) + button GPIO
  codec.cc / codec.h   — SAI1 + ADAU1961 driver (I2C2, DMA double-buffer)
  oled.cc / oled.h     — SH1106 128x64 OLED driver (I2C1, page-at-a-time update)
  font5x7.h            — 5x7 bitmap font (ASCII 32-126)
  elements/drivers/
    debug_pin.h        — Local shim (empty stubs for hardware debug pins)

scripts/
  elements_build.py    — PlatformIO build script (FPU flags, source dirs,
                       and RESOLUTION: the resonator mode count, 40)

third_party/eurorack/  — Git submodule: pichenettes/eurorack (MIT license)
  elements/dsp/        — Elements DSP core
  stmlib/              — Mutable Instruments DSP/utility library
```

## Libraries

- **expander I/O**: `libs/expander_io` — 74HC595 expander driver (`Expander595`) and MCP4822 helper (`Mcp4822Expander`). See [libs/expander_io/README.md](libs/expander_io/README.md) for API and wiring.
- **OLED UI**: `libs/eurorack_ui` — reusable SSD1306 menu helpers (`OledMenu`, `OledHomeMenu`, `OledHelpers`) for short/long-press menu navigation. Used by `pico2w_oc`.

## Credits & Licenses

Original work in this repository is **MIT-licensed** — see [`LICENSE`](LICENSE).
The exception is the [`esp32_clklink`](src/esp32_clklink/) module, which
links against the Ableton Link library and is therefore distributed as
**GPL-2.0-or-later** — see [`LICENSE.esp32_clklink`](LICENSE.esp32_clklink).

The full list of third-party code each module depends on, with licenses,
is in [`NOTICE.md`](NOTICE.md). Headline acknowledgments:

- **Ableton Link** (GPL-2.0-or-later) + **docwilco/esp_abl_link** (GPL-2.0-or-later) — the beat-sync engine that makes `esp32_clklink` work. This is what triggers the GPL on that module.
- **Mutable Instruments Elements + stmlib** (MIT, [Émilie Gillet](https://github.com/pichenettes)) — the modal-synthesis DSP that `ksoloti_elements` (**Girl**) ports to the Ksoloti Big Genes board. Vendored under `third_party/eurorack/`; each file retains its original MIT header. The Elements port rewrites one line — the resonator mode count, set by `RESOLUTION` in `scripts/elements_build.py` — into `elements/dsp/voice.cc` at build time; the third-party source is otherwise unmodified.
- **ESP-IDF** (Apache-2.0, Espressif) + **Arduino-ESP32** (LGPL) + **pioarduino/platform-espressif32** — runtime for `esp32_clklink` and `nanoesp32_corthex`.
- **Teensyduino** (MIT, PJRC) — for `teensy_chaos` and `teensy_move`.
- **Adafruit**, **ArduinoJson**, **ESPAsyncWebServer**, **FortySevenEffects MIDI Library**, **Bounce2** — peripheral and protocol libraries across multiple modules. Full attributions per module in [`NOTICE.md`](NOTICE.md).
