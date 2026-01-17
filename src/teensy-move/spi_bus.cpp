#include "spi_bus.h"
#include <SPI.h>

static uint8_t g_latchPin = 255;
// NOTE: This byte is the *74HC595 Q output level* (pre-inverter).
// If your expander hardware inverts these lines (e.g. via a 74HCT14), then:
//   - Q HIGH -> jack LOW
//   - Q LOW  -> jack HIGH
// The main loop updates these bits continuously; this just defines a safe power-on image.
static uint8_t g_image = 0xFF; // Q HIGH (DAC CS inactive; others depend on downstream inversion)

void expanderInit(uint8_t latchPin){
  g_latchPin = latchPin;
  pinMode(g_latchPin, OUTPUT);
  digitalWrite(g_latchPin, LOW);
  // Ensure SPI is started by caller (main.cpp does SPI.begin())
  expanderWrite(g_image);
}

void expanderWrite(uint8_t image){
  g_image = image;
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(g_image);
  SPI.endTransaction();
  // Latch rising edge
  digitalWrite(g_latchPin, HIGH);
  // short pulse for reliability
  delayMicroseconds(1);
  digitalWrite(g_latchPin, LOW);
}

uint8_t expanderImage(){ return g_image; }
