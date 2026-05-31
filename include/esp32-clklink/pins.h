// Pin assignments for the ESP32 Clk/Link module.
//
// The hardware is shared with the previous esp32oscclk firmware; only the
// CV-in role changes (was pitch CV for the oscillator; now an external
// reset trigger input that fires the reset pulse on Channel B).

#pragma once

constexpr int PIN_POT      = 32;  // ADC, BPM in INTERNAL or CV value in LINK
constexpr int PIN_CV_RESET = 33;  // ADC, external reset trigger input
constexpr int PIN_SW_LINK  = 35;  // input-only, ON-OFF-ON switch upper throw
constexpr int PIN_SW_INT   = 34;  // input-only, ON-OFF-ON switch lower throw
constexpr int PIN_LED      = 2;   // onboard LED (status)
