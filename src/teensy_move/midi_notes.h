// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// MIDI notes from USB (the Move) and the TRS input, merged, for the modes that only want notes.
// Channel 10 is left out, so a Move drum track doesn't change the chord. Transport stop, All Sound Off
// and All Notes Off all call `stop`.

#pragma once

#include <Arduino.h>

namespace midi_notes {

using Note = void (*)(uint8_t note);
using Bend = void (*)(float semitones);
using Stop = void (*)();

void begin(Note on, Note off, Bend bend, Stop stop);
// MIDI clock (24 ticks a quarter note) and Start or Continue, from both inputs, for a mode that wants them
using Tick = void (*)();
void clock(Tick tick, Tick start);
void poll();  // every pass of the loop

}  // namespace midi_notes
