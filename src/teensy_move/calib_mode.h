// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The CALIBRATE item in the chooser: the CV outputs' calibration (spec requirement 10), as David chose it
// (2026-11-21): a meter sets the ADC with two readings on one jack, the ADC then measures every output at
// every whole volt from -5 to +5 V by loopback, and the meter spot-checks the result.

#pragma once

namespace calib_mode {
void begin();
void loop();
}  // namespace calib_mode
