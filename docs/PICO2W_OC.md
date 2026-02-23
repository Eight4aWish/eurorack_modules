````markdown
# Eurorack Firmware — Pico 2 W Guide (OC)

This guide covers navigation, controls, and behavior for the current functional patches on Pico 2 W. The OLED UI uses a compact fixed grid optimized for split (yellow/blue) SSD1306 displays.

## UI Basics

- Home Menu: Short press cycles selection; long press enters the highlighted patch.
- In-Patch: Short press performs the patch’s action (e.g., run/stop, cycle parameter, toggle edit target). Long press returns to the menu.
- Pots: Pot1, Pot2, Pot3 are read inverted so clockwise increases value. Some patches smooth pots to reduce jitter.
- Layout Grid: Title at `y=0`. Content rows at `y=16, 26, 36, 46, 56`.
- Title Right: Some patches show mode/status on the right side of the title line.

## Navigation Summary

- Menu: short = next, long = enter.
- Patch: short = patch-specific action, long = back to menu.

## Patches

### Diag
- Purpose: Hardware diagnostics for pots, ADS1115 inputs, and MCP4728 outputs.
- Display:
  - Button state and raw reads for Pot1/2/3.
  - ADS raw codes for ADC0/ADC1.
  - MCP codes for physical CV0..CV3 (mapped via `include/pico2w_oc/pins.h`).
- Controls:
  - Short press: Cycle selected physical CV output (CV0→CV1→CV2→CV3).
  - Pot1: Sets the DAC code (0..4095) for the selected CV only; other CVs are set to 0.
  - Pot2/Pot3: No effect.

### Clock
- Purpose: Lightweight 4-channel clock with divisions and optional external clocking.
- Outputs: 10 ms gates on CV0..CV3 using fixed “gate codes” (about 0 V at code ~2047, +5 V at code 0), intentionally ignoring calibration.
- Display: `INT/EXT`, BPM, and `RUN/STOP`; per-channel division labels for CH0..CH3.
- Controls:
  - Short press: Toggle RUN/STOP.
  - Pot1: BPM (INT mode: 30–300 BPM). If external clock edges are detected on `AD_EXT_CLOCK_CH`, the clock follows the external period.
  - Pot2: Division for CH0; CH1 is derived at half speed of CH0.
  - Pot3: Division for CH2; CH3 is derived at half speed of CH2.

### Euclid
- Purpose: Euclidean drum triggers on up to 4 outputs.
- Outputs: 30 ms gates on CV0..CV3 (fixed gate codes as above).
- Modes (shown on title right):
  - Simple: Shared Steps/Pulses/Rotation; 4 channels are rotated variants.
  - Complex: Per-channel Steps/Pulses/Rotation.
- Controls:
  - Pot1: BPM (30–300).
  - Pot2: Mode select — <50%: Simple, ≥50%: Complex.
  - Pot3: Adjusts the currently selected parameter (Steps/Pulses/Rotation).
  - Short press: Cycles the selected parameter. In Complex mode, when the cycle wraps, the selected channel advances (CH0→CH1→CH2→CH3).

### Env (Dual Envelopes)
- Purpose: Two independent macro ADSR-style envelopes.
- Outputs: Env1→CV0, Env2→CV1. 0 V baseline at code ~2047, up to ~+5 V at code 0. Each envelope has its own velocity scaling.
- Triggers: Env1 from `AD_EXT_CLOCK_CH`; Env2 from `AD1_CH` (rising-edge detection).
- Controls (smoothed):
  - Pot1: Velocity (amplitude) for the selected envelope.
  - Pot2: AD macro — Attack and Decay are linked; turning clockwise lengthens both. Short settings are punchy.
  - Pot3: S/R macro — Sustain level and Release time together; higher values raise sustain and lengthen release.
  - Short press: Toggle which envelope is edited (E1/E2). Header shows the active target; first row displays `Vel <n>%` for the selected envelope.
- Envelope Model:
  - Attack(ms) = 1 + (AD²) × 2000.
  - Decay(ms) = Attack × (0.15 + 0.85 × Sustain) for percussive response.
  - Release(ms) = 1 + (SR²) × 2000.
  - Output = Level × Velocity, mapped to DAC codes for ~0..+5 V.

### Quant
- Purpose: 2-channel semitone quantizer (1 V/oct).
- Inputs: ADS `AD0_CH`, `AD1_CH` (mapped to volts via calibration if available).
- Outputs: CV0, CV1 (mapped back to DAC codes via calibration).
- Display: In0/Out0 and In1/Out1 values plus current DAC codes.
- Controls: None (always-on behavior).

### Scope
- Purpose: Simple oscilloscope for ADS channel 0.
- Display: Waveform trace under the title; title-right shows `Vx<gain> H<samples> M<mid>`.
- Controls:
  - Pot1: Vertical gain (~0.25x to 4x).
  - Pot2: Horizontal window (32..128 samples).
  - Pot3: Midpoint offset.

### Calibration
- Approach: Use the `Diag` patch for DMM-first calibration. Record raw ADC codes vs known volts, and raw DAC codes vs measured volts, then fit straight lines per channel.
- Integration: Static fits are compiled in (see `include/pico2w_oc/calib_static.h`). Diagnostics remain raw-only.

### QuadLFO
- Purpose: 4 independent LFOs with per-LFO amplitude, rate, and shape.
- Outputs: CV0..CV3 emit LFOs; bipolar ±amp mapped via calibration to DAC codes.
- Controls (smoothed, inverted):
  - Pot1: Amplitude (0..~5 V peak per LFO). Header shows amplitude for the selected LFO.
  - Pot2: Rate (≈0.05–20 Hz with squared mapping for fine low-end control).
  - Pot3: Shape (Sin/Tri/Sq/Up/Down).
  - Short press: Cycle edited LFO target (L0→L1→L2→L3). Title right shows `L<idx>`.
  - Long press: Return to menu.
 - Notes: All LFOs run continuously; editing only affects the selected LFO’s parameters.

## Tips

- Physical Mapping: DAC channels use physical macros `CV0_DA_CH..CV3_DA_CH`; ADS channels use `AD0_CH`, `AD1_CH`, and `AD_EXT_CLOCK_CH` in `include/pico2w_oc/pins.h`.
- External Clocking: Provide clean rising edges into `AD_EXT_CLOCK_CH` for reliable detection.
- OLED Grid: Keep titles at `y=0`; use rows `16/26/36/46/56` for content.
- Menu: Currently 7 patches — `Clock`, `Quant`, `Euclid`, `LFO`, `Env`, `Scope`, `Diag`.

## PlatformIO Quick Commands

```sh
# Build default environment
pio run

# Build Pico 2 W explicitly
pio run -e pico2w_oc

# Upload to Pico 2 W
pio run -e pico2w_oc -t upload

# Monitor serial at 115200
pio device monitor -b 115200
```

## Known Hardware Limitations — External Clock Detection

The external clock detection path has several hardware-level constraints that limit timing accuracy. These are inherent to the current board design and cannot be fully resolved in firmware alone.

### 1. I2C Bus Contention (Critical)

The ADS1115 (ADC), SSD1306 OLED, and MCP4728 (DAC) all share a single I2C bus (Wire1 at 400 kHz). The OLED framebuffer push (~1 KB = 8192 bits) takes approximately **20 ms** to transfer. During that window the bus is locked — no ADS1115 reads or MCP4728 writes can occur.

**Impact:** If an external clock edge arrives while an OLED frame is being pushed, detection is delayed by up to 20 ms. The Clock patch uses `UI_FRAME_MS_ACTIVE = 50 ms`, so roughly 40% of wall-clock time is occupied by OLED transfers (20 ms blocked out of every 50 ms cycle).

**Mitigation (hardware):** Move the OLED to its own I2C bus. The RP2350 has two I2C peripherals (Wire and Wire1); currently only Wire1 is used. Wiring the OLED to Wire on separate SDA/SCL pins would eliminate contention entirely.

**Mitigation (firmware):** Use `slowOled = true` for the Clock patch to reduce OLED refresh to 150 ms, shrinking the bus-blocked fraction to ~13%. Alternatively, implement partial OLED updates (dirty-rectangle) to shorten transfer time.

### 2. No ADS1115 ALERT/DRDY Interrupt

The ADS1115 has an ALERT/RDY output pin that can be configured as a data-ready signal or a window comparator output. **This pin is not connected to any Pico GPIO** on the current board.

All edge detection is therefore polled: each `clock_tick()` call issues a blocking `readADC_SingleEnded()` which starts a single-shot conversion, waits for it to finish (~1.2 ms at 860 SPS), then reads the result.

**Impact:** Worst-case edge detection latency is `CTRL_TICK_MS` (5 ms) + conversion time (1.2 ms) = **~6.2 ms**. At 300 BPM (200 ms period) this is 3% phase error per edge. At the rejection boundary of 600 BPM (100 ms period) it's 6%.

**Mitigation (hardware):** Wire ADS1115 ALERT/RDY to a free Pico GPIO. Configure the ADS1115 in continuous mode with the comparator set as a window detector — the ALERT pin fires a hardware interrupt when the input crosses the gate threshold. This would reduce detection latency to sub-millisecond.

### 3. Polled Single-Shot ADC Mode

The Adafruit ADS1X15 library's `readADC_SingleEnded()` performs a full single-shot cycle per call:
1. Write config register (start conversion + set mux channel)
2. Poll the "conversion ready" bit over I2C
3. Read the 16-bit result

At 860 SPS, each call blocks for ~1.2 ms minimum, consuming I2C bandwidth and adding fixed latency to every tick.

**Impact:** With `CTRL_TICK_MS = 5 ms`, the ADC read consumes ~24% of each tick's time budget. If additional reads are needed (e.g., Env reads two channels), the per-tick cost doubles.

**Mitigation (firmware — no hardware change):** Switch to continuous conversion mode. Start a conversion at the end of each tick; on the next tick, read the ready result immediately (no polling wait). This saves ~0.8 ms per read. Combined with the ALERT/DRDY interrupt above, continuous mode with hardware interrupt is the ideal configuration.

### 4. Polling Rate Ceiling

`CTRL_TICK_MS = 5 ms` sets a hard ceiling of **200 Hz** on clock edge sampling. The firmware's exponential smoothing filter (α = 0.3) reduces average tempo drift but cannot correct the per-edge phase quantisation imposed by the 5 ms poll interval.

| BPM  | Period (ms) | Polls/Period | Max Phase Error |
|------|-------------|-------------|-----------------|
| 120  | 500         | 100         | 1.0%            |
| 200  | 300         | 60          | 1.7%            |
| 300  | 200         | 40          | 2.5%            |
| 600  | 100         | 20          | 5.0%            |

**Mitigation (firmware):** Reducing `CTRL_TICK_MS` to 2 ms would cut phase error proportionally, but increases I2C traffic and CPU load. This is only practical if the ADS read pathway is optimised (continuous mode, non-blocking reads) so ticks complete within their budget.

### 5. Shared Clock/CV Input Jack

`AD_EXT_CLOCK_CH` is aliased to `AD0_CH` (ADS1115 channel 1). The external clock and CV input 0 are the **same physical jack**. The analog front-end is an inverting stage designed for ±5 V CV signals (biased around ~1.65 V midpoint), not optimised for digital gate/clock detection.

**Impact:** The inverting front-end means lower ADC codes = higher Eurorack voltages. Threshold constants (`kGateOnThresh = 10000`, `kGateOffThresh = 12000`) are calibrated to this mapping at the current gain/bias. If the front-end is redesigned or the ADS gain changes, these thresholds must be recalibrated.

**Consideration:** Gate signals from some modules can be +3.3 V (Pico-level) rather than the Eurorack-standard +5 V. At +3.3 V the ADC code would be ~(13567 − 3.3 × 2418) ≈ 5588, which is well below `kGateOnThresh = 10000`, so detection will work. However, modules outputting very low gates (+1.0 V or less) could fall between the `kGateOnThresh` and 0 V baseline, causing unreliable detection.

**Mitigation (hardware):** Add a dedicated digital clock input jack with a simple comparator/Schmitt trigger circuit feeding a Pico GPIO directly. Schmitt-triggered GPIO input provides clean edges with sub-microsecond latency and zero I2C overhead.

### Summary — Priority of Improvements

| Priority | Change | Type | Impact |
|----------|--------|------|--------|
| 1 | Move OLED to separate I2C bus | HW | Eliminates 20 ms blind spots |
| 2 | Wire ADS1115 ALERT/RDY to GPIO | HW | Sub-ms edge detection via interrupt |
| 3 | Dedicated clock input (GPIO + Schmitt) | HW | Zero-latency digital clock; frees ADC channel |
| 4 | Switch ADS to continuous mode | FW | Saves ~0.8 ms per read; enables DRDY interrupt |
| 5 | Mark Clock patch `slowOled = true` | FW | Quick win: reduces bus contention |
| 6 | Reduce `CTRL_TICK_MS` to 2 ms | FW | Halves phase quantisation error |

## Contributing

- Keep per-patch UIs compact and consistent with the grid.
- Use physical channel macros for all I/O; avoid legacy aliases.

````