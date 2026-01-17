# eurorack_calib

Minimal linear calibration helpers for ADC and DAC channels across devices (Teensy, RP2040, ESP32, Daisy).

- Simple 2‑point linear calibration per channel
- ADC: raw -> volts; DAC: volts -> code (12‑bit typical)
- Tiny serialization format with magic/version; platform‑agnostic storage

## Data model
- `LinCalib { a, b }` maps y = a*x + b
- `AdcCalib { raw_to_volts }`
- `DacCalib { volts_to_code }`
- `CalibPack { magic, version, adcCount, dacCount, adc[8], dac[8] }`

## Usage
1) Capture two points per channel (e.g., −4 V and +4 V)
2) Build linear maps with `fromTwoPoints(x1,y1,x2,y2)`
3) Apply via `apply(calib, x)`
4) Serialize with `pack()`; persist using your platform’s storage (EEPROM, NVS, Flash)

## Example (sketch)
```cpp
#include <eurorack_calib/Calib.h>
using namespace eurorack_calib;

CalibPack P;

void make_example() {
  P.adcCount = 2; P.dacCount = 4;
  // ADC 2-point capture: raw codes at -4V and +4V
  int r0m = 1250, r0p = 28750; // example
  P.adc[0].raw_to_volts = fromTwoPoints((float)r0m, -4.f, (float)r0p, 4.f);
  // DAC 2-point capture: measured volts at two codes
  uint16_t c0m=4095, c0p=0; float v0m=-4.02f, v0p=+3.98f;
  // Want mapping volts -> code: fit (v->code)
  P.dac[0].volts_to_code = fromTwoPoints(v0m, (float)c0m, v0p, (float)c0p);
}
```

See `docs/CALIBRATION.md` for the step-by-step process.
