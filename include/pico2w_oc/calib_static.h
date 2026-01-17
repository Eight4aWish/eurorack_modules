#ifndef PICO2W_OC_CALIB_STATIC_H
#define PICO2W_OC_CALIB_STATIC_H

// Static calibration constants and helpers for Pico 2 W (ADS1115 + MCP4728)
// Note: We map logical CV indices (CV0..CV3) to PHYSICAL MCP channels (A/B/C/D = 0..3)
// using the macros in pins.h, so your per-channel coefficients can be keyed to
// the actual MCP channels regardless of CV naming.

#include "pico2w_oc/pins.h"
// ADC formulas provided as: code = a + b * volts
// DAC formulas provided as: volts = vOff + b * code (b negative)

namespace pico2w_oc_calib {
// ADC (CV inputs)
static constexpr float ADC0_A = 13567.0f; 
static constexpr float ADC0_B = -2418.1f; 
static constexpr float ADC1_A = 13557.0f; 
static constexpr float ADC1_B = -2412.5f;

// DAC (CV outputs) — Calibration Notes
// - Define slopes/offsets against PHYSICAL MCP4728 channel order (A/B/C/D = 0/1/2/3).
// - Logical CV names (CV0..CV3) are mapped to physical MCP channels via CVx_DA_CH in pins.h.
// - To compute coefficients, measure with a DMM and fit: volts = m * code + c.
//   Set DACx_B = m and DACx_VOFF = c for each physical MCP channel.
// - If CV jacks are re-ordered, adjust only CVx_DA_CH in pins.h — leave these coefficients as-is.
// - Use sufficient significant figures for slopes (≥ 6) to minimize pitch error.
// Default slopes match prior behavior; update per physical channel as needed.
static constexpr float DAC0_B = -0.0024814f; // volts per code (MCP ch 0 / A)
static constexpr float DAC1_B = -0.0024792f; // volts per code (MCP ch 1 / B)
static constexpr float DAC2_B = -0.0025007f; // volts per code (MCP ch 2 / C)
static constexpr float DAC3_B = -0.0025050f; // volts per code (MCP ch 3 / D)
static constexpr float DAC0_VOFF = 5.1032f;
static constexpr float DAC1_VOFF = 5.1125f;
static constexpr float DAC2_VOFF = 5.1166f;
static constexpr float DAC3_VOFF = 5.1298f;

// Map raw ADC code to CV volts using logical input index (0=AD0,1=AD1)
// Select coefficients by PHYSICAL ADS channel via ADx_CH mapping to avoid
// confusion when jacks are swapped.
inline float adcCodeToVolts(int logicalIndex, int16_t code) {
  const int phys = (logicalIndex == 0) ? AD0_CH : AD1_CH; // 0..1 physical ADS channel
  const float A[2] = { ADC0_A, ADC1_A };
  const float B[2] = { ADC0_B, ADC1_B };
  return (static_cast<float>(code) - A[phys]) / B[phys];
}

// Map target CV volts to DAC code using logical CV index (0..3)
// Map target CV volts to DAC code using logical CV index (0..3),
// selecting per-physical MCP channel coefficients via CVx_DA_CH mapping.
inline uint16_t dacVoltsToCode(int logicalIndex, float volts) {
  // Map logical CV index to physical MCP channel (0..3)
  const int phys = (logicalIndex==0)?CV0_DA_CH:(logicalIndex==1)?CV1_DA_CH:(logicalIndex==2)?CV2_DA_CH:CV3_DA_CH;
  // Coefficients array by physical channel order
  const float B[4]    = { DAC0_B, DAC1_B, DAC2_B, DAC3_B };
  const float Voff[4] = { DAC0_VOFF, DAC1_VOFF, DAC2_VOFF, DAC3_VOFF };
  float vOff = Voff[phys];
  float b    = B[phys];
  float codef = (volts - vOff) / b; // slopes can differ per channel (typically negative)
  if (codef < 0.0f) codef = 0.0f; else if (codef > 4095.0f) codef = 4095.0f;
  return static_cast<uint16_t>(codef + 0.5f);
}

// Map target CV volts to DAC code by PHYSICAL MCP channel index (A/B/C/D = 0..3)
inline uint16_t dacVoltsToCodePhys(int physIndex, float volts) {
  if (physIndex < 0 || physIndex > 3) return 0u;
  const float B[4]    = { DAC0_B, DAC1_B, DAC2_B, DAC3_B };
  const float Voff[4] = { DAC0_VOFF, DAC1_VOFF, DAC2_VOFF, DAC3_VOFF };
  float codef = (volts - Voff[physIndex]) / B[physIndex];
  if (codef < 0.0f) codef = 0.0f; else if (codef > 4095.0f) codef = 4095.0f;
  return static_cast<uint16_t>(codef + 0.5f);
}

// Helper: estimate CV volts from DAC code using PHYSICAL MCP channel index (0..3).
// Useful for OLED diagnostics to validate mapping/calibration.
inline float dacCodeToVoltsPhys(int physIndex, uint16_t code) {
  if (physIndex < 0 || physIndex > 3) return NAN;
  const float B[4]    = { DAC0_B, DAC1_B, DAC2_B, DAC3_B };
  const float Voff[4] = { DAC0_VOFF, DAC1_VOFF, DAC2_VOFF, DAC3_VOFF };
  return Voff[physIndex] + B[physIndex] * static_cast<float>(code);
}
}

#endif // PICO2W_OC_CALIB_STATIC_H
