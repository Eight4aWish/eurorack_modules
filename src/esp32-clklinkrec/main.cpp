// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// ESP32 Clk/Link/Rec — combined Ableton Link clock generator and Recorder
// trigger module, targeted at the Seeed Studio XIAO ESP32-C5.
//
// This module links against the Ableton Link library (GPL-2.0-or-later);
// the firmware binary is therefore GPL-2.0-or-later.
//
// See:
//   docs/ESP32_CLKLINKREC.md   — hardware design, pin allocation, netlist
//   docs/RECORDER_PROTOCOL.md  — wire contract for the Mac recorder app
//
// Status: skeleton only. Firmware implementation is pending the next pass.
// The pin allocation in include/esp32-clklinkrec/pins.h is finalised and
// the hardware design is locked.

#include <Arduino.h>
#include "esp32-clklinkrec/pins.h"

void setup() {
    // TODO: WiFi connect (secrets in include/shared/secrets.h)
    // TODO: Link instance + enable_start_stop_sync
    // TODO: pinMode for buttons (INPUT_PULLUP) + 74HCT14 outputs (HIGH = idle)
    // TODO: Schmitt-trigger inverted logic — drive GPIO LOW to assert
    // TODO: mDNS resolver for _recorder._tcp.local
}

void loop() {
    // TODO: poll switches, debounce-toggle Link enable on momentary press
    // TODO: tick Link beat detection, fire CLK/RST/RUN pulses
    // TODO: edge-detect reset input, force Link beat realignment
    // TODO: on Capture button edge, spawn FreeRTOS task to POST /capture
    // TODO: red LED on while POST in flight, off when 200 received
    // TODO: blue LED reflects Link peer state (off / flash / solid)
}
