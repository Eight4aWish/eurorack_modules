# AI Modules — Shared Hardware Design

Two firmwares share one PCB:
- **Surprise Machine** — on-device VAE generates clock-locked 1-bar phrases; hold-to-keep, all-six-held = like.
- **LLM module** — cloud LLM (Anthropic API) drives closed-loop sound exploration via tool calls; web UI over WiFi.

Hardware is identical. Software is the only differentiator.

This doc is the canonical hardware spec. Both design briefings (`surprise-machine-briefing.md`, `llm-eurorack-module-design.md`) defer to it where they conflict.

---

## 1. Locked decisions

| Decision | Choice |
|---|---|
| MCU | Arduino Nano ESP32 (NORA-W106 / ESP32-S3, 8 MB PSRAM, 16 MB flash) |
| Power input | Eurorack +12V → Nano VIN. Separate 5V buck for MCP4822 chain. |
| Panel | 10HP, 3 columns × 6 rows on N8Synth 10HP Control Deck |
| Construction | Two stacked N8Synth decks (top = Control Deck, bottom = component deck) |
| Faceplate | 3D-printed (David). Allows button + LED in single N8 cell using a–b / c–d pads. |
| Audio passthrough | Hardwired direct (jack-to-jack). Listening is a parallel high-impedance tap. |
| CV outputs | 6 channels via 3× MCP4822 (write-only, no daisy-chain), bipolar ±5V via TL074. Reference is a buffered voltage divider (~2.04 V) — works fine; LM4040 remains an option for higher stability. |

---

## 2. Panel layout

```
 col 1                  col 2                col 3
┌─────────────────┬─────────────────────┬──────────────┐
│ master btn+LED  │ ch 1 btn + LED      │ CV out 1     │  row 1
├─────────────────┼─────────────────────┼──────────────┤
│ clock in        │ ch 2 btn + LED      │ CV out 2     │  row 2
├─────────────────┼─────────────────────┼──────────────┤
│ audio L in      │ ch 3 btn + LED      │ CV out 3     │  row 3
├─────────────────┼─────────────────────┼──────────────┤
│ audio R in      │ ch 4 btn + LED      │ CV out 4     │  row 4
├─────────────────┼─────────────────────┼──────────────┤
│ audio L out     │ ch 5 btn + LED      │ CV out 5     │  row 5
├─────────────────┼─────────────────────┼──────────────┤
│ audio R out     │ ch 6 btn + LED      │ CV out 6     │  row 6
└─────────────────┴─────────────────────┴──────────────┘
```

Master button + master LED share row 1 of column 1, using the same N8 cell layout as column 2 cells (button on a–b, LED on c–d).

Column 2: button wired to N8 cell pads **a–b**, LED wired to pads **c–d**.

---

## 3. Pin map (Arduino Nano ESP32)

Pin assignment chosen to match physical panel layout: odd channels (1/3/5) on right side of Nano, even channels (2/4/6) on left side, button + LED for each channel adjacent on the same side.

| Pin label | GPIO | Function | Notes |
|---|---|---|---|
| D13 | GPIO48 | SPI SCK | hardware SPI |
| A0 | GPIO1 | Audio L in | ADC1 |
| A1 | GPIO2 | Audio R in | ADC1 |
| A2 | GPIO3 | Channel 2 button | **strapping pin** (JTAG select) — see below |
| A3 | GPIO4 | Channel 2 LED |  |
| A4 | GPIO11 | Channel 4 button | ADC2, used as digital input |
| A5 | GPIO12 | Channel 4 LED | ADC2, used as digital output |
| A6 | GPIO13 | Channel 6 button | ADC2, used as digital input |
| A7 | GPIO14 | Channel 6 LED | ADC2, used as digital output |
| D12 | GPIO47 | Master button | reclaimed from MISO (MCP4822 is write-only) |
| D11 | GPIO38 | SPI MOSI | hardware SPI |
| D10 | GPIO21 | Master LED |  |
| D9 | GPIO18 | Clock in |  |
| D8 | GPIO17 | Channel 1 button |  |
| D7 | GPIO10 | Channel 1 LED |  |
| D6 | GPIO9 | Channel 3 button |  |
| D5 | GPIO8 | Channel 3 LED |  |
| D4 | GPIO7 | Channel 5 button |  |
| D3 | GPIO6 | Channel 5 LED |  |
| D2 | GPIO5 | MCP4822 #1 CS |  |
| D0/RX0 | GPIO44 | MCP4822 #2 CS | UART RX sacrificed |
| D1/TX0 | GPIO43 | MCP4822 #3 CS | UART TX sacrificed |

All 22 GPIOs assigned. No spare for V2 expansion (e.g. variety knob). Both UART pins fully sacrificed — debug is via USB-CDC Serial only.

**A2 / GPIO3 strapping caveat.** GPIO3 selects the JTAG signal source at reset (USB-bridge JTAG vs. external GPIO39–42 JTAG). For a button input wired to GND: the button is normally open at reset, so the pin floats per its strap default and JTAG behaves normally. Edge case: if button 2 is held during reset, JTAG switches to external mode — affects debugger sessions but not USB-CDC Serial. Acceptable risk. Do **not** put an LED here instead — that would pull GPIO3 LOW at every reset and permanently lock JTAG to external mode.

**Verify before committing**: confirm A0 and A1 are ADC1 channels on the Nano ESP32 datasheet. ESP32-S3's ADC1 = GPIO1–10, ADC2 = GPIO11–20. ADC2 conflicts with WiFi when used as ADC, which is why audio must be on ADC1.

PWM (LEDC peripheral) is available on any GPIO, so all LEDs support brightness/breathing without re-mapping.

### 3.1 Visual layout

Nano ESP32 viewed component-side up, USB-C at the top.

```
                            USB-C
                         ┌───────┐
                    ╔════╧═══════╧════╗
   SPI SCK     D13 ─╢ ●             ● ╟─ D12   Master button
                    ║                  ║
                    ╢ ● 3V3        D11 ● ╟─    SPI MOSI
                    ║                  ║
                    ╢ ● AREF       D10 ● ╟─    Master LED
                    ║                  ║
   Audio L     A0  ─╢ ● ADC1        D9 ● ╟─    Clock in
                    ║                  ║
   Audio R     A1  ─╢ ● ADC1        D8 ● ╟─    Ch 1 button
                    ║                  ║
   Ch 2 button A2  ─╢ ● ADC1*       D7 ● ╟─    Ch 1 LED
                    ║                  ║
   Ch 2 LED    A3  ─╢ ● ADC1        D6 ● ╟─    Ch 3 button
                    ║                  ║
   Ch 4 button A4  ─╢ ● ADC2        D5 ● ╟─    Ch 3 LED
                    ║                  ║
   Ch 4 LED    A5  ─╢ ● ADC2        D4 ● ╟─    Ch 5 button
                    ║                  ║
   Ch 6 button A6  ─╢ ● ADC2        D3 ● ╟─    Ch 5 LED
                    ║                  ║
   Ch 6 LED    A7  ─╢ ● ADC2        D2 ● ╟─    CS 1 (MCP4822 #1)
                    ║                  ║
                    ╢ ● 5V         GND ●
                    ║                  ║
                    ╢ ● RST         RST ●
                    ║                  ║
                    ╢ ● GND     RX/D0 ● ╟─    CS 2 (MCP4822 #2)
                    ║                  ║
   +12V        VIN ─╢ ● VIN     TX/D1 ● ╟─    CS 3 (MCP4822 #3)
   from PSU        ║                  ║
                    ╚══════════════════╝

Legend:
   ADC1  = audio-capable, no WiFi conflict
   ADC2  = digital I/O only (WiFi-conflicting if used as ADC)
   *     = GPIO3 strapping pin (JTAG select); see §3 caveat
```

The signal groups, from a wiring standpoint:

- **Even channels (2, 4, 6)**: button + LED pairs adjacent on the **left** side (A2–A7).
- **Odd channels (1, 3, 5)**: button + LED pairs adjacent on the **right** side (D3–D8).
- **Master button + Master LED**: top of right side (D10, D12), near the panel's column 1 row 1 cell.
- **SPI bus to MCPs** (D11/D13 plus three CS lines): top-right and bottom-right of the board — clean bus crossing to the MCP4822s on the bottom deck.
- **Audio in** (A0, A1): adjacent on the left side, top — short analog trace to the listening-tap conditioning.
- **Clock in** (D9): right side, near the master controls.

---

## 4. Power architecture

```
Eurorack +12V ──┬── Nano VIN (Nano's onboard buck → 3.3V for MCU + ADC ref)
                │
                └── 5V buck regulator ──┬── MCP4822 #1 Vdd
                                        ├── MCP4822 #2 Vdd
                                        ├── MCP4822 #3 Vdd
                                        └── LM4040-2.048 reference (via 2.7kΩ)

Eurorack +12V ── TL074 V+ (audio + CV bipolar shift sections)
Eurorack -12V ── TL074 V−
Eurorack GND  ── common ground (single point)
```

**Buck choice.** Any small step-down handling 12V→5V at ≥250 mA. Three MCP4822s draw ~1 mA each at idle, more during writes — total well under 50 mA. Headroom for the LM4040 (~5 mA) and any future 5V loads.

**Decoupling.**
- 100 nF ceramic at every IC Vdd / V+ pin to GND, as close as possible.
- 10 µF electrolytic bulk near the buck output.
- 10 µF bulk on +12V and −12V at the TL074s.

**Power LED.** Optional. If wanted, hardware-tied to the 5V rail through 1 kΩ → LED → GND. Lights whenever rails are alive regardless of firmware. Not on the panel in the locked layout — could go on the bottom deck for assembly verification.

---

## 5. CV output chain (×6)

Per-channel signal flow:

```
MCP4822 OUT (0–4.096 V, 12-bit)
     │
     ├── 1 kΩ series ──┬── TL074 (difference-amp bipolar shift)
     │                  │           gain ≈ 2.44, offset −5 V
     │                  │           reference: LM4040-2.048
     │                  │
     │                  └── output 0–4.096 V → ±5 V
     │
     └── filter cap (100 pF) to GND if reconstruction noise observed
                                            │
                                            ├── 470 Ω series
                                            ├── BAT85 to +12V (clamp)
                                            ├── BAT85 to −12V (clamp)
                                            └── jack tip
```

### 5.1 MCP4822 wiring (×3 chips)

8-pin DIP pinout:

| Pin | Function | Connection |
|---|---|---|
| 1 | Vdd | +5V (with 100 nF to GND at pin) |
| 2 | CS̄ | per-chip CS line from Nano (D10, D12, A2) |
| 3 | SCK | shared SPI SCK from Nano (D13) |
| 4 | SDI | shared SPI MOSI from Nano (D11) |
| 5 | LDAC̄ | tied to GND (immediate update) |
| 6 | Vout B | to bipolar shift stage B |
| 7 | Vss | GND |
| 8 | Vout A | to bipolar shift stage A |

Daisy-chaining is **not** supported (no SDO). Three CS lines, parallel SPI bus.

### 5.2 LM4040-2.048 reference (×1, shared)

```
+5V ── 2.7 kΩ ──┬── LM4040 cathode ── GND  (anode)
                │
                └── 100 nF to GND
                │
                └── to all 6× bipolar shift stages (high-Z input, no buffer needed
                    if the difference-amp resistors are ≥10 kΩ)
```

One LM4040 supplies all six shift stages. SOT-23 or TO-92 package.

### 5.3 Bipolar shift stage (×6, two TL074s)

Difference amplifier topology, one TL074 section per output. Six channels = 6 op-amp sections = 1.5 TL074s, round up to **2× TL074**.

Per-section resistors (1% metal film, 10 kΩ family):

```
DAC out ──[10 kΩ]──┬──── TL074 (−)
                    │
                   [10 kΩ]
                    │
                   GND

VREF (2.048V) ──[10 kΩ]──┬──── TL074 (+)
                          │
                         [24.3 kΩ]   (sets gain to ~2.44 for ±5V swing)
                          │
                         GND

TL074 out ── 1 kΩ ──> output protection
```

Tune the 24.3 kΩ value by trimpot if you want exact ±5.000 V swing on the bench. 24.3 kΩ is the closest E96 value to the calculated 24.32 kΩ.

### 5.4 Output protection (×6)

```
TL074 out ── 470 Ω ──┬─── jack tip
                      ├── BAT85 to +12V
                      └── BAT85 to −12V
```

470 Ω limits short-circuit current to ~25 mA (well within TL074 spec). BAT85 Schottkys clamp any back-driven voltage from a downstream module to ±12V + 0.3 V drop, well below TL074 absolute max.

---

## 6. Audio path

### 6.1 Passthrough (hardwired)

```
L in jack ──────────────── L out jack    (pure wire, no buffer)
R in jack ──────────────── R out jack
```

Zero active components. Audio flows even with the module unpowered. Eurorack signals are typically driven from low-impedance sources, so the parallel 100 kΩ tap (next section) doesn't audibly load them.

### 6.2 Listening tap (×2 channels: L and R)

One TL074 section per channel = 2 sections. (Stereo conditioning costs nothing extra since TL074 is quad — V1 firmware can use mono or stereo at will.)

```
Audio jack ──[Cin]──[Rin 100kΩ]──┬──── TL074 (−)
                                  │
                                 [Rf 33kΩ ‖ Cf 220 pF]
                                  │
                                  └── TL074 out ── 100 Ω ──┬── ADC pin (A0/A1)
                                                            ├── BAT85 to GND
                                                            └── BAT85 to 3.3V

Bias for TL074 (+) input:
+3.3V ──[10 kΩ]──┬──── TL074 (+)
                  │
                 [10 kΩ]
                  │
                  └──[10 µF to GND]
                  │
                 GND
```

**Component values, in plain English.**
- **Cin = 220 nF film** (non-polarised box film, e.g. Wima MKS): AC-couples the input. Without this cap, the V+ bias at 1.65 V multiplies through (1 + Rf/Rin) and the output DC sits at 2.2 V instead of 1.65 V — loud peaks then clip on the upper Schottky. Cin and Rin form a high-pass at ~7 Hz, comfortably below audio. Don't use an electrolytic — the input swings through 0 V.
- Rin = 100 kΩ: light tap, high enough not to load the source.
- Rf = 33 kΩ: gain ≈ 0.33, scales ±5V Eurorack hot to ±1.65V (AC).
- Cf = 220 pF: anti-alias single-pole rolloff at ~22 kHz, in parallel with Rf.
- Bias 1.65V from a 10k+10k divider off 3.3V, decoupled with 10 µF: doesn't need a precision reference. Op-amp's CMRR rejects rail noise; firmware calibrates DC offset by measuring silence baseline. (1.63 V or any nearby value works fine.)
- BAT85 clamps + 100 Ω at ADC pin: protects ADC from overshoot.

Output sits at 1.65 V centred, ±1.65 V swing → 0–3.3 V into ADC. Perfect for 12-bit conversion.

### 6.3 TL074 budgeting

Total TL074 sections needed: **6 (bipolar shift) + 2 (audio listening) = 8 sections = 2× TL074**.

If you want to split for cleaner layer routing: 1× TL074 on the bottom deck for shift channels 1–4, 1× TL074 split between deck 1 (audio listening, 2 sections) and deck 2 (shift channels 5–6, 2 sections). Or just put both TL074s on the bottom deck and bring the audio-listening signal up via short interconnect.

---

## 7. Clock input

```
Clock jack tip ──[100 kΩ]──┬───[100 kΩ]── GND
                            │
                            ├── BAT85 to 3.3V
                            ├── BAT85 to GND
                            │
                            └── Nano D3 (GPIO6, internal pull-down enabled)
```

50% attenuation handles up to 10 V hot signals. Schottky clamps protect the GPIO. ESP32-S3 GPIO has small internal hysteresis — sufficient for sharp Eurorack clock edges. If a particular real-world clock source causes false triggers, debounce in software (timestamp-based, ignore edges within 1 ms) or drop in a 74LVC1G14 single-gate Schmitt later.

Interrupt on rising edge.

---

## 8. Buttons (×7 — master + 6 channel)

All buttons are tact switches between GPIO and GND, with the MCU's internal pull-up enabled. Active-low.

Per button:

```
Nano GPIO ── tact switch ── GND
              (with optional 100 nF across switch terminals
               for hardware debouncing if software debounce proves insufficient)
```

In N8 cells, the button uses pads **a–b**: pad a → MCU GPIO, pad b → GND.

Software debouncing only (20–50 ms typical). No external pull-up resistor needed; `INPUT_PULLUP` in firmware enables the internal one.

---

## 9. LEDs (×7 — master + 6 channel)

Per LED:

```
Nano GPIO ── 1 kΩ ── LED anode
                     LED cathode ── GND
```

In N8 cells, the LED uses pads **c–d**: pad c → through 1 kΩ → MCU GPIO, pad d → GND.

**Polarity.** LED **anode** (long leg, round case side) → pad c (resistor / GPIO side). LED **cathode** (short leg, flat case side) → pad d (GND side). Active-high drive: `digitalWrite(pin, HIGH)` → LED on.

1 kΩ at 3.3 V drives ~1.7 mA into a red LED — comfortably bright and well within the ESP32-S3's 40 mA per-pin source limit. For green/blue/white LEDs reduce to 470 Ω.

LEDs driven from PWM (LEDC peripheral) for brightness control, breathing, and confirmation pulses.

---

## 10. Layer assignment (two-deck stack)

The N8Synth stack has a top Control Deck (panel-facing) and a bottom deck. Components grouped to minimise inter-deck signal crossings.

### Top deck (Control Deck)
- All panel jacks: master button, clock in, audio L/R in, audio L/R out, 6× CV out
- All 6× channel buttons + LEDs (in N8 cells)
- All LED current-limit resistors (1 kΩ ×6) — keeps short traces from GPIO to LED
- Clock input divider + clamps (so the conditioned signal is the only thing crossing decks)
- **Optional**: Nano ESP32 on top deck if you want USB-C accessible from the panel face for programming/debug. Otherwise put it on the bottom deck and rely on OTA updates.

### Bottom deck
- 5V buck regulator + bulk decoupling
- 3× MCP4822 + per-chip 100 nF decoupling
- LM4040-2.048 reference + 2.7 kΩ + 100 nF
- 2× TL074 (6 bipolar shift sections + 2 audio listening sections)
- All resistor networks for bipolar shifts (60 resistors total — 10 per channel ×6)
- All output protection (470 Ω + 2× BAT85 ×6)
- Audio listening conditioning (Rin, Rf, Cf, bias divider, ADC clamps) ×2

### Signals crossing decks

If Nano on top:
- 6× CV out signal (bottom → top jacks)
- 2× audio listening signal (top → bottom for conditioning, then conditioned → top to ADC pin)
  - **Better**: put the audio conditioning's TL074 input network on the top deck near the jack, pass the conditioned (low-impedance) signal to the bottom only if needed. Or just keep the whole conditioning on the top deck — it's only 2 op-amp sections.
- 5× SPI signals (MOSI, SCK, 3× CS) from Nano (top) → MCP4822s (bottom)

If Nano on bottom:
- 6× CV out signal (bottom → top jacks)
- 2× audio in signal (top → bottom)
- 7× button signal (top → bottom)
- 6× LED signal (bottom → top)
- 1× clock signal (top → bottom)
- USB-C buried; OTA firmware updates only

**Recommendation: Nano on top deck.** Programming convenience outweighs the minor wiring savings. Then group SPI signals as a 5-wire bus crossing to the bottom for the MCPs — clean and short.

---

## 11. Bill of materials

### ICs
- 1× Arduino Nano ESP32 (NORA-W106 module, ABX00083)
- 3× MCP4822 (8-pin DIP)
- 2× TL074 (14-pin DIP, JFET quad op-amp)
- 1× LM4040DIZ-2.0 (TO-92 voltage reference)
- 1× small 12V→5V buck regulator module (any reputable, ≥250 mA)

### Passives (per channel ×6 unless noted)
- Bipolar shift: 4× 10 kΩ + 1× 24.3 kΩ (1% metal film)
- DAC series: 1× 1 kΩ
- Output: 1× 470 Ω + 2× BAT85
- Optional output filter: 1× 100 pF

### Passives (audio listening, ×2 channels)
- 1× 220 nF film cap (Cin, AC coupling — must be non-polarised)
- 1× 100 kΩ + 1× 33 kΩ + 1× 220 pF (gain network)
- 2× 10 kΩ + 1× 10 µF (bias divider, shared between channels — only need one)
- 2× BAT85 + 1× 100 Ω (ADC clamp)

### Passives (clock input, ×1)
- 2× 100 kΩ + 2× BAT85

### Passives (LM4040 ref)
- 1× 2.7 kΩ + 1× 100 nF

### Passives (decoupling, distributed)
- ~12× 100 nF ceramic (one per IC Vdd, plus a couple at the Nano)
- 2× 10 µF electrolytic (12V/−12V bulk near TL074s)
- 1× 10 µF electrolytic (5V buck output)

### Panel hardware
- 7× tact switch (master + 6× channel buttons, all fit in N8 cells)
- 7× 3 mm LED (master + 6× channel LEDs, all fit in N8 cells)
- 11× 3.5 mm mono jack (1× clock in, 2× audio in, 2× audio out, 6× CV out)
- 3D-printed faceplate (David)

### Passives (LEDs)
- 7× 1 kΩ (red LED) — adjust to 470 Ω if using green/blue/white

---

## 12. Open items

These don't block bring-up but are worth resolving before final assembly:

- **Nano ESP32 ADC1 confirmation.** Verify A0/A1 = ADC1 channels on the Nano ESP32 datasheet. Locked map assumes yes.
- **Mono vs stereo listening for V1 firmware.** Hardware supports stereo (both A0 and A1 wired). Firmware decision only.
- **Power LED placement.** Not in the locked panel layout. Optional on the bottom deck for assembly verification.
- **Combined firmware (one binary, two modes).** Deferred. Surprise Machine and LLM module currently ship as separate binaries sharing a common library. Combining is feasible (master button long-press = mode switch) but adds complexity; revisit after LLM-first proves the substrate.

---

## 13. Build order (firmware-side)

The diagnostics page (§14) is built early so it serves as the bench instrument for everything that follows.

1. **Bare bring-up sketch.** Serial-only. Exercise every pin: read all buttons, drive all LEDs, sweep all 6 CV outs, sample audio ADC, detect clock edges. Smoke test on first power-on.
2. **WiFi + captive portal + static web server.** First boot opens an AP, serves a config page for home WiFi creds, stores in NVS, reboots into station mode. mDNS for `aimodule.local`.
3. **WebSocket telemetry + diag page v1.** Live view of all I/O — buttons, LEDs, ADC, clock, CV outs, system info. Browser-side controls to set CVs and toggle LEDs. From here on, the diag page is the bench instrument.
4. **Probe primitive endpoints.** Diag page can fire `sweep`, `pulse`, `ramp`, `play_envelope` and visualise what the audio ADC saw. This is exactly the LLM module's tool vocabulary.
5. **Shared library** (`libs/ai_module_common/`): promote `cv_out`, `audio_in`, `features`, `clock`, `ui`, `webserver` modules now they've been written once.
6. **LLM client + tool calling.** Wire Anthropic API. Diag page gains a chat UI panel. Closed-loop sound exploration with the Behringer Proton as initial target.
7. **Surprise Machine.** Separate binary on the same library + same diag page.

---

## 14. Diagnostics page

A persistent web-based debug surface, not a bring-up scaffold. Stays in production firmware. Once the chat UI exists (step 6), the diag page is reached via a button on the main interface.

### 14.1 Tech stack

- **ESPAsyncWebServer + AsyncTCP** (me-no-dev fork). Async HTTP + WebSocket on one server. Non-blocking so WiFi task on Core 0 stays responsive while audio + CV scheduler run on Core 1.
- **LittleFS** for static assets (HTML/CSS/JS). Uploaded via `pio run -t uploadfs`. Edit-and-reflash without rebuilding C++.
- **WiFiManager** (tzapu) for first-run captive portal.
- **mDNS** (built into Arduino-ESP32) for `aimodule.local`.
- **Vanilla JS + WebSocket** on the page. No framework. `<canvas>` for the audio scope. Target <50 KB total static assets.

### 14.2 First-run flow

1. Boot → check NVS for stored WiFi creds.
2. If absent or connection fails for 30 s → start AP mode, SSID `AI-Module-XXXX` (last 4 of MAC), open captive portal.
3. User connects, picks home network, enters password. Stored to NVS. Reboot.
4. Subsequent boots connect directly. mDNS announces `aimodule.local`.

### 14.3 Reset gesture

Hold **master + channel 1 buttons at power-on for 3 s** → wipe NVS WiFi creds, reboot into AP mode. LED 1 pulses while the gesture is being recognised so the user gets feedback before commit.

### 14.4 Auth

None on V1. Trusted-LAN model. Anyone with rack access is already in the trust boundary.

### 14.5 What the page shows

**Telemetry (live, ~30 Hz over WebSocket):**
- All 7 button states (held / pressed-count since page load)
- Audio ADC L/R: live RMS bar + scrolling waveform (~1 s window)
- Clock: pulse count, last interval (ms), BPM estimate
- 6× CV out: current value (V), 12-bit code
- 6× LED: state, PWM duty
- System: free heap, RSSI, uptime, IP

**Controls:**
- Per-CV slider: set output to any value −5 to +5 V. Locks out firmware writes to that channel while held.
- Per-LED click-to-toggle and brightness slider.
- Probe panel: pick channel + primitive (`sweep`, `pulse`, `ramp`, `play_envelope`, `start_lfo`, `pulse_trigger`), set parameters, fire. Audio scope highlights the response window.
- WiFi reset button (with confirmation).

### 14.6 Endpoint design

- `GET /` → static index, served from LittleFS
- `GET /assets/*` → static CSS/JS/fonts
- `WS /ws` → bidirectional. Server pushes telemetry frames; client sends control messages.
- `POST /api/probe` → fire a probe primitive (also reachable via WS for lower-latency)
- `POST /api/wifi-reset` → wipe creds and reboot

### 14.7 PlatformIO env additions

When the env is set up:
```ini
lib_deps =
  esp32async/ESPAsyncWebServer @ ^3.6.0
  esp32async/AsyncTCP @ ^3.2.10
  tzapu/WiFiManager @ ^2.0.17
board_build.filesystem = littlefs
```

### 14.8 Firmware — use SPI3 (HSPI), not the default SPI2 (FSPI)

ESP32-S3's FSPI peripheral has IOMUX direct-connection defaults for **all** its signals — CLK (GPIO12), Q/MISO (GPIO13), D/MOSI (GPIO11), HD (GPIO9), WP (GPIO14), CS0 (GPIO10). Several of those collide with our pin map (BUT 3 on D6=GPIO9, BUT 6 on A6=GPIO13, etc.). Even if firmware only specifies SCK/MOSI/CS, the FSPI peripheral can still claim its IOMUX defaults for HD/WP and silently break those GPIOs.

Always use SPI3 (HSPI) for the MCP4822 bus:

```cpp
#include <SPI.h>
SPIClass HSpi(HSPI);  // SPI3 — pure GPIO-matrix routing, no IOMUX defaults

void setup() {
  HSpi.begin(D13, -1, D11, -1);  // SCK, MISO, MOSI, SS — only these get claimed
  ...
}
```

---

## 15. Build log

### Phase 1 (complete, 2026-04-29 → 2026-05-02): hardware + bring-up

All hardware is built on the n8synth two-deck stack and validated end-to-end via the `[env:ai-module-bringup]` firmware. Status:

| Subsystem | Status |
|---|---|
| Master + 6× channel buttons | ✅ all 7 working |
| Master + 6× channel LEDs | ✅ all 7 working |
| Audio L/R listening tap | ✅ DC ~1967 (close to 2048 mid-rail), pp tracks input cleanly |
| Clock input | ✅ ISR-driven, BPM math correct |
| 3× MCP4822 SPI bus | ✅ all 6 raw outputs verified at unique fixed test voltages |
| 6× bipolar shift stages | ✅ ±5 V swing on all six, calibration table captured |
| Front-panel jacks | pending — output protection (1 kΩ + 2× 1N5819) + jack wiring |

### Substitutions vs. the doc spec

These are deviations made during the build. None affect functional outcomes; doc has been updated where relevant.

| Doc spec | Built | Why |
|---|---|---|
| 220 nF film (Cin, audio listening) | 470 nF film | David's bin had 100 nF and 470 nF; 470 nF gives more LF headroom |
| 220 pF ceramic (Cf, audio listening) | 100 pF ceramic | Bin part; looser anti-alias rolloff (48 kHz vs 22 kHz), still well above audio |
| BAT85 Schottky (×18 — clamps) | 1N5819 | Bin part; functionally equivalent at our currents |
| LM4040-2.048 reference | Buffered voltage divider (~2.04 V) | Tracks Vdd, but Vdd is fixed (4.96 V) and the divider is shared across all 6 channels |
| 24.3 kΩ Rf (gain ≈ 2.44, ±5 V at code 0/4095) | Different ratio giving gain ≈ 3.0 (±5 V at code ~370/~3700) | Deliberate — keeps ±5 V output inside the DAC's linear region, avoiding endpoint INL/DNL |

### CV calibration (per-channel)

Measured 2026-05-02 with Vdd = 4.96 V, Vref ≈ 2.04 V. Stored in `memory/project_ai_modules_cv_calibration.md`. Re-measure if Vdd or any bipolar-shift resistor changes.

### Phase 2 (next): WiFi + OTA + diag page

Per §13 step 2 onward.

---
