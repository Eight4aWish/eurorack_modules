// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Boy: the Move companion and more, on the analogue board Rev A (Teensy 4.1). It took over
// src/teensy_move from the old Boy, whose firmware is now src/teensy_move_old.
//
// The mode is chosen at power-on and holds for the session, because modes set the six CV jacks as
// inputs or outputs and that shouldn't change under a patch. The chooser starts on the last mode:
// turn the encoder to pick another, press to start. Untouched, it starts the last mode after 3 s,
// so a rack powering up comes back as it was. Holding the push for 2 s in any mode ends the session
// and returns here without a power cycle; the chooser then waits for a press.
//
// Modes: 1, the Move companion; 2, Warps. To come: 3, other two-input processing.

#include <Arduino.h>
#include <CrashReport.h>

#include "board.h"
#include "move_mode.h"
#include "warps_mode.h"

struct Mode {
  const char *name;
  void (*begin)();
  void (*loop)();
};

static const Mode MODES[] = {
    {"MOVE companion", move_mode::begin, move_mode::loop},
    {"WARPS", warps_mode::begin, warps_mode::loop},  // warps_mode::MODE is this place, 1
};
constexpr uint8_t NUM_MODES = sizeof(MODES) / sizeof(MODES[0]);
constexpr uint32_t CHOOSER_WAIT_MS = 3000;

static const Mode *mode;

static uint8_t choose(uint8_t pick, bool wait) {
  uint32_t start = millis();
  char line[32];
  while (true) {
    while (usbMIDI.read()) {
    }  // nothing listens yet: keep the host's queue empty
    int32_t turns = board::encoder_turns();
    if (turns) {
      pick = constrain((int32_t)pick + turns, 0, NUM_MODES - 1);
      wait = true;  // a turn means the choice is being made by hand
    }
    board::Push push = board::poll_push();
    if (push == board::Push::SHORT || push == board::Push::LONG) return pick;
    uint32_t left = CHOOSER_WAIT_MS - min(millis() - start, CHOOSER_WAIT_MS);
    if (!wait && !left) return pick;

    screen::row(0, "BOY mode");
    snprintf(line, sizeof(line), "> %u %s", pick + 1, MODES[pick].name);
    screen::row(1, line);
    if (pick + 1 < NUM_MODES)
      snprintf(line, sizeof(line), "  %u %s", pick + 2, MODES[pick + 1].name);
    else
      line[0] = 0;
    screen::row(2, line);
    if (wait)
      snprintf(line, sizeof(line), "turn, press to start");
    else
      snprintf(line, sizeof(line), "press, or wait %lus", (left + 999) / 1000);
    screen::row(3, line);
    screen::service();
  }
}

void setup() {
  // Full-speed USB (12 Mbit), as the old Boy: at 480 Mbit enumeration fails now and then through bus-powered
  // hubs, and MIDI needs none of the bandwidth. The core has already connected at 480, so detach,
  // set PFSC and re-attach for the host to renegotiate. Costs about a second at boot.
  USB1_USBCMD &= ~USB_USBCMD_RS;
  delay(50);
  USB1_PORTSC1 |= USB_PORTSC1_PFSC;
  USB1_USBCMD |= USB_USBCMD_RS;

  if (CrashReport) {
    while (!Serial && millis() < 4000) {
    }
    Serial.print(CrashReport);
  }
  if (!board::begin()) {
    // with VIN-VUSB cut the Teensy only runs from the rack, so a silent EEPROM is a fault
    Serial.println("Boy: the EEPROM at 0x50 doesn't answer; the board is left alone.");
    while (true) {
    }
  }
  screen::begin();

  bool wait = board::take_menu_request();
  uint8_t pick = board::saved_mode();
  if (pick >= NUM_MODES) pick = 0;
  pick = choose(pick, wait);
  board::save_mode(pick);
  for (uint8_t r = 0; r < 4; r++) screen::row(r, "");
  mode = &MODES[pick];
  mode->begin();
}

void loop() { mode->loop(); }
