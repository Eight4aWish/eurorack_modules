# expander_io

Minimal Arduino-friendly helpers for a 74HC595 output expander and MCP4822 DACs whose chip-select lines are driven from the expander.

- Header-only; no `.cpp` files
- Shares the global `SPI` bus safely (uses `SPI.beginTransaction`)
- Keeps DAC CS lines deasserted (HIGH) by default to avoid glitches

## Files
- include/expander_io/Expander595.h — 74HC595 image + latch handling
- include/expander_io/Mcp4822Expander.h — MCP4822 writes with CS via expander

## Bit Mapping (default)
These match the current Teensy wiring; change usage in your code if your wiring differs.

- Q0: `ExpanderBits::V2_GATE`
- Q1: `ExpanderBits::V1_GATE`
- Q2–Q5: `ExpanderBits::DRUM1..DRUM4`
- Q6: `ExpanderBits::DAC1_CS` (active-low)
- Q7: `ExpanderBits::DAC2_CS` (active-low)

## API

### Expander595
- `Expander595(SPIClass &spi, uint8_t latchPin, uint32_t hz = 4000000)`
- `void begin()` — initializes latch pin and pushes default image `0xFF`
- `uint8_t image() const` — current latched image (1=HIGH, 0=LOW)
- `void write(uint8_t img)` — shifts and latches new image
- `void setBit(uint8_t bit, bool high)` — convenience setter with write-if-changed
- `void deassertCs(uint8_t csBitA, uint8_t csBitB)` — ensures CS bits are HIGH; writes only if needed

### Mcp4822Expander
- `Mcp4822Expander(Expander595 &exp, SPIClass &spi, uint8_t csModBit, uint8_t csPitchBit, uint32_t hz = 4000000)`
- `void write(uint8_t which, uint8_t ch, uint16_t v, bool gain2x = true)`
  - `which`: 0 → `csModBit`, 1 → `csPitchBit`
  - `ch`: 0 → channel A, 1 → channel B
  - `v`: 12‑bit code (0..4095)
  - Gain is fixed to 2× (GA=0 in frame); `gain2x` is reserved for future
  - Behavior: deassert both CS (HIGH), assert target CS (LOW), `SPI.transfer16(frame)`, then deassert both CS (HIGH)

## Example
```cpp
#include <SPI.h>
#include "expander_io/Expander595.h"
#include "expander_io/Mcp4822Expander.h"
using namespace expander_io;

constexpr uint8_t LATCH_PIN = 32;  // example

Expander595 exp(SPI, LATCH_PIN, 4000000);
Mcp4822Expander mcp(exp, SPI, ExpanderBits::DAC1_CS, ExpanderBits::DAC2_CS, 4000000);

void setup() {
  SPI.begin();
  exp.begin();
  // Keep CS high; set Gate1 asserted (active-low after HCT14)
  uint8_t img = exp.image();
  img |= (1u<<ExpanderBits::DAC1_CS) | (1u<<ExpanderBits::DAC2_CS);
  img &= ~(1u<<ExpanderBits::V1_GATE);
  exp.write(img);

  // Write MCP4822 via expander CS
  mcp.write(0 /*DAC1 CS*/, 0 /*A*/, 2048);
  mcp.write(1 /*DAC2 CS*/, 1 /*B*/, 1234);
}
```

## Notes
- The classes are not thread-safe; use from a single control context.
- Keep unified expander image writes for gates/drums so CS lines remain HIGH unless actively transferring.
- Compatible with Teensy 4.1, RP2040, ESP32, and similar Arduino cores.

See the project’s root `README.md` and `docs/TEENSY_MOVE.md` for wiring and integration details.
