// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mode 1, the Move companion. Ported from the old Boy (tag teensy-move-old-final):
// - three voices, MIDI ch 1-3: GATE1-3 and PITCH1-3 (the old Boy's ch 4 and mod outs have no jacks)
// - drums on ch 10: note 36 fires DRM1, note 37 DRM2, 500 us pulses
// - MIDI clock: a pulse on CLK every 16th; Start sends a pulse on RST
// - MIDI from USB (the Move) and from the TRS input, merged
// - encoder push: a long press sends a reset pulse; held for 2 s it ends the session and goes back
//   to the mode chooser

#include "move_mode.h"

#include <Audio.h>
#include <MIDI.h>

#include "board.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, trsMIDI);

namespace move_mode {

constexpr uint8_t VOICES = 3;
constexpr uint8_t PITCH_CV[VOICES] = {board::PITCH1, board::PITCH2, board::PITCH3};
constexpr uint8_t GATE_CV[VOICES] = {board::GATE1, board::GATE2, board::GATE3};
constexpr float GATE_VOLTS = 5.0f;

constexpr uint8_t DRUMS = 2;
constexpr uint8_t DRUM_NOTE[DRUMS] = {36, 37};
constexpr uint8_t DRUM_BIT[DRUMS] = {board::DRUM1, board::DRUM2};
constexpr uint32_t DRUM_TRIG_US = 500;  // short, so gate-sensitive hats don't read a trigger as a gate
constexpr uint32_t DRUM_DISP_MS = 300;  // a hit stays on screen this long

constexpr uint32_t CLK_PULSE_MS = 5, RST_PULSE_MS = 8;

struct Voice {
  int8_t note = -1;
  float bend = 0;  // semitones
  float volts = 0;
  bool gate = false;    // wanted
  bool gateOut = false; // on the jack
  bool dirty = true;    // pitch to write
  uint32_t retrigUntilUs = 0;
};
static Voice voice[VOICES];

static bool drumTrig[DRUMS];
static uint32_t drumUntilUs[DRUMS], drumSeenMs[DRUMS];
static bool clk, rst;
static uint32_t clkUntil, rstUntil, lastClockTickMs;
static uint8_t gateWord;

static char midiSrc = 'U';  // which input the handlers are being called from
static char lastSrc;
static uint8_t lastCh, lastNote, lastVel;
static uint32_t lastMidiMs, lastPaintMs;

static AudioOutputI2S *audioOut;

static void updatePitch(Voice &v) {
  v.volts = ((v.note < 0 ? 36 : v.note) - 36) / 12.0f + v.bend / 12.0f;  // note 36 at 0 V, as the old Boy
  v.dirty = true;
}

// Gate retrigger, as the old Boy: a sequencer's NoteOff and the next NoteOn often land in one drain pass,
// so the gate would never drop. A NoteOn while the gate is up holds it low for RETRIG_US first.
constexpr uint32_t RETRIG_US = 1000;
static void armRetrig(Voice &v) {
  if (v.gateOut) {
    uint32_t t = micros() + RETRIG_US;
    v.retrigUntilUs = t ? t : 1;
  }
}

static bool gateLevel(Voice &v, uint32_t nowUs) {
  if (v.retrigUntilUs) {
    if ((int32_t)(nowUs - v.retrigUntilUs) < 0) return false;
    v.retrigUntilUs = 0;
  }
  return v.gate;
}

static void noteOff(byte ch, byte note, byte) {
  lastSrc = midiSrc, lastCh = ch, lastNote = note, lastVel = 0, lastMidiMs = millis();
  if (ch < 1 || ch > VOICES) return;
  Voice &v = voice[ch - 1];
  if (v.note != note) return;
  v.gate = false;
  v.note = -1;
}

static void noteOn(byte ch, byte note, byte vel) {
  if (!vel) return noteOff(ch, note, 0);
  lastSrc = midiSrc, lastCh = ch, lastNote = note, lastVel = vel, lastMidiMs = millis();
  if (ch == 10) {
    for (uint8_t i = 0; i < DRUMS; i++)
      if (note == DRUM_NOTE[i]) {
        drumTrig[i] = true;
        drumUntilUs[i] = micros() + DRUM_TRIG_US;
        drumSeenMs[i] = millis();
      }
    return;
  }
  if (ch < 1 || ch > VOICES) return;
  Voice &v = voice[ch - 1];
  v.note = note;
  updatePitch(v);
  armRetrig(v);
  v.gate = true;
}

// Both Teensy's usbMIDI and the MIDI library pass the bend centred: -8192 to +8191. (The old Boy
// subtracted 8192 again, so a centred bend left the voice two semitones flat.)
static void pitchBend(byte ch, int value) {
  if (ch < 1 || ch > VOICES) return;
  Voice &v = voice[ch - 1];
  v.bend = 2.0f * value / 8192.0f;  // +-2 semitones
  if (v.note >= 0) updatePitch(v);
}

// MIDI clock is 24 ppqn, so BEAT_DIV 6 puts 16ths on CLK: a step for the drum modules it feeds,
// which then advance on real edges rather than extrapolating between quarter notes (the old Boy's reasoning)
constexpr uint8_t BEAT_DIV = 6;
static uint32_t midiTicks;
static void resetTicks() { midiTicks = BEAT_DIV - 1; }  // so the first clock fires beat 1

static void onClock() {
  lastClockTickMs = millis();
  if (++midiTicks % BEAT_DIV == 0) {
    clk = true;
    clkUntil = millis() + CLK_PULSE_MS;
  }
}

static void onStart() {
  rst = true;
  rstUntil = millis() + RST_PULSE_MS;
  resetTicks();
}

static void onStop() {
  for (Voice &v : voice) v.gate = false;
  clk = rst = false;
  resetTicks();
}

static void onContinue() { resetTicks(); }

void begin() {
  // 0 V everywhere before the switches close (requirement 3.6)
  for (uint8_t i = 0; i < VOICES; i++) {
    board::cv_write(PITCH_CV[i], 0);
    board::cv_write(GATE_CV[i], 0);
  }
  board::cv_outputs(board::CV_ALL);
  board::gates_write(0);
  board::gate_outputs(true);

  // Mode 1 makes no sound, but the audio outs are DC-coupled: clock the codec and send silence, so
  // they hold the 0 V measured in the bring-up instead of whatever an unclocked codec gives
  AudioMemory(2);
  audioOut = new AudioOutputI2S();

  usbMIDI.setHandleNoteOn(noteOn);
  usbMIDI.setHandleNoteOff(noteOff);
  usbMIDI.setHandlePitchChange(pitchBend);
  usbMIDI.setHandleClock(onClock);
  usbMIDI.setHandleStart(onStart);
  usbMIDI.setHandleStop(onStop);
  usbMIDI.setHandleContinue(onContinue);
  trsMIDI.setHandleNoteOn(noteOn);
  trsMIDI.setHandleNoteOff(noteOff);
  trsMIDI.setHandlePitchBend(pitchBend);
  trsMIDI.setHandleClock(onClock);
  trsMIDI.setHandleStart(onStart);
  trsMIDI.setHandleStop(onStop);
  trsMIDI.setHandleContinue(onContinue);
  trsMIDI.begin(MIDI_CHANNEL_OMNI);
  trsMIDI.turnThruOff();  // no MIDI out on Boy
  resetTicks();
}

static void paint(uint32_t now) {
  static const char kNoteL[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
  static const char kNoteS[12] = {'-', '#', '-', '#', '-', '-', '#', '-', '#', '-', '#', '-'};
  char line[32], g[VOICES][3], d[DRUMS];
  for (uint8_t i = 0; i < VOICES; i++) {  // the note name while the gate is on, "--" while it's off
    const Voice &v = voice[i];
    bool on = v.gate && v.note >= 0;
    g[i][0] = on ? kNoteL[v.note % 12] : '-';
    g[i][1] = on ? kNoteS[v.note % 12] : '-';
    g[i][2] = 0;
  }
  for (uint8_t i = 0; i < DRUMS; i++) d[i] = drumSeenMs[i] && now - drumSeenMs[i] <= DRUM_DISP_MS ? '#' : '-';
  bool clocked = lastClockTickMs && now - lastClockTickMs <= 500;
  snprintf(line, sizeof(line), "G %s %s %s  D %c%c %s", g[0], g[1], g[2], d[0], d[1], clocked ? "CLK" : "");
  screen::row(0, line);
  snprintf(line, sizeof(line), "P%+6.2f%+6.2f%+6.2f", voice[0].volts, voice[1].volts, voice[2].volts);
  screen::row(1, line);
  if (lastMidiMs && now - lastMidiMs <= 1000)
    snprintf(line, sizeof(line), "%s ch%-2u n%-3u v%u", lastSrc == 'U' ? "USB" : "TRS", lastCh, lastNote, lastVel);
  else
    snprintf(line, sizeof(line), "ch1-3 CV  ch10 drums");
  screen::row(2, line);
  screen::row(3, "MOVE   hold: menu");
}

void loop() {
  midiSrc = 'U';
  while (usbMIDI.read()) {
  }  // drain everything pending: the clock's timing depends on it
  midiSrc = 'T';
  while (trsMIDI.read()) {
  }

  switch (board::poll_push()) {
    case board::Push::LONG:
      rst = true;
      rstUntil = millis() + RST_PULSE_MS;
      break;
    case board::Push::MENU:
      board::back_to_menu();
    default:
      break;
  }

  uint32_t now = millis(), nowUs = micros();
  if (clkUntil && (int32_t)(now - clkUntil) >= 0) clk = false, clkUntil = 0;
  if (rstUntil && (int32_t)(now - rstUntil) >= 0) rst = false, rstUntil = 0;
  for (uint8_t i = 0; i < DRUMS; i++)
    if (drumUntilUs[i] && (int32_t)(nowUs - drumUntilUs[i]) >= 0) drumTrig[i] = false, drumUntilUs[i] = 0;

  // pitches before gates, so anything sampling pitch on the gate's edge sees the new note
  for (uint8_t i = 0; i < VOICES; i++) {
    Voice &v = voice[i];
    if (v.dirty) board::cv_write(PITCH_CV[i], v.volts), v.dirty = false;
  }
  for (uint8_t i = 0; i < VOICES; i++) {
    Voice &v = voice[i];
    bool level = gateLevel(v, nowUs);
    if (level != v.gateOut) board::cv_write(GATE_CV[i], level ? GATE_VOLTS : 0), v.gateOut = level;
  }

  uint8_t word = (clk << board::CLK) | (rst << board::RST);
  for (uint8_t i = 0; i < DRUMS; i++) word |= drumTrig[i] << DRUM_BIT[i];
  if (word != gateWord) board::gates_write(word), gateWord = word;

  if (now - lastPaintMs >= 250) {
    paint(now);
    lastPaintMs = now;
  }
  screen::service();
}

}  // namespace move_mode
