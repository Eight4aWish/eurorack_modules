// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include "midi_notes.h"

#include <MIDI.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, trsNotes);

namespace midi_notes {

constexpr uint8_t DRUMS_CH = 10;

static Note noteOn, noteOffFn;
static Bend bendFn;

static void off(byte ch, byte note, byte) {
  if (ch != DRUMS_CH) noteOffFn(note);
}

static void on(byte ch, byte note, byte vel) {
  if (!vel) return off(ch, note, 0);
  if (ch != DRUMS_CH) noteOn(note);
}

// Both inputs pass the bend centred, -8192 to +8191; +-2 semitones, as mode 1
static void bend(byte, int value) { bendFn(2.0f * value / 8192.0f); }

void begin(Note onFn, Note offFn, Bend bendTo) {
  noteOn = onFn;
  noteOffFn = offFn;
  bendFn = bendTo;
  usbMIDI.setHandleNoteOn(on);
  usbMIDI.setHandleNoteOff(off);
  usbMIDI.setHandlePitchChange(bend);
  trsNotes.setHandleNoteOn(on);
  trsNotes.setHandleNoteOff(off);
  trsNotes.setHandlePitchBend(bend);
  trsNotes.begin(MIDI_CHANNEL_OMNI);
  trsNotes.turnThruOff();  // no MIDI out on Boy
}

void poll() {
  while (usbMIDI.read()) {
  }
  while (trsNotes.read()) {
  }
}

}  // namespace midi_notes
