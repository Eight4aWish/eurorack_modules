// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// See chord_engine.h. Everything here runs on one 96 kHz core with room to spare, so it favours
// simple per-sample work and does the expensive sums (powers, tangents) once a block.

#include "chord_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "chord_library.h"

namespace chord {

namespace {

constexpr float kPi = 3.14159265f;

struct Scale {
  const char *name;
  int8_t steps[7];
};
const Scale kScales[] = {
    {"MAJOR", {0, 2, 4, 5, 7, 9, 11}},    {"MINOR", {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", {0, 2, 3, 5, 7, 9, 10}},   {"MIXOLYD", {0, 2, 4, 5, 7, 9, 10}},
    {"LYDIAN", {0, 2, 4, 6, 7, 9, 11}},   {"PHRYGIAN", {0, 1, 3, 5, 7, 8, 10}},
    {"HARM MIN", {0, 2, 3, 5, 7, 8, 11}},
};
constexpr int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

// How a scale builds a chord on a note: scale degrees above it
struct Stack {
  const char *name;
  int8_t degrees[kNotes];
};
const Stack kStacks[] = {
    {"7TH", {0, 2, 4, 6}},     {"9TH", {0, 2, 6, 8}},  {"SUS", {0, 3, 4, 7}},
    {"QUARTAL", {0, 3, 6, 9}}, {"OPEN", {0, 4, 7, 9}}, {"FIFTHS", {0, 4, 7, 11}},
};
constexpr int kStackCount = sizeof(kStacks) / sizeof(kStacks[0]);

const ChordProgression *Progression(int i) {
  for (uint8_t c = 0; c < kNumCategories; c++) {
    if (i < kChordCategories[c].count) return &kChordCategories[c].progressions[i];
    i -= kChordCategories[c].count;
  }
  return nullptr;
}

int ProgressionCount() {
  int n = 0;
  for (uint8_t c = 0; c < kNumCategories; c++) n += kChordCategories[c].count;
  return n;
}

// A progression's eight chords on the keys: the white keys are the first seven in any octave, each black
// key plays the one below it, and A# is the eighth.
const uint8_t kKeyToChord[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 7, 6};

// Names a chord from its notes, measured from the bass
struct Quality {
  uint16_t mask;
  const char *suffix;
};
constexpr uint16_t M(int a, int b, int c, int d = 0) { return 1 << 0 | 1 << a | 1 << b | 1 << c | 1 << d; }
const Quality kQualities[] = {
    {M(4, 7, 0), ""},       {M(3, 7, 0), "m"},       {M(4, 7, 10), "7"},     {M(4, 7, 11), "maj7"},
    {M(3, 7, 10), "m7"},    {M(3, 6, 10), "m7b5"},   {M(3, 6, 0), "dim"},    {M(3, 6, 9), "dim7"},
    {M(4, 8, 0), "+"},      {M(5, 7, 0), "sus4"},    {M(2, 7, 0), "sus2"},   {M(7, 0, 0), "5"},
    {M(5, 7, 10), "7sus"},  {M(2, 4, 7), "add9"},    {M(2, 3, 7), "madd9"},  {M(2, 4, 10), "9"},
    {M(2, 3, 10), "m9"},    {M(2, 4, 11), "maj9"},   {M(3, 7, 11), "mM7"},   {M(4, 7, 9), "6"},
    {M(3, 7, 9), "m6"},     {M(5, 10, 3), "m11"},    {M(2, 7, 9), "6/9"},
};

inline float Sine(float phase) {  // phase 0-1
  float x = 2.0f * phase - 1.0f;
  float s = 4.0f * x * (1.0f - fabsf(x));
  return 0.775f * s + 0.225f * s * fabsf(s);
}

inline float Blep(float t, float dt) {  // two-sample polyBLEP
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

inline float SoftClip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float Noise(uint32_t &s) {  // xorshift, -1 to 1
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return static_cast<int32_t>(s) * 4.6566e-10f;
}

inline float NoteHz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }

// Evenly across the stack, and each note a different way across the stereo field
constexpr float kStackPos[kStack] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
constexpr float kVoicePos[kNotes] = {0.0f, -0.7f, 0.7f, -0.25f};
// Rates that never line up, so the movement doesn't repeat for a very long time, in Hz
constexpr float kAmpRate[kNotes] = {0.031f, 0.047f, 0.071f, 0.113f};
constexpr float kToneRate[kNotes] = {0.019f, 0.037f, 0.059f, 0.083f};

// The nearest note of this pitch class to `from`, within lo to hi
int Nearest(int pc, int from, int lo, int hi) {
  int best = lo, best_d = 1000;
  for (int n = lo; n <= hi; n++) {
    if ((n % 12 + 12) % 12 != pc) continue;
    int d = abs(n - from);
    if (d < best_d) best = n, best_d = d;
  }
  return best;
}

}  // namespace

int HarmonyCount() { return kScaleCount + ProgressionCount(); }
const char *HarmonyName(int i) { return i < kScaleCount ? kScales[i].name : Progression(i - kScaleCount)->name; }
int StackCount() { return kStackCount; }
const char *StackName(int i) { return kStacks[i].name; }
int VoicingCount() { return 1 + VOICING_COUNT; }
const char *VoicingName(int i) { return i == 0 ? "LEAD" : kVoicingNames[i - 1]; }
const char *KeyName(int key) {
  static const char *const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return kNames[((key % 12) + 12) % 12];
}

Engine &shared() {
  static Engine engine;
  return engine;
}

void Engine::Init(float sample_rate) {
  sr_ = sample_rate;
  memset(lines_, 0, sizeof(lines_));
  for (int v = 0; v < kNotes; v++) {
    Voice &o = voice_[v];
    memset(&o, 0, sizeof(o));
    o.line = lines_[v];
    o.note = o.target = 48.0f;
    o.pending = -1;
    o.noise = 0x9E3779B9u * (v + 1);
    o.delay = sr_ / NoteHz(o.note);
    o.lfo_amp = 0.23f * v;
    o.lfo_tone = 0.61f * v;
    o.lfo_amp_inc = kAmpRate[v];
    o.lfo_tone_inc = kToneRate[v];
    for (int s = 0; s < kStack; s++) {
      rng_ = rng_ * 1664525u + 1013904223u;  // so a stack's oscillators don't start in phase
      o.osc[s].phase = (rng_ >> 8) * 5.9604645e-8f;
    }
  }
  n_held_ = 0;
  started_ = false;
  triggers_ = 0;
  snprintf(name_, sizeof(name_), "---");
}

bool Engine::GateOn() const {
  for (const Voice &v : voice_)
    if (v.gate > 0.5f) return true;
  return false;
}

// Which notes the chord wants, and which voice takes which
void Engine::BuildChord(uint8_t played) {
  int tones[kNotes], anchor;
  int key = p.key % 12, h = p.harmony;
  if (h >= HarmonyCount()) h = 0;
  if (h < kScaleCount) {
    const Scale &sc = kScales[h];
    int pc = (played - key + 120) % 12, deg = 0;
    for (int d = 0; d < 7; d++)
      if (sc.steps[d] <= pc) deg = d;  // a note outside the scale takes the degree below it
    anchor = played - (pc - sc.steps[deg]) + 12 * p.octave;
    const Stack &st = kStacks[p.stack < kStackCount ? p.stack : 0];
    for (int i = 0; i < kNotes; i++) {
      int d = deg + st.degrees[i];
      tones[i] = sc.steps[d % 7] + 12 * (d / 7) - sc.steps[deg];
    }
  } else {
    const ChordVoicing &cv = Progression(h - kScaleCount)->chords[kKeyToChord[played % 12]];
    anchor = key + 12 * (played / 12) + 12 * p.octave;
    for (int i = 0; i < kNotes; i++) tones[i] = cv.intervals[i];
  }

  // The name, from the notes measured from the bass
  uint16_t mask = 0;
  for (int i = 0; i < kNotes; i++) mask |= 1 << (((tones[i] - tones[0]) % 12 + 12) % 12);
  const char *suffix = "~";
  for (const Quality &q : kQualities)
    if (q.mask == mask) suffix = q.suffix;
  snprintf(name_, sizeof(name_), "%s%s", KeyName(anchor + tones[0]), suffix);

  int want[kNotes];
  if (p.voicing == 0) {
    // Voice leading: the bass stays low, and the three above it each take the chord note that means
    // the least movement overall. Chord notes close together cost a little, so it stays open.
    int prev[kNotes];
    for (int v = 0; v < kNotes; v++)
      prev[v] = started_ ? static_cast<int>(lroundf(voice_[v].target)) : anchor + (v == 0 ? -12 : tones[v] % 12);
    int bass_pc = ((anchor + tones[0]) % 12 + 12) % 12;
    want[0] = Nearest(bass_pc, prev[0], anchor - 14, anchor - 3);
    static const uint8_t kPerm[6][3] = {{1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1}};
    int best_cost = 100000;
    for (const auto &perm : kPerm) {
      int try_note[3], cost = 0;
      for (int i = 0; i < 3; i++) {
        int pc = ((anchor + tones[perm[i]]) % 12 + 12) % 12;
        try_note[i] = Nearest(pc, prev[i + 1], anchor - 2, anchor + 20);
        cost += 4 * abs(try_note[i] - prev[i + 1]);
      }
      for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++) {
          int gap = abs(try_note[a] - try_note[b]);
          if (gap == 0) cost += 12;
          else if (gap <= 2) cost += 3;
        }
      if (cost < best_cost) {
        best_cost = cost;
        for (int i = 0; i < 3; i++) want[i + 1] = try_note[i];
      }
    }
  } else {
    int8_t t[kNotes];
    for (int i = 0; i < kNotes; i++) t[i] = static_cast<int8_t>(tones[i]);
    applyVoicing(t, static_cast<VoicingType>(p.voicing - 1));
    for (int i = 0; i < kNotes; i++) want[i] = anchor + t[i];
  }

  // Each note enters in turn, the bass first
  float gap_ms = p.strum * p.strum * 0.06f;  // up to 600 ms
  for (int v = 0; v < kNotes; v++) {
    Voice &o = voice_[v];
    o.pending_note = static_cast<float>(want[v]);
    o.pending = static_cast<int32_t>(v * gap_ms * 0.001f * sr_);
    if (!started_) o.note = o.target = o.pending_note;
  }
  started_ = true;
}

// One note enters
void Engine::Start(Voice &o, float note) {
  o.target = note;
  if (o.env < 0.02f) o.note = note;  // nothing to glide from
  o.gate = 1.0f;
  o.bloom = 1.0f;
  float swell = p.swell * 0.01f;
  o.burst_len = o.burst = static_cast<int32_t>(sr_ / NoteHz(note));
  o.burst_amp = 0.8f * (1.0f - swell) * (1.0f - swell);
  triggers_ |= 1 << (&o - voice_);
}

void Engine::NoteOn(uint8_t note) {
  for (int i = 0; i < n_held_; i++)
    if (held_[i] == note) {  // the same note again: take it out and put it on top
      memmove(&held_[i], &held_[i + 1], n_held_ - i - 1);
      n_held_--;
      break;
    }
  if (n_held_ == kHeld) {
    memmove(&held_[0], &held_[1], kHeld - 1);
    n_held_--;
  }
  held_[n_held_++] = note;
  BuildChord(note);
}

void Engine::NoteOff(uint8_t note) {
  bool top = n_held_ && held_[n_held_ - 1] == note;
  for (int i = 0; i < n_held_; i++)
    if (held_[i] == note) {
      memmove(&held_[i], &held_[i + 1], n_held_ - i - 1);
      n_held_--;
      break;
    }
  if (n_held_) {
    if (top) BuildChord(held_[n_held_ - 1]);  // back to the finger still down
  } else {
    HoldChanged();
  }
}

void Engine::HoldChanged() {
  if (p.hold || n_held_) return;
  for (Voice &o : voice_) {
    o.gate = 0.0f;
    o.pending = -1;
  }
}

// Once a block: everything that changes slowly
void Engine::Control(int n) {
  const float dt = n / sr_;
  const float swell = p.swell * 0.01f, motion = p.motion * 0.01f, spread = p.spread * 0.01f;
  const float bright = p.bright * 0.01f, shape = p.shape * 0.01f, glide = p.glide * 0.01f;

  const float attack = 0.003f * powf(2000.0f, swell);    // 3 ms to 6 s
  const float release = 0.08f * powf(150.0f, swell);     // 80 ms to 12 s
  const float k_att = 1.0f - expf(-dt / attack), k_rel = 1.0f - expf(-dt / release);
  const float glide_s = 3.0f * glide * glide;            // up to 3 s
  const float k_glide = glide_s < dt ? 1.0f : 1.0f - expf(-dt / glide_s);
  const float k_bloom = expf(-dt / (0.12f + 3.0f * swell));
  const float t60 = 0.6f + 14.0f * swell;
  const float damp = 0.03f + 0.6f * (1.0f - bright) * (1.0f - bright);
  const float cents = 1.0f + 32.0f * spread * spread;

  w_sin_ = shape < 0.5f ? 1.0f - 2.0f * shape : 0.0f;
  w_tri_ = shape < 0.5f ? 2.0f * shape : 2.0f - 2.0f * shape;
  w_saw_ = shape < 0.5f ? 0.0f : 2.0f * shape - 1.0f;

  float out = 0.0f;
  for (int i = 0; i < kNotes; i++) {
    Voice &o = voice_[i];
    if (o.pending >= 0) {
      o.pending -= n;
      if (o.pending < 0) Start(o, o.pending_note);
    }
    o.note += k_glide * (o.target - o.note);
    o.bloom *= k_bloom;
    o.env += (o.gate > 0.5f ? k_att : k_rel) * (o.gate - o.env);

    o.lfo_amp += o.lfo_amp_inc * dt;
    if (o.lfo_amp >= 1.0f) o.lfo_amp -= 1.0f;
    o.lfo_tone += o.lfo_tone_inc * dt;
    if (o.lfo_tone >= 1.0f) o.lfo_tone -= 1.0f;
    float amp_lfo = Sine(o.lfo_amp), tone_lfo = Sine(o.lfo_tone);
    out += amp_lfo * 0.25f;

    // The amount this note is heard, ramped across the block so an envelope never clicks
    float breath = 1.0f - motion * 0.75f * (0.5f + 0.5f * amp_lfo);
    float amp_end = o.env * breath;
    o.amp_step = (amp_end - o.amp_now) / n;

    float pitch = o.note + bend_;
    float hz = NoteHz(pitch);

    // The stack: detuned about the note, a slow wander on each oscillator, spread across the field
    for (int s = 0; s < kStack; s++) {
      Osc &c = o.osc[s];
      c.drift += 0.002f * Noise(o.noise) - 0.0006f * c.drift;
      if (c.drift > 1.0f) c.drift = 1.0f;
      if (c.drift < -1.0f) c.drift = -1.0f;
      float offset = kStackPos[s] * cents + c.drift * motion * 12.0f;
      c.inc = hz * (1.0f + offset * 5.7762e-4f) / sr_;
      if (c.inc > 0.45f) c.inc = 0.45f;
      float wide = spread * 2.5f > 1.0f ? 1.0f : spread * 2.5f;
      float pos = kStackPos[s] * wide + kVoicePos[i] * spread * 0.5f;
      if (pos > 1.0f) pos = 1.0f;
      if (pos < -1.0f) pos = -1.0f;
      c.gain_l = sqrtf(0.5f * (1.0f - pos));
      c.gain_r = sqrtf(0.5f * (1.0f + pos));
    }

    // The low-pass follows the note, opens on each entry, and moves slowly with the motion
    float octaves = 0.6f + bright * 7.5f + 2.5f * o.bloom + motion * 1.5f * tone_lfo;
    float fc = hz * powf(2.0f, octaves);
    if (fc > sr_ * 0.45f) fc = sr_ * 0.45f;
    o.g = tanf(kPi * fc / sr_);
    const float k = 1.1f;
    o.a1 = 1.0f / (1.0f + o.g * (o.g + k));
    o.a2 = o.g * o.a1;
    o.a3 = o.g * o.a2;

    // The string: tuned by its delay, less what its own damping adds
    float comp = damp / (1.0f - damp);
    float want_delay = sr_ / hz - comp;
    if (want_delay > kLine - 4.0f) want_delay = kLine - 4.0f;
    if (want_delay < 4.0f) want_delay = 4.0f;
    o.delay_step = (want_delay - o.delay) / n;
    o.fb = powf(10.0f, -3.0f * (want_delay / sr_) / t60);
    o.damp = damp;
    float pan = kVoicePos[i] * (0.3f + 0.7f * spread);
    o.gain_str_l = sqrtf(0.5f * (1.0f - pan));
    o.gain_str_r = sqrtf(0.5f * (1.0f + pan));
  }
  motion_out_ = out * motion;
}

void Engine::RenderOscillators(Voice &o, float *l, float *r, int n, float mix) {
  const float ws = w_sin_, wt = w_tri_, wa = w_saw_;
  float amp = o.amp_now;
  float s1l = o.s1_l, s2l = o.s2_l, s1r = o.s1_r, s2r = o.s2_r;
  const float a1 = o.a1, a2 = o.a2, a3 = o.a3;
  for (int i = 0; i < n; i++) {
    float sum_l = 0.0f, sum_r = 0.0f;
    for (int s = 0; s < kStack; s++) {
      Osc &c = o.osc[s];
      c.phase += c.inc;
      if (c.phase >= 1.0f) c.phase -= 1.0f;
      float out = 0.0f;
      if (ws > 0.0f) out += ws * Sine(c.phase);
      if (wt > 0.0f) out += wt * (4.0f * fabsf(c.phase - 0.5f) - 1.0f);
      if (wa > 0.0f) out += wa * (2.0f * c.phase - 1.0f - Blep(c.phase, c.inc));
      sum_l += out * c.gain_l;
      sum_r += out * c.gain_r;
    }
    // Andrew Simper's state-variable filter, the low-pass output of each side
    float v3 = sum_l - s2l;
    float v1 = a1 * s1l + a2 * v3;
    float v2 = s2l + a2 * s1l + a3 * v3;
    s1l = 2.0f * v1 - s1l;
    s2l = 2.0f * v2 - s2l;
    float out_l = v2;
    v3 = sum_r - s2r;
    v1 = a1 * s1r + a2 * v3;
    v2 = s2r + a2 * s1r + a3 * v3;
    s1r = 2.0f * v1 - s1r;
    s2r = 2.0f * v2 - s2r;
    float out_r = v2;
    amp += o.amp_step;
    l[i] += out_l * amp * mix;
    r[i] += out_r * amp * mix;
  }
  o.s1_l = s1l, o.s2_l = s2l, o.s1_r = s1r, o.s2_r = s2r;
}

void Engine::RenderString(Voice &o, const float *in, float *l, float *r, int n, float mix) {
  const float fb = o.fb, damp = o.damp;
  // Held gently by air while the chord is on, so a string can sustain on its own
  const float air = 0.1f * o.env;
  float *line = o.line;
  uint32_t write = o.write;
  float delay = o.delay, state = o.damp_state;
  for (int i = 0; i < n; i++) {
    delay += o.delay_step;
    float read = static_cast<float>(write) - delay;
    if (read < 0.0f) read += kLine;
    int32_t a = static_cast<int32_t>(read);
    float frac = read - a;
    int32_t b = a + 1 == kLine ? 0 : a + 1;
    float y = line[a] + frac * (line[b] - line[a]);
    state = y * (1.0f - damp) + state * damp;

    float x = air * Noise(o.noise);
    if (in) x += in[i] * 0.3f;
    if (o.burst > 0) {
      x += o.burst_amp * Noise(o.noise);
      o.burst--;
    }
    float w = x + fb * state;
    if (w > 4.0f) w = 4.0f;
    if (w < -4.0f) w = -4.0f;
    line[write] = w;
    if (++write == kLine) write = 0;

    float out = state * mix;
    l[i] += out * o.gain_str_l;
    r[i] += out * o.gain_str_r;
  }
  o.write = write;
  o.delay = delay;
  o.damp_state = state;
}

void Engine::Process(const float *in, float *l, float *r, int n) {
  Control(n);
  for (int i = 0; i < n; i++) l[i] = r[i] = 0.0f;
  const float sound = p.sound * 0.01f;
  for (Voice &o : voice_) {
    if (sound < 1.0f) RenderOscillators(o, l, r, n, (1.0f - sound) * 0.2f);
    if (sound > 0.0f) RenderString(o, in, l, r, n, sound * 1.3f);
    o.amp_now += o.amp_step * n;
    if (sound >= 1.0f) o.amp_now = o.env;
    if (sound <= 0.0f) o.burst = 0;
  }
  const float level = p.level * 0.01f * 1.4f;
  for (int i = 0; i < n; i++) {
    l[i] = SoftClip(l[i] * level);
    r[i] = SoftClip(r[i] * level);
  }
}

}  // namespace chord
