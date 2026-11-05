// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mode 2, Chordal: the chord engine (chord_engine.h) on Boy's jacks. It brings back and goes well past
// the old Boy's chord mode and drone (last in 842fc36^ and d691282^).
// - any note, on any MIDI channel but 10, plays a chord: in a scale, the chord built on that note; in
//   a progression, the eight chords on the keys. With HOLD on, the chord stays until the next one, or
//   until MIDI stop, which ends it whatever HOLD says.
// - audio out L and R are the chord in stereo. Audio in L and R, summed, ring the strings: patch
//   drums in, turn SOUND up, and they sound in the chord.
// - CV out: PITCH1-3 and GATE1 are the four notes, V/oct, gliding as they lead (note 36 at 0 V, as
//   mode 1). GATE2 is 5 V while a chord is on. GATE3 is the motion, slowly, +-5 V.
// - gate outs: a 5 ms trigger as each note enters, in order, on CLK, DRUM2, DRUM1 and RST.
// - the encoder, as everywhere: a turn moves the marker, a press takes the marked item to edit, a
//   turn sets it, a press lets it go. Held for 2 s it goes back to the mode chooser.
// - PATCH, at the top, picks from a bank of sixteen and changes the sound as it turns. The items below
//   it then start from there; a * shows the sound has moved away from the patch.

#include "chordal_mode.h"

#include <Audio.h>

#include "board.h"
#include "midi_notes.h"

namespace chordal_mode {

constexpr uint8_t MODE = 1;  // this mode's place in main.cpp's MODES: its EEPROM page
constexpr uint8_t SETTINGS_MAGIC = 0x44;

struct Settings {
  uint8_t magic;
  uint8_t patch;
  chord::Params p;
};
static_assert(sizeof(Settings) <= 32, "a mode's EEPROM page holds 32 bytes");

constexpr uint8_t PITCH_JACK[chord::kNotes] = {board::PITCH1, board::PITCH2, board::PITCH3, board::GATE1};
constexpr uint8_t GATE_JACK = board::GATE2, MOTION_JACK = board::GATE3;
constexpr float GATE_VOLTS = 5.0f, MOTION_VOLTS = 5.0f;
constexpr uint32_t TRIG_MS = 5;

static chord::Engine &engine = chord::shared();
static uint8_t patch;  // its place in the bank

bool load_params(chord::Params *p) {
  Settings s;
  if (!board::mode_load(MODE, &s, sizeof(s)) || s.magic != SETTINGS_MAGIC || s.patch >= chord::kPatches) return false;
  *p = s.p;
  patch = s.patch;
  return true;
}

static void on(uint8_t note) {
  AudioNoInterrupts();
  engine.NoteOn(note);
  AudioInterrupts();
}

static void off(uint8_t note) {
  AudioNoInterrupts();
  engine.NoteOff(note);
  AudioInterrupts();
}

static void bend(float semitones) { engine.Bend(semitones); }

static void stop() {
  AudioNoInterrupts();
  engine.Stop();
  AudioInterrupts();
}

class ChordalStream : public AudioStream {
 public:
  ChordalStream() : AudioStream(2, queue_) {}
  void update() override {
    audio_block_t *in_l = receiveReadOnly(0), *in_r = receiveReadOnly(1);
    audio_block_t *out_l = allocate(), *out_r = allocate();
    if (out_l && out_r) {
      static float in[AUDIO_BLOCK_SAMPLES], l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
      for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {  // Boy's inputs invert (spec requirement 6)
        int32_t sum = (in_l ? in_l->data[i] : 0) + (in_r ? in_r->data[i] : 0);
        in[i] = -sum * (0.5f / 32768.0f);
      }
      engine.Process(in, l, r, AUDIO_BLOCK_SAMPLES);
      for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        out_l->data[i] = (int16_t)lroundf(l[i] * 32767.0f);
        out_r->data[i] = (int16_t)lroundf(r[i] * 32767.0f);
      }
      transmit(out_l, 0);
      transmit(out_r, 1);
    }
    for (audio_block_t *b : {in_l, in_r, out_l, out_r})
      if (b) release(b);
  }

 private:
  audio_block_t *queue_[2];
};

// ---- the encoder and the screen
enum Item : uint8_t { PATCH, KEY, HARMONY, STACK, VOICE, OCTAVE, SOUND, OSCS, SHAPE, BRIGHT, RESO, SPREAD, MOTION, SWELL, DECAY, STRUM, GLIDE, HOLD, LEVEL, ITEMS };
static const char *const NAME[ITEMS] = {"PATCH", "KEY", "HARM", "STACK", "VOICE", "OCT", "SOUND", "OSCS", "SHAPE", "BRIGHT", "RESO", "SPREAD", "MOTION", "SWELL", "DECAY", "STRUM", "GLIDE", "HOLD", "LEVEL"};

static uint8_t sel = PATCH;
static bool editing;
static uint32_t changedMs, lastTurnMs, lastPaintMs, lastReportMs;

static uint8_t *percent(uint8_t item) {  // the items set from 0 to 100
  chord::Params &p = engine.p;
  switch (item) {
    case SOUND: return &p.sound;
    case SHAPE: return &p.shape;
    case BRIGHT: return &p.bright;
    case RESO: return &p.reso;
    case DECAY: return &p.decay;
    case SPREAD: return &p.spread;
    case MOTION: return &p.motion;
    case SWELL: return &p.swell;
    case STRUM: return &p.strum;
    case GLIDE: return &p.glide;
    case LEVEL: return &p.level;
    default: return nullptr;
  }
}

static void adjust(int32_t turns, int32_t step) {
  chord::Params &p = engine.p;
  if (uint8_t *v = percent(sel)) {
    *v = constrain((int32_t)*v + turns * step, 0, 100);
    return;
  }
  switch (sel) {
    case PATCH:
      patch = constrain((int32_t)patch + turns, 0, chord::kPatches - 1);
      chord::ApplyPatch(&p, patch);
      break;
    case OSCS: p.oscs = constrain((int32_t)p.oscs + turns, 0, chord::OscsCount() - 1); break;
    case KEY: p.key = (p.key + 12 + (turns % 12)) % 12; break;
    case HARMONY: p.harmony = constrain((int32_t)p.harmony + turns, 0, chord::HarmonyCount() - 1); break;
    case STACK: p.stack = constrain((int32_t)p.stack + turns, 0, chord::StackCount() - 1); break;
    case VOICE: p.voicing = constrain((int32_t)p.voicing + turns, 0, chord::VoicingCount() - 1); break;
    case OCTAVE: p.octave = constrain((int32_t)p.octave + turns, -2, 2); break;
    case HOLD:
      p.hold = turns > 0;
      AudioNoInterrupts();
      engine.HoldChanged();
      AudioInterrupts();
      break;
    default: break;
  }
}

static void ui() {
  int32_t turns = board::encoder_turns();
  if (turns && !editing) {
    sel = constrain((int32_t)sel + turns, 0, ITEMS - 1);
  } else if (turns) {
    uint32_t now = millis();
    adjust(turns, now - lastTurnMs < 40 ? 4 : 1);  // turned fast: bigger steps
    lastTurnMs = now;
    changedMs = now ? now : 1;
  }
  switch (board::poll_push()) {
    case board::Push::SHORT:
    case board::Push::LONG:
      editing = !editing;
      break;
    case board::Push::MENU:
      board::back_to_menu();
    default:
      break;
  }
  if (changedMs && millis() - changedMs >= 3000) {
    Settings s = {SETTINGS_MAGIC, patch, engine.p};
    board::mode_store(MODE, &s, sizeof(s));
    changedMs = 0;
  }
}

static void value_of(uint8_t item, char *out, size_t n) {
  const chord::Params &p = engine.p;
  if (const uint8_t *v = percent(item)) {
    snprintf(out, n, "%u%%", *v);
    return;
  }
  switch (item) {
    case PATCH: snprintf(out, n, "%u %s%s", patch + 1, chord::PatchName(patch), chord::PatchEdited(p, patch) ? "*" : ""); break;
    case OSCS: snprintf(out, n, "%s", chord::OscsName(p.oscs)); break;
    case KEY: snprintf(out, n, "%s", chord::KeyName(p.key)); break;
    case HARMONY: snprintf(out, n, "%s", chord::HarmonyName(p.harmony)); break;
    case STACK: snprintf(out, n, "%s", chord::StackName(p.stack)); break;
    case VOICE: snprintf(out, n, "%s", chord::VoicingName(p.voicing)); break;
    case OCTAVE: snprintf(out, n, "%+d", p.octave); break;
    case HOLD: snprintf(out, n, "%s", p.hold ? "on" : "off"); break;
    default: out[0] = 0; break;
  }
}

static void paint() {
  char line[32], value[16];
  snprintf(line, sizeof(line), "%-8s%s", engine.ChordName(), engine.GateOn() ? (engine.p.hold ? "HOLD" : "ON") : "");
  screen::row(0, line);
  uint8_t top = sel < 1 ? 0 : sel > ITEMS - 2 ? ITEMS - 3 : sel - 1;
  for (uint8_t r = 0; r < 3; r++) {
    uint8_t it = top + r;
    value_of(it, value, sizeof(value));
    snprintf(line, sizeof(line), "%c%-6s %s", it == sel ? '>' : ' ', NAME[it], value);
    screen::row(r + 1, line, it == sel && editing);
  }
}

// ---- the jacks
static float jackVolts[chord::kNotes + 2];
static uint32_t trigUntil[chord::kNotes];
static uint8_t gateWord;

static void write_if_moved(uint8_t i, uint8_t jack, float volts) {
  if (fabsf(volts - jackVolts[i]) < 0.0005f) return;
  board::cv_write(jack, volts);
  jackVolts[i] = volts;
}

static void jacks(uint32_t now) {
  // pitches before the gate, so anything sampling pitch on the gate's edge sees the new note
  for (uint8_t v = 0; v < chord::kNotes; v++)
    write_if_moved(v, PITCH_JACK[v], constrain((engine.Pitch(v) - 36.0f) / 12.0f, -10.0f, 10.0f));
  write_if_moved(chord::kNotes, GATE_JACK, engine.GateOn() ? GATE_VOLTS : 0.0f);
  write_if_moved(chord::kNotes + 1, MOTION_JACK, engine.MotionOut() * MOTION_VOLTS);

  AudioNoInterrupts();
  uint8_t entered = engine.TakeTriggers();
  AudioInterrupts();
  uint8_t word = 0;
  for (uint8_t v = 0; v < chord::kNotes; v++) {
    if (entered & (1 << v)) trigUntil[v] = (now + TRIG_MS) | 1;
    if (trigUntil[v] && (int32_t)(now - trigUntil[v]) >= 0) trigUntil[v] = 0;
    if (trigUntil[v]) word |= 1 << v;
  }
  if (word != gateWord) board::gates_write(word), gateWord = word;
}

void begin() {
  engine.Init(AUDIO_SAMPLE_RATE_EXACT);
  chord::ApplyPatch(&engine.p, 0);
  load_params(&engine.p);

  // 0 V everywhere before the switches close (requirement 3.6)
  for (uint8_t v = 0; v < chord::kNotes; v++) board::cv_write(PITCH_JACK[v], 0);
  board::cv_write(GATE_JACK, 0);
  board::cv_write(MOTION_JACK, 0);
  board::cv_outputs(board::CV_ALL);
  board::gates_write(0);
  board::gate_outputs(true);

  midi_notes::begin(on, off, bend, stop);

  AudioMemory(16);
  // made in signal order, so the library updates them in that order within a block
  AudioInputI2S *in = new AudioInputI2S();
  ChordalStream *chordal = new ChordalStream();
  AudioOutputI2S *out = new AudioOutputI2S();
  new AudioConnection(*in, 0, *chordal, 0);
  new AudioConnection(*in, 1, *chordal, 1);
  new AudioConnection(*chordal, 0, *out, 0);
  new AudioConnection(*chordal, 1, *out, 1);
}

void loop() {
  midi_notes::poll();
  ui();
  uint32_t now = millis();
  jacks(now);
  if (now - lastPaintMs >= 100) {
    paint();
    lastPaintMs = now;
  }
  screen::service();
  if (now - lastReportMs >= 2000) {  // for the bench: pio device monitor
    Serial.printf("Chordal: cpu %.1f%% (max %.1f%%), blocks %u (max %u), %s, %.0f %.0f %.0f %.0f\n", AudioProcessorUsage(),
                  AudioProcessorUsageMax(), AudioMemoryUsage(), AudioMemoryUsageMax(), engine.ChordName(), engine.Pitch(0),
                  engine.Pitch(1), engine.Pitch(2), engine.Pitch(3));
    lastReportMs = now;
  }
}

}  // namespace chordal_mode
