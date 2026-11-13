// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// What a MOD2 voice sees of the Rev B frame (mod2.cpp). Each voice in voices/ is one MOD2 sketch:
// its sound engine as published, with its setup()/loop()/ISRs turned into the calls below. The
// frame runs Render() at the sketch's own sample rate and turns the PWM level it returns into
// volts the way the MOD2's output stage does. docs/REVB_MOD2.md has the whole story.

#ifndef MOD2_VOICE_H
#define MOD2_VOICE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace mod2 {

// The panel as a MOD2 sketch reads it, refreshed by the frame every sample.
struct Panel {
  // The three ADC inputs, 0 to 1, as the MOD2's ADC reads them: a[0] is POT1, a[1] POT2, and a[2]
  // is POT3 plus the CV through the MOD2's inverting input stage, so it falls as POT3 turns
  // clockwise or the CV rises. Each sketch corrects for that itself, or doesn't, as published.
  float a[3];
  bool in2;        // IN2 high now
  bool in2Recent;  // IN2 high now or in the last 15 ms (the snare's and the laser's window)
};
extern Panel panel;

// As the sketches read them: 12-bit (the newer, DMA-averaged sketches) or 10-bit (analogRead)
inline uint16_t Adc12(int ch) { return (uint16_t)(panel.a[ch] * 4095.0f + 0.5f); }

constexpr float PI_F = 3.14159265358979323846f;
// Arduino's PI, a double: the older sketches (clap, claves, hi-hat, kick, FM drum) compute with it,
// so their ports do too, where they do, to give the same samples
constexpr double PI_D = 3.1415926535897932384626433832795;
// The older sketches' rate: a PWM slice wrapping at 4096 counts of the MOD2's 150 MHz clock
constexpr float FS_36K6 = 150000000.0f / 4096.0f;
// The most a voice keeps in flash, as its sketch keeps in EEPROM
constexpr size_t SAVE_BYTES = 32;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// newlib's rand() and srand(), as the MOD2's Arduino core has them, for the noise tables. (Calling
// newlib's own would pull in its malloc locks, which this SDK build doesn't provide.)
constexpr int RAND_MAX_NEWLIB = 0x7FFFFFFF;
struct NewlibRand {
  uint64_t next;
  explicit NewlibRand(uint32_t seed) : next(seed) {}
  int operator()() {
    next = next * 6364136223846793005ULL + 1;
    return (int)((next >> 32) & RAND_MAX_NEWLIB);
  }
};

// A voice owns nothing outside itself (mod2.cpp asserts that each is trivially destructible), so
// the frame replaces one with the next without destroying it. (A virtual destructor would bring in
// operator delete, and with it newlib's malloc, whose locks this SDK build doesn't provide.)
class Voice {
 public:
  // The sketch's audio sample rate and PWM resolution (the PWM counts 0 to Wrap())
  virtual float Rate() const = 0;
  virtual uint16_t Wrap() const = 0;

  // Build the tables setup() built. Called from the background loop before the voice plays.
  virtual void Init() {}
  // The settings the sketch keeps in EEPROM. Save returns the bytes written (at most SAVE_BYTES);
  // Load gets them back after Init, and checks them as the sketch does.
  virtual size_t Save(uint8_t *out) const {
    (void)out;
    return 0;
  }
  virtual void Load(const uint8_t *in, size_t n) {
    (void)in;
    (void)n;
  }

  // From the audio interrupt: IN1's rising edge, the MOD2's BUTTON (the encoder's push: pressed,
  // or released after heldMs), and the sketch's loop() work on the pots, 1000 times a second
  virtual void Trigger() = 0;
  virtual void Button(bool pressed, uint32_t heldMs) = 0;
  virtual void Controls() {}
  // The next sample at Rate(), as the PWM level the sketch would write
  virtual uint16_t Render() = 0;

  // From the background loop: work too slow for the interrupt (the hi-hat's noise table)
  virtual void Background() {}

  virtual bool Active() const = 0;  // sounding (or about to)
  virtual float Led() const = 0;    // the MOD2's LED, 0 to 1
  virtual void Label(char *out) const = 0;  // the screen's top line, up to 10 characters

  // The sketch would write its EEPROM now; the frame does it once the voice is quiet. (Set from
  // the interrupt, taken from the background loop: hence the atomic exchange.)
  bool TakeSaveRequest() { return __atomic_exchange_n(&saveRequested, false, __ATOMIC_SEQ_CST); }

 protected:
  bool saveRequested = false;
};

// Store and fetch floats in a save blob, as EEPROM.put/get do
inline void PutF(uint8_t *out, int slot, float v) { memcpy(out + 4 * slot, &v, 4); }
inline float GetF(const uint8_t *in, int slot) {
  float v;
  memcpy(&v, in + 4 * slot, 4);
  return v;
}

}  // namespace mod2

#endif
