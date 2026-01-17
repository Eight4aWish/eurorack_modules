# Calibration Process

This repository includes a small, reusable calibration library and a Pico 2 W serial CLI to capture real calibration for ADCs and DACs. The same flow can be adapted to Teensy, Daisy, and ESP32.

## Goals
- Replace hardcoded formulas with measured 2‑point linear calibration per channel
- Persist calibration on‑device (EEPROM/NVS/Flash) for unattended use
- Keep math and storage platform‑agnostic (see `libs/eurorack_calib`)

## Library
- Code: `libs/eurorack_calib`
- Types:
  - `AdcCalib { raw_to_volts }` maps ADC raw code → Volts
  - `DacCalib { volts_to_code }` maps Volts → DAC code (12‑bit typical)
  - `CalibPack` bundles N ADC + M DAC channels with magic/version
- API:
  - `fromTwoPoints(x1,y1,x2,y2)` → `LinCalib`
  - `apply(calib, x)` to evaluate
  - `pack()/unpack()` to serialize/deserialize a `CalibPack`

## Pico 2 W: OLED Diagnostics (preferred)
Use the `Diag` patch to capture raw ADS codes and raw MCP codes directly on the OLED, and fit lines offline with a DMM.

## Integration in firmware
- Load calibration at startup; fall back to defaults if missing
- Replace formula‑based mapping with calls to `apply()` using the loaded `CalibPack`
- Recommended channel assignment (Pico):
  - ADC: `AD0_CH` → `adc[0]`, `AD1_CH` → `adc[1]`
  - DAC: `CV0..CV3` → `dac[0]..dac[3]`

## Adapting to other devices
- Teensy 4.1 (MCP4822 x2):
  - DAC: Set two frames per channel (e.g., code 4095 for ~−4 V, code 0 for ~+4 V). Capture measured volts and fit V→code.
  - ADC: If using analog inputs or external ADCs, apply the 2‑point method to each input.
  - Storage: Teensy EEPROM; serialize `CalibPack` via `pack()`.
- Daisy (DaisyDuino):
  - ADC/DAC: Apply 2‑point method per used channel.
  - Storage: Flash/QSPI or a small reserved area; persist the packed bytes.
- ESP32 (MCP4728):
  - DAC: Same as Pico. ADC as needed.
  - Storage: NVS (Preferences) or EEPROM emulation.

## Tips
- Use consistent reference voltages (e.g., −4.000 V and +4.000 V) across devices.
- Let outputs settle (~10–20 ms) before measuring or capturing codes.
- If calibrating using onboard ADC instead of a DMM, note the accuracy limits.
- Store a magic + version to detect uninitialized or stale calibration.

## Roadmap
- Add Teensy/Daisy/ESP32 calibration CLI environments mirroring the Pico flow
- Provide OLED‑driven UI flows as an alternative to the serial CLI
- Optional CRC for the serialized pack

## OLED Diagnostics (DMM‑first workflow)

If you prefer a no‑serial, DMM‑based workflow, use the OLED Diagnostics screens to read pure raw ADC/DAC values and fit calibration offline.

- Where: Pico 2 W `Diag` patch (see [docs/PICO2W_OC.md](PICO2W_OC.md)). Similar screens can be added to Teensy/Daisy/ESP32.
- Display: Raw ADS codes for inputs (ADC0/ADC1) and raw MCP codes for outputs (CV0..CV3).
- Controls:
  - Short press: cycles the selected CV output (CV0→CV1→CV2→CV3).
  - `P1`: sets the DAC code (0..4095) for the selected CV only. Others are zeroed.
- Procedure:
  1) For ADC channels, apply known voltages (e.g., −5 V, −3 V, 0 V, +3 V, +5 V) and record the raw codes shown. Fit a straight line `code = a + b·V`. Inversion is fine; use the measured slope sign.
  2) For each DAC channel, set several codes with `P1` (e.g., 0, 4095, and midpoints), measure volts with a DMM at the jack, and fit `code = a + b·V`.
  3) Store `a` (offset) and `b` (codes per volt) in your firmware or persist via EEPROM/NVS.

### Using fits in firmware

For ADC: `V = (code − a) / b`.

For DAC: `code = a + b·V` (clamp to the device’s code range). If the output stage inverts or scales, the fit will capture it automatically via `b`.

### Rolling out across modules

To add the same Diagnostics UX to other targets:

- UI: A compact OLED screen with two input rows (ADC raw codes) and two output rows (CV raw codes), plus a pointer `>` at the selected CV.
- Input: Map physical ADC channels to logical names via your `pins.h`.
- Output control: Short press cycles the selected CV; `P1` writes the selected DAC code; other CVs set to 0.
- Persistence (optional): Save per‑channel `(a,b)` to EEPROM/NVS; load at boot.

This approach keeps calibration simple, portable, and verifiable with a DMM, without relying on serial or Wi‑Fi.
