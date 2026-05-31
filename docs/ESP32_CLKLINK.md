# ESP32 Clk/Link

A re-purpose of the ESP32-Dev + MCP4728 hardware that previously ran
`esp32oscclk`. The module provides three modes selected by an ON-OFF-ON
switch:

- **OFF** — all jacks idle at 0 V
- **INTERNAL** — pot sets BPM, Channel A clocks, Channel B fires reset on
  mode entry and on any external CV trigger
- **LINK** — *(Phase 2, not yet implemented)* sync to an Ableton Link
  network, output clock and bar-reset, pot drives Channel C as a manual
  CV utility

## Channel map

| DAC channel | Function |
|---|---|
| A | Clock (0 V idle, +5 V trigger pulses, 10 ms wide) |
| B | Reset (one-shot on mode entry, external CV trigger, or Link bar boundary) |
| C | Manual CV from pot (LINK mode only; 0 V idle in OFF/INTERNAL) |
| D | unused (not wired) |

DAC values are calibrated to the non-inverting unipolar gain-2 output
stage: `clock_low = 0` (jack 0 V) and `clock_high = 2048` (jack +5 V).

## Pins

| Function | GPIO | Notes |
|---|---|---|
| Pot | 32 | ADC. BPM in INTERNAL, CV value in LINK |
| External reset CV-in | 33 | ADC. Rising-edge trigger detection |
| Switch upper (LINK) | 35 | Input-only |
| Switch lower (INTERNAL) | 34 | Input-only |
| Status LED | 2 | Onboard, mode indicator |

## External reset behaviour

The CV input is a Eurorack trigger input. The firmware detects a rising
edge (ADC > 2000 → trigger fires; hysteresis at < 500 to re-arm), fires
a reset pulse on Channel B, and realigns Channel A so the next clock
tick coincides with the reset. Works in INTERNAL mode in Phase 1; will
also work in LINK mode in Phase 2.

## Status LED

| Mode | LED |
|---|---|
| OFF | off |
| INTERNAL | solid (clocking) |
| LINK (Phase 1 stub) | slow heartbeat (~1 Hz) — feature not yet built |

## Build & flash

```sh
pio run -e esp32-clklink
pio run -e esp32-clklink -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor -b 115200
```

PlatformIO sometimes auto-picks the wrong serial port (e.g. macOS Bluetooth
serial). Pass `--upload-port` explicitly.

## INTERNAL mode tuning

Constants at the top of [src/esp32-clklink/main.cpp](../src/esp32-clklink/main.cpp):

- `clock_high` / `clock_low` — DAC values for +5 V / 0 V at the jack
- `PULSE_WIDTH_US` — clock and reset pulse width (default 10 ms)
- `bpm_from_pot()` — pot 0..4095 → BPM 50..200 by default (CW = fast)
- `CV_HIGH_THRESH` / `CV_LOW_THRESH` — Schmitt thresholds for the external
  reset trigger input

## Phase 1 scheduler

Pulse timing is currently driven from a polling loop using `micros()`.
At PPQN = 1 and 50..200 BPM, the clock period is 200 ms..1.5 s and main
loop latency is well under 1 ms — the resulting jitter is inaudible and
well below sequencer input tolerances. A hardware-timer-ISR scheduler is
on the Phase 2 roadmap, primarily because Link sync benefits from
sample-accurate beat scheduling.

## Roadmap — Phase 2

- WiFi connection (creds in `include/shared/secrets.h`, gitignored)
- Ableton Link client (library integration is the unresolved piece; see
  `docs/PROTON_SIGNAL_ROUTING.md` for unrelated context — for Link, the
  candidate path is either ESP-IDF framework switch or vendoring the
  official `Ableton/link` header-only library with custom platform glue)
- Channel C as manual CV utility from the pot in LINK mode
- Status LED feedback for WiFi / Link peer state
