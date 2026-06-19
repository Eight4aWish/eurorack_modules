// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// Pin allocations for the esp32_clklinkrec module, targeted at the Seeed
// Studio XIAO ESP32-C5. Constants are raw ESP32 GPIO numbers, NOT Xiao
// D-pad numbers — the D-label macros are only defined in the
// XIAO_ESP32C5 board variant, and under arduino+espidf the build picks
// up the generic esp32c5 variant (which lacks D0..D10) instead. Raw
// GPIOs work regardless of which variant the build settles on.
// The Xiao D-label is in the comment for cross-reference with
// docs/ESP32_CLKLINKREC.md and the silkscreen.

#pragma once

// --- Front-panel inputs ----------------------------------------------------
constexpr int PIN_SW_LINK     = 1;    // Xiao D0   — momentary, Link enable toggle
constexpr int PIN_SW_CAPTURE  = 9;    // Xiao D9   — momentary, Capture trigger
constexpr int PIN_RESET_IN    = 10;   // Xiao D10  — Eurorack reset input (via 74HCT14 ch.6)

// --- Trigger outputs (to 74HCT14 inputs; inverting) ------------------------
constexpr int PIN_CLK_OUT     = 23;   // Xiao D4   — Clock pulse output
constexpr int PIN_RST_OUT     = 24;   // Xiao D5   — Reset pulse output
constexpr int PIN_RUN_OUT     = 11;   // Xiao D6   — Run gate output

// --- Front-panel LEDs (driven via 74HCT14; inverting) ----------------------
constexpr int PIN_RED_LED     = 12;   // Xiao D7   — Red LED (Capture)
constexpr int PIN_BLUE_LED    = 8;    // Xiao D8   — Blue LED (Link)

// --- Unused / left disconnected --------------------------------------------
// Xiao D1  / GPIO0   — boot pin, leave floating
// Xiao D2  / GPIO25  — strapping pin, leave floating
// Xiao D3  / GPIO7   — strapping pin, leave floating
// MTMS, MTDI, MTCK, MTDO — JTAG pads on the back, available as spare GPIOs
//                          if a future revision needs more I/O
