# ESP32 Clk/Link/Rec

A Eurorack utility that combines two jobs in a single 4 HP module:

1. **Ableton Link clock generator** — joins a Link network over WiFi and
   emits Eurorack clock, reset, and run-gate triggers synchronised to the
   session's tempo and transport.
2. **Recorder trigger** — pressing the Capture button sends an HTTP POST
   to a Mac-side menu-bar app, which saves the last N seconds of audio
   it was playing. The audio recording itself happens on the Mac.

The module is a successor to [`esp32-clklink`](ESP32_CLKLINK.md) — same
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
previous `esp32-clklink` build).

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
| D1 | GPIO0 | **boot** | unused | leave floating |
| D2 | GPIO25 | **strapping** | unused | leave floating |
| D3 | GPIO7 | **strapping** | unused | leave floating |
| D4 | GPIO23 | no | CLK_OUT → U2.1 | drives 74HCT14 ch.1 |
| D5 | GPIO24 | no | RST_OUT → U2.3 | drives 74HCT14 ch.2 |
| D6 | GPIO11 | no | RUN_OUT → U2.5 | drives 74HCT14 ch.3 |
| D7 | GPIO12 | no | RED_LED → U2.9 | drives 74HCT14 ch.4 |
| D8 | GPIO8 | no | BLUE_LED → U2.11 | drives 74HCT14 ch.5 |
| D9 | GPIO9 | no | Capture switch | momentary, INPUT_PULLUP, debounce C2 |
| D10 | GPIO10 | no | RESET_IN ← U2.12 | reads 74HCT14 ch.6 output |

All other Xiao GPIOs are reserved internally (USB-JTAG, SPI flash,
battery sense, onboard LED) and not accessible on the breakout.

## 74HCT14 channel allocation

| Channel | Input net | Output net | Direction |
|---|---|---|---|
| 1 (pins 1/2) | CLK_OUT (D4) | → R2 1 kΩ → J1 tip (Clock Out) | output |
| 2 (pins 3/4) | RST_OUT (D5) | → R3 1 kΩ → J2 tip (Reset Out) | output |
| 3 (pins 5/6) | RUN_OUT (D6) | → R4 1 kΩ → J4 tip (Running Out) | output |
| 4 (pins 9/8) | RED_LED (D7) | → R5 330 Ω → LED1 (Red) | LED driver |
| 5 (pins 11/10) | BLUE_LED (D8) | → R6 220 Ω → LED2 (Blue) | LED driver |
| 6 (pins 13/12) | J3 tip via R7 10 kΩ (Reset In) | → R8 1 kΩ → D10 | input buffer |

74HC**T**14 is critical, not 74HC14. The HCT variant has TTL-compatible
input thresholds (Vih ≈ 2.0 V), so the C5's 3.3 V GPIO outputs drive
the 5 V chip reliably.

## Netlist summary

See [the previous final netlist in the chat history](#) for the
full per-pin connections. The key facts:

```
COMPONENTS

U1  Seeed Studio XIAO ESP32-C5
U2  74HCT14 hex inverting Schmitt trigger, 5 V powered

R2  1 kΩ      Clock output protection
R3  1 kΩ      Reset output protection
R4  1 kΩ      Running output protection
R5  330 Ω     Red LED current limit
R6  220 Ω     Blue LED current limit
R7  10 kΩ     Reset-in series protection (clamps via U2 input ESD)
R8  1 kΩ      Reset-in level-shift to C5 (clamps via C5 GPIO ESD)

C1  100 nF    74HCT14 VCC decoupling, close to U2 pin 14
C2  100 nF    Capture-button debounce, D9 ↔ GND
C3  100 nF    Link-switch debounce, D0 ↔ GND

LED1  3 mm red,  Vf ≈ 1.8 V
LED2  3 mm blue, Vf ≈ 3.0 V

SW1   6 mm momentary tactile  Capture
SW2   6 mm momentary tactile  Link enable

J1   3.5 mm mono jack — Clock Out
J2   3.5 mm mono jack — Reset Out
J3   3.5 mm mono jack — Reset In
J4   3.5 mm mono jack — Running Out
```

The Reset In path uses U2's spare 6th channel as a Schmitt-trigger
input buffer. R7 (10 kΩ series) absorbs overvoltage up to roughly
±12 V at the jack, clamped by U2's input ESD diodes. R8 (1 kΩ series)
then shifts the 0/+5 V output down to a level the C5 GPIO can safely
accept, relying on the C5's internal ESD clamp to absorb the residual.

## Firmware notes

- **74HCT14 inverts everything.** To assert a +5 V trigger at a jack
  or turn an LED on, the firmware drives the corresponding GPIO **LOW**.
  Idle (0 V at the jack, LED off) is GPIO **HIGH**. The Reset In path
  is also inverted: a +5 V trigger at the jack causes the C5 to read
  **LOW** on D10. Convention: "GPIO LOW = signal active" everywhere.
- **Boot-safe outputs.** Drive all five output GPIOs HIGH in `setup()`
  before `pinMode(OUTPUT)` to ensure the 74HCT14 outputs LOW and the
  jacks idle at 0 V from the moment the chip wakes up.
- **Both buttons** use `INPUT_PULLUP`. The Link switch is edge-detected
  to toggle a `link_enabled` boolean (no NVS persistence — boots OFF).
- **External reset trigger** is a rising edge on D10 (which is a falling
  edge at the GPIO because of U2's inversion). On edge, fire a software
  Reset pulse on J2 *and* realign the Link beat phase to "next pulse =
  now."
- **Capture button** spawns a one-shot FreeRTOS task that runs the HTTP
  POST in the background. The main loop returns immediately so Link's
  beat ticking is not blocked by network round-trip latency.

See [`include/esp32-clklinkrec/pins.h`](../include/esp32-clklinkrec/pins.h)
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
