// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The two parts of the MOD2 frame that turn a voice's PWM levels into volts, shared by mod2.cpp and
// the host renderer (host_render.cpp), so what renders on a computer is what plays on the board.

#ifndef MOD2_FRAME_H
#define MOD2_FRAME_H

#include "voice.h"

namespace mod2 {

// Runs a voice at its own rate and carries its PWM level to the codec's rate by linear
// interpolation (one voice sample late). A 48 kHz voice runs one sample per codec sample.
struct Runner {
  bool direct = false;
  float step = 1.0f, phase = 0.0f, prev = 0.0f, cur = 0.0f, scale = 1.0f / 1024.0f;

  // returns the voice's rest level, as a PWM duty (0 to 1)
  float Start(Voice *v, float fs) {
    float rate = v->Rate();
    direct = fabsf(rate - 48000.0f) < 1.0f;
    step = rate / fs;
    scale = 1.0f / (v->Wrap() + 1.0f);
    prev = cur = v->Render();  // a quiet voice gives its rest level
    phase = 0.0f;
    return cur * scale;
  }
  // the next codec sample, as a PWM duty
  float Next(Voice *v) {
    if (direct) return v->Render() * scale;
    phase += step;
    while (phase >= 1.0f) {
      prev = cur;
      cur = v->Render();
      phase -= 1.0f;
    }
    return (prev + (cur - prev) * phase) * scale;
  }
};

// The MOD2's output in software: the PWM's average (3.3 V full scale) through R17/C13(+C14) and
// R19/C16(+C17), then the x3.06 amplifier. DC is JP2 closed (0 to about +10 V, +5 V at rest); AC is
// JP2 open, through C18 into R20 (about 1.6 Hz), so about +-5 V. JP1 sets the filter: bright is
// 15.9 kHz (1 k, 10 nF), dark 5.0 kHz (1 k, 32 nF).
struct OutputStage {
  float fs = 48000.0f;
  bool bright = true;
  float rcAlpha = 0.0f, hpAlpha = 0.0f, lp1 = 0.5f, lp2 = 0.5f, hp = 0.5f * 3.3f * 3.06f;
  float ac = 0.0f, dc = 0.0f;

  explicit OutputStage(float fs_) : fs(fs_) {
    hpAlpha = 1.0f - expf(-2.0f * PI_F * 1.59f / fs);
    SetFilter(true);
  }
  void SetFilter(bool b) {
    bright = b;
    float fc = 1.0f / (2.0f * PI_F * 1000.0f * (b ? 10e-9f : 32e-9f));
    rcAlpha = 1.0f - expf(-2.0f * PI_F * fc / fs);
  }
  void Process(float duty) {
    lp1 += rcAlpha * (duty - lp1);
    lp2 += rcAlpha * (lp1 - lp2);
    dc = lp2 * 3.3f * 3.06f;
    hp += hpAlpha * (dc - hp);
    ac = dc - hp;
  }
};

}  // namespace mod2

#endif
