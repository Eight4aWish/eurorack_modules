#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace expander_io {

// Default bit mapping for Q0..Q7; override at call sites if wired differently
namespace ExpanderBits {
  static const uint8_t V2_GATE = 0;  // Q0 (Gate 2)
  static const uint8_t V1_GATE = 1;  // Q1 (Gate 1)
  static const uint8_t DRUM1   = 2;  // Q2
  static const uint8_t DRUM2   = 3;  // Q3
  static const uint8_t DRUM3   = 4;  // Q4
  static const uint8_t DRUM4   = 5;  // Q5
  static const uint8_t DAC1_CS = 6;  // Q6 (active-low, keep HIGH when idle)
  static const uint8_t DAC2_CS = 7;  // Q7 (active-low, keep HIGH when idle)
}

class Expander595 {
public:
  Expander595(SPIClass &spi, uint8_t latchPin, uint32_t hz = 4000000)
    : spi_(spi), latchPin_(latchPin), spiHz_(hz) {}

  void begin() {
    pinMode(latchPin_, OUTPUT);
    digitalWrite(latchPin_, LOW);
    write(image_); // push default image (all HIGH)
  }

  inline uint8_t image() const { return image_; }

  void write(uint8_t img) {
    image_ = img;
    spi_.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
    spi_.transfer(image_);
    spi_.endTransaction();
    // latch rising edge
    digitalWrite(latchPin_, HIGH);
    delayMicroseconds(1);
    digitalWrite(latchPin_, LOW);
  }

  // Convenience: set or clear a bit and write if changed
  inline void setBit(uint8_t bit, bool high) {
    uint8_t newImg = high ? (image_ | (1u << bit)) : (image_ & ~(1u << bit));
    if (newImg != image_) write(newImg);
  }

  // Ensure CS lines are high for the given bits (no write if already high)
  inline void deassertCs(uint8_t csBitA, uint8_t csBitB) {
    uint8_t newImg = image_ | (1u << csBitA) | (1u << csBitB);
    if (newImg != image_) write(newImg);
  }

private:
  SPIClass &spi_;
  uint8_t latchPin_;
  uint32_t spiHz_;
  uint8_t image_ = 0xFF; // all HIGH by default
};

} // namespace expander_io
