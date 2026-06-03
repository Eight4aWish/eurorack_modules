// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// Pin allocations for the esp32-clklinkrec module, targeted at the Seeed
// Studio XIAO ESP32-C5. The seeed_xiao_esp32c5 board variant supplies
// D0..D10 macros that map to the correct underlying GPIOs; always use
// the D-label macros and never raw GPIO numbers. (Building against the
// generic esp32-c5-devkitc-1 board would silently treat the small ints
// as raw GPIO numbers — most LEDs ended up driving strapping pins on
// the first bring-up before the board was switched.)

#pragma once

#include <Arduino.h>

// --- Front-panel inputs ----------------------------------------------------
constexpr int PIN_SW_LINK     = D0;   // GPIO1   — momentary, Link enable toggle
constexpr int PIN_SW_CAPTURE  = D9;   // GPIO9   — momentary, Capture trigger
constexpr int PIN_RESET_IN    = D10;  // GPIO10  — Eurorack reset input (via 74HCT14 ch.6)

// --- Trigger outputs (to 74HCT14 inputs; inverting) ------------------------
constexpr int PIN_CLK_OUT     = D4;   // GPIO23  — Clock pulse output
constexpr int PIN_RST_OUT     = D5;   // GPIO24  — Reset pulse output
constexpr int PIN_RUN_OUT     = D6;   // GPIO11  — Run gate output

// --- Front-panel LEDs (driven via 74HCT14; inverting) ----------------------
constexpr int PIN_RED_LED     = D7;   // GPIO12  — Red LED (Capture)
constexpr int PIN_BLUE_LED    = D8;   // GPIO8   — Blue LED (Link)

// --- Unused / left disconnected --------------------------------------------
// D1  / GPIO0   — boot pin, leave floating
// D2  / GPIO25  — strapping pin, leave floating
// D3  / GPIO7   — strapping pin, leave floating
// MTMS, MTDI, MTCK, MTDO — JTAG pads on the back, available as spare GPIOs
//                          if a future revision needs more I/O
