// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Secret on the Teensy Chaos hardware: Teensy 4.1, Audio Shield (SGTL5000), 128x64 OLED,
// ADS1115 CV inputs, MCP4822 CV outputs. The DSP is libs/chaos_core, a copy of the one
// Secret runs on the Alchemy Lab: twelve models, played through Voice::setPitch() so V/OCT
// plays notes and TAME runs from free chaos to a locked tone. This file is the platform
// layer: panel, jacks, the audio callback, the load governor and the screen.
//
// It replaced the Chaos firmware on 2026-11-11 (Chaos is at tag teensy-chaos-final). The
// jacks keep the jobs they had under Chaos; the pots beside them, bottom to top:
//
//   jack   job                         pot     job
//   RST    GATE                        DEPTH   TAME, none (0) to full (1)
//   CLK    V/OCT                       CHAR    the secondary parameter
//   ASGN   TUNE CV, 1 V/oct like CLK   RATE    TUNE, 27.5-880 Hz, exponential
//   MOD    CHAOS CV                    CHAOS   the bifurcation parameter
//
//   BTN    short press: the next of twelve models (Rossler, Van der Pol, Lorenz, Chua,
//          Duffing, Coupled Rossler, Pendulum, Lorenz-Lu-Chen, Moore-Spiegel, Brusselator,
//          Colpitts, Hindmarsh-Rose). Long press: the ENV page, where CHAOS chooses Drone
//          or Gated, CHAR sets attack + decay and DEPTH sustain + release. RATE stays TUNE
//          on both pages; the other three wait to be turned past their value (pickup).
//   X, Y   the attractor's raw state, as CV; audio L and R carry X and Y.
//   Audio in is not used yet.
//
// In Gated mode a rising edge on GATE re-seeds the attractor, but only from silence, and
// each channel's attack waits for its next zero crossing (Voice does both). Drone ignores
// GATE. The screen goes off after 20 s with nothing touched.

#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "chaos_core/Registry.h"   // ChaosBase, the twelve models, algos[] / N_ALGOS
#include "chaos_core/Voice.h"      // platform-free voice: pitch, TAME, envelope, output
#include "teensy_chaos/pins.h"

using namespace chaos_core;

// ─── ADS1115 (Wire1, 0x48) ────────────────────────────────────────────────────
#define ADS_ADDR     0x48
#define ADS_REG_CONV 0x00
#define ADS_REG_CFG  0x01

static bool adsWriteCfg(uint8_t mux) {
    // OS=1 start, MUX=mux, PGA=001 (±4.096V), MODE=1 single-shot,
    // DR=111 (860 SPS), COMP_QUE=11 disable comparator
    uint16_t cfg = 0x8000 | (uint16_t(mux & 0x7) << 12) | 0x0200 | 0x0100 | 0x00E0 | 0x0003;
    Wire1.beginTransmission(ADS_ADDR);
    Wire1.write(ADS_REG_CFG);
    Wire1.write((uint8_t)(cfg >> 8));
    Wire1.write((uint8_t)(cfg & 0xFF));
    return Wire1.endTransmission() == 0;
}

static bool adsReadConv(int16_t& out) {
    Wire1.beginTransmission(ADS_ADDR);
    Wire1.write(ADS_REG_CONV);
    if (Wire1.endTransmission(false) != 0)      return false;
    if (Wire1.requestFrom((int)ADS_ADDR, 2) != 2) return false;
    uint8_t msb = Wire1.read();
    uint8_t lsb = Wire1.read();
    out = (int16_t)((uint16_t(msb) << 8) | lsb);
    return true;
}

// Wait for the conversion to land, by polling the config register's OS bit
// rather than trusting a fixed delay. 860 SPS is nominally 1.163 ms, but that
// rate comes from the ADS1115's own oscillator and carries a tolerance, so a
// blind 1200 us wait sat only ~3% clear of it — and a conversion that ran even
// slightly long returned the *previous* channel's result instead. That reads as
// one CV input briefly taking another's value: MOD jumping because RST moved.
// Sleep most of the interval, then poll, so the common case costs no more than
// the old delay did.
static bool adsWaitReady(uint32_t timeoutUs) {
    delayMicroseconds(1000);
    uint32_t t0 = micros();
    while ((micros() - t0) < timeoutUs) {
        Wire1.beginTransmission(ADS_ADDR);
        Wire1.write(ADS_REG_CFG);
        if (Wire1.endTransmission(false) != 0)        return false;
        if (Wire1.requestFrom((int)ADS_ADDR, 2) != 2) return false;
        uint8_t msb = Wire1.read(); (void)Wire1.read();
        if (msb & 0x80) return true;   // OS = 1 → conversion complete
    }
    return false;
}

static void adsReadAll(int16_t out[4]) {
    // Hold the last good sample per channel. A dropped I2C read used to surface
    // as 0xFFFF → −1, and −1 reads as roughly +5.5 V on every input: CHAOS slams
    // to its limit, V/OCT jumps five octaves, and GATE crosses its threshold — a
    // spurious note from one bus hiccup. Seeded with the measured 0 V codes so a
    // failure before the first successful read is quiet rather than garbage.
    static int16_t last[4] = { 13236, 13240, 13241, 13241 };
    for (int ch = 0; ch < 4; ch++) {
        if (!adsWriteCfg(0b100 + ch)) continue;
        if (!adsWaitReady(2000))      continue;
        int16_t v;
        if (adsReadConv(v)) last[ch] = v;
    }
    for (int ch = 0; ch < 4; ch++) out[ch] = last[ch];
}

// ─── What the control loop asks the voice to play ─────────────────────────────
struct Params {
    uint8_t model   = 0;
    float   chaos   = 0.0f, charV = 0.0f, hz = 110.0f, tame = 0.0f;
    float   atkMs   = 10.0f, decMs = 200.0f, sustain = 0.5f, relMs = 200.0f;
    bool    gated   = false;   // envelope: Drone (false) or Gated by GATE
    bool    gate    = false;   // the GATE jack, after hysteresis
};

// ─── AudioSecret ──────────────────────────────────────────────────────────────
// An AudioStream around chaos_core::Voice, played as Secret plays it on the Alchemy
// Lab: the control loop posts a whole Params set, and each audio block applies it
// before rendering. A set applied part-way — a new CHAOS against an old CHAR —
// can put a model somewhere neither setting was, and a block is thousands of RK4
// steps, plenty for a runaway nobody asked for.
class AudioSecret : public AudioStream {
public:
    AudioSecret() : AudioStream(0, nullptr) {
        voice_.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
        voice_.setReseedOnGate(true);   // Gated: a gate re-seeds, from silence only
    }

    Voice&       voice()       { return voice_; }
    const Voice& voice() const { return voice_; }

    // From loop(). Interrupts off for the copy, a few dozen bytes: the audio update
    // runs in an interrupt and must never see a half-written set.
    void post(const Params& p) {
        __disable_irq();
        params_ = p;
        __enable_irq();
    }

    void update() override {
        const uint32_t tStart = ARM_DWT_CYCCNT;
        const Params p = params_;   // loop() cannot run while this does
        voice_.setAlgo(algos[p.model < N_ALGOS ? p.model : 0]);   // no-op unless it changed
        voice_.setEnvEnabled(p.gated);
        voice_.setEnvGate(p.gate);
        voice_.setEnvADSR(p.atkMs, p.decMs, p.sustain, p.relMs);
        voice_.setPitch(p.chaos, p.charV, p.hz, p.tame);

        audio_block_t* bL = allocate();
        if (!bL) return;
        audio_block_t* bR = allocate();
        if (!bR) { release(bL); return; }

        // Static rather than automatic: two float blocks is 1 kB, and this runs
        // in the audio interrupt, which does not preempt itself.
        static float l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
        voice_.render(l, r, AUDIO_BLOCK_SAMPLES);
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            bL->data[i] = (int16_t)(l[i] * 32000.0f);
            bR->data[i] = (int16_t)(r[i] * 32000.0f);
        }
        transmit(bL, 0); transmit(bR, 1);
        release(bL); release(bR);
        governLoad(ARM_DWT_CYCCNT - tStart);
    }

private:
    // Load governor. A step cap set too close, or the callback's own work on top,
    // can let a block run past its slice, and that failure latches: update() runs
    // in an interrupt that preempts loop(), so once a block takes longer than a
    // block period the control loop is starved — dead panel, stuck CV — and can
    // never turn the pitch back down. So measure each block here and run fewer
    // steps if it runs long. The pitch goes flat at the extreme instead; '!' by
    // the CPU figure says so. Secret's governor is the same, and for the same reason.
    inline void governLoad(uint32_t cycles) {
        const uint32_t budget = AUDIO_BLOCK_SAMPLES * (F_CPU_ACTUAL / 44100u);
        // Read back from the voice, not mirrored: setAlgo() resets it to 1 because
        // a throttle describes the cost of the model that earned it.
        float sc = voice_.loadScale();
        if (cycles > (budget >> 1) + (budget >> 2)) {          // over 75% of the block
            sc *= 0.85f;                                       // ~4 blocks (12 ms) to halve
            if (sc < 0.02f) sc = 0.02f;
        } else if (cycles < (budget >> 1) && sc < 1.0f) {      // under 50%: creep back
            sc += 0.02f;
            if (sc > 1.0f) sc = 1.0f;
        }
        voice_.setLoadScale(sc);
    }

    Voice  voice_;
    Params params_;
};

// ─── Audio graph ──────────────────────────────────────────────────────────────
AudioSecret          engine;
AudioOutputI2S       audioOut;
AudioControlSGTL5000 codec;
AudioConnection      patchL(engine, 0, audioOut, 0);
AudioConnection      patchR(engine, 1, audioOut, 1);

// ─── OLED ─────────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ─── MCP4822 DAC ──────────────────────────────────────────────────────────────
// Calibrated: Vout = code * 0.002431 − 4.972 (both channels, ±10mV)
// 0V at code ~2044, range −4.97V (code 0) to +4.98V (code 4095)
#define DAC_SCALE  0.002431f
#define DAC_OFFSET 4.972f

void dacWrite(uint8_t ch, uint16_t val) {
    uint16_t frame = (ch ? 0x8000 : 0) | 0x1000 | (val & 0x0FFF);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWriteFast(PIN_CS_DAC, LOW);
    SPI.transfer16(frame);
    digitalWriteFast(PIN_CS_DAC, HIGH);
    SPI.endTransaction();
}

void dacWriteVolts(uint8_t ch, float volts) {
    if (!isfinite(volts)) volts = 0.0f;   // a NaN would pass the clamp and cast badly
    int code = (int)((volts + DAC_OFFSET) / DAC_SCALE);
    dacWrite(ch, (uint16_t)constrain(code, 0, 4095));
}

// ─── Output level ─────────────────────────────────────────────────────────────
// SGTL5000 CHIP_LINE_OUT_VOL, via codec.lineOutLevel(). The field runs 13..31 and
// is an ATTENUATION, so lower is LOUDER: 13 ≈ 3.16 V p-p, 31 ≈ 1.16 V p-p, in
// 0.5 dB steps. Measured under Chaos on a Tiliqua vectorscope (XY, audio L/R
// patched to it): 13 puts the module at the eurorack ±5 V standard, about 64% of
// Tiliqua's ±8.192 V, which carries more headroom than eurorack uses. Don't
// calibrate this to "fills the scope": that would be ±8 V and clip what it feeds.
static constexpr uint8_t LINE_OUT_LEVEL = 13;

// ─── Control tunables ─────────────────────────────────────────────────────────
// Envelope macros, as Secret: AD spans attack + decay, SR sustain level + release.
static constexpr float    ENV_ATK_MIN_MS = 0.5f,  ENV_ATK_MAX_MS = 1000.0f;
static constexpr float    ENV_DEC_MIN_MS = 2.0f,  ENV_DEC_MAX_MS = 2000.0f;
static constexpr float    ENV_REL_MIN_MS = 2.0f,  ENV_REL_MAX_MS = 4000.0f;
static constexpr float    TUNE_MIN_HZ    = 27.5f, TUNE_MAX_HZ    = 880.0f;   // A0 to A5
// CHAOS CV: ±5 V sweeps the knob from its centre to either end, as Secret's J5.
static constexpr float    CHAOS_CV_PER_V = 0.1f;
static constexpr uint16_t BTN_LONG_MS    = 500;    // long press toggles the ENV page
// GATE (the RST jack): lower ADS code = higher eurorack voltage (inverting front end).
static constexpr int16_t  GATE_ON  = 10820;  // ~+1.0 V: gate goes high
static constexpr int16_t  GATE_OFF = 11600;  // ~+0.7 V: gate goes low (hysteresis)

// CV input calibration (measured 2026-04-07; inverting front end: higher V → lower code)
static constexpr float CV_MOD_ZERO  = 13236.0f, CV_MOD_CPV  = 2414.0f;
static constexpr float CV_ASGN_ZERO = 13240.0f, CV_ASGN_CPV = 2424.0f;
static constexpr float CV_CLK_ZERO  = 13241.0f, CV_CLK_CPV  = 2421.0f;

// Exponential pot map: norm 0..1 → [lo, hi] geometrically (musical for times and pitch).
static inline float expoMap(float norm, float lo, float hi) {
    if (norm < 0.0f) norm = 0.0f; else if (norm > 1.0f) norm = 1.0f;
    return lo * powf(hi / lo, norm);
}
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Soft takeover for the ENV page. There CHAOS/CHAR/DEPTH change meaning (Drone or
// Gated / AD / SR), so on a page switch their pots stay inactive until each crosses
// (or matches) the stored value of what it now sets.
struct Pickup3 { bool caught[3] = {true, true, true}; float ref[3] = {0, 0, 0}; };
static void pickup3_arm(Pickup3& pk, float a, float b, float c) {
    pk.ref[0] = a; pk.ref[1] = b; pk.ref[2] = c;
    pk.caught[0] = pk.caught[1] = pk.caught[2] = false;
}
static bool pickup3_update(Pickup3& pk, int i, float pot, float target) {
    if (pk.caught[i]) return true;
    if (fabsf(pot - target) <= 0.02f) { pk.caught[i] = true; return true; }
    if ((pk.ref[i] - target) * (pot - target) < 0.0f) { pk.caught[i] = true; return true; }
    return false;
}

// ─── Step-cap bench (pio run -e teensy_chaos_bench) ───────────────────────────
// The step caps in chaos_core were measured on the Alchemy Lab at 480 MHz, and a cap is
// per chip. This runs each model flat out before audio starts, as Secret's `make BENCH=1`
// does, and prints the load over USB serial every few seconds after; the module then
// plays as normal. The pitch asked for is far above anything playable, so the schedule
// clamps at the cap: the worst block the model can produce. TAME 1 is the worst case per
// step (the feedback's history and three reads run every step).
#ifdef SECRET_BENCH
struct BenchRow { float steps, cycPerStep, avg[2], worst[2]; };
static BenchRow benchRows[N_ALGOS];
static float    benchBudget = 0.0f;

static void bench() {
    static Voice v;
    static float l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
    constexpr int kBlocks = 200;   // 0.6 s of audio per measurement
    benchBudget = AUDIO_BLOCK_SAMPLES * (float)F_CPU_ACTUAL / AUDIO_SAMPLE_RATE_EXACT;
    v.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
    AudioNoInterrupts();   // no audio updates in the measurement
    for (int m = 0; m < N_ALGOS; m++) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("BENCH "); display.print(m + 1); display.print('/'); display.print(N_ALGOS);
        display.setCursor(0, 12);
        display.print(algos[m]->name);
        display.display();
        ChaosBase* a = algos[m];
        const float chaos = 0.5f * (a->chaosMin + a->chaosMax);
        const float charV = 0.5f * (a->charMin + a->charMax);
        v.setAlgo(a);
        BenchRow& row = benchRows[m];
        for (int t = 0; t < 2; t++) {
            v.setLoadScale(1.0f);
            uint32_t sum = 0, most = 0;
            for (int b = 0; b < kBlocks; b++) {
                v.setPitch(chaos, charV, 1.0e6f, t ? 1.0f : 0.0f);   // far past any cap
                const uint32_t t0 = ARM_DWT_CYCCNT;
                v.render(l, r, AUDIO_BLOCK_SAMPLES);
                const uint32_t dt = ARM_DWT_CYCCNT - t0;
                sum += dt;
                if (dt > most) most = dt;
            }
            row.avg[t]   = (float)sum / kBlocks / benchBudget;
            row.worst[t] = (float)most / benchBudget;
            row.steps    = v.stepsPerSample();
        }
        row.cycPerStep = row.avg[0] * benchBudget / (row.steps * AUDIO_BLOCK_SAMPLES);
    }
    v.setAlgo(nullptr);
    AudioInterrupts();
}

static void benchPrint() {
    Serial.printf("bench: %lu MHz, %.0f cycles per %d-sample block at %.1f Hz\n",
                  (unsigned long)(F_CPU_ACTUAL / 1000000u), benchBudget,
                  AUDIO_BLOCK_SAMPLES, AUDIO_SAMPLE_RATE_EXACT);
    for (int m = 0; m < N_ALGOS; m++) {
        const BenchRow& r = benchRows[m];
        // The cap that would put TAME 1's average at 65% of a block, as Secret's are set.
        const float cap65 = algos[m]->maxStepsPerSecond * 0.65f / r.avg[1];
        Serial.printf("bench %-15s cap %7.0f st/s = %5.1f st/smp, %4.0f cyc/st | "
                      "TAME0 %3.0f%% (worst %3.0f%%), TAME1 %3.0f%% (%3.0f%%) | 65%% cap %7.0f\n",
                      algos[m]->name, algos[m]->maxStepsPerSecond, r.steps, r.cycPerStep,
                      r.avg[0] * 100.0f, r.worst[0] * 100.0f,
                      r.avg[1] * 100.0f, r.worst[1] * 100.0f, cap65);
    }
}
#endif

// ─── setup ────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(10, OUTPUT);
    digitalWriteFast(10, HIGH);   // disable Audio Shield SD card CS (shared pin)

    Wire.setSDA(18); Wire.setSCL(19);
    Wire.begin(); Wire.setClock(400000);

    Wire1.setSDA(17); Wire1.setSCL(16);
    Wire1.begin(); Wire1.setClock(400000);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);

    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(28, 24);
    display.print("SECRET");
    display.display();
    display.setTextSize(1);

#ifdef SECRET_BENCH
    bench();
#else
    delay(800);   // long enough to read the name
#endif

    AudioMemory(12);   // one stereo voice: 12 blocks is comfortable
    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);
    codec.volume(0.7);
    codec.lineOutLevel(LINE_OUT_LEVEL);
}

// ─── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // Pots — light EMA smoothing to steady params and the pickup comparison.
    static float ps[4]; static bool psInit = false;
    int praw[4] = { readPot(PIN_CHAOS), readPot(PIN_RATE), readPot(PIN_CHAR), readPot(PIN_DEPTH) };
    if (!psInit) { for (int i = 0; i < 4; i++) ps[i] = praw[i]; psInit = true; }
    for (int i = 0; i < 4; i++) ps[i] += (praw[i] - ps[i]) * 0.2f;
    const float chaosN = ps[0] / 1023.0f, tuneN = ps[1] / 1023.0f;
    const float charN  = ps[2] / 1023.0f, depthN = ps[3] / 1023.0f;

    // CV inputs (ADS1115 AIN0-3: MOD, ASGN, CLK, RST)
    int16_t cv[4];
    adsReadAll(cv);
    const float chaosCvV = (CV_MOD_ZERO  - cv[0]) / CV_MOD_CPV;    // MOD  → CHAOS
    const float tuneCvV  = (CV_ASGN_ZERO - cv[1]) / CV_ASGN_CPV;   // ASGN → TUNE
    const float voctV    = (CV_CLK_ZERO  - cv[2]) / CV_CLK_CPV;    // CLK  → V/OCT

    // ── Button: short press = next model, long press = toggle ENV page ──
    static uint8_t  model     = 0;
    static bool     lastBtn   = false;
    static uint32_t btnDownMs = 0;
    static bool     envPage   = false;
    static Pickup3  pagePickup;
    // On the ENV page CHAOS chooses Drone or Gated. Latch it until the pot is
    // actually moved there: long-pressing just to look, with CHAOS past halfway,
    // would otherwise switch to Gated on entry — silence, with nothing in GATE.
    static float    envSwitchRef  = 0.0f;
    static bool     envSwitchLive = false;
    const bool btn = !digitalRead(PIN_BTN);
    if (btn && !lastBtn) btnDownMs = millis();
    if (!btn && lastBtn) {
        const uint32_t held = millis() - btnDownMs;
        if (held >= BTN_LONG_MS) {
            envPage = !envPage;                              // CHAOS/CHAR/DEPTH change meaning…
            pickup3_arm(pagePickup, chaosN, charN, depthN);  // …so hold them until re-caught
            envSwitchRef = chaosN; envSwitchLive = false;    // and don't flip the mode on entry
        } else if (held > 20) {                              // debounce short press
            model = (model + 1) % N_ALGOS;
            AudioProcessorUsageMaxReset();                   // peak CPU is per model
        }
    }
    lastBtn = btn;

    // ── Screen auto-off ──
    // The noise test (eurorack_electronics/docs/noise_test.md) measured the panel, when powered,
    // at about +11 dB on the output, a 5.7 kHz line at -76 dB. So after 20 s with no knob or
    // button moved, it goes fully off, display and charge pump, and the first touch wakes it.
    static constexpr uint32_t SCREEN_IDLE_MS = 20000;
    static bool     screenOn   = true;
    static uint32_t lastActive = millis();
    static float    actRef[4]  = {ps[0], ps[1], ps[2], ps[3]};
    bool active = btn;
    for (int i = 0; i < 4; i++)
        if (fabsf(ps[i] - actRef[i]) > 15.0f) { active = true; actRef[i] = ps[i]; }
    if (active) {
        lastActive = millis();
        if (!screenOn) {
            display.ssd1306_command(SSD1306_CHARGEPUMP);
            display.ssd1306_command(0x14);
            display.ssd1306_command(SSD1306_DISPLAYON);
            screenOn = true;
        }
    } else if (screenOn && millis() - lastActive > SCREEN_IDLE_MS) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        display.ssd1306_command(SSD1306_CHARGEPUMP);
        display.ssd1306_command(0x10);
        screenOn = false;
    }

    // ── Control state, as 0..1 norms so it survives model and page switches ──
    static float ctlChaos = 0.5f, ctlChar = 0.3f, ctlTame = 0.0f;
    static float ctlAd    = 0.3f, ctlSr   = 0.4f;
    static bool  gated    = false;   // Drone by default: it sounds with nothing patched
    if (!envPage) {
        if (pickup3_update(pagePickup, 0, chaosN, ctlChaos)) ctlChaos = chaosN;
        if (pickup3_update(pagePickup, 1, charN,  ctlChar))  ctlChar  = charN;
        if (pickup3_update(pagePickup, 2, depthN, ctlTame))  ctlTame  = depthN;
    } else {
        if (!envSwitchLive && fabsf(chaosN - envSwitchRef) > 0.05f) envSwitchLive = true;
        if (envSwitchLive) {
            if (chaosN > 0.55f)      gated = true;
            else if (chaosN < 0.45f) gated = false;
        }
        if (pickup3_update(pagePickup, 1, charN,  ctlAd)) ctlAd = charN;
        if (pickup3_update(pagePickup, 2, depthN, ctlSr)) ctlSr = depthN;
    }

    // GATE with hysteresis. The voice does the rest: the attack waits for a zero
    // crossing, and a gate re-seeds the attractor only from silence.
    static bool gateHigh = false;
    if (!gateHigh && cv[3] < GATE_ON)       gateHigh = true;
    else if (gateHigh && cv[3] > GATE_OFF)  gateHigh = false;

    // ── What to play, as Secret maps its panel ──
    const ChaosBase* algo = algos[model];
    Params p;
    p.model   = model;
    p.chaos   = algo->chaosMin + clamp01(ctlChaos + chaosCvV * CHAOS_CV_PER_V) * (algo->chaosMax - algo->chaosMin);
    p.charV   = algo->charMin  + ctlChar * (algo->charMax - algo->charMin);
    p.hz      = expoMap(tuneN, TUNE_MIN_HZ, TUNE_MAX_HZ) * exp2f(voctV + tuneCvV);
    p.tame    = ctlTame;
    p.atkMs   = expoMap(ctlAd, ENV_ATK_MIN_MS, ENV_ATK_MAX_MS);
    p.decMs   = expoMap(ctlAd, ENV_DEC_MIN_MS, ENV_DEC_MAX_MS);
    p.sustain = ctlSr;
    p.relMs   = expoMap(ctlSr, ENV_REL_MIN_MS, ENV_REL_MAX_MS);
    p.gated   = gated;
    p.gate    = gateHigh;
    engine.post(p);

    // The pitch actually playing: the note asked for, unless the model's step cap or
    // the governor holds it below.
    const Voice& voice = engine.voice();
    const float playedHz = voice.effectiveDt() * AUDIO_SAMPLE_RATE_EXACT
                         * algo->naturalFreq(p.chaos, p.charV) * voice.loadScale();

    // Phase plot ring buffer — sampled every loop iteration
    #define PLOT_W  128
    #define PLOT_H   32
    #define TRAIL    96
    static uint8_t trailX[TRAIL], trailY[TRAIL];
    static uint8_t trailHead = 0;
    {
        float px = (voice.getX() - algo->xMin) * (PLOT_W - 1) / algo->xRange;
        float py = (voice.getY() - algo->yMin) * (PLOT_H - 1) / algo->yRange;
        // constrain() compares, and every comparison against NaN is false, so a
        // NaN would pass straight through to the cast. Plot it as 0.
        if (!isfinite(px)) px = 0.0f;
        if (!isfinite(py)) py = 0.0f;
        trailX[trailHead] = (uint8_t)constrain(px, 0, PLOT_W - 1);
        trailY[trailHead] = (uint8_t)constrain(PLOT_H - 1 - py, 0, PLOT_H - 1);
        trailHead = (trailHead + 1) % TRAIL;
    }

    // Display update (~10 fps)
    static uint32_t lastDisp = 0;
    if (screenOn && millis() - lastDisp > 100) {  // nothing to draw while it sleeps
        lastDisp = millis();
        display.clearDisplay();

        // Phase space plot (top 32 rows)
        for (uint8_t i = 0; i < TRAIL; i++) display.drawPixel(trailX[i], trailY[i], SSD1306_WHITE);

        // Model number, top-left over the plot.
        display.fillRect(0, 0, 30, 8, SSD1306_BLACK);
        display.setCursor(0, 0);
        display.print(model + 1); display.print('/'); display.print(N_ALGOS);

        // Peak audio-update load since the last model change, top-right over the
        // plot: the number that says how close a patch is to the audio budget.
        // '!' if the governor is holding the step count down (the pitch runs flat).
        {
            int cpu = (int)(AudioProcessorUsageMax() + 0.5f);
            if (cpu > 999) cpu = 999;
            const bool governed = voice.loadScale() < 0.999f;
            const int digits = (cpu >= 100) ? 3 : (cpu >= 10 ? 2 : 1);
            const int w = (digits + 1 + (governed ? 1 : 0)) * 6;   // digits + '%' (+ '!'), 6 px/char
            display.fillRect(127 - w, 0, w + 1, 8, SSD1306_BLACK);
            display.setCursor(128 - w, 0);
            if (governed) display.print('!');
            display.print(cpu); display.print('%');
        }

        // Row 1: the model, and GATE when the envelope is Gated
        display.setCursor(0, 34);
        if (!envPage) display.print(algo->name);
        else          display.print("ENVELOPE");
        display.setCursor(envPage ? 98 : 104, 34);
        if (envPage)    display.print(gated ? "GATED" : "DRONE");
        else if (gated) display.print("GATE");

        if (!envPage) {
            // Row 2: CHAOS and CHAR, in the model's own units
            display.setCursor(0, 44);
            display.print(algo->chaosLabel); display.print(':'); display.print(p.chaos, 2);
            display.setCursor(64, 44);
            display.print(algo->charLabel); display.print(':'); display.print(p.charV, 2);
            // Row 3: the pitch playing, and TAME
            display.setCursor(0, 54);
            if (playedHz < 1000.0f) display.print(playedHz, 1);
            else                    display.print((int)(playedHz + 0.5f));
            display.print("Hz");
            display.setCursor(64, 54);
            display.print("TAME "); display.print(p.tame, 2);
        } else {
            // ENV page: AD (attack/decay) and SR (sustain level/release)
            display.setCursor(0, 44);
            display.print("A:"); display.print(p.atkMs, 0);
            display.print(" D:"); display.print(p.decMs, 0);
            display.setCursor(0, 54);
            display.print("S:"); display.print((int)(p.sustain * 100)); display.print('%');
            display.print(" R:");
            if (p.relMs >= 1000.0f) { display.print(p.relMs / 1000.0f, 1); display.print('s'); }
            else                    { display.print(p.relMs, 0); display.print("ms"); }
        }

        display.display();
    }

    // CV outputs: the raw state, not the limited audio — the true attractor for a scope.
    dacWriteVolts(0, constrain(voice.getX() * algo->cvScaleX, -4.9f, 4.9f));
    dacWriteVolts(1, constrain(voice.getY() * algo->cvScaleY, -4.9f, 4.9f));

#ifdef SECRET_BENCH
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 3000) { lastPrint = millis(); benchPrint(); }
#endif
}
