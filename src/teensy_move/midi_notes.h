// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// MIDI notes from USB (the Move) and the TRS input, merged, for the modes that only want notes.
// Channel 10 is left out, so a Move drum track doesn't change the chord.

#pragma once

#include <Arduino.h>

namespace midi_notes {

using Note = void (*)(uint8_t note);
using Bend = void (*)(float semitones);

void begin(Note on, Note off, Bend bend);
void poll();  // every pass of the loop

}  // namespace midi_notes
