// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Teensy Move v3 — Ableton Move <-> Eurorack bridge
// v3: removed the drone synth, added a stereo Filter->Delay->Reverb FX send on
//     the passthrough, fixed the audio-out DC (LINE OUT AC-coupling + ADC HPF
//     freeze), and added OLED screen-sleep. Chord/progressions mode was later
//     removed. Two pages: STATUS (all channels on one screen; Pot1-4 sweep
//     Mod1-4 across -5..+5V with soft-takeover) and FX.
//     OLED switching noise is handled by grounding (star return) + screen-sleep
//     (a dedicated OLED supply LDO was tried and barely helped, ruling out the
//     supply rail). Possible v4 (software, optional): timer-ISR gate/clock/drum
//     timing off the main loop, keeping the 595 and arbitrating the shared SPI bus.
// - 4x MIDI-to-CV (gate / pitch / mod) on ch1-4, 4 drum triggers on ch10
//   (mod source per channel: velocity, CC#42, or STATUS-page pot — last actuator wins)
// - Stereo line passthrough with a Filter->Delay->Reverb FX send (four pots)
// - Partial OLED updates (only changed rows) to limit loop() blocking

#include <Arduino.h>
#include <SPI.h>
#include <CrashReport.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Audio.h>
#include "spi_bus.h"
#include "teensy_move/pins.h"
#include "teensy_move/calib_static.h"

#define OLED_W 128
#define OLED_H 32
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

// ============================================================================
// AUDIO OBJECTS — stereo FX send (Filter -> Delay -> Reverb) on the line passthrough
// ============================================================================
AudioInputI2S           i2sIn;
AudioOutputI2S          i2sOut;
#ifdef USB_MIDI_AUDIO_SERIAL
AudioOutputUSB          usbOut;
#endif
AudioControlSGTL5000    sgtl5000;

// Stereo FX send on the line passthrough: Filter -> Delay (w/ feedback) -> Reverb.
//   i2sIn -> SVF low-pass -> delay(+feedback) -> [dry+delay] + reverb wet -> i2sOut
// The four pots on the FX page set cutoff / delay time / feedback / reverb mix.
AudioFilterStateVariable  fxFiltL, fxFiltR;     // low-pass per channel
AudioMixer4               fxDlyInL, fxDlyInR;    // ch0 = filtered in, ch1 = feedback
AudioEffectDelay          fxDlyL, fxDlyR;        // delay lines (tap 0)
AudioMixer4               fxVerbIn;              // sum filtered+delayed to mono reverb in
AudioEffectFreeverbStereo fxVerb;               // stereo reverb (mono in, stereo out)
AudioMixer4               fxOutL, fxOutR;        // ch0 dry, ch1 delay wet, ch2 reverb wet

// Filter the stereo input
AudioConnection  fxc01(i2sIn, 0, fxFiltL, 0);
AudioConnection  fxc02(i2sIn, 1, fxFiltR, 0);
// Filtered -> delay input mixers
AudioConnection  fxc03(fxFiltL, 0, fxDlyInL, 0);
AudioConnection  fxc04(fxFiltR, 0, fxDlyInR, 0);
// Delay input mixers -> delay lines
AudioConnection  fxc05(fxDlyInL, 0, fxDlyL, 0);
AudioConnection  fxc06(fxDlyInR, 0, fxDlyR, 0);
// Delay tap 0 -> feedback into the input mixers
AudioConnection  fxc07(fxDlyL, 0, fxDlyInL, 1);
AudioConnection  fxc08(fxDlyR, 0, fxDlyInR, 1);
// Feed reverb from the delayed (wet) signal, summed to mono
AudioConnection  fxc09(fxDlyL, 0, fxVerbIn, 0);
AudioConnection  fxc10(fxDlyR, 0, fxVerbIn, 1);
AudioConnection  fxc11(fxVerbIn, 0, fxVerb, 0);
// Output mix: dry (filtered) + delay wet + reverb wet
AudioConnection  fxc12(fxFiltL, 0, fxOutL, 0);
AudioConnection  fxc13(fxFiltR, 0, fxOutR, 0);
AudioConnection  fxc14(fxDlyL, 0, fxOutL, 1);
AudioConnection  fxc15(fxDlyR, 0, fxOutR, 1);
AudioConnection  fxc16(fxVerb, 0, fxOutL, 2);
AudioConnection  fxc17(fxVerb, 1, fxOutR, 2);
// To the codec line out
AudioConnection  fxc18(fxOutL, 0, i2sOut, 0);
AudioConnection  fxc19(fxOutR, 0, i2sOut, 1);
#ifdef USB_MIDI_AUDIO_SERIAL
AudioConnection  fxc20(fxOutL, 0, usbOut, 0);
AudioConnection  fxc21(fxOutR, 0, usbOut, 1);
#endif

#define PIN_BTN      2
#define PIN_CS_DAC1 33
#define PIN_CS_DAC2 34
#define PIN_CLOCK   39
#define PIN_RESET   37
#define PIN_GATE1   40
#define PIN_GATE2   38
#define PIN_595_LATCH 32
#define GATE_WRITE(pin, s) digitalWrite((pin), (s)?LOW:HIGH)   // HCT14 invert

static const uint8_t DRUM_BASE_NOTE = 36;
static const uint8_t DRUM_COUNT = 4;
static const uint32_t DRUM_TRIG_US[DRUM_COUNT] = { 500, 500, 500, 500 };


#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

// Prototypes
void onNoteOn(byte ch, byte note, byte vel);
void onNoteOff(byte ch, byte note, byte vel);
void onPitchBend(byte ch, int value);
void onControlChange(byte ch, byte cc, byte val);
void onStart();
void onStop();
void onClock();

// MCP4822
enum { CH_A=0, CH_B=1 };
static inline uint16_t frame4822(uint8_t ch, uint16_t v){ return (ch?0x8000:0)|0x1000|(v & 0x0FFF); }
static inline void mcp4822_write(uint8_t cs, uint8_t ch, uint16_t v){
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  SPI.transfer16(frame4822(ch, v));
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

// Expander DACs via Q6/Q7 CS
static inline void mcp4822_write_expander(uint8_t whichDac /*0->Q6,1->Q7*/, uint8_t ch, uint16_t v){
  uint8_t img = expanderImage();
  img |= (1u<<ExpanderBits::DAC1_CS) | (1u<<ExpanderBits::DAC2_CS);
  if (whichDac == 0) img &= ~(1u<<ExpanderBits::DAC1_CS); else img &= ~(1u<<ExpanderBits::DAC2_CS);
  expanderWrite(img);
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI.transfer16(frame4822(ch, v));
  SPI.endTransaction();
  img |= (1u<<ExpanderBits::DAC1_CS) | (1u<<ExpanderBits::DAC2_CS);
  expanderWrite(img);
}

// Per-channel calibration using static (DMM-fitted) calibration data
static inline uint16_t pitchVolt_to_code_ch(uint8_t ch, float vOut){
  return teensy_move_calib::pitchVoltsToCode(ch, vOut);
}
static inline uint16_t modVolt_to_code_ch(uint8_t ch, float vOut){
  return teensy_move_calib::modVoltsToCode(ch, vOut);
}

struct Voice { int8_t note=-1; float bend=0, modV=0, pitchHeldV=0; };
static Voice v1, v2, v3, v4;
static inline float midiNote_to_volts(int note){ return (note-36)/12.0f; }
static inline void updatePitch(Voice& v){ float base=midiNote_to_volts(v.note<0?36:v.note); v.pitchHeldV = base + v.bend/12.0f; }

// Mod CV source, per channel — last actuator wins:
//   VEL (default) — note velocity writes Mod at note-on (0..5V)
//   CC  — CC#42 (the live-coding mod CC standardised across our MIDI-to-CV
//         modules) drives Mod (0..5V); grabs the channel on first receipt
//   POT — the STATUS-page pot drives Mod (-5..+5V); grabs on a deliberate move
// Once a channel leaves VEL, velocity never writes it again (until power-off),
// so streamed notes can't stomp a CC or pot sweep. CC and POT can take the
// channel from each other at any time (last actuator wins).
enum ModSource : uint8_t { MODSRC_VEL = 0, MODSRC_CC, MODSRC_POT };
static const uint8_t MOD_CC = 42;
static ModSource modSource[4] = {MODSRC_VEL, MODSRC_VEL, MODSRC_VEL, MODSRC_VEL};

// Dirty flags
static volatile bool dirtyPitch1=true, dirtyPitch2=true, dirtyMod1=true, dirtyMod2=true;
static volatile bool dirtyPitch3=true, dirtyPitch4=true, dirtyMod3=true, dirtyMod4=true;

// Realtime outputs
static volatile bool gate1=false, gate2=false, clk=false, rst=false;
static volatile bool gate3=false, gate4=false;
static volatile uint32_t clkUntil=0, rstUntil=0; const uint32_t PULSE_MS=5;

// Drums
static volatile bool drumTrig[DRUM_COUNT] = {false,false,false,false};
static volatile uint32_t drumUntilUs[DRUM_COUNT] = {0,0,0,0};

// Display helpers for the STATUS row: drum pulses (500us) are far shorter than
// the OLED refresh, so latch each hit for ~300ms on screen (outputs unaffected).
// Clock presence = any MIDI clock tick within the last 500ms -> show "CLK".
static const uint32_t DRUM_DISP_MS = 300;
static volatile uint32_t drumSeenMs[DRUM_COUNT] = {0,0,0,0};
static volatile uint32_t lastClockTickMs = 0;

// Debug
static volatile uint8_t lastMidiCh=0, lastMidiNote=0, lastMidiVel=0; static volatile uint32_t lastMidiMs=0;

// Button/OLED
static uint32_t btnDownAt=0; static bool btnPrev=HIGH; const uint16_t LONG_MS=600; static uint32_t lastBeat=0;
static uint32_t btnLastChange=0; const uint16_t DEBOUNCE_MS=30;  // Debounce window
static uint32_t lastOledPaintMs=0;
static const uint32_t OLED_FPS_MS=250;  // Slower refresh -> fewer I2C bursts (noise) + less loop() blocking
// Screen sleep: power the OLED down (DISPLAYOFF stops the charge-pump and the
// I2C refresh bursts — the two things that couple noise into the audio) after a
// spell of no BUTTON activity. The button (not MIDI) wakes it, so playing keeps
// the screen dark and quiet; press the button to peek at status.
static bool screenAsleep=false;
static uint32_t lastUiActivityMs=0;
static bool btnWakeConsumed=false;
// 0 disables screen sleep and the display stays on. Be aware of what sleep was
// there for: DISPLAYOFF stops the charge-pump and the I2C refresh bursts, which
// are the two things that couple OLED noise into the audio path. Grounding (star
// return) did most of that work and sleep was belt-and-braces, so staying awake
// should be fine - but if a faint whine appears in the audio with the screen lit,
// this is the first thing to put back. 10000 restores the old ten-second sleep.
static const uint32_t SCREEN_SLEEP_MS=0;
static inline void drawRow(uint8_t row,const char* s){ oled.setCursor(0,row*8); oled.print(s); }
static char lineBuf[64];
static uint8_t gOledPage = 0; // 0 = STATUS (all channels; pots -> Mod1-4), 1 = FX

// OLED row cache for partial updates
static char oledRowCache[4][22] = {"","","",""};  // 21 chars max per row + null
static bool oledRowDirty[4] = {true, true, true, true};

// ============================================================================
// MOD POT STATE (STATUS page) — manual pot control of the four Mod outputs
// ============================================================================
// Pot1-4 sweep Mod1-4 across -5..+5V (pot centre = 0V). Soft-takeover: on
// entering the page each pot is inactive until it crosses (or lands near) the
// channel's current Mod voltage, so grabbing a pot never snaps the output.
static bool  modPotCaught[4] = {false, false, false, false};
static float modPotRef[4]    = {0, 0, 0, 0};  // pot volts at page entry (crossing ref)
static uint8_t gPrevOledPage = 255;           // page-change detection for arming (255 = arm at boot)
static uint32_t lastModPotMs = 0;             // last mod-pot movement (row-3 src feedback)

// ============================================================================
// FX STATE (stereo Filter -> Delay -> Reverb on the audio passthrough)
// ============================================================================
// Defaults are fully clean (open filter, no delay/reverb) so the passthrough is
// a transparent level-shifted send until FX is dialed in on the FX page.
static float fxCutoff   = 1.0f;   // 0..1 -> ~80 Hz .. 12 kHz (log); 1.0 = open
static float fxDelayMs  = 250.0f; // delay time
static float fxFeedback = 0.0f;   // P3: delay amount (wet level + feedback)
static float fxVerbMix  = 0.0f;   // P4: reverb wet level
static const float FX_DELAY_MAX_MS = 400.0f;

static void updateFxCutoff(float v) {  // v: 0..1
  fxCutoff = v;
  float hz = 80.0f * powf(150.0f, v);  // 80 Hz .. ~12 kHz, log
  fxFiltL.frequency(hz);
  fxFiltR.frequency(hz);
}
static void updateFxDelay(float v) {   // v: 0..1
  fxDelayMs = 20.0f + v * (FX_DELAY_MAX_MS - 20.0f);
  fxDlyL.delay(0, fxDelayMs);
  fxDlyR.delay(0, fxDelayMs);
}
static void updateFxFeedback(float v) { // v: 0..1 -> delay amount (wet + feedback)
  fxFeedback = v * 0.85f;              // feedback gain, capped below unity
  fxDlyInL.gain(1, fxFeedback);
  fxDlyInR.gain(1, fxFeedback);
  float wet = v * 0.6f;                // delay wet level: 0 at v=0 -> clean
  fxOutL.gain(1, wet);
  fxOutR.gain(1, wet);
}
static void updateFxVerbMix(float v) {  // v: 0..1
  fxVerbMix = v;
  fxOutL.gain(2, fxVerbMix);
  fxOutR.gain(2, fxVerbMix);
}

// Initialize the FX audio graph
static void initFx() {
  fxFiltL.resonance(0.7f); fxFiltR.resonance(0.7f);
  // Delay input mixers: ch0 = filtered input, ch1 = feedback
  fxDlyInL.gain(0, 1.0f); fxDlyInR.gain(0, 1.0f);
  fxDlyL.delay(0, fxDelayMs); fxDlyR.delay(0, fxDelayMs);
  // Reverb input sum (filtered+delayed L/R -> mono), modest level
  fxVerbIn.gain(0, 0.5f); fxVerbIn.gain(1, 0.5f);
  fxVerb.roomsize(0.7f); fxVerb.damping(0.5f);
  // Output mix: dry filtered + delay wet (set by P3) + reverb wet (set by P4)
  fxOutL.gain(0, 0.85f); fxOutR.gain(0, 0.85f);
  updateFxCutoff(fxCutoff);
  updateFxFeedback(fxFeedback);  // also sets delay wet level
  updateFxVerbMix(fxVerbMix);
}

// ============================================================================
// MOD POTS (STATUS page) — Pot1-4 manually sweep Mod1-4 across -5..+5V
// ============================================================================

// Arm soft-takeover for all four pots (called when STATUS is entered / at boot).
static void armModPots() {
  for (int i = 0; i < 4; i++) {
    modPotCaught[i] = false;
    int raw = 4095 - analogRead(i == 0 ? PIN_POT1 : i == 1 ? PIN_POT2
                                : i == 2 ? PIN_POT3 : PIN_POT4);
    modPotRef[i] = (raw / 4095.0f) * 10.0f - 5.0f;
  }
}

// Read the pots and drive the Mod outputs. A pot engages once it crosses (or
// lands within 0.15V of) the channel's current Mod voltage; from then on it
// tracks directly and grabs the channel's mod source (MODSRC_POT). Returns
// true if any pot actually moved a value (for screen-wake).
static bool updateModPots() {
  int raw[4];
  raw[0] = 4095 - analogRead(PIN_POT1);  // Invert: CW = max
  raw[1] = 4095 - analogRead(PIN_POT2);
  raw[2] = 4095 - analogRead(PIN_POT3);
  raw[3] = 4095 - analogRead(PIN_POT4);

  Voice* vs[4] = { &v1, &v2, &v3, &v4 };
  volatile bool* dirty[4] = { &dirtyMod1, &dirtyMod2, &dirtyMod3, &dirtyMod4 };

  bool moved = false;
  for (int i = 0; i < 4; i++) {
    float potV = (raw[i] / 4095.0f) * 10.0f - 5.0f;  // -5..+5V, centre = 0V
    float target = vs[i]->modV;
    if (!modPotCaught[i]) {
      bool near    = fabsf(potV - target) <= 0.15f;
      bool crossed = (modPotRef[i] - target) * (potV - target) < 0.0f;
      if (near || crossed) modPotCaught[i] = true;
      else continue;
    }
    // Deadband so ADC jitter doesn't churn DAC writes or hold the screen awake.
    if (fabsf(potV - target) > 0.05f) {
      vs[i]->modV = potV;
      modSource[i] = MODSRC_POT;
      *dirty[i] = true;
      moved = true;
    }
  }
  return moved;
}

// Diagnostics mode (boot-hold)
static bool gDiagMode = false;
static uint16_t gDiagCodes[8] = {0,0,0,0,0,0,0,0}; // M1,P1,M2,P2,M3,P3,M4,P4
static uint8_t gDiagSel = 0;
static const char* kDiagLabels[8] = {"M1","P1","M2","P2","M3","P3","M4","P4"};

static void diag_write_channel(uint8_t idx, uint16_t code) {
  switch(idx) {
    case 0: mcp4822_write(PIN_CS_DAC1, CH_A, code); break;  // M1
    case 1: mcp4822_write(PIN_CS_DAC1, CH_B, code); break;  // P1
    case 2: mcp4822_write(PIN_CS_DAC2, CH_A, code); break;  // M2
    case 3: mcp4822_write(PIN_CS_DAC2, CH_B, code); break;  // P2
    case 4: mcp4822_write_expander(0, EXP_MOD3_CH_IDX, code); break;   // M3
    case 5: mcp4822_write_expander(1, EXP_PITCH3_CH_IDX, code); break; // P3
    case 6: mcp4822_write_expander(0, EXP_MOD4_CH_IDX, code); break;   // M4
    case 7: mcp4822_write_expander(1, EXP_PITCH4_CH_IDX, code); break; // P4
  }
}

static void diag_render() {
  oled.clearDisplay();
  snprintf(lineBuf,sizeof(lineBuf),"DIAG Sel:%s Pot->Code", kDiagLabels[gDiagSel]);
  oled.setCursor(0,0); oled.print(lineBuf);
  snprintf(lineBuf,sizeof(lineBuf),"M1:%4u P1:%4u", gDiagCodes[0], gDiagCodes[1]); oled.setCursor(0,8); oled.print(lineBuf);
  snprintf(lineBuf,sizeof(lineBuf),"M2:%4u P2:%4u", gDiagCodes[2], gDiagCodes[3]); oled.setCursor(0,16); oled.print(lineBuf);
  snprintf(lineBuf,sizeof(lineBuf),"M3:%4u M4:%4u", gDiagCodes[4], gDiagCodes[6]); oled.setCursor(0,24); oled.print(lineBuf);
  oled.display();
}

static void diag_tick() {
  // Write all channels
  for(uint8_t i=0;i<8;i++) diag_write_channel(i, gDiagCodes[i]);
  // Update expander gates/drums off, CS high
  uint8_t img = 0xFF; // all high = gates off, CS deasserted
  expanderWrite(img);
  // Button: short press cycles channel
  bool b = digitalRead(PIN_BTN);
  uint32_t now = millis();
  if(b!=btnPrev && (now - btnLastChange) >= DEBOUNCE_MS){
    btnLastChange = now;
    if(b==LOW) btnDownAt=now;
    else if(now - btnDownAt < LONG_MS) gDiagSel = (gDiagSel+1) & 7;
    btnPrev=b;
  }
  // Pot1 drives selected channel (inverted so CW = max)
  int raw = 4095 - analogRead(PIN_POT1);
  int code = raw;
  if(code<0) code=0; else if(code>4095) code=4095;
  gDiagCodes[gDiagSel] = (uint16_t)code;
}

// MIDI callbacks — the CV bridge (ch1-4) is active on every page.
void onNoteOn(byte ch, byte note, byte vel){
  lastMidiCh=ch; lastMidiNote=note; lastMidiVel=vel; lastMidiMs=millis();
  if(!vel){ onNoteOff(ch,note,0); return; }

  // Drums (ch10)
  if(ch==10){
    int idx=(int)note-(int)DRUM_BASE_NOTE;
    if(idx>=0 && idx<(int)DRUM_COUNT){
      drumTrig[idx]=true;
      drumUntilUs[idx]=micros()+DRUM_TRIG_US[idx];
      drumSeenMs[idx]=millis();
    }
    return;
  }

  // Channels 1-4 CV/Gate; velocity drives Mod only while the channel's mod
  // source is still VEL (CC#42 or a STATUS-page pot may have grabbed it).
  float modV = (vel / 127.0f) * 5.0f;  // 0-5V velocity
  if(ch==1){
    v1.note=note; updatePitch(v1);
    if(modSource[0]==MODSRC_VEL){ v1.modV=modV; dirtyMod1=true; }
    gate1=true; dirtyPitch1=true;
  }
  else if(ch==2){
    v2.note=note; updatePitch(v2);
    if(modSource[1]==MODSRC_VEL){ v2.modV=modV; dirtyMod2=true; }
    gate2=true; dirtyPitch2=true;
  }
  else if(ch==3){
    v3.note=note; updatePitch(v3);
    if(modSource[2]==MODSRC_VEL){ v3.modV=modV; dirtyMod3=true; }
    gate3=true; dirtyPitch3=true;
  }
  else if(ch==4){
    v4.note=note; updatePitch(v4);
    if(modSource[3]==MODSRC_VEL){ v4.modV=modV; dirtyMod4=true; }
    gate4=true; dirtyPitch4=true;
  }
}
void onNoteOff(byte ch, byte note, byte){
  lastMidiCh=ch; lastMidiNote=note; lastMidiVel=0; lastMidiMs=millis();
  if(ch==1 && v1.note==note){ gate1=false; v1.note=-1; dirtyPitch1=true; }
  else if(ch==2 && v2.note==note){ gate2=false; v2.note=-1; dirtyPitch2=true; }
  else if(ch==3 && v3.note==note){ gate3=false; v3.note=-1; dirtyPitch3=true; }
  else if(ch==4 && v4.note==note){ gate4=false; v4.note=-1; dirtyPitch4=true; }
}
void onPitchBend(byte ch, int value){
  float semis=2.0f*(float)(value-8192)/8192.0f;
  if(ch==1){ v1.bend=semis; if(v1.note>=0){ updatePitch(v1); dirtyPitch1=true; } }
  else if(ch==2){ v2.bend=semis; if(v2.note>=0){ updatePitch(v2); dirtyPitch2=true; } }
  else if(ch==3){ v3.bend=semis; if(v3.note>=0){ updatePitch(v3); dirtyPitch3=true; } }
  else if(ch==4){ v4.bend=semis; if(v4.note>=0){ updatePitch(v4); dirtyPitch4=true; } }
}
void onControlChange(byte ch, byte cc, byte val){
  // CC#42 on ch1-4 -> that voice's Mod CV (0-5V), grabbing the channel's mod
  // source (last actuator wins vs the STATUS-page pots). Works regardless of the
  // current OLED page. FX stays pot-driven — no other CCs.
  if (cc != MOD_CC || ch < 1 || ch > 4) return;
  float modV = (val / 127.0f) * 5.0f;
  modSource[ch - 1] = MODSRC_CC;
  switch (ch) {
    case 1: v1.modV = modV; dirtyMod1 = true; break;
    case 2: v2.modV = modV; dirtyMod2 = true; break;
    case 3: v3.modV = modV; dirtyMod3 = true; break;
    case 4: v4.modV = modV; dirtyMod4 = true; break;
  }
}

// MIDI clock
// MIDI clock is 24 ppqn, so BEAT_DIV sets what lands on PIN_CLOCK: 24 = quarter
// notes, 6 = 16th notes. Sixteenths, because that is a step for the drum
// modules this feeds. A module clocked at step resolution advances on real
// edges; clocked at quarter notes it has to invent the three steps in between
// by extrapolating from the last measured period, which is only ever as steady
// as the incoming clock and cannot carry timing that is deliberately uneven.
static volatile uint32_t midiTickCount=0; static const uint8_t BEAT_DIV=6;
static void resetMidiClockCounter(){ midiTickCount=BEAT_DIV-1; } // so first onClock() fires beat 1
void onStart(){ rst=true; rstUntil=millis()+8; GATE_WRITE(PIN_RESET,true); resetMidiClockCounter(); }
void onStop(){ gate1=false; gate2=false; gate3=false; gate4=false; clk=false; rst=false; GATE_WRITE(PIN_CLOCK,false); resetMidiClockCounter(); }
void onContinue(){ resetMidiClockCounter(); }
void onClock(){ lastClockTickMs=millis(); midiTickCount++; if(midiTickCount % BEAT_DIV == 0){ clk=true; clkUntil=millis()+PULSE_MS; GATE_WRITE(PIN_CLOCK,true); } }

// Wake the OLED from sleep: power it back on and force a full repaint.
static void wakeScreen() {
  oled.ssd1306_command(SSD1306_DISPLAYON);
  screenAsleep = false;
  oledRowDirty[0]=oledRowDirty[1]=oledRowDirty[2]=oledRowDirty[3]=true;
}

// Register UI activity (button or pot move): keep the screen awake / wake it.
static void registerUiActivity() {
  lastUiActivityMs = millis();
  if (screenAsleep) wakeScreen();
}

// Helper to update OLED row only if changed (truncation to 21 chars intended)
static void updateOledRow(uint8_t row, const char* newText) {
  if (strncmp(oledRowCache[row], newText, sizeof(oledRowCache[row])-1) != 0) {
    strlcpy(oledRowCache[row], newText, sizeof(oledRowCache[row]));
    oledRowDirty[row] = true;
  }
}

// Setup
void setup(){
  // Force Full Speed USB (12 Mbps) for reliable operation through USB hubs.
  // The Teensy 4.1 defaults to High Speed (480 Mbps) which causes intermittent
  // enumeration failures via bus-powered hubs. MIDI needs negligible bandwidth.
  USB1_PORTSC1 |= USB_PORTSC1_PFSC;

  if (CrashReport) { while (!Serial && millis() < 1500) {} Serial.print(CrashReport); }
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);
  pinMode(PIN_BTN,INPUT_PULLUP);
  pinMode(PIN_CS_DAC1,OUTPUT); digitalWrite(PIN_CS_DAC1,HIGH);
  pinMode(PIN_CS_DAC2,OUTPUT); digitalWrite(PIN_CS_DAC2,HIGH);
  pinMode(PIN_CLOCK,OUTPUT); pinMode(PIN_RESET,OUTPUT);
  pinMode(PIN_GATE1,OUTPUT); pinMode(PIN_GATE2,OUTPUT);
  GATE_WRITE(PIN_CLOCK,false); GATE_WRITE(PIN_RESET,false);
  GATE_WRITE(PIN_GATE1,false); GATE_WRITE(PIN_GATE2,false);
  SPI.begin();
  expanderInit(PIN_595_LATCH);
  mcp4822_write(PIN_CS_DAC1, CH_A, modVolt_to_code_ch(0, 0.0f));
  mcp4822_write(PIN_CS_DAC1, CH_B, pitchVolt_to_code_ch(0, 0.0f));
  mcp4822_write(PIN_CS_DAC2, CH_A, modVolt_to_code_ch(1, 0.0f));
  mcp4822_write(PIN_CS_DAC2, CH_B, pitchVolt_to_code_ch(1, 0.0f));
  AudioMemory(320);  // large pool: the two FX delay lines allocate blocks from here
  initFx();  // Initialize the stereo Filter -> Delay -> Reverb graph
  Wire.begin();
  Wire.setClock(400000);  // 400kHz I2C for faster OLED — set before configuring the SGTL
  sgtl5000.enable();
  sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000.adcHighPassFilterFreeze();  // block ADC DC offset without adapting to program
  sgtl5000.lineInLevel(6);
  sgtl5000.lineOutLevel(14);   // ~3.0 Vpp → ×3.4 gain → ~10 Vpp (eurorack standard)
  sgtl5000.volume(0.8f);
  if(oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)){
    oled.dim(true);  // low contrast -> less charge-pump load -> lower coupled noise
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setCursor(0,0); oled.display();
  }
  analogReadResolution(12);
  // Boot-hold diagnostics: hold BTN during boot
  if(digitalRead(PIN_BTN)==LOW){ delay(LONG_MS+100); if(digitalRead(PIN_BTN)==LOW) gDiagMode=true; }
  usbMIDI.setHandleNoteOn(onNoteOn);
  usbMIDI.setHandleNoteOff(onNoteOff);
  usbMIDI.setHandlePitchChange(onPitchBend);
  usbMIDI.setHandleControlChange(onControlChange);
  usbMIDI.setHandleStart(onStart);
  usbMIDI.setHandleStop(onStop);
  usbMIDI.setHandleClock(onClock);
  usbMIDI.setHandleContinue(onContinue);
  lastUiActivityMs = millis();  // start the screen-sleep timer from boot
}

// Loop
void loop(){
  // Diagnostics mode
  if(gDiagMode){ while(usbMIDI.read()) {} diag_tick(); diag_render(); delay(10); return; }
  
  while(usbMIDI.read()) {}  // drain ALL pending MIDI — critical for clock timing

  // Note: the screen wakes on the BUTTON only (not on MIDI), so playing keeps it
  // dark/quiet. Press the button to peek; it re-sleeps after SCREEN_SLEEP_MS.

  // Arm mod-pot soft-takeover whenever the STATUS page is (re)entered (pots
  // must cross the current Mod value before they engage — no snap on grab).
  if (gOledPage != gPrevOledPage) {
    if (gOledPage == 0) armModPots();
    gPrevOledPage = gOledPage;
  }

  // Pots: on STATUS they sweep Mod1-4; on FX they set the effects. A pot move
  // counts as UI activity so the screen stays awake while you edit.
  if (gOledPage == 0) {
    if (updateModPots()) { lastModPotMs = millis(); registerUiActivity(); }
  } else if (gOledPage == 1) {
    // FX page: P1 cutoff, P2 delay time, P3 feedback, P4 reverb mix.
    // Wide deadband so the noisy ADC doesn't jitter params or hold the screen on.
    static int16_t lastFxPots[4] = {-1, -1, -1, -1};
    const int FX_POT_DEADBAND = 80;
    int16_t raw[4];
    raw[0] = 4095 - analogRead(PIN_POT1);  // Invert: CW = max
    raw[1] = 4095 - analogRead(PIN_POT2);
    raw[2] = 4095 - analogRead(PIN_POT3);
    raw[3] = 4095 - analogRead(PIN_POT4);
    bool potMoved = false;
    for (int i = 0; i < 4; i++) {
      if (abs(raw[i] - lastFxPots[i]) > FX_POT_DEADBAND || lastFxPots[i] < 0) {
        float v = raw[i] / 4095.0f;
        switch (i) {
          case 0: updateFxCutoff(v);   break;
          case 1: updateFxDelay(v);    break;
          case 2: updateFxFeedback(v); break;
          case 3: updateFxVerbMix(v);  break;
        }
        lastFxPots[i] = raw[i];
        potMoved = true;
      }
    }
    if (potMoved) registerUiActivity();
  }
  
  bool b=digitalRead(PIN_BTN);
  uint32_t btnNow = millis();
  if(b!=btnPrev && (btnNow - btnLastChange) >= DEBOUNCE_MS){
    btnLastChange = btnNow;
    if(b==LOW) {
      btnDownAt=btnNow;
      lastUiActivityMs = btnNow;
      if(screenAsleep){ wakeScreen(); btnWakeConsumed = true; }  // this press only wakes
    } else {
      uint32_t held = btnNow - btnDownAt;
      if(btnWakeConsumed) {
        btnWakeConsumed = false;  // swallow the wake press — no page/long action
      } else if(held >= LONG_MS){
        rst=true; rstUntil=btnNow+8;  // long press = reset pulse (all pages)
      }
      else { gOledPage = (gOledPage + 1) % 2; }  // short press = toggle STATUS/FX
      lastUiActivityMs = btnNow;
    }
    btnPrev=b;
  }
  uint32_t now=millis();
  uint32_t nowUs=micros();
  if(clkUntil && (int32_t)(now - clkUntil) >= 0){ clk=false; clkUntil=0; }
  if(rstUntil && (int32_t)(now - rstUntil) >= 0){ rst=false; rstUntil=0; }
  for (uint8_t i=0;i<DRUM_COUNT;i++){
    uint32_t untilUs = drumUntilUs[i];
    if (untilUs && (int32_t)(nowUs - untilUs) >= 0) {
      drumTrig[i]=false;
      drumUntilUs[i]=0;
    }
  }
  
  // Gate outputs (directly on Teensy pins) — bridge active on every page
  GATE_WRITE(PIN_CLOCK, clk); GATE_WRITE(PIN_RESET, rst);
  GATE_WRITE(PIN_GATE1, gate1); GATE_WRITE(PIN_GATE2, gate2);

  // CV outputs: pitch and mod for channels 1-4
  if(dirtyPitch1){ mcp4822_write(PIN_CS_DAC1, CH_B, pitchVolt_to_code_ch(0, v1.pitchHeldV)); dirtyPitch1=false; }
  if(dirtyPitch2){ mcp4822_write(PIN_CS_DAC2, CH_B, pitchVolt_to_code_ch(1, v2.pitchHeldV)); dirtyPitch2=false; }
  if(dirtyPitch3){ mcp4822_write_expander(1, EXP_PITCH3_CH_IDX, pitchVolt_to_code_ch(2, v3.pitchHeldV)); dirtyPitch3=false; }
  if(dirtyPitch4){ mcp4822_write_expander(1, EXP_PITCH4_CH_IDX, pitchVolt_to_code_ch(3, v4.pitchHeldV)); dirtyPitch4=false; }
  if(dirtyMod1){ mcp4822_write(PIN_CS_DAC1, CH_A, modVolt_to_code_ch(0, v1.modV)); dirtyMod1=false; }
  if(dirtyMod2){ mcp4822_write(PIN_CS_DAC2, CH_A, modVolt_to_code_ch(1, v2.modV)); dirtyMod2=false; }
  if(dirtyMod3){ mcp4822_write_expander(0, EXP_MOD3_CH_IDX, modVolt_to_code_ch(2, v3.modV)); dirtyMod3=false; }
  if(dirtyMod4){ mcp4822_write_expander(0, EXP_MOD4_CH_IDX, modVolt_to_code_ch(3, v4.modV)); dirtyMod4=false; }
  
  if (now - lastBeat >= 1000) { lastBeat = now; digitalToggle(LED_BUILTIN); }
  // Combined expander image update: gates + drums (drums work in both modes)
  {
    uint8_t img = expanderImage(); uint8_t newImg = img;
    
    // Gates 3-4 from expander
    if (gate3) newImg &= ~(1u<<ExpanderBits::V1_GATE); else newImg |= (1u<<ExpanderBits::V1_GATE);
    if (gate4) newImg &= ~(1u<<ExpanderBits::V2_GATE); else newImg |= (1u<<ExpanderBits::V2_GATE);
    
    // Drum outputs (Q2-Q5) - work in BOTH modes
    uint8_t drumsMask=(1u<<ExpanderBits::DRUM1)|(1u<<ExpanderBits::DRUM2)|(1u<<ExpanderBits::DRUM3)|(1u<<ExpanderBits::DRUM4);
    newImg |= drumsMask;  // All off by default
    for(uint8_t i=0;i<DRUM_COUNT;i++){
      if(drumTrig[i]) newImg &= ~(1u<<(ExpanderBits::DRUM1+i));  // Active = LOW
    }
    
    newImg |= (1u<<ExpanderBits::DAC1_CS) | (1u<<ExpanderBits::DAC2_CS);
    if(newImg!=img){ expanderWrite(newImg); }
  }
  
  // Power the screen down after a spell of no UI activity.
  if (SCREEN_SLEEP_MS && !screenAsleep && (now - lastUiActivityMs >= SCREEN_SLEEP_MS)) {
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
    screenAsleep = true;
  }

  // OLED update (partial, rate-limited) — skipped entirely while asleep.
  if (!screenAsleep && now - lastOledPaintMs >= OLED_FPS_MS) {
    
    // Build row strings based on current page/mode
    if(gOledPage == 0) {
      // Page 0: STATUS — all four channels on one screen. Rows are exactly 21
      // chars (the display limit): four signed 5-char voltages pack into 20
      // with the sign as separator, giving aligned columns across P and M.
      // Gate slots: 2 chars per channel — the note name while the gate is on
      // ("C#", "A-"), "--" when off. "CLK" appears while MIDI clock is arriving.
      static const char kNoteL[12] = {'C','C','D','D','E','F','F','G','G','A','A','B'};
      static const char kNoteS[12] = {'-','#','-','#','-','-','#','-','#','-','#','-'};
      Voice* vs[4] = { &v1, &v2, &v3, &v4 };
      bool gs[4] = { gate1, gate2, gate3, gate4 };
      char slot[8];
      for (int i = 0; i < 4; i++) {
        if (gs[i] && vs[i]->note >= 0) {
          uint8_t pc = vs[i]->note % 12;
          slot[2*i] = kNoteL[pc]; slot[2*i+1] = kNoteS[pc];
        } else {
          slot[2*i] = '-'; slot[2*i+1] = '-';
        }
      }
      char d[4];
      for (int i = 0; i < 4; i++)
        d[i] = (drumSeenMs[i] && now - drumSeenMs[i] <= DRUM_DISP_MS) ? '#' : '-';
      bool clkActive = lastClockTickMs && (now - lastClockTickMs <= 500);
      snprintf(lineBuf,sizeof(lineBuf),"G:%c%c%c%c%c%c%c%c D:%c%c%c%c %s",
               slot[0],slot[1],slot[2],slot[3],slot[4],slot[5],slot[6],slot[7],
               d[0], d[1], d[2], d[3], clkActive ? "CLK" : "");
      updateOledRow(0, lineBuf);

      float vP1 = teensy_move_calib::PITCH_M[0]*pitchVolt_to_code_ch(0, v1.pitchHeldV) + teensy_move_calib::PITCH_C[0];
      float vP2 = teensy_move_calib::PITCH_M[1]*pitchVolt_to_code_ch(1, v2.pitchHeldV) + teensy_move_calib::PITCH_C[1];
      float vP3 = teensy_move_calib::PITCH_M[2]*pitchVolt_to_code_ch(2, v3.pitchHeldV) + teensy_move_calib::PITCH_C[2];
      float vP4 = teensy_move_calib::PITCH_M[3]*pitchVolt_to_code_ch(3, v4.pitchHeldV) + teensy_move_calib::PITCH_C[3];
      snprintf(lineBuf,sizeof(lineBuf),"P%+5.2f%+5.2f%+5.2f%+5.2f", vP1, vP2, vP3, vP4);
      updateOledRow(1, lineBuf);

      snprintf(lineBuf,sizeof(lineBuf),"M%+5.2f%+5.2f%+5.2f%+5.2f", v1.modV, v2.modV, v3.modV, v4.modV);
      updateOledRow(2, lineBuf);

      // Row 3, by priority: mod-pot src feedback while sweeping (V=velocity,
      // C=CC#42, P=pot; '~' = pot not yet caught), else last MIDI event, else
      // a usage hint.
      if (now - lastModPotMs <= 2000) {
        const char srcCh[3] = {'V', 'C', 'P'};
        char s[4], c[4];
        for (int i = 0; i < 4; i++) {
          s[i] = srcCh[modSource[i]];
          c[i] = modPotCaught[i] ? ' ' : '~';
        }
        snprintf(lineBuf,sizeof(lineBuf),"src %c%c %c%c %c%c %c%c",
                 s[0], c[0], s[1], c[1], s[2], c[2], s[3], c[3]);
      } else if (now - lastMidiMs <= 1000) {
        snprintf(lineBuf,sizeof(lineBuf),"MIDI ch:%2u n:%3u v:%3u", lastMidiCh, lastMidiNote, lastMidiVel);
      } else {
        snprintf(lineBuf,sizeof(lineBuf),"ch1-4:CV ch10:Drum");
      }
      updateOledRow(3, lineBuf);

    } else if(gOledPage == 1) {
      // Page 1: FX MODE - stereo Filter -> Delay -> Reverb on the passthrough
      snprintf(lineBuf,sizeof(lineBuf),"FX  Filt>Dly>Verb");
      updateOledRow(0, lineBuf);

      float cutHz = 80.0f * powf(150.0f, fxCutoff);
      snprintf(lineBuf,sizeof(lineBuf),"Cut:%5.0fHz Dly:%3.0f", cutHz, fxDelayMs);
      updateOledRow(1, lineBuf);

      snprintf(lineBuf,sizeof(lineBuf),"Fb:%2.0f%% Verb:%2.0f%%", fxFeedback / 0.85f * 100.0f, fxVerbMix * 100.0f);
      updateOledRow(2, lineBuf);

      snprintf(lineBuf,sizeof(lineBuf),"P1cut P2dly P3fb P4vb");
      updateOledRow(3, lineBuf);
    }
    
    // Only do full refresh if any row changed
    bool anyDirty = oledRowDirty[0] || oledRowDirty[1] || oledRowDirty[2] || oledRowDirty[3];
    if (anyDirty) {
      oled.clearDisplay();
      for (uint8_t r = 0; r < 4; r++) {
        oled.setCursor(0, r * 8);
        oled.print(oledRowCache[r]);
        oledRowDirty[r] = false;
      }
      oled.display();
    }
    lastOledPaintMs = now;
  }
}
