// teensy-drums — MIDI ch10 drum trigger module
// Teensy 4.1 + Teensy Audio Board
// Receives MIDI note-on on channel 10, fires digital trigger pulses

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <MIDI.h>
#include "teensy-drums/pins.h"

// ---------------------------------------------------------------------------
// Audio — passthrough (extend with synthesis as needed)
// ---------------------------------------------------------------------------
AudioInputI2S            i2sIn;
AudioOutputI2S           i2sOut;
AudioConnection          patchL(i2sIn, 0, i2sOut, 0);
AudioConnection          patchR(i2sIn, 1, i2sOut, 1);
AudioControlSGTL5000     sgtl5000;

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------
MIDI_CREATE_DEFAULT_INSTANCE();

// ---------------------------------------------------------------------------
// Trigger state
// ---------------------------------------------------------------------------
struct Trigger {
    uint8_t  pin;
    uint32_t offTime;   // millis() when to go LOW
    bool     active;
};

// Map GM drum note numbers to trigger outputs
// https://www.midi.org/specifications-old/item/gm-level-1-sound-set
static Trigger triggers[] = {
    { PIN_TRIG_KICK,  0, false },  // note 36 — Bass Drum 1
    { PIN_TRIG_SNARE, 0, false },  // note 38 — Acoustic Snare
    { PIN_TRIG_HIHAT, 0, false },  // note 42 — Closed Hi-Hat
    { PIN_TRIG_CLAP,  0, false },  // note 39 — Hand Clap
    { PIN_TRIG_TOM1,  0, false },  // note 45 — Low Tom
    { PIN_TRIG_TOM2,  0, false },  // note 48 — Hi-Mid Tom
    { PIN_TRIG_RIDE,  0, false },  // note 51 — Ride Cymbal 1
    { PIN_TRIG_CRASH, 0, false },  // note 49 — Crash Cymbal 1
};

static const uint8_t gmNotes[] = { 36, 38, 42, 39, 45, 48, 51, 49 };
static const uint8_t NUM_TRIGGERS = sizeof(triggers) / sizeof(triggers[0]);

void fireTrigger(uint8_t idx) {
    digitalWrite(triggers[idx].pin, HIGH);
    triggers[idx].offTime = millis() + TRIG_PULSE_MS;
    triggers[idx].active  = true;
}

void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (ch != 10 || vel == 0) return;
    for (uint8_t i = 0; i < NUM_TRIGGERS; i++) {
        if (gmNotes[i] == note) {
            fireTrigger(i);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
    AudioMemory(16);

    sgtl5000.enable();
    sgtl5000.volume(0.5f);

    for (uint8_t i = 0; i < NUM_TRIGGERS; i++) {
        pinMode(triggers[i].pin, OUTPUT);
        digitalWrite(triggers[i].pin, LOW);
    }

    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.setHandleNoteOn(handleNoteOn);
}

void loop() {
    MIDI.read();

    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_TRIGGERS; i++) {
        if (triggers[i].active && now >= triggers[i].offTime) {
            digitalWrite(triggers[i].pin, LOW);
            triggers[i].active = false;
        }
    }
}
