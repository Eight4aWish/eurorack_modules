#pragma once

// Static calibration constants and helpers for Teensy 4.1 + Expander (MCP4822)
// Equations provided from DMM measurements (Excel fits):
//   volts = m * DAC_code + c
// We invert to compute DAC codes from target volts:
//   code = (volts - c) / m

namespace teensy_move_calib {

// Mod channels (M1..M4)
static constexpr float MOD_M[4] = { 0.0024272f, 0.0024278f, 0.0024264f, 0.0024392f};
static constexpr float MOD_C[4] = { -5.0459f, -5.0454f, -5.0609f, -5.0573f};

// Pitch channels (P1..P4)
static constexpr float PITCH_M[4] = { 0.0023378f, 0.0023201f, 0.0023457f, 0.0023256f};
static constexpr float PITCH_C[4] = { -2.9933f, -2.9519f, -3.0030f, -3.0114f};

inline uint16_t clamp12(float codef){
  if(codef < 0.0f) codef = 0.0f; else if(codef > 4095.0f) codef = 4095.0f;
  return static_cast<uint16_t>(codef + 0.5f);
}

// Map target volts to DAC code for Mod channel index 0..3 (M1..M4)
inline uint16_t modVoltsToCode(int idx, float volts){
  float codef = (volts - MOD_C[idx & 3]) / MOD_M[idx & 3];
  return clamp12(codef);
}

// Map target volts to DAC code for Pitch channel index 0..3 (P1..P4)
inline uint16_t pitchVoltsToCode(int idx, float volts){
  float codef = (volts - PITCH_C[idx & 3]) / PITCH_M[idx & 3];
  return clamp12(codef);
}

}
