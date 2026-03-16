#pragma once

// Teensy 4.1 + Audio Board — Drum trigger outputs
// Adjust pin assignments to match your hardware

// Digital trigger outputs (3.3V, use a buffer/level shifter for 5V eurorack)
#define PIN_TRIG_KICK    2
#define PIN_TRIG_SNARE   3
#define PIN_TRIG_HIHAT   4
#define PIN_TRIG_CLAP    5
#define PIN_TRIG_TOM1    6
#define PIN_TRIG_TOM2    7
#define PIN_TRIG_RIDE    8
#define PIN_TRIG_CRASH   9

// Trigger pulse duration (ms)
#define TRIG_PULSE_MS    10
