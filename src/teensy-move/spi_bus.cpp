#include "spi_bus.h"
#include <SPI.h>

static uint8_t g_latchPin = 255;
static uint8_t g_image = 0xFF; // default all HIGH (gates/drums deasserted, DAC CS inactive)

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
