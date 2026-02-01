// Teensy Move V2 — Optimized for rock-solid MIDI timing
// Changes from V1:
// - Reduced OLED refresh rate (150ms vs 80ms)
// - Partial OLED updates (only changed rows)
// - Loop timing diagnostics available
// - Two modes: MIDI-to-CV and Chord mode
// - Drums (ch10) work in both modes

#include <Arduino.h>
#include <SPI.h>
#include <CrashReport.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Audio.h>
#include "spi_bus.h"
#include "teensy-move-v2/pins.h"
#include "teensy-move-v2/calib_static.h"
#include "teensy-move-v2/chord_library.h"

#define OLED_W 128
#define OLED_H 32
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

// ============================================================================
// AUDIO OBJECTS — Simple line passthrough
// ============================================================================
AudioInputI2S           i2sIn;
AudioOutputI2S          i2sOut;
AudioOutputUSB          usbOut;
AudioControlSGTL5000    sgtl5000;

// Audio connections — Direct passthrough
AudioConnection         pcOutL(i2sIn, 0, i2sOut, 0);
AudioConnection         pcOutR(i2sIn, 1, i2sOut, 1);
AudioConnection         pcUsbL(i2sIn, 0, usbOut, 0);
AudioConnection         pcUsbR(i2sIn, 1, usbOut, 1);

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

// Chord mode constants
static const uint8_t CHORD_MIDI_CH = 6;  // MIDI channel for chord input
static const uint8_t CHORD_VOICE_COUNT = 4;

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

// Calibration
const float kPitchSlope  = 5.0f * (20.0f / (22.0f + 20.0f));
const float kPitchOffset = -(39.0f/10.0f) * 0.750f;
const float kModSlope    = 5.0f * (20.0f / (8.2f + 20.0f));
const float kModOffset   = -(20.0f/8.2f) * 2.050f;

static inline uint16_t pitchVolt_to_code(float vOut){
  float vDac=(vOut-kPitchOffset)/kPitchSlope;
  float code=vDac*(4095.0f/4.096f);
  if(code<0)code=0; else if(code>4095)code=4095;
  return (uint16_t)(code+0.5f);
}
static inline uint16_t modVolt_to_code(float vOut){
  float vDac=(vOut-kModOffset)/kModSlope;
  float code=vDac*(4095.0f/4.096f);
  if(code<0)code=0; else if(code>4095)code=4095;
  return (uint16_t)(code+0.5f);
}

// Per-channel calibration wrappers using static calibration data
static inline uint16_t pitchVolt_to_code_ch(uint8_t ch, float vOut){
  return teensy_move_calib::pitchVoltsToCode(ch, vOut);
}
static inline uint16_t modVolt_to_code_ch(uint8_t ch, float vOut){
  return teensy_move_calib::modVoltsToCode(ch, vOut);
}

struct Voice { int8_t note=-1; float bend=0, modV=0, pitchHeldV=0, calib=0; };
static Voice v1, v2, v3, v4;
static inline float midiNote_to_volts(int note){ return (note-36)/12.0f; }
static inline void updatePitch(Voice& v){ float base=midiNote_to_volts(v.note<0?36:v.note); v.pitchHeldV = base + v.bend/12.0f + v.calib; }

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
static volatile bool drumDirty = false;

// Debug
static volatile uint8_t lastMidiCh=0, lastMidiNote=0, lastMidiVel=0; static volatile uint32_t lastMidiMs=0;

// Button/OLED
static uint32_t btnDownAt=0; static bool btnPrev=HIGH; const uint16_t LONG_MS=600; static uint32_t lastBeat=0;
static uint32_t lastOledPaintMs=0;
static const uint32_t OLED_FPS_MS=150;  // V2: Slower refresh (was 80ms) — reduces blocking
static inline void drawRow(uint8_t row,const char* s){ oled.setCursor(0,row*8); oled.print(s); }
static char lineBuf[64];
static uint8_t gOledPage = 0; // 0 = CH1-2, 1 = CH3-4, 2 = CHORD, 3 = SYNTH

// V2: OLED row cache for partial updates
static char oledRowCache[4][22] = {"","","",""};  // 21 chars max per row + null
static bool oledRowDirty[4] = {true, true, true, true};

// V2: Loop timing diagnostics
static uint32_t loopMaxUs = 0;
static uint32_t loopAvgUs = 0;
static uint32_t loopCount = 0;
static const uint32_t LOOP_STATS_INTERVAL_MS = 5000;
static uint32_t lastLoopStatsMs = 0;

// ============================================================================
// CHORD MODE STATE
// ============================================================================
static uint8_t chordRootNote = 0;        // 0=C, 1=C#, ... 11=B (from POT1)
static uint8_t chordCategory = 0;        // Category index (from POT2)
static uint8_t chordProgression = 0;     // Progression index within category (from POT3)
static VoicingType chordVoicing = VOICING_ROOT;  // Voicing type (from POT4)

// Chord output state
static volatile float chordPitchV[4] = {0, 0, 0, 0};   // Pitch voltages for chord voices
static volatile bool chordGate[4] = {false, false, false, false};
static volatile bool chordDirty = true;  // Flag to update chord DACs
static volatile int8_t chordHeldNote = -1;  // Currently held chord trigger note
static volatile uint8_t chordCurrentIdx = 0;  // Current chord index (0-7) being played

// Last pot readings for change detection
static uint16_t lastPotRaw[4] = {0, 0, 0, 0};
static const uint16_t POT_DEADBAND = 30;  // Ignore small changes

// Note names for display
static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Current chord name for display (updated when chord triggered)
static char chordNameBuf[8] = "---";

// ============================================================================
// CHORD HELPERS
// ============================================================================

// Detect chord type from intervals and build chord name
static void buildChordName(const int8_t* intervals, uint8_t rootNote) {
    // The intervals are absolute semitones from key root
    // First, normalize all to pitch classes (0-11)
    int8_t pc[4];
    for (int i = 0; i < 4; i++) {
        int v = intervals[i] % 12;
        if (v < 0) v += 12;
        pc[i] = v;
    }
    
    // Find unique pitch classes and sort them
    int8_t unique[4];
    int numUnique = 0;
    for (int i = 0; i < 4; i++) {
        bool found = false;
        for (int j = 0; j < numUnique; j++) {
            if (unique[j] == pc[i]) { found = true; break; }
        }
        if (!found) unique[numUnique++] = pc[i];
    }
    // Sort unique pitch classes
    for (int i = 0; i < numUnique - 1; i++) {
        for (int j = i + 1; j < numUnique; j++) {
            if (unique[i] > unique[j]) { int8_t t = unique[i]; unique[i] = unique[j]; unique[j] = t; }
        }
    }
    
    // The chord root is the lowest absolute interval's pitch class
    int8_t lowestInterval = intervals[0];
    for (int i = 1; i < 4; i++) {
        if (intervals[i] < lowestInterval) lowestInterval = intervals[i];
    }
    int chordRootPC = lowestInterval % 12;
    if (chordRootPC < 0) chordRootPC += 12;
    
    // Calculate the actual note name for the chord root
    int chordRootNote = ((int)rootNote + chordRootPC) % 12;
    
    // Build interval set relative to chord root
    bool has[12] = {false};
    for (int i = 0; i < numUnique; i++) {
        int rel = (unique[i] - chordRootPC + 12) % 12;
        has[rel] = true;
    }
    
    // Detect chord quality based on which intervals are present
    // has[0] = root, has[3] = m3, has[4] = M3, has[6] = dim5, has[7] = P5,
    // has[8] = aug5, has[10] = m7, has[11] = M7, has[2] = 2/9, has[5] = 4/11
    const char* suffix = "";
    
    bool hasM3 = has[4];
    bool hasm3 = has[3];
    bool hasP5 = has[7];
    bool hasd5 = has[6];
    bool hasA5 = has[8];
    bool hasM7 = has[11];
    bool hasm7 = has[10];
    bool has4  = has[5];
    bool has2  = has[2];
    
    if (hasM3 && hasP5 && hasM7) suffix = "M7";
    else if (hasM3 && hasP5 && hasm7) suffix = "7";
    else if (hasm3 && hasP5 && hasm7) suffix = "m7";
    else if (hasm3 && hasP5 && hasM7) suffix = "mM7";
    else if (hasm3 && hasd5 && hasm7) suffix = "m7b5";
    else if (hasm3 && hasd5 && (has[9])) suffix = "o7";  // dim7 has bb7 (9 semitones)
    else if (hasM3 && hasA5) suffix = "+";
    else if (hasm3 && hasd5) suffix = "dim";
    else if (has4 && hasP5 && !hasM3 && !hasm3) suffix = "sus4";
    else if (has2 && hasP5 && !hasM3 && !hasm3) suffix = "sus2";
    else if (hasm3 && hasP5) suffix = "m";
    else if (hasM3 && hasP5) suffix = "";  // Major triad
    else if (hasM3) suffix = "";  // Major (no 5th)
    else if (hasm3) suffix = "m";  // Minor (no 5th)
    else suffix = "";  // Default - just show root
    
    snprintf(chordNameBuf, sizeof(chordNameBuf), "%s%s", kNoteNames[chordRootNote], suffix);
}

// Convert semitone interval to voltage (1V/oct, 0V = C3 = MIDI 48)
static inline float semitoneToVolt(int8_t semitone, uint8_t rootNote, uint8_t baseOctave) {
    // baseOctave: the octave of the played note (0-based from MIDI note)
    // rootNote: 0-11 for C-B
    // semitone: interval from the chord root
    int totalSemitones = (int)rootNote + (int)semitone + (baseOctave - 3) * 12;
    return totalSemitones / 12.0f;  // 1V per octave
}

// Read pots and update chord parameters
static void updateChordParams() {
    uint16_t raw[4];
    raw[0] = 4095 - analogRead(PIN_POT1);  // Invert: CW = max
    raw[1] = 4095 - analogRead(PIN_POT2);
    raw[2] = 4095 - analogRead(PIN_POT3);
    raw[3] = 4095 - analogRead(PIN_POT4);
    
    // Check for significant changes
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        if (abs((int)raw[i] - (int)lastPotRaw[i]) > POT_DEADBAND) {
            lastPotRaw[i] = raw[i];
            changed = true;
        }
    }
    
    if (!changed && chordHeldNote < 0) return;  // No change and no held note
    
    // POT1: Root note (0-11 mapped from 0-4095)
    uint8_t newRoot = (raw[0] * 12) / 4096;
    if (newRoot > 11) newRoot = 11;
    
    // POT2: Category
    uint8_t newCat = (raw[1] * kNumCategories) / 4096;
    if (newCat >= kNumCategories) newCat = kNumCategories - 1;
    
    // POT3: Progression within category
    uint8_t numProgs = kChordCategories[newCat].count;
    uint8_t newProg = (raw[2] * numProgs) / 4096;
    if (newProg >= numProgs) newProg = numProgs - 1;
    
    // POT4: Voicing
    uint8_t newVoice = (raw[3] * VOICING_COUNT) / 4096;
    if (newVoice >= VOICING_COUNT) newVoice = VOICING_COUNT - 1;
    
    // Update if changed
    if (newRoot != chordRootNote || newCat != chordCategory || 
        newProg != chordProgression || newVoice != (uint8_t)chordVoicing) {
        chordRootNote = newRoot;
        chordCategory = newCat;
        chordProgression = newProg;
        chordVoicing = (VoicingType)newVoice;
        
        // If a chord is held, update the output
        if (chordHeldNote >= 0) {
            chordDirty = true;
        }
    }
}

// Trigger a chord from a MIDI note
static void triggerChord(uint8_t midiNote) {
    chordHeldNote = midiNote;
    
    // Get chord index from note
    uint8_t chordIdx = noteToChordIndex(midiNote);
    chordCurrentIdx = chordIdx;  // Store for display
    
    // Get the progression
    const ChordProgression& prog = kChordCategories[chordCategory].progressions[chordProgression];
    
    // Copy intervals and apply voicing
    int8_t intervals[4];
    for (int i = 0; i < 4; i++) {
        intervals[i] = prog.chords[chordIdx].intervals[i];
    }
    
    // Build chord name before voicing (for display)
    buildChordName(prog.chords[chordIdx].intervals, chordRootNote);
    
    applyVoicing(intervals, chordVoicing);
    
    // Determine base octave from the played note
    uint8_t baseOctave = midiNote / 12;
    
    // Convert to voltages
    for (int i = 0; i < 4; i++) {
        chordPitchV[i] = semitoneToVolt(intervals[i], chordRootNote, baseOctave);
        chordGate[i] = true;
    }
    
    chordDirty = true;
}

// Release chord
static void releaseChord(uint8_t midiNote) {
    if (chordHeldNote == midiNote) {
        chordHeldNote = -1;
        for (int i = 0; i < 4; i++) {
            chordGate[i] = false;
        }
        chordDirty = true;
    }
}

// Write chord pitches to Pitch DACs (using the 4 pitch outputs in chord mode)
static void writeChordPitchesToPitchOutputs() {
    if (!chordDirty) return;
    
    // Pitch1 = DAC1.B, Pitch2 = DAC2.B, Pitch3 = Exp.DAC2, Pitch4 = Exp.DAC2
    mcp4822_write(PIN_CS_DAC1, CH_B, pitchVolt_to_code_ch(0, chordPitchV[0]));
    mcp4822_write(PIN_CS_DAC2, CH_B, pitchVolt_to_code_ch(1, chordPitchV[1]));
    mcp4822_write_expander(1, EXP_PITCH3_CH_IDX, pitchVolt_to_code_ch(2, chordPitchV[2]));
    mcp4822_write_expander(1, EXP_PITCH4_CH_IDX, pitchVolt_to_code_ch(3, chordPitchV[3]));
    
    chordDirty = false;
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
  if(b!=btnPrev){
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

// MIDI callbacks - behavior depends on current mode (gOledPage)
// Pages 0-1: CV mode (ch1-4 CV/Gate with velocity to mod, ch10 drums)
// Page 2: Chord mode (ch6 triggers chords on pitch/gate outputs, ch10 drums still work)
void onNoteOn(byte ch, byte note, byte vel){
  lastMidiCh=ch; lastMidiNote=note; lastMidiVel=vel; lastMidiMs=millis();
  if(!vel){ onNoteOff(ch,note,0); return; }
  
  // Drums always work (ch10) in both modes
  if(ch==10){
    int idx=(int)note-(int)DRUM_BASE_NOTE;
    if(idx>=0 && idx<(int)DRUM_COUNT){
      drumTrig[idx]=true;
      drumUntilUs[idx]=micros()+DRUM_TRIG_US[idx];
      drumDirty=true;
    }
    return;
  }
  
  // Mode-based MIDI handling
  if(gOledPage <= 1) {
    // CV MODE: Channels 1-4 CV/Gate with velocity to mod outputs
    float modV = (vel / 127.0f) * 5.0f;  // 0-5V velocity
    if(ch==1){ 
      v1.note=note; v1.modV=modV; updatePitch(v1); 
      gate1=true; dirtyPitch1=true; dirtyMod1=true; 
    }
    else if(ch==2){ 
      v2.note=note; v2.modV=modV; updatePitch(v2); 
      gate2=true; dirtyPitch2=true; dirtyMod2=true; 
    }
    else if(ch==3){ 
      v3.note=note; v3.modV=modV; updatePitch(v3); 
      gate3=true; dirtyPitch3=true; dirtyMod3=true; 
    }
    else if(ch==4){ 
      v4.note=note; v4.modV=modV; updatePitch(v4); 
      gate4=true; dirtyPitch4=true; dirtyMod4=true; 
    }
  } else if(gOledPage == 2) {
    // CHORD MODE: Channel 6 triggers chords on pitch/gate outputs
    if(ch==CHORD_MIDI_CH){
      triggerChord(note);
    }
  }
}
void onNoteOff(byte ch, byte note, byte){
  lastMidiCh=ch; lastMidiNote=note; lastMidiVel=0; lastMidiMs=millis();
  
  // Mode-based MIDI handling
  if(gOledPage <= 1) {
    // CV MODE
    if(ch==1 && v1.note==note){ gate1=false; v1.note=-1; dirtyPitch1=true; }
    else if(ch==2 && v2.note==note){ gate2=false; v2.note=-1; dirtyPitch2=true; }
    else if(ch==3 && v3.note==note){ gate3=false; v3.note=-1; dirtyPitch3=true; }
    else if(ch==4 && v4.note==note){ gate4=false; v4.note=-1; dirtyPitch4=true; }
  } else if(gOledPage == 2) {
    // CHORD MODE
    if(ch==CHORD_MIDI_CH){
      releaseChord(note);
    }
  }
}
void onPitchBend(byte ch, int value){
  float semis=2.0f*(float)(value-8192)/8192.0f;
  if(ch==1){ v1.bend=semis; if(v1.note>=0){ updatePitch(v1); dirtyPitch1=true; } }
  else if(ch==2){ v2.bend=semis; if(v2.note>=0){ updatePitch(v2); dirtyPitch2=true; } }
  else if(ch==3){ v3.bend=semis; if(v3.note>=0){ updatePitch(v3); dirtyPitch3=true; } }
  else if(ch==4){ v4.bend=semis; if(v4.note>=0){ updatePitch(v4); dirtyPitch4=true; } }
}
void onControlChange(byte, byte, byte){ }

// MIDI clock
static volatile uint32_t midiTickCount=0; static const uint8_t PPQN=24, BEAT_DIV=24;
static void resetMidiClockCounter(){ midiTickCount=0; }
void onStart(){ rst=true; rstUntil=millis()+8; resetMidiClockCounter(); }
void onStop(){ gate1=false; gate2=false; clk=false; rst=false; resetMidiClockCounter(); }
void onContinue(){ resetMidiClockCounter(); }
void onClock(){ midiTickCount++; if(midiTickCount % BEAT_DIV == 0){ clk=true; clkUntil=millis()+PULSE_MS; } }

// V2: Helper to update OLED row only if changed
static void updateOledRow(uint8_t row, const char* newText) {
  if (strncmp(oledRowCache[row], newText, sizeof(oledRowCache[row])-1) != 0) {
    strncpy(oledRowCache[row], newText, sizeof(oledRowCache[row])-1);
    oledRowCache[row][sizeof(oledRowCache[row])-1] = '\0';
    oledRowDirty[row] = true;
  }
}

// Setup
void setup(){
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
  mcp4822_write(PIN_CS_DAC1, CH_A, modVolt_to_code(0.0f));
  mcp4822_write(PIN_CS_DAC1, CH_B, pitchVolt_to_code(0.0f));
  mcp4822_write(PIN_CS_DAC2, CH_A, modVolt_to_code(0.0f));
  mcp4822_write(PIN_CS_DAC2, CH_B, pitchVolt_to_code(0.0f));
  AudioMemory(16);
  sgtl5000.enable();
  sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000.adcHighPassFilterDisable();
  sgtl5000.lineInLevel(6);
  sgtl5000.lineOutLevel(29);
  sgtl5000.volume(0.8f);
  Wire.begin();
  Wire.setClock(400000);  // V2: Ensure 400kHz I2C for faster OLED
  if(oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)){
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
  
  // V2: Initialize loop timing
  lastLoopStatsMs = millis();
}

// Loop
void loop(){
  uint32_t loopStartUs = micros();  // V2: timing
  
  // Diagnostics mode
  if(gDiagMode){ usbMIDI.read(); diag_tick(); diag_render(); delay(10); return; }
  
  usbMIDI.read();
  
  // Read pots for chord parameters when in chord mode
  if (gOledPage == 2) {
    updateChordParams();    // Chord page: update chord parameters
  }
  
  bool b=digitalRead(PIN_BTN);
  if(b!=btnPrev){
    if(b==LOW) btnDownAt=millis();
    else {
      uint32_t held = millis() - btnDownAt;
      if(held >= LONG_MS){ rst=true; rstUntil=millis()+8; }  // long press = reset
      else { gOledPage = (gOledPage + 1) % 3; }  // short press = toggle page (3 pages: 0,1=CV, 2=Chord)
    }
    btnPrev=b;
  }
  uint32_t now=millis();
  uint32_t nowUs=micros();
  if(clkUntil && (int32_t)(now-(int32_t)clkUntil)>=0){ clk=false; clkUntil=0; }
  if(rstUntil && (int32_t)(now-(int32_t)rstUntil)>=0){ rst=false; rstUntil=0; }
  for (uint8_t i=0;i<DRUM_COUNT;i++){
    uint32_t untilUs = drumUntilUs[i];
    if (untilUs && (int32_t)(nowUs - untilUs) >= 0) {
      drumTrig[i]=false;
      drumUntilUs[i]=0;
      drumDirty=true;
    }
  }
  
  // Mode-dependent gate outputs for gates 1-2 (directly on Teensy pins)
  GATE_WRITE(PIN_CLOCK, clk); GATE_WRITE(PIN_RESET, rst);
  if(gOledPage == 2) {
    // CHORD MODE: Use gate1/2 for chord voice 1/2 gates
    GATE_WRITE(PIN_GATE1, chordGate[0]); GATE_WRITE(PIN_GATE2, chordGate[1]);
  } else {
    // CV MODE: Normal gate1/2
    GATE_WRITE(PIN_GATE1, gate1); GATE_WRITE(PIN_GATE2, gate2);
  }
  
  // Mode-based CV outputs
  if(gOledPage <= 1) {
    // CV MODE: Write pitch and mod (velocity) CVs for channels 1-4
    if(dirtyPitch1){ mcp4822_write(PIN_CS_DAC1, CH_B, pitchVolt_to_code_ch(0, v1.pitchHeldV)); dirtyPitch1=false; }
    if(dirtyPitch2){ mcp4822_write(PIN_CS_DAC2, CH_B, pitchVolt_to_code_ch(1, v2.pitchHeldV)); dirtyPitch2=false; }
    if(dirtyPitch3){ mcp4822_write_expander(1, EXP_PITCH3_CH_IDX, pitchVolt_to_code_ch(2, v3.pitchHeldV)); dirtyPitch3=false; }
    if(dirtyPitch4){ mcp4822_write_expander(1, EXP_PITCH4_CH_IDX, pitchVolt_to_code_ch(3, v4.pitchHeldV)); dirtyPitch4=false; }
    // Mod outputs = velocity
    if(dirtyMod1){ mcp4822_write(PIN_CS_DAC1, CH_A, modVolt_to_code_ch(0, v1.modV)); dirtyMod1=false; }
    if(dirtyMod2){ mcp4822_write(PIN_CS_DAC2, CH_A, modVolt_to_code_ch(1, v2.modV)); dirtyMod2=false; }
    if(dirtyMod3){ mcp4822_write_expander(0, EXP_MOD3_CH_IDX, modVolt_to_code_ch(2, v3.modV)); dirtyMod3=false; }
    if(dirtyMod4){ mcp4822_write_expander(0, EXP_MOD4_CH_IDX, modVolt_to_code_ch(3, v4.modV)); dirtyMod4=false; }
  } else {
    // CHORD MODE: Write chord pitches to pitch outputs
    writeChordPitchesToPitchOutputs();
  }
  
  if (now - lastBeat >= 1000) { lastBeat = now; digitalToggle(LED_BUILTIN); }
  // Combined expander image update: gates + drums (drums work in both modes)
  {
    uint8_t img = expanderImage(); uint8_t newImg = img;
    
    // Gates 3-4 from expander - mode dependent
    if(gOledPage == 2) {
      // CHORD MODE: Use gate3/4 for chord voice 3/4 gates
      if (chordGate[2]) newImg &= ~(1u<<ExpanderBits::V1_GATE); else newImg |= (1u<<ExpanderBits::V1_GATE);
      if (chordGate[3]) newImg &= ~(1u<<ExpanderBits::V2_GATE); else newImg |= (1u<<ExpanderBits::V2_GATE);
    } else {
      // CV MODE: Normal gate3/4
      if (gate3) newImg &= ~(1u<<ExpanderBits::V1_GATE); else newImg |= (1u<<ExpanderBits::V1_GATE);
      if (gate4) newImg &= ~(1u<<ExpanderBits::V2_GATE); else newImg |= (1u<<ExpanderBits::V2_GATE);
    }
    
    // Drum outputs (Q2-Q5) - work in BOTH modes
    uint8_t drumsMask=(1u<<ExpanderBits::DRUM1)|(1u<<ExpanderBits::DRUM2)|(1u<<ExpanderBits::DRUM3)|(1u<<ExpanderBits::DRUM4);
    newImg |= drumsMask;  // All off by default
    for(uint8_t i=0;i<DRUM_COUNT;i++){
      if(drumTrig[i]) newImg &= ~(1u<<(ExpanderBits::DRUM1+i));  // Active = LOW
    }
    
    newImg |= (1u<<ExpanderBits::DAC1_CS) | (1u<<ExpanderBits::DAC2_CS);
    if(newImg!=img){ expanderWrite(newImg); drumDirty=false; }
  }
  
  // V2: OLED update with reduced impact
  if (now - lastOledPaintMs >= OLED_FPS_MS) {
    
    // Build row strings based on current page/mode
    if(gOledPage == 0) {
      // Page 0: CV MODE - Channels 1-2
      snprintf(lineBuf,sizeof(lineBuf),"CV MODE  G1:%c G2:%c", gate1?'#':'-', gate2?'#':'-');
      updateOledRow(0, lineBuf);
      
      float vP1 = teensy_move_calib::PITCH_M[0]*pitchVolt_to_code_ch(0, v1.pitchHeldV) + teensy_move_calib::PITCH_C[0];
      float vP2 = teensy_move_calib::PITCH_M[1]*pitchVolt_to_code_ch(1, v2.pitchHeldV) + teensy_move_calib::PITCH_C[1];
      snprintf(lineBuf,sizeof(lineBuf),"P1:%+.2fV  P2:%+.2fV", vP1, vP2);
      updateOledRow(1, lineBuf);
      
      // Show drum triggers status
      char d1=drumTrig[0]?'#':'-', d2=drumTrig[1]?'#':'-', d3=drumTrig[2]?'#':'-', d4=drumTrig[3]?'#':'-';
      snprintf(lineBuf,sizeof(lineBuf),"Drums:%c%c%c%c CLK:%c", d1, d2, d3, d4, clk?'#':'-');
      updateOledRow(2, lineBuf);
      
      // Row 3: MIDI info
      if (now - lastMidiMs <= 1000) {
        snprintf(lineBuf,sizeof(lineBuf),"MIDI ch:%2u n:%3u v:%3u", lastMidiCh, lastMidiNote, lastMidiVel);
      } else {
        snprintf(lineBuf,sizeof(lineBuf),"ch1-4:CV ch10:Drum");
      }
      updateOledRow(3, lineBuf);
      
    } else if(gOledPage == 1) {
      // Page 1: CV MODE - Channels 3-4
      snprintf(lineBuf,sizeof(lineBuf),"CV MODE  G3:%c G4:%c", gate3?'#':'-', gate4?'#':'-');
      updateOledRow(0, lineBuf);
      
      float vP3 = teensy_move_calib::PITCH_M[2]*pitchVolt_to_code_ch(2, v3.pitchHeldV) + teensy_move_calib::PITCH_C[2];
      float vP4 = teensy_move_calib::PITCH_M[3]*pitchVolt_to_code_ch(3, v4.pitchHeldV) + teensy_move_calib::PITCH_C[3];
      snprintf(lineBuf,sizeof(lineBuf),"P3:%+.2fV  P4:%+.2fV", vP3, vP4);
      updateOledRow(1, lineBuf);
      
      // Show drum triggers status
      char d1=drumTrig[0]?'#':'-', d2=drumTrig[1]?'#':'-', d3=drumTrig[2]?'#':'-', d4=drumTrig[3]?'#':'-';
      snprintf(lineBuf,sizeof(lineBuf),"Drums:%c%c%c%c RST:%c", d1, d2, d3, d4, rst?'#':'-');
      updateOledRow(2, lineBuf);
      
      // Row 3: MIDI info
      if (now - lastMidiMs <= 1000) {
        snprintf(lineBuf,sizeof(lineBuf),"MIDI ch:%2u n:%3u v:%3u", lastMidiCh, lastMidiNote, lastMidiVel);
      } else {
        snprintf(lineBuf,sizeof(lineBuf),"ch1-4:CV ch10:Drum");
      }
      updateOledRow(3, lineBuf);
      
    } else if(gOledPage == 2) {
      // Page 2: CHORD MODE - chord settings and output voltages
      snprintf(lineBuf,sizeof(lineBuf),"CHORD %s %s P:%d", kNoteNames[chordRootNote], kChordCategories[chordCategory].name, chordProgression+1);
      updateOledRow(0, lineBuf);
      
      // Show voicing and current chord name
      if (chordHeldNote >= 0) {
        snprintf(lineBuf,sizeof(lineBuf),"V:%s -> %s", kVoicingNames[chordVoicing], chordNameBuf);
      } else {
        snprintf(lineBuf,sizeof(lineBuf),"V:%s -> ---", kVoicingNames[chordVoicing]);
      }
      updateOledRow(1, lineBuf);
      
      // Show chord voltages
      snprintf(lineBuf,sizeof(lineBuf),"V:%+.1f %+.1f %+.1f %+.1f", chordPitchV[0], chordPitchV[1], chordPitchV[2], chordPitchV[3]);
      updateOledRow(2, lineBuf);
      
      // Show gates and drums
      char g1=chordGate[0]?'#':'-', g2=chordGate[1]?'#':'-', g3=chordGate[2]?'#':'-', g4=chordGate[3]?'#':'-';
      char d1=drumTrig[0]?'#':'-', d2=drumTrig[1]?'#':'-', d3=drumTrig[2]?'#':'-', d4=drumTrig[3]?'#':'-';
      snprintf(lineBuf,sizeof(lineBuf),"G:%c%c%c%c D:%c%c%c%c", g1, g2, g3, g4, d1, d2, d3, d4);
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
  
  // V2: Loop timing diagnostics (optional serial output)
  uint32_t loopElapsedUs = micros() - loopStartUs;
  if (loopElapsedUs > loopMaxUs) loopMaxUs = loopElapsedUs;
  loopAvgUs = (loopAvgUs * loopCount + loopElapsedUs) / (loopCount + 1);
  loopCount++;
  
  if (now - lastLoopStatsMs >= LOOP_STATS_INTERVAL_MS) {
    // Uncomment for debugging: Serial.printf("Loop: max=%luus avg=%luus\n", loopMaxUs, loopAvgUs);
    loopMaxUs = 0;
    loopAvgUs = 0;
    loopCount = 0;
    lastLoopStatsMs = now;
  }
}
