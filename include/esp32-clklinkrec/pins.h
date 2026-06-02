// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// Pin allocations for the esp32-clklinkrec module, targeted at the Seeed
// Studio XIAO ESP32-C5. All references use Xiao D-labels; underlying
// GPIO numbers are noted in comments but the firmware should not depend
// on raw GPIO numbers because they vary across Xiao variants.

#pragma once

// --- Front-panel inputs ----------------------------------------------------
constexpr int PIN_SW_LINK     = 0;   // D0  / GPIO1   — momentary, Link enable toggle
constexpr int PIN_SW_CAPTURE  = 9;   // D9  / GPIO9   — momentary, Capture trigger
constexpr int PIN_RESET_IN    = 10;  // D10 / GPIO10  — Eurorack reset input (via 74HCT14 ch.6)

// --- Trigger outputs (to 74HCT14 inputs; inverting) ------------------------
constexpr int PIN_CLK_OUT     = 4;   // D4  / GPIO23  — Clock pulse output
constexpr int PIN_RST_OUT     = 5;   // D5  / GPIO24  — Reset pulse output
constexpr int PIN_RUN_OUT     = 6;   // D6  / GPIO11  — Run gate output

// --- Front-panel LEDs (driven via 74HCT14; inverting) ----------------------
constexpr int PIN_RED_LED     = 7;   // D7  / GPIO12  — Red LED (Capture)
constexpr int PIN_BLUE_LED    = 8;   // D8  / GPIO8   — Blue LED (Link)

// --- Unused / left disconnected --------------------------------------------
// D1  / GPIO0   — boot pin, leave floating
// D2  / GPIO25  — strapping pin, leave floating
// D3  / GPIO7   — strapping pin, leave floating
// MTMS, MTDI, MTCK, MTDO — JTAG pads on the back, available as spare GPIOs
//                          if a future revision needs more I/O
