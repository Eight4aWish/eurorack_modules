// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Runs the chord engine on a Mac, and through Warps' real DSP, to hear it before it's flashed.
//
//   scripts/host/build_chord_render.sh
//   chord_render <scene> <out.f32> [in.f32 [second_in.f32]]
//
// The output is interleaved stereo float at 96 kHz, and any input is mono float at 96 kHz.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "chord_engine.h"
#include "chord_library.h"
#include "stmlib/dsp/dsp.h"
#include "warps/dsp/modulator.h"
#include "warps/resources.h"

using chord::Engine;
constexpr float SR = 96000.0f;
constexpr int BLOCK = 96;
static std::vector<float> out_buf, in_buf, in2_buf;
static size_t in_pos;

struct Event {
  float at;
  int note;
  bool on;
};

static void gap(float sec) { out_buf.resize(out_buf.size() + 2 * size_t(sec * SR), 0.0f); }

static void dry(float sec, float gain = 1.0f) {
  for (size_t i = 0; i < size_t(sec * SR); i++) {
    float x = in_pos < in_buf.size() ? in_buf[in_pos++] * gain : 0.0f;
    out_buf.push_back(x);
    out_buf.push_back(x);
  }
}

static void play(Engine &e, const std::vector<Event> &score, float sec, bool use_in, float in_gain = 1.0f) {
  size_t next = 0;
  float l[BLOCK], r[BLOCK], in[BLOCK];
  for (size_t b = 0; b < size_t(sec * SR / BLOCK); b++) {
    float now = b * BLOCK / SR;
    while (next < score.size() && score[next].at <= now) {
      if (score[next].on) {
        e.NoteOn(score[next].note);
        printf("  %5.2fs %-7s", now, e.ChordName());
      } else {
        e.NoteOff(score[next].note);
      }
      next++;
    }
    for (int i = 0; i < BLOCK; i++) in[i] = use_in && in_pos < in_buf.size() ? in_buf[in_pos++] * in_gain : 0.0f;
    e.Process(use_in ? in : nullptr, l, r, BLOCK);
    if (b % 500 == 480) printf("[%.0f %.0f %.0f %.0f]\n", e.Pitch(0), e.Pitch(1), e.Pitch(2), e.Pitch(3));
    for (int i = 0; i < BLOCK; i++) {
      out_buf.push_back(l[i]);
      out_buf.push_back(r[i]);
    }
  }
}

// The old Boy's drone, as it was: four notes of two saws 12 cents apart, 350 ms in, 600 ms out, and
// each chord in root position (main.cpp at d691282^)
static void old_drone(const std::vector<std::vector<int>> &chords, float each) {
  float phase[8] = {0}, env = 0;
  for (const auto &ch : chords) {
    for (size_t n = 0; n < size_t(each * SR); n++) {
      float gate = n < size_t((each - 0.15f) * SR) ? 1.0f : 0.0f;
      env += (gate - env) * (gate > env ? 1.0f / (0.35f * SR) : 1.0f / (0.6f * SR));
      float s = 0;
      for (int v = 0; v < 4; v++) {
        float hz = 440.0f * powf(2.0f, (ch[v] - 69) / 12.0f);
        for (int o = 0; o < 2; o++) {
          float d = powf(2.0f, ((o == v % 2) ? 12.0f : -12.0f) / 1200.0f);
          float &ph = phase[v * 2 + o];
          ph += hz * d / SR;
          if (ph >= 1) ph -= 1;
          s += (2 * ph - 1) * 0.5f * 0.25f;
        }
      }
      s *= env * 0.8f;
      out_buf.push_back(s);
      out_buf.push_back(s);
    }
  }
}

static std::vector<Event> line(const std::vector<int> &notes, float each, float start = 0.0f) {
  std::vector<Event> s;
  float t = start;
  for (int n : notes) {
    s.push_back({t, n, true});
    s.push_back({t + each * 0.9f, n, false});
    t += each;
  }
  return s;
}

static void vocoder(Engine &e, const std::vector<Event> &score, float sec, float algo, float timbre, float freeze_at) {
  static warps::Modulator mod;
  mod.Init(SR);
  warps::ShortFrame in[BLOCK], out[BLOCK];
  float l[BLOCK], r[BLOCK];
  size_t next = 0;
  for (size_t b = 0; b < size_t(sec * SR / BLOCK); b++) {
    float now = b * BLOCK / SR;
    while (next < score.size() && score[next].at <= now) {
      if (score[next].on) {
        e.NoteOn(score[next].note);
        printf("  %5.2fs %-7s\n", now, e.ChordName());
      } else {
        e.NoteOff(score[next].note);
      }
      next++;
    }
    e.Process(nullptr, l, r, BLOCK);
    for (int i = 0; i < BLOCK; i++) {
      float x = in_pos < in_buf.size() ? in_buf[in_pos++] : 0.0f;
      in[i].l = int16_t(fmaxf(-1.0f, fminf(1.0f, (l[i] + r[i]) * 0.7f)) * 32767.0f);  // the carrier
      in[i].r = int16_t(fmaxf(-1.0f, fminf(1.0f, x)) * 32767.0f);                     // the voice
    }
    warps::Parameters *p = mod.mutable_parameters();
    p->channel_drive[0] = 0.8f;
    p->channel_drive[1] = 0.8f;
    // ALGO fully up freezes the vocoder's bands where they are
    p->modulation_algorithm = now >= freeze_at ? 1.0f : algo;
    p->modulation_parameter = timbre;
    p->note = 60.0f;
    p->carrier_shape = 0;
    mod.Process(in, out, BLOCK);
    for (int i = 0; i < BLOCK; i++) {
      out_buf.push_back(out[i].l / 32768.0f);
      out_buf.push_back(out[i].l / 32768.0f);  // OUT on both sides: AUX is another signal
    }
  }
}

// One of Parasites' modes, with the knobs as warps_mode.cpp hands them over. ALGO and TIMB move from
// their first value to their second across the section, as a hand on the encoder would.
struct Section {
  warps::FeatureMode mode;
  const char *name;
  float sec, lev1, lev2, algo0, algo1, timb0, timb1;
  int button;
  bool chord;
  const std::vector<float> *in2;
  float note;
  bool pair;  // OUT and AUX are a stereo pair here; otherwise OUT goes to both sides
};

static void section(Engine &e, warps::Modulator &mod, const Section &s, const std::vector<Event> &score) {
  printf("%6.1f s  %s\n", out_buf.size() / 2 / SR, s.name);
  mod.set_feature_mode(s.mode);
  warps::ShortFrame in[BLOCK], out[BLOCK];
  float l[BLOCK], r[BLOCK], dc_x[2] = {0, 0}, dc_y[2] = {0, 0};
  const float pole = 1.0f - 2.0f * 3.14159265f * 2.7f / SR;  // warps_mode.cpp's output capacitor
  size_t next = 0, blocks = size_t(s.sec * SR / BLOCK), pos = 0;
  for (size_t b = 0; b < blocks; b++) {
    float now = b * BLOCK / SR, t = float(b) / blocks;
    while (next < score.size() && score[next].at <= now) {
      if (score[next].on)
        e.NoteOn(score[next].note);
      else
        e.NoteOff(score[next].note);
      next++;
    }
    e.Process(nullptr, l, r, BLOCK);
    for (int i = 0; i < BLOCK; i++, pos++) {
      float x = s.chord ? (l[i] + r[i]) * 0.7f : 0.0f;
      float y = s.in2 ? (*s.in2)[pos % s.in2->size()] : 0.0f;
      in[i].l = int16_t(fmaxf(-1.0f, fminf(1.0f, x)) * 32767.0f);
      in[i].r = int16_t(fmaxf(-1.0f, fminf(1.0f, y)) * 32767.0f);
    }
    float algo = s.algo0 + (s.algo1 - s.algo0) * t, timb = s.timb0 + (s.timb1 - s.timb0) * t;
    warps::Parameters *p = mod.mutable_parameters();
    p->channel_drive[0] = s.lev1 * s.lev1;
    p->channel_drive[1] = s.lev2 * s.lev2;
    p->raw_level[0] = s.lev1;
    p->raw_level[1] = s.lev2;
    float curved = stmlib::Interpolate(warps::lut_pot_curve, algo, 512.0f);
    p->modulation_algorithm = curved;
    p->modulation_parameter = timb;
    p->raw_algorithm_pot = curved;
    p->raw_algorithm_cv = 0;
    p->raw_algorithm = algo;
    p->note = s.note;
    p->carrier_shape = s.button;
    mod.Process(in, out, BLOCK);
    for (int i = 0; i < BLOCK; i++) {
      float o[2] = {out[i].l / 32768.0f, out[i].r / 32768.0f};
      for (int c = 0; c < 2; c++) {
        float y = o[c] - dc_x[c] + pole * dc_y[c];
        dc_x[c] = o[c], dc_y[c] = y;
        o[c] = y;
      }
      // warps_mode.cpp clamps here too
      out_buf.push_back(fmaxf(-1.0f, fminf(1.0f, o[0])));
      out_buf.push_back(fmaxf(-1.0f, fminf(1.0f, s.pair ? o[1] : o[0])));
    }
  }
}

static std::vector<float> load(const char *path) {
  std::vector<float> v;
  FILE *f = fopen(path, "rb");
  if (!f) exit(2);
  fseek(f, 0, SEEK_END);
  v.resize(ftell(f) / 4);
  fseek(f, 0, SEEK_SET);
  if (fread(v.data(), 4, v.size(), f) != v.size()) exit(3);
  fclose(f);
  return v;
}

int main(int argc, char **argv) {
  if (argc < 3) return 1;
  std::string scene = argv[1];
  if (argc > 3) in_buf = load(argv[3]);
  if (argc > 4) in2_buf = load(argv[4]);
  static Engine e;
  const std::vector<int> keys = {48, 50, 52, 53, 48, 50, 52, 53};  // C D E F: I V vi IV
  chord::Params d;

  if (scene == "old_new") {
    const ChordProgression &pr = kPopProgressions[0];
    std::vector<std::vector<int>> chords;
    for (int i = 0; i < 8; i++) {
      const ChordVoicing &cv = pr.chords[(keys[i] == 48) ? 0 : keys[i] == 50 ? 1 : keys[i] == 52 ? 2 : 3];
      std::vector<int> c;
      for (int v = 0; v < 4; v++) c.push_back(48 + cv.intervals[v]);
      chords.push_back(c);
    }
    old_drone(chords, 2.0f);
    gap(1.0f);
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    play(e, line(keys, 2.0f), 17.0f, false);
  } else if (scene == "jump_lead") {
    for (int voicing : {1, 0}) {
      e.Init(SR);
      e.p = d;
      e.p.harmony = 7;
      e.p.voicing = voicing;
      e.p.shape = 55;
      e.p.spread = 18;
      e.p.motion = 10;
      e.p.swell = 15;
      e.p.strum = 0;
      e.p.glide = 28;
      printf("voicing %s\n", chord::VoicingName(voicing));
      play(e, line(keys, 1.8f), 15.0f, false);
      gap(0.8f);
    }
  } else if (scene == "drone") {
    int motion[2] = {0, 80}, spread[2] = {5, 50};
    for (int i = 0; i < 2; i++) {
      e.Init(SR);
      e.p = d;
      e.p.harmony = 1;
      e.p.stack = 5;
      e.p.key = 9;
      e.p.motion = motion[i];
      e.p.spread = spread[i];
      e.p.bright = 40;
      e.p.swell = 55;
      play(e, {{0.0f, 45, true}}, 14.0f, false);
      gap(0.8f);
    }
  } else if (scene == "strings") {
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    e.p.sound = 100;
    e.p.swell = 18;
    e.p.strum = 30;
    e.p.bright = 65;
    printf("plucked\n");
    play(e, line(keys, 1.8f), 15.0f, false);
    gap(0.8f);
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    e.p.sound = 100;
    e.p.swell = 70;
    e.p.strum = 10;
    e.p.bright = 55;
    e.p.glide = 40;
    printf("bowed\n");
    play(e, line({48, 52, 53, 50}, 4.0f), 18.0f, false);
  } else if (scene == "sings") {
    dry(4.0f);
    gap(0.8f);
    in_pos = 0;
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    e.p.sound = 100;
    e.p.swell = 38;
    e.p.strum = 0;
    e.p.bright = 60;
    e.p.spread = 55;
    play(e, line({48, 52, 53, 50}, 3.0f), 12.0f, true);
  } else if (scene == "vocoder") {
    dry(float(in_buf.size()) / SR + 0.4f);
    gap(0.6f);
    in_pos = 0;
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    e.p.shape = 100;
    e.p.bright = 85;
    e.p.spread = 25;
    e.p.motion = 15;
    e.p.swell = 8;
    e.p.strum = 0;
    float len = float(in_buf.size()) / SR;
    // The second half of the line, then the last vowel held while the chords carry on. The bands
    // freeze a little before the voice stops, on a vowel.
    float freeze = len - 0.55f;
    auto score = line({48, 52, 53, 50, 48, 52, 53, 50}, len / 8.0f);
    float t = len;
    for (int n : {48, 52, 53, 50}) {
      score.push_back({t, n, true});
      score.push_back({t + 1.4f, n, false});
      t += 1.5f;
    }
    vocoder(e, score, len + 6.2f, 0.78f, 0.5f, freeze);
  } else if (scene == "parasites") {  // in = drums, second in = a voice
    static warps::Modulator mod;
    mod.Init(SR);
    e.Init(SR);
    e.p = d;
    e.p.harmony = 7;
    e.p.bright = 70;
    e.p.spread = 30;
    e.p.motion = 20;
    e.p.swell = 8;
    e.p.strum = 0;
    const auto chords = line({48, 52, 53, 50}, 2.0f);
    using namespace warps;
    section(e, mod, {FEATURE_MODE_DOPPLER, "DOPPLER, chords circling the head", 8, 0.28f, 0.8f, 0.5f, 0.5f, 0.15f, 0.15f, 1, true, nullptr, 48, true}, chords);
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_FOLD, "FOLD, chords, folds opening", 8, 0.8f, 0, 0, 0.85f, 0.5f, 0.5f, 0, true, nullptr, 48, false}, chords);
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_CHEBYSCHEV, "CHEBY, the internal sine, order rising", 8, 0.85f, 0, 0, 1, 0.8f, 0.8f, 1, false, nullptr, 48, false}, {});
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_FREQUENCY_SHIFTER, "SHIFT, chords, up on the left, down on the right", 8, 0.25f, 1, 0.52f, 0.72f, 1, 1, 1, true, nullptr, 48, true}, chords);
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_BITCRUSHER, "CRUSH, chords and drums, bits falling, XMOD rising", 8, 0.55f, 0.45f, 0, 0.75f, 0, 0.8f, 0, true, &in_buf, 48, false}, chords);
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_COMPARATOR, "COMPARE, chords and drums, through the functions", 8, 0.8f, 0.7f, 0, 1, 0.15f, 0.15f, 0, true, &in_buf, 48, false}, chords);
    gap(0.5f);
    section(e, mod, {FEATURE_MODE_VOCODER, "VOCODER, a voice singing the chords, WARP moving", 9.5f, 0.9f, 0.9f, 0.5f, 0.82f, 0.3f, 0.3f, 0, true, &in2_buf, 48, false}, line({48, 52, 53, 50, 48}, 1.9f));
    gap(0.5f);
    e.p.hold = 0;
    std::vector<Event> stabs;
    int notes[] = {48, 52, 53, 50, 48, 52};
    for (int i = 0; i < 6; i++) {
      stabs.push_back({i * 2.0f, notes[i], true});
      stabs.push_back({i * 2.0f + 0.2f, notes[i], false});
    }
    section(e, mod, {FEATURE_MODE_DELAY, "DELAY, chord stabs, ping-pong, the tape slowing", 14, 0.65f, 0.55f, 1, 0.8f, 0.6f, 0.6f, 3, true, nullptr, 48, true}, stabs);
  } else {
    return 4;
  }
  FILE *f = fopen(argv[2], "wb");
  fwrite(out_buf.data(), 4, out_buf.size(), f);
  fclose(f);
  return 0;
}
