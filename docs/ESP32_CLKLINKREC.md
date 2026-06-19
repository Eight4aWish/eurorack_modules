# ESP32 Clk/Link/Rec

A Eurorack utility that combines two jobs in a single 4 HP module:

1. **Ableton Link clock generator** — joins a Link network over WiFi and
   emits Eurorack clock, reset, and run-gate triggers synchronised to the
   session's tempo and transport.
2. **Recorder trigger** — pressing the Capture button sends an HTTP POST
   to a Mac-side menu-bar app, which saves the last N seconds of audio
   it was playing. The audio recording itself happens on the Mac.

The module is a successor to [`esp32_clklink`](ESP32_CLKLINK.md) — same
core Link-sync behaviour, plus the Recorder trigger feature and a
hardware refresh.

## Hardware platform

- **MCU**: Seeed Studio XIAO ESP32-C5 (RISC-V, 240 MHz high-perf core +
  48 MHz low-power core, dual-band Wi-Fi 6, 8 MB PSRAM, USB-C)
- **Trigger output buffer**: 74HCT14 hex inverting Schmitt trigger,
  powered from +5 V. Drives clean 0/+5 V Eurorack triggers from the C5's
  3.3 V GPIOs and provides hysteresis on the external reset input.
- **No DAC, no op-amps, no I²C** — everything is digital. The previous
  module's DAC + analog output stage is gone; outputs are sharper, the
  BOM is smaller, and there's no analog calibration.
- **Power**: +5 V and GND only. Handled by the user's power section;
  this design assumes a clean +5 V rail. The C5's 3.3 V regulator
  handles the MCU side.

The XIAO ESP32-C5 was chosen over the original ESP32 specifically for
**dual-band Wi-Fi 6**: the 5 GHz capability lets the module sit on the
same band as Mac / Push / Move / Note without depending on the router
to bridge multicast between 2.4 GHz and 5 GHz (an issue we hit on the
previous `esp32_clklink` build).

## Front panel (4 HP n8synth control board)

Six cells, all populated:

| Cell | Component | Purpose |
|---|---|---|
| 1 | Blue LED + momentary switch | Link enable toggle + Link status |
| 2 | Red LED + momentary switch | Capture trigger + transaction state |
| 3 | 3.5 mm jack | Reset In |
| 4 | 3.5 mm jack | Reset Out |
| 5 | 3.5 mm jack | Clock Out |
| 6 | 3.5 mm jack | Running Out |

Both switches are momentary tactile, not toggle — Link state is held
in software via edge-detect on press. USB-C is **not** routed to the
front panel; the XIAO's USB-C is accessible from inside the case for
reflashing.

## LED semantics

| LED | Off | Flashing | Solid |
|---|---|---|---|
| Blue (Link) | Switch off | Pairing — WiFi up, no peers yet | Paired — Link locked to ≥1 peer |
| Red (Capture) | Idle | (not used) | Either "POST in flight" or "error: no response from Mac" |

The red LED is binary: it lights when the Capture button is pressed and
stays lit until the Mac app responds with `200 OK`. If the Mac never
responds, the LED stays on — that's the explicit error signal, no
timeout-to-off.

## Switch semantics

- **Blue switch (Link enable)**: momentary. Each press toggles
  `link_enabled`. Boot default is **OFF** — the module powers up quiet
  and doesn't try to join WiFi until explicitly enabled. State is not
  persisted across reboots.
- **Red switch (Capture)**: momentary. Falling edge fires one HTTP
  POST to the Mac recorder app. While the request is in flight, the
  red LED is on. No queueing — pressing again before the previous
  request completes is ignored.

## Pin allocation (XIAO ESP32-C5)

| Xiao pad | GPIO | Strapping? | Function | Notes |
|---|---|---|---|---|
| D0 | GPIO1 | no | Link switch | momentary, INPUT_PULLUP, debounce C3 |
| D1 | GPIO0 | boot | unused | leave floating (Xiao internal pull-up handles boot) |
| D2 | GPIO25 | strapping | unused | leave floating |
| D3 | GPIO7 | strapping | unused | leave floating |
| D4 | GPIO23 | no | CLK_OUT → U2.1 | drives 74HCT14 ch.1, external pull-up R10 |
| D5 | GPIO24 | no | RST_OUT → U2.3 | drives 74HCT14 ch.2, external pull-up R11 |
| D6 | GPIO11 | no | RUN_OUT → U2.5 | drives 74HCT14 ch.3, external pull-up R12 |
| D7 | GPIO12 | no | RED_LED → U2.9 | drives 74HCT14 ch.4, external pull-up R13 |
| D8 | GPIO8 | no | BLUE_LED → U2.11 | drives 74HCT14 ch.5, external pull-up R14 |
| D9 | GPIO9 | no | Capture switch | momentary, INPUT_PULLUP, debounce C2 |
| D10 | GPIO10 | no | RESET_IN ← U2.12 (via R8/R9 divider) | reads 74HCT14 ch.6 output |

All other Xiao GPIOs are reserved internally (USB-JTAG, SPI flash,
battery sense, onboard LED) and not accessible on the breakout.

## 74HCT14 channel allocation

| Channel | Input pin | Input net | Output pin | Output net |
|---|---|---|---|---|
| 1 | 1 | CLK_OUT (D4) | 2 | → R2 1 kΩ → J1 Clock Out tip |
| 2 | 3 | RST_OUT (D5) | 4 | → R3 1 kΩ → J2 Reset Out tip |
| 3 | 5 | RUN_OUT (D6) | 6 | → R4 1 kΩ → J4 Running Out tip |
| 4 | 9 | RED_LED (D7) | 8 | → R5 1 kΩ → LED1 (Red) |
| 5 | 11 | BLUE_LED (D8) | 10 | → R6 1 kΩ → LED2 (Blue) |
| 6 | 13 | J3 Reset In tip via R7 10 kΩ | 12 | → R8/R9 divider → D10 |

All six channels are used — no unused inputs to tie off. 74HC**T**14 is
required (not 74HC14): the HCT variant has TTL-compatible input
thresholds (Vih ≈ 2.0 V), so the C5's 3.3 V GPIO outputs drive the 5 V
chip reliably.

## Reset input path — voltage protection in detail

The Reset In path bridges three voltage domains: the Eurorack jack
(could be patched with ±12 V), the 74HCT14 (operating at 5 V), and
the C5 GPIO (3.3 V max). Two resistors handle the two boundaries.

```
J3 (Reset In jack)
0 V idle, +5 V trigger nominal, up to ±12 V if mispatched
                │
                ▼
            R7 = 10 kΩ              ← protects the 74HCT14 input from
                │                     over-voltage at the jack
                ▼
U2 pin 13 (74HCT14 channel 6 input)
Internal ESD clamps to +5 V rail (documented, ±20 mA continuous OK).
At +12 V on the jack: clamp current = (12 − 5.7) / 10 kΩ = 0.63 mA   ✓
                │
                │  Schmitt-trigger thresholds: Vil_max 0.8 V,
                │  Vih_min 2.0 V, ~1.2 V hysteresis — clean edges
                │  on any noisy / slow trigger input.
                ▼
U2 pin 12 (74HCT14 channel 6 output, 0 / +5 V swing, INVERTED)
                │
                ▼
            R8 = 10 kΩ              ← upper leg of voltage divider
                │
                ├──► C5 D10 GPIO
                │
            R9 = 15 kΩ              ← lower leg of voltage divider
                │                     also serves as pull-down so the
                ▼                     GPIO has a defined LOW state
              GND

Divider ratio: 15 / (10 + 15) = 0.6
GPIO sees: 5 V × 0.6 = 3.0 V when 74HCT14 output is HIGH
           0 V        when 74HCT14 output is LOW
```

`R8/R9` form a *real* voltage divider — no reliance on the C5's ESD
clamp diodes for active circuit protection. 3.0 V is comfortably below
the C5's 3.3 V max input and comfortably above its ~1.6 V Vih
threshold.

The 74HCT14 inverts: a HIGH jack voltage produces a LOW signal at the
C5 GPIO. Firmware convention is **"GPIO LOW = signal active"** for
both the inputs and the outputs in this design.

## Boot-safe output state

Between power-up and the first `digitalWrite` in `setup()` (a window
of perhaps 50 ms), the C5's GPIOs are floating high-impedance inputs.
Without external pull-ups, the 74HCT14 inputs would float and the
outputs would be unpredictable — possibly emitting a brief glitch on
every jack at boot.

**R10–R14** are 10 kΩ pull-ups from each GPIO-to-74HCT14 net to the
+3V3 rail. They hold the 74HCT14 inputs HIGH during the boot window,
so the chip's outputs are LOW: jacks idle at 0 V, LEDs off. Once the
firmware starts driving the GPIOs explicitly, the pull-ups are
overpowered (10 kΩ vs the C5's ~25 Ω output impedance, easily).

The firmware should still drive output mode HIGH as the *very first
operation* in `setup()` to minimise the glitch window.

## Netlist

```
COMPONENTS

U1  Seeed Studio XIAO ESP32-C5
U2  74HCT14 hex inverting Schmitt trigger, +5 V powered

R2   1 kΩ       Clock output short-circuit protection (U2.2 → J1 tip)
R3   1 kΩ       Reset output short-circuit protection (U2.4 → J2 tip)
R4   1 kΩ       Running output short-circuit protection (U2.6 → J4 tip)
R5   1 kΩ       Red LED current limit, ~2.5 mA actual (U2.8 → LED1.A)
R6   1 kΩ       Blue LED current limit, ~1.5 mA actual (U2.10 → LED2.A)
R7   10 kΩ      Reset-in series protection (J3 tip → U2.13)
R8   10 kΩ      Reset-in voltage divider upper leg (U2.12 → D10)
R9   15 kΩ      Reset-in voltage divider lower leg + pull-down (D10 → GND)
R10  10 kΩ      Boot-safe pull-up (D4 → +3V3)
R11  10 kΩ      Boot-safe pull-up (D5 → +3V3)
R12  10 kΩ      Boot-safe pull-up (D6 → +3V3)
R13  10 kΩ      Boot-safe pull-up (D7 → +3V3)
R14  10 kΩ      Boot-safe pull-up (D8 → +3V3)

C1   100 nF     74HCT14 VCC decoupling, close to U2 pin 14
C2   100 nF     Capture-button debounce, D9 ↔ GND
C3   100 nF     Link-switch debounce, D0 ↔ GND

LED1  3 mm red,  Vf ≈ 1.8 V   anode = R5 output, cathode = GND
LED2  3 mm blue, Vf ≈ 3.0 V   anode = R6 output, cathode = GND

SW1   6 mm momentary tactile, NO   Capture
SW2   6 mm momentary tactile, NO   Link enable

J1   3.5 mm mono jack — Clock Out      tip = R2 out, sleeve = GND
J2   3.5 mm mono jack — Reset Out      tip = R3 out, sleeve = GND
J3   3.5 mm mono jack — Reset In       tip = R7 in,  sleeve = GND
J4   3.5 mm mono jack — Running Out    tip = R4 out, sleeve = GND
```

Resistor counts by value:
- 1 kΩ × 5 (R2, R3, R4, R5, R6)
- 10 kΩ × 7 (R7, R8, R10, R11, R12, R13, R14)
- 15 kΩ × 1 (R9)

All standard E12 values. Three distinct resistor values across 13 parts.

## Firmware notes

- **74HCT14 inverts everything.** To assert a +5 V trigger at a jack
  or turn an LED on, the firmware drives the corresponding GPIO **LOW**.
  Idle (0 V at the jack, LED off) is GPIO **HIGH**. The Reset In path
  is also inverted: a +5 V trigger at the jack causes the C5 to read
  **LOW** on D10. Convention: "GPIO LOW = signal active" everywhere.
- **Boot-safe outputs in firmware too.** Drive all five output GPIOs
  HIGH in `setup()` before `pinMode(OUTPUT)`. The external pull-ups
  (R10–R14) hold the 74HCT14 inputs HIGH during the boot window, so
  the chip's outputs idle at 0 V. The firmware's first job is to keep
  it that way until it intentionally fires a pulse.
- **Both buttons** use `INPUT_PULLUP`. The Link switch is edge-detected
  to toggle a `link_enabled` boolean (no NVS persistence — boots OFF).
- **External reset trigger** is a rising edge at the jack, which
  appears as a *falling* edge on D10 (because of U2's inversion). On
  edge, fire a software Reset pulse on J2 *and* realign the Link beat
  phase to "next pulse = now."
- **Capture button** spawns a one-shot FreeRTOS task that runs the HTTP
  POST in the background. The main loop returns immediately so Link's
  beat ticking is not blocked by network round-trip latency.

See [`include/esp32_clklinkrec/pins.h`](../include/esp32_clklinkrec/pins.h)
for the canonical pin assignments.

## Cross-system communication

The Mac-side menu-bar app lives in a separate repository
(`~/GitHub/seeed-recorder`). The wire-level contract between firmware
and Mac is defined in [`RECORDER_PROTOCOL.md`](RECORDER_PROTOCOL.md).
That document is the source of truth for both sides — when the
protocol changes, edit there and update both implementations.

## Roadmap

The MVP scope is Link sync + Recorder trigger to the Mac. Bigger
ideas explicitly deferred:

- **Link Audio source** (publish a Eurorack audio input as a Link Audio
  channel for the Mac/Push/Move to record). Requires an external I²S
  audio ADC (PCM1808 or similar), pin shuffling onto the JTAG pads,
  and bench-tested CPU headroom. Promising but bleeding edge.
- **On-module retrospective recording**. Would need a partner SOM
  (Daisy Patch SubModule for SDRAM + codec + SD storage). The C5
  alone doesn't have the buffer space or the audio I/O to do this.
- **Bidirectional Link** — letting the module set tempo / transport.
  Requires a control surface (extra encoder/pot) that doesn't fit in
  4 HP.

## Distribution & user experience (forward-looking)

The current "clone the repo, edit `secrets.h`, build with PlatformIO"
workflow is fine while it's just the builder. If/when this module
ends up in the hands of users who don't want a toolchain, the friction
points are:

1. **WiFi credentials.** Compiled into the binary today. Untenable for
   any external distribution. Options:
   - **SoftAP captive portal on first boot.** Module brings up its own
     AP (e.g. `clklinkrec-setup-XXXX`), serves a tiny HTML form, the
     user picks an SSID and types a password. Credentials persist in
     NVS. Standard pattern in arduino-esp32 — WiFiManager and similar
     libraries cover this; rolling it bespoke is also straightforward.
     Cost: ~10–20 KB flash and a panel hint ("hold Capture at boot to
     re-provision").
   - **BLE provisioning.** ESP-IDF has an `esp_wifi_provisioning`
     component (BLE or SoftAP backends) that pairs with a phone app.
     More flash, less obvious UX, no companion app currently.
   - **USB-CDC line-protocol provisioning.** Pipe a short SSID/pass
     pair over the serial console. Quickest to implement, requires the
     user to plug into a Mac/PC — fine for the "I bought a kit, now
     setting it up at my bench" flow.
2. **Recorder address.** Today: optional `RECORDER_HOST` in `secrets.h`
   with mDNS preferred. For external users, same persistence story —
   discovered or provisioned at runtime, persisted in NVS. The mDNS
   path covers the common case; the manual-IP fallback is the escape
   hatch for guest-VLAN-style networks.
3. **Pre-built binaries.** GitHub Releases artefact per pioarduino
   version, plus the matching `bootloader.bin`, `partitions.bin`,
   `boot_app0.bin`. End user flashes via `esptool.py`, the official
   Espressif WebFlasher (browser-based, no install), or a tiny
   Mac/Windows GUI we ship. Espressif's WebFlasher is the lowest-effort
   shippable.
4. **First-run state machine.** Combined version of the above:
   - Boot with no NVS credentials → SoftAP + captive portal
   - Credentials saved → normal connect path, optionally fall back to
     "press Capture for 5 s to re-provision" gesture
   - Recorder address discovered automatically via mDNS; settable
     manually in the captive portal as a power-user option
5. **Firmware update path.** OTA over WiFi (HTTP-pull from a release
   URL, or LAN-side push from a companion app). The `default_8MB.csv`
   partition table already has two OTA app slots, so the firmware is
   ready for it.

None of this is in scope for the MVP. The current bench-tested
firmware is the "I'm the developer, I'll edit secrets.h" flow and it
works. The notes above exist so that future-us doesn't re-derive the
landscape when the module starts ending up on other people's racks.

## Design audit history

This document reflects corrections made on 2026-06-02 to an earlier
draft. The corrections:

1. **R8 became a real voltage divider** (10 kΩ + R9 15 kΩ to GND).
   The previous design had R8 = 1 kΩ alone and relied on the C5's
   internal ESD clamp diode to do the level shifting — a tolerated
   hobbyist pattern but not properly engineered. The voltage divider
   makes the level shift explicit.
2. **LED current-limit resistors increased to 1 kΩ** (from 330 Ω red
   / 220 Ω blue). The earlier values pushed the 74HCT14's output
   current 2.5× over its recommended max of 4 mA. 1 kΩ keeps each
   output within spec at ~2.5 mA actual, with LEDs visibly lit.
3. **Boot-safe pull-ups (R10–R14) added** on each GPIO-to-74HCT14
   net. The earlier design left the 74HCT14 inputs floating during
   the C5's ~50 ms boot window, potentially producing a glitch pulse
   on every jack.
4. **Channel 6 of the 74HCT14 is now correctly documented** as the
   Reset In path; the earlier netlist still labelled it as "unused,
   tie to GND."
