#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "expander_io/Expander595.h"

namespace expander_io {

class Mcp4822Expander {
public:
  Mcp4822Expander(Expander595 &exp, SPIClass &spi, uint8_t csModBit, uint8_t csPitchBit, uint32_t hz = 4000000)
    : exp_(exp), spi_(spi), csModBit_(csModBit), csPitchBit_(csPitchBit), spiHz_(hz) {}

  // which: 0 -> csModBit, 1 -> csPitchBit; ch: 0 A, 1 B; v: 12-bit code
  void write(uint8_t which, uint8_t ch, uint16_t v, bool gain2x = true) {
    // Build frame: 0 0 1 GA SHDN D11..D0 (matching Teensy code, GA=0 for 2x gain)
    uint16_t frame = (ch ? 0x8000 : 0) | 0x1000 | (v & 0x0FFF);
    // Ensure both CS high, then assert target
    uint8_t img = exp_.image();
    img |= (1u << csModBit_) | (1u << csPitchBit_);
    if (which == 0) img &= ~(1u << csModBit_); else img &= ~(1u << csPitchBit_);
    exp_.write(img);

    spi_.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
    spi_.transfer16(frame);
    spi_.endTransaction();

    // Deassert CS
    exp_.deassertCs(csModBit_, csPitchBit_);
  }

  inline uint8_t csModBit() const { return csModBit_; }
  inline uint8_t csPitchBit() const { return csPitchBit_; }

private:
  Expander595 &exp_;
  SPIClass &spi_;
  uint8_t csModBit_;
  uint8_t csPitchBit_;
  uint32_t spiHz_;
};

} // namespace expander_io
