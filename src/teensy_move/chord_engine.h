// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The chord engine: one finger plays a chord of four notes, which lead smoothly into the next. Each
// note is a stereo stack of detuned oscillators and a tuned string, a comb resonator that an audio
// input or a pluck can ring. Used by Chordal (mode 2), and by Warps as a carrier for its vocoder.
//
// Plain C++ with no Arduino in it, so scripts/host/chord_render.cpp can run it on a Mac.

#pragma once

#include <cstdint>

namespace chord {

constexpr int kNotes = 4;  // notes in a chord: a bass and three above it
constexpr int kStack = 5;  // oscillators on each note

// What the panel sets, kept as it is in the EEPROM: 15 bytes, inside a mode's 16 with a magic byte
struct Params {
  uint8_t key = 0;       // 0 = C
  uint8_t harmony = 0;   // a scale, then the progressions: HarmonyName()
  uint8_t stack = 0;     // how a scale builds its chords: StackName()
  uint8_t voicing = 0;   // 0 leads each chord into the next, then fixed voicings: VoicingName()
  int8_t octave = 0;
  // 0-100 from here
  uint8_t sound = 0;     // 0 the oscillators, 100 the strings
  uint8_t shape = 100;   // 0 sine, 50 triangle, 100 saw
  uint8_t bright = 45;
  uint8_t spread = 35;   // detune and stereo width
  uint8_t motion = 35;   // how much it drifts and breathes
  uint8_t swell = 25;    // attack, release and how long the strings ring
  uint8_t strum = 8;     // the gap between one note entering and the next
  uint8_t glide = 20;    // how long a note takes to reach the next chord
  uint8_t hold = 1;      // 1: a chord stays until the next one
  uint8_t level = 70;
};

int HarmonyCount();
const char *HarmonyName(int i);
int StackCount();
const char *StackName(int i);
int VoicingCount();
const char *VoicingName(int i);
const char *KeyName(int key);

class Engine {
 public:
  void Init(float sample_rate);

  Params p;  // read once a block

  // Call these with the audio interrupt off
  void NoteOn(uint8_t note);
  void NoteOff(uint8_t note);
  void Bend(float semitones) { bend_ = semitones; }
  void HoldChanged();  // after p.hold changes: with hold off and no key down, the chord lets go

  // A block of samples at ±1, once per block. `in` may be null: it is what rings the strings.
  void Process(const float *in, float *l, float *r, int n);

  // For the CV outs and the screen
  float Pitch(int v) const { return voice_[v].note + bend_; }  // a MIDI note, while it glides
  bool GateOn() const;
  float MotionOut() const { return motion_out_; }  // -1 to 1, slowly
  const char *ChordName() const { return name_; }
  // Bit n: note n has entered since the last call
  uint8_t TakeTriggers() {
    uint8_t t = triggers_;
    triggers_ = 0;
    return t;
  }

 private:
  struct Osc {
    float phase, inc, drift, gain_l, gain_r;
  };
  struct Voice {
    Osc osc[kStack];
    float note, target, pending_note;
    int32_t pending;  // samples until pending_note takes over; negative when there's none
    float env, gate, bloom;
    float s1_l, s2_l, s1_r, s2_r;  // the low-pass, each side
    float g, a1, a2, a3;
    float lfo_amp, lfo_amp_inc, lfo_tone, lfo_tone_inc;
    float gain_str_l, gain_str_r;
    float *line;
    uint32_t write;
    float delay, delay_step, fb, damp, damp_state;
    int32_t burst, burst_len;
    float burst_amp;
    float amp_now, amp_step;
    uint32_t noise;
  };

  void Control(int n);
  void BuildChord(uint8_t note);
  void Start(Voice &v, float note);
  void RenderOscillators(Voice &v, float *l, float *r, int n, float mix);
  void RenderString(Voice &v, const float *in, float *l, float *r, int n, float mix);

  static constexpr int kLine = 3400;  // samples: down to about 28 Hz at 96 kHz
  static constexpr int kHeld = 8;

  float sr_ = 96000.0f;
  Voice voice_[kNotes];
  float lines_[kNotes][kLine];
  float bend_ = 0, motion_out_ = 0;
  float w_sin_ = 0, w_tri_ = 0, w_saw_ = 1;
  uint8_t held_[kHeld];
  int n_held_ = 0;
  bool started_ = false;
  uint8_t triggers_ = 0;
  uint32_t rng_ = 0x51ED270B;
  char name_[8] = "---";
};

// The one engine: only one mode runs in a session
Engine &shared();

}  // namespace chord
