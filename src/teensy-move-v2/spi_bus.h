#pragma once
#include <Arduino.h>

// 74HCT595 bit mapping (Q0..Q7)
namespace ExpanderBits {
  // Per user wiring: Q0 -> Gate 2, Q1 -> Gate 1
  static const uint8_t V2_GATE = 0;  // Q0 (Gate 2)
  static const uint8_t V1_GATE = 1;  // Q1 (Gate 1)
  static const uint8_t DRUM1   = 2;  // Q2
  static const uint8_t DRUM2   = 3;  // Q3
  static const uint8_t DRUM3   = 4;  // Q4
  static const uint8_t DRUM4   = 5;  // Q5
  static const uint8_t DAC1_CS = 6;  // Q6 (active-low, keep HIGH when idle)
  static const uint8_t DAC2_CS = 7;  // Q7 (active-low, keep HIGH when idle)
}

void expanderInit(uint8_t latchPin);
void expanderWrite(uint8_t image);
uint8_t expanderImage();
