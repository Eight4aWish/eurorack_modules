# ESP32 Clk/Link

An ESP32-Dev + MCP4728 Eurorack utility that generates clocks and reset
triggers, optionally synced to an Ableton Link network. The module
provides three modes selected by an ON-OFF-ON switch:

- **OFF** — all jacks idle at 0 V
- **INTERNAL** — pot sets BPM (40–300), Channel A clocks, Channel B fires
  reset on mode entry and on any external CV trigger
- **LINK** — sync to an Ableton Link network on the same WiFi LAN.
  Channel A pulses on each beat, Channel B fires a reset pulse on each
  bar boundary, and Channel C outputs the pot value as a manual CV utility.

## Channel map

| DAC channel | Function |
|---|---|
| A | Clock (0 V idle, +5 V trigger pulses, 10 ms wide; gated by Link play state in LINK) |
| B | Reset (mode entry, external CV trigger, Link bar boundary, Link transport-start edge) |
| C | Manual CV from pot (LINK mode only; 0 V idle in OFF/INTERNAL) |
| D | Run gate (LINK mode only: +5 V while Link reports playing, 0 V stopped) |

DAC values are calibrated to the non-inverting unipolar gain-2 output
stage: `clock_low = 0` (jack 0 V) and `clock_high = 2048` (jack +5 V).
Channel C uses the full DAC range so the pot maps to 0–10 V at the jack.

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
a reset pulse on Channel B, and in INTERNAL mode realigns Channel A so
the next clock tick coincides with the reset. In LINK mode the trigger
forces a re-sync to the Link beat phase on the next tick.

## Status LED

| Mode | LED |
|---|---|
| OFF | off |
| INTERNAL | solid (clocking) |
| LINK, WiFi connecting | fast blink (~5 Hz) |
| LINK, WiFi up but no peers | medium blink (~2 Hz) |
| LINK, locked to session | solid |

## LINK mode

- **Quantum**: 4 beats per bar (Channel B fires a reset every 4 beats).
- **PPQN**: 1 (one Channel A pulse per quarter-note beat).
- **Initial tempo**: 120 BPM at boot — overridden once the Link session
  agrees on a tempo with peers.
- **Start/stop sync**: enabled. When any peer (Live, Move, …) presses
  play, Channel A starts emitting clocks and Channel D goes to +5 V
  (run gate). When any peer presses stop, A stops emitting and D drops
  back to 0 V. The play-rising edge also fires a reset on B so
  downstream sequencers restart at step 1.
- **Pot → Channel C**: the pot value is mirrored on Channel C as a free
  manual CV (0–10 V across the sweep, CW = higher).
- **WiFi creds**: read from `include/shared/secrets.h` (gitignored). The
  template `include/shared/secrets.h.example` shows the expected macros
  (`WIFI_SSID`, `WIFI_PASS`).
- **Task pinning**: Link runs without core affinity
  (`CONFIG_LINK_ESP_TASK_CORE_ID=-1`); FreeRTOS schedules it on whichever
  core has headroom.

## Build & flash

```sh
pio run -e esp32-clklink
pio run -e esp32-clklink -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor -b 115200
```

PlatformIO sometimes auto-picks the wrong serial port (e.g. macOS Bluetooth
serial). Pass `--upload-port` explicitly.

The first build downloads the pioarduino fork (IDF 5.5 + Arduino 3.x) and
the `docwilco/esp_abl_link` IDF component plus its `asio` dependency — that
takes a few minutes. Subsequent builds are quick.

## INTERNAL mode tuning

Constants at the top of [src/esp32-clklink/main.cpp](../src/esp32-clklink/main.cpp):

- `clock_high` / `clock_low` — DAC values for +5 V / 0 V at the jack
- `PULSE_WIDTH_US` — clock and reset pulse width (default 10 ms)
- `bpm_from_pot()` — pot 0..4095 → BPM 40..300 by default (CW = fast)
- `CV_HIGH_THRESH` / `CV_LOW_THRESH` — Schmitt thresholds for the external
  reset trigger input
- `LINK_INITIAL_TEMPO` — Link starting BPM before session sync (120)
- `LINK_QUANTUM` — beats per bar (4)

## Architecture notes

- The clock scheduler is in the main loop, using `micros()` for timing.
  At PPQN=1 and 40..300 BPM the period is 200 ms..1.5 s; main-loop latency
  is well under 1 ms, so the jitter is inaudible. A hardware-timer ISR
  scheduler may be worth the complexity if you push PPQN higher.
- Beat detection in LINK mode uses `abl_link_beat_at_time()` to read the
  fractional beat position, then triggers a Channel A pulse on each
  integer-beat crossing.
- The env uses `framework = arduino, espidf` (Arduino-as-IDF-component)
  via the `pioarduino/platform-espressif32` fork because the official
  PlatformIO espressif32 platform doesn't ship IDF 5.5, which the Link
  component requires.

## Known risks / things to confirm

- **Router multicast**: Link uses UDP multicast (224.76.78.75 / port
  20808). If your router blocks multicast on the test network (some
  consumer routers do on guest SSIDs), peers won't discover each other
  even though WiFi is up.
- **Beat→trigger jitter**: capturing the audio session state from the
  main loop adds ~1 ms of quantization. Acceptable for clock/trigger
  outputs at this PPQN. For audio-rate gates we'd want a timer-ISR path.
