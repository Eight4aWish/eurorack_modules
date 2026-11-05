// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mode 3, Warps: Mutable Instruments' meta-modulator, with the eight further modes of Matthias Puech's
// Parasites firmware. Its DSP is built unchanged from third_party/parasites (after Emilie Gillet, MIT)
// by scripts/warps_build.py; this file stands in for Warps' own hardware layer (warps.cc, cv_scaler.cc,
// ui.cc): the codec, the CVs, the knobs and the button.
// - audio in L is Warps' IN 1, the carrier; audio in R is IN 2, the modulator
// - audio out L is Warps' OUT; audio out R is AUX
// - CV in: PITCH1 Level 1 (V/oct too, with the internal oscillator on), PITCH2 Level 2, GATE1
//   Algorithm, GATE2 Timbre. Every jack switch stays off, so all six are inputs; GATE3 and PITCH3
//   are unused, and so are the gate outs.
// - MIDI plays the chord engine, with CHORD on: its chords replace IN 1, in Chordal's last settings.
//   In the vocoder, with CARR on EXT, that's a voice on IN 2 singing the chords.
// - the encoder stands in for the four knobs and the button. As in the chooser, a turn moves the
//   marker; a press takes the marked item to edit (its row lit), a turn sets it, and a press lets
//   it go. Held for 2 s it goes back to the mode chooser.
// - 96 kHz, as Warps: AUDIO_SAMPLE_RATE_EXACT in platformio.ini
//
// Levels, from Warps' hardware (warps_v30.pdf, sheets 1 and 2) and Boy's:
// - in: Warps' codec reaches full scale at 16 Vpp (warps.cc), Boy's at 22 Vpp, so the inputs gain
//   22/16. They also invert (spec requirement 6).
// - out: Warps' full scale is about 19.5 Vpp (the WM8731's 1 Vrms, x6.9) and Boy's measured 19.4,
//   so 1:1. Warps' outs are AC-coupled (10 uF into 5.9 kOhm: 2.7 Hz) and Boy's aren't, so a one-pole
//   high-pass at that corner does the capacitor's job.
// - CV: Warps reads Level 1 and 2 as 0.109 of its ADC's range per volt (36k/100k into 3.3 V), and
//   Algorithm and Timbre as 0.100 (33k/100k).
//
// Patch detection. Warps finds an unpatched CV jack by sending noise through its switched contact;
// Boy has none. An open input instead sits still at the ADS8688's bias, +0.207 V: a jack that reads
// within 20 mV of it, steady to 4 mV, for 100 ms counts as open until it leaves that window. A
// patched source held still in the window reads as open too (spec requirement 8).

#include "warps_mode.h"

#include <Audio.h>

#include "board.h"
#include "chord_engine.h"
#include "chordal_mode.h"
#include "midi_notes.h"
#include "stmlib/dsp/dsp.h"
#include "warps/dsp/modulator.h"
#include "warps/resources.h"

namespace warps_mode {

using warps::ShortFrame;

constexpr uint8_t MODE = 2;  // this mode's place in main.cpp's MODES: its EEPROM page

// ---- jacks
enum Knob : uint8_t { LEVEL1, LEVEL2, ALGO, TIMBRE, KNOBS };
constexpr uint8_t CV_JACK[KNOBS] = {board::PITCH1, board::PITCH2, board::GATE1, board::GATE2};
constexpr uint8_t SPARE_JACK[2] = {board::GATE3, board::PITCH3};  // unused here: read for the bench report
constexpr float CV_SCALE[KNOBS] = {36 / 100.0f / 3.3f, 36 / 100.0f / 3.3f, 33 / 100.0f / 3.3f, 33 / 100.0f / 3.3f};
constexpr float OPEN_WINDOW = 0.020f, OPEN_STEADY = 0.004f;  // V
constexpr uint32_t OPEN_MS = 100;

// ---- levels
constexpr int32_t IN_GAIN_NUM = 22, IN_GAIN_DEN = 16;  // Boy's input full scale over Warps', Vpp
constexpr float DC_POLE = 1 - 2 * PI * 2.7f / AUDIO_SAMPLE_RATE_EXACT;

// Warps runs its CV scaler once a 60-sample block, 1.6 kHz; Boy once a 96-sample block, 1 kHz. A
// one-pole coefficient c there is 1 - (1 - c)^1.6 here, for the same time constant.
static float per_block(float c) { return 1 - powf(1 - c, AUDIO_BLOCK_SAMPLES / 60.0f); }
// Parasites reacts faster at the Levels than stock Warps does: 0.5 where stock has 0.08
static const float K_CV[KNOBS] = {per_block(0.5f), per_block(0.5f), per_block(0.08f), per_block(0.08f)};
static const float K_POT[KNOBS] = {per_block(0.33f * 0.5f), per_block(0.33f * 0.5f), per_block(0.33f * 0.08f), per_block(0.33f * 0.08f)};
static const float K_NOTE = per_block(0.1f);

// ---- settings, kept in the EEPROM so the mode comes back as it was left
constexpr uint8_t SETTINGS_MAGIC = 0x58;
struct Settings {
  uint8_t magic;
  uint8_t knob[KNOBS];  // 0-100, the knobs' positions
  uint8_t carrier;      // Warps' button: 0 external, 1 sine, 2 triangle, 3 saw; Doppler and Delay have their own
  uint8_t feature;      // warps::FeatureMode: stock Warps is FEATURE_MODE_META
  uint8_t chord;        // 1: the chord engine is IN 1
};
static Settings set;
static uint32_t changedMs;  // a change waits 3 s to be written, so a turn isn't a dozen writes

// ---- shared with the audio interrupt
static volatile float cv_volts[KNOBS];
static volatile bool cv_open[KNOBS] = {true, true, true, true};
static float spare_volts[2];

static chord::Engine &chords = chord::shared();
static warps::Modulator modulator;
static float lp_pot[KNOBS], lp_cv[KNOBS], note_cv, note_pot;

// What Warps' CvScaler::Read does with its pots and CVs, here with the encoder's knobs
static void read_parameters(warps::Parameters *p) {
  for (uint8_t k = 0; k < KNOBS; k++) {
    lp_pot[k] += K_POT[k] * (set.knob[k] * 0.01f - lp_pot[k]);
    float cv = cv_open[k] ? 0.0f : cv_volts[k] * CV_SCALE[k];  // Warps' unpatched jacks are grounded
    lp_cv[k] += K_CV[k] * (cv - lp_cv[k]);
  }
  for (uint8_t i = 0; i < 2; i++) {  // Level: the CV is a VCA, the knob its range
    float pot = lp_pot[LEVEL1 + i], cv = lp_cv[LEVEL1 + i] * 1.6f;
    float v = pot * pot * cv, raw = pot * cv;  // raw: the modes that use Level as feedback, wet or the like
    if (cv_open[LEVEL1 + i]) v = pot * pot, raw = pot;
    CONSTRAIN(v, 0.0f, 1.0f);
    CONSTRAIN(raw, 0.0f, 1.0f);
    p->channel_drive[i] = v;
    p->raw_level[i] = raw;
  }
  float algorithm = stmlib::Interpolate(warps::lut_pot_curve, lp_pot[ALGO], 512.0f) + lp_cv[ALGO] * 2.0f;
  CONSTRAIN(algorithm, 0.0f, 1.0f);
  if (algorithm <= 0.125f) {  // Warps' fix for wavefolder bleed from a slight offset
    algorithm = algorithm * 1.08f - 0.01f;
    CONSTRAIN(algorithm, 0.0f, 1.0f);
  }
  p->modulation_algorithm = algorithm;
  float parameter = lp_pot[TIMBRE] + lp_cv[TIMBRE] * 2.0f;
  CONSTRAIN(parameter, 0.0f, 1.0f);
  p->modulation_parameter = parameter;

  // the raw mappings, for the modes that don't go through the algorithm's curve
  p->raw_algorithm_pot = stmlib::Interpolate(warps::lut_pot_curve, lp_pot[ALGO], 512.0f);
  p->raw_algorithm_cv = lp_cv[ALGO] * 2.0f;
  CONSTRAIN(p->raw_algorithm_cv, -1.0f, 1.0f);
  float raw_algorithm = lp_pot[ALGO] + lp_cv[ALGO] * 2.0f;
  CONSTRAIN(raw_algorithm, 0.0f, 1.0f);
  p->raw_algorithm = raw_algorithm;

  // the internal oscillator: Level 1's knob sets its pitch, and its jack is V/oct
  float note = 12.0f * cv_volts[LEVEL1];
  float interval = note - note_cv;
  if (interval < -0.4f || interval > 0.4f)
    note_cv = note;
  else
    note_cv += K_NOTE * interval;
  note_pot += K_NOTE * (60.0f * set.knob[LEVEL1] * 0.01f + 12.0f - note_pot);
  p->note = cv_open[LEVEL1] ? note_pot + 24.0f : note_pot + note_cv;

  p->carrier_shape = set.carrier;
}

static inline int16_t take(const audio_block_t *b, uint32_t i) {  // an input sample, at Warps' level
  if (!b) return 0;
  int32_t s = -b->data[i] * IN_GAIN_NUM / IN_GAIN_DEN;
  return s < -32768 ? -32768 : s > 32767 ? 32767 : s;
}

struct DcBlock {  // Warps' output capacitor
  float x1, y1;
  int16_t process(int16_t in) {
    float x = in, y = x - x1 + DC_POLE * y1;
    x1 = x, y1 = y;
    return y < -32768 ? -32768 : y > 32767 ? 32767 : (int16_t)lroundf(y);
  }
};

class WarpsStream : public AudioStream {
 public:
  WarpsStream() : AudioStream(2, queue_) {}
  void update() override {
    audio_block_t *in_l = receiveReadOnly(0), *in_r = receiveReadOnly(1);  // I2S left, right
    audio_block_t *out_l = allocate(), *out_r = allocate();
    if (out_l && out_r) {
      static ShortFrame in[AUDIO_BLOCK_SAMPLES], out[AUDIO_BLOCK_SAMPLES];
      for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        in[i].l = take(in_r, i);  // the carrier: the panel's in L, I2S right (the panel swaps them)
        in[i].r = take(in_l, i);  // the modulator: the panel's in R, I2S left
      }
      if (set.chord) {
        static float l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
        chords.Process(nullptr, l, r, AUDIO_BLOCK_SAMPLES);
        for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          float x = (l[i] + r[i]) * 0.7f;
          in[i].l = (int16_t)lroundf((x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x) * 32767.0f);
        }
      }
      read_parameters(modulator.mutable_parameters());
      modulator.set_feature_mode(static_cast<warps::FeatureMode>(set.feature));
      modulator.Process(in, out, AUDIO_BLOCK_SAMPLES);
      for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        out_l->data[i] = dc_[0].process(out[i].l);  // OUT: the panel's out L, I2S left
        out_r->data[i] = dc_[1].process(out[i].r);  // AUX: out R, I2S right
      }
      transmit(out_l, 0);
      transmit(out_r, 1);
    }
    for (audio_block_t *b : {in_l, in_r, out_l, out_r})
      if (b) release(b);
  }

 private:
  audio_block_t *queue_[2];
  DcBlock dc_[2];
};

// ---- patch detection, run from the loop
struct Detect {
  uint32_t since;  // when the reading settled in the window; 0 while it hasn't
  float ref;
};
static Detect detect[KNOBS];
static bool cv_ok;

static void read_cvs() {
  if (!cv_ok) return;  // every jack stays open
  float v[KNOBS];
  board::cv_read(CV_JACK, KNOBS, v);
  board::cv_read(SPARE_JACK, 2, spare_volts);
  uint32_t now = millis();
  for (uint8_t k = 0; k < KNOBS; k++) {
    Detect &d = detect[k];
    if (fabsf(v[k] - board::CV_OPEN) > OPEN_WINDOW) {
      d.since = 0;
      cv_open[k] = false;
    } else if (!d.since || fabsf(v[k] - d.ref) > OPEN_STEADY) {
      d.since = now ? now : 1;
      d.ref = v[k];
    } else if (now - d.since >= OPEN_MS) {
      cv_open[k] = true;
    }
    cv_volts[k] = v[k];
  }
}

// ---- the encoder and the screen
enum Item : uint8_t { FEATURE, KNOB_ITEM, CARRIER = KNOB_ITEM + KNOBS, CHORD, ITEMS };

// What the panel means in each of the modes, from Parasites' manual
struct Face {
  const char *name;
  const char *knob[KNOBS];
  const char *button;
  const char *const *button_value;
};
static const char *const CARRIERS[4] = {"EXT", "SINE", "TRI", "SAW"};
static const char *const SPACES[4] = {"TINY", "SMALL", "LARGE", "HUGE"};
static const char *const TOPOLOGIES[4] = {"OPEN", "DUAL", "TAPE", "PING"};
static const Face FACE[] = {  // in warps::FeatureMode's order
    {"DOPPLER", {"RATE", "DEPTH", "X", "Y"}, "SPACE", SPACES},
    {"FOLD", {"LEV1", "LEV2", "FOLDS", "BIAS"}, "CARR", CARRIERS},
    {"CHEBY", {"LEV1", "LEV2", "ORDER", "GAIN"}, "CARR", CARRIERS},
    {"SHIFT", {"FDBK", "WET", "FREQ", "MIX"}, "CARR", CARRIERS},
    {"CRUSH", {"LEV1", "LEV2", "BITS", "XMOD"}, "CARR", CARRIERS},
    {"COMPARE", {"LEV1", "LEV2", "FUNC", "ORDER"}, "CARR", CARRIERS},
    {"VOCODER", {"LEV1", "LEV2", "WARP", "RELS"}, "CARR", CARRIERS},
    {"DELAY", {"FDBK", "WET", "SPEED", "LEN"}, "TYPE", TOPOLOGIES},
    {"META", {"LEV1", "LEV2", "ALGO", "TIMB"}, "CARR", CARRIERS},
};
static uint8_t sel;
static bool editing;  // a turn sets the marked item; otherwise it moves the marker
static uint32_t lastTurnMs, lastPaintMs, lastReportMs;

static void ui() {
  int32_t turns = board::encoder_turns();
  if (turns && !editing) {
    sel = constrain((int32_t)sel + turns, 0, ITEMS - 1);
  } else if (turns) {
    uint32_t now = millis();
    int32_t step = now - lastTurnMs < 40 ? 4 : 1;  // turned fast: bigger steps
    lastTurnMs = now;
    if (sel == FEATURE)
      set.feature = constrain((int32_t)set.feature + turns, 0, warps::FEATURE_MODE_META);
    else if (sel < CARRIER)
      set.knob[sel - KNOB_ITEM] = constrain((int32_t)set.knob[sel - KNOB_ITEM] + turns * step, 0, 100);
    else if (sel == CARRIER)
      set.carrier = constrain((int32_t)set.carrier + turns, 0, 3);
    else
      set.chord = turns > 0;
    changedMs = now ? now : 1;
  }
  switch (board::poll_push()) {
    case board::Push::SHORT:
    case board::Push::LONG:  // any press short of the 2 s hold, as in the chooser
      editing = !editing;
      break;
    case board::Push::MENU:
      board::back_to_menu();
    default:
      break;
  }
  if (changedMs && millis() - changedMs >= 3000) {
    board::mode_store(MODE, &set, sizeof(set));
    changedMs = 0;
  }
}

static const char *algorithm_name(float a) {  // the nearest of Warps' positions
  static const char *const NAMES[] = {"XFADE", "FOLD", "DIODE RM", "DIGI RM", "XOR", "COMPARE", "VOCODER"};
  uint8_t i = lroundf(a * 8);
  return NAMES[min(i, (uint8_t)6)];
}

static void paint() {
  static const char *const JACK[KNOBS] = {"P1", "P2", "G1", "G2"};
  const Face &face = FACE[set.feature];
  uint8_t top = sel < 4 ? 0 : sel - 3;
  for (uint8_t r = 0; r < 4; r++) {
    uint8_t it = top + r;
    char value[16], jack[4] = "", line[32];
    const char *name;
    if (it == FEATURE) {
      name = "MODE";
      snprintf(value, sizeof(value), "%s", face.name);
    } else if (it < CARRIER) {
      uint8_t k = it - KNOB_ITEM;
      name = face.knob[k];
      if (k == ALGO && set.feature == warps::FEATURE_MODE_META)
        snprintf(value, sizeof(value), "%s", algorithm_name(modulator.parameters().modulation_algorithm));
      else
        snprintf(value, sizeof(value), "%u%%", set.knob[k]);
      snprintf(jack, sizeof(jack), "%s%c", JACK[k], cv_open[k] ? ' ' : '*');  // *: a cable found
    } else if (it == CARRIER) {
      name = face.button;
      snprintf(value, sizeof(value), "%s", face.button_value[set.carrier]);
    } else {
      name = "CHORD";
      snprintf(value, sizeof(value), "%s", set.chord ? chords.ChordName() : "off");
    }
    snprintf(line, sizeof(line), "%c%-5s %-10s%s", it == sel ? '>' : ' ', name, value, jack);
    screen::row(r, line, it == sel && editing);
  }
}

static void chord_on(uint8_t note) {
  AudioNoInterrupts();
  chords.NoteOn(note);
  AudioInterrupts();
}

static void chord_off(uint8_t note) {
  AudioNoInterrupts();
  chords.NoteOff(note);
  AudioInterrupts();
}

static void chord_bend(float semitones) { chords.Bend(semitones); }

static void chord_stop() {
  AudioNoInterrupts();
  chords.Stop();
  AudioInterrupts();
}

void begin() {
  // board::begin() left every jack switch off and the DAC at 0 V behind them, so the six CV jacks
  // are inputs and the DAC holds steady (spec requirement 11)
  cv_ok = board::cv_in_begin();
  if (!cv_ok) Serial.println("Warps: the ADS8688 doesn't echo its ranges; CV ins are off.");
  if (!board::mode_load(MODE, &set, sizeof(set)) || set.magic != SETTINGS_MAGIC || set.carrier > 3 ||
      set.feature > warps::FEATURE_MODE_META || set.chord > 1)
    set = {SETTINGS_MAGIC, {80, 80, 0, 50}, 0, warps::FEATURE_MODE_META, 0};
  for (uint8_t k = 0; k < KNOBS; k++) lp_pot[k] = set.knob[k] * 0.01f;  // no sweep from 0 at start
  note_pot = 60.0f * lp_pot[LEVEL1] + 12.0f;

  chords.Init(AUDIO_SAMPLE_RATE_EXACT);
  chords.use_chorus = false;
  chord::ApplyPatch(&chords.p, 0);
  chordal_mode::load_params(&chords.p);
  midi_notes::begin(chord_on, chord_off, chord_bend, chord_stop);

  modulator.Init(AUDIO_SAMPLE_RATE_EXACT);
  AudioMemory(16);  // 8 was reached on the bench, with Warps and the chords running
  // made in signal order, so the library updates them in that order within a block
  AudioInputI2S *in = new AudioInputI2S();
  WarpsStream *warps = new WarpsStream();
  AudioOutputI2S *out = new AudioOutputI2S();
  new AudioConnection(*in, 0, *warps, 0);
  new AudioConnection(*in, 1, *warps, 1);
  new AudioConnection(*warps, 0, *out, 0);
  new AudioConnection(*warps, 1, *out, 1);
}

void loop() {
  midi_notes::poll();
  read_cvs();
  ui();

  uint32_t now = millis();
  if (now - lastPaintMs >= 100) {
    paint();
    lastPaintMs = now;
  }
  screen::service();

  if (now - lastReportMs >= 2000) {  // for the bench: pio device monitor
    Serial.printf("Warps %s%s: cpu %.1f%% (max %.1f%%), blocks %u (max %u); P1 %+.3f P2 %+.3f G1 %+.3f G2 %+.3f G3 %+.3f P3 %+.3f V, open %d%d%d%d\n",
                  FACE[set.feature].name, set.chord ? " + chords" : "", AudioProcessorUsage(), AudioProcessorUsageMax(),
                  AudioMemoryUsage(), AudioMemoryUsageMax(),
                  cv_volts[0], cv_volts[1], cv_volts[2], cv_volts[3], spare_volts[0], spare_volts[1], cv_open[0], cv_open[1],
                  cv_open[2], cv_open[3]);
    lastReportMs = now;
  }
}

}  // namespace warps_mode
