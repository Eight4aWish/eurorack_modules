// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "chaos_core/Registry.h"   // ChaosBase, the six attractors, algos[] / N_ALGOS
#include "chaos_core/Voice.h"      // platform-free voice: schedule, envelope, output
#include "teensy_chaos/pins.h"

// The attractor DSP lives in libs/chaos_core and knows nothing about Teensy.
// This file is the platform layer: audio graph, ADC/DAC, OLED, control loop.
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
    // as 0xFFFF → −1, and −1 reads as roughly +5.5 V on every input: the chaos
    // parameter slams to its limit, V/Oct jumps to maximum oversampling, and RST
    // crosses its gate threshold — a spurious note plus an attractor re-init from
    // one bus hiccup. Seeded with the measured 0 V codes so a failure before the
    // first successful read is quiet rather than garbage.
    static int16_t last[4] = { 13236, 13240, 13241, 13241 };
    for (int ch = 0; ch < 4; ch++) {
        if (!adsWriteCfg(0b100 + ch)) continue;
        if (!adsWaitReady(2000))      continue;
        int16_t v;
        if (adsReadConv(v)) last[ch] = v;
    }
    for (int ch = 0; ch < 4; ch++) out[ch] = last[ch];
}

// ─── AudioChaosEngine ─────────────────────────────────────────────────────────
// Thin platform binding: an AudioStream wrapper around chaos_core::Voice.
//
// The attractor, the oversampling schedule, the AD/SR envelope, DC blocking and
// the output limiter all live in the library now, so they move to other hardware
// unchanged. What is left in this file is only what is genuinely Teensy — the
// audio callback and its blocks, the DWT cycle counter the load governor
// measures with, and the conversion from Voice's float output to the codec's
// 16-bit samples.
class AudioChaosEngine : public AudioStream {
public:
    AudioChaosEngine() : AudioStream(0, nullptr) {}

    chaos_core::Voice&       voice()       { return voice_; }
    const chaos_core::Voice& voice() const { return voice_; }

    void update() override {
        const uint32_t tStart = ARM_DWT_CYCCNT;
        if (!voice_.algo()) return;
        audio_block_t* bL = allocate();
        if (!bL) return;
        audio_block_t* bR = allocate();
        if (!bR) { release(bL); return; }

        // Static rather than automatic: two float blocks is 1 kB, and this runs
        // in the audio interrupt. update() is not reentrant — the audio ISR does
        // not preempt itself — so one shared pair is safe and keeps it off the
        // interrupt stack.
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
    // Load governor. The per-algorithm step ceilings are estimates from static
    // instruction counts, so a mis-set one could still let a block overrun its
    // slice — and that failure latches. update() runs in the audio ISR, which
    // preempts loop(); only loop() can lower the step rate, so once a block takes
    // longer than a block period the control loop is starved and can never wind
    // the rate back. The module looks frozen: dead panel, stuck CV.
    //
    // So measure the block and throttle the step count if it runs long. A note
    // that goes flat under an extreme patch beats a module that needs a power
    // cycle. It should never engage in normal use — if it does, the algorithm's
    // maxStepsPerSecond is set too high. `!` on the OLED next to the CPU figure
    // says it is active.
    //
    // The policy lives here rather than in the library because only the platform
    // knows what a block costs and what it is allowed to cost.
    inline void governLoad(uint32_t cycles) {
        const uint32_t budget = AUDIO_BLOCK_SAMPLES * (F_CPU_ACTUAL / 44100u);
        // The throttle is read back from the voice rather than mirrored here.
        // Voice::setAlgo() resets it to 1.0 because a throttle describes the cost
        // of the algorithm that earned it; a second copy in this class would
        // survive that reset and wind the new algorithm straight back down.
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

    chaos_core::Voice voice_;
};

// ─── Audio graph (single engine, no mixer needed) ─────────────────────────────
AudioChaosEngine     engine;
AudioAmplifier       ampL;
AudioAmplifier       ampR;
AudioOutputI2S       audioOut;
AudioAnalyzePeak     peakL;
AudioAnalyzePeak     peakR;
AudioControlSGTL5000 codec;

AudioConnection  patchL    (engine, 0, ampL,    0);
AudioConnection  patchR    (engine, 1, ampR,    0);
AudioConnection  patchConnL(ampL,   0, audioOut, 0);
AudioConnection  patchConnR(ampR,   0, audioOut, 1);
AudioConnection  patchPeakL(ampL,   0, peakL,   0);
AudioConnection  patchPeakR(ampR,   0, peakR,   0);

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
    int code = (int)((volts + DAC_OFFSET) / DAC_SCALE);
    dacWrite(ch, (uint16_t)constrain(code, 0, 4095));
}

// ─── Output level ─────────────────────────────────────────────────────────────
// SGTL5000 CHIP_LINE_OUT_VOL, via codec.lineOutLevel(). The field runs 13..31 and
// is an ATTENUATION, so lower is LOUDER: 13 ≈ 3.16 V p-p, 31 ≈ 1.16 V p-p, in
// 0.5 dB steps. This sat at 29 — near the quiet end, leaving ~8 dB unused, which
// is why every algorithm read as quiet no matter how the DSP was scaled.
//
// Turned up to 24 (+2.5 dB), deliberately a modest step rather than a jump to 13.
// The codec is not the only gain in the chain: the module's analog output stage
// has its own headroom, and past some point driving the codec harder just clips
// there instead. Lower this further by ear or on a scope until it stops getting
// cleaner, then back off one.
static constexpr uint8_t LINE_OUT_LEVEL = 24;

// ─── Envelope + control tunables ──────────────────────────────────────────────
// AD macro spans attack + decay; SR macro spans sustain level + release.
static constexpr float    ENV_ATK_MIN_MS = 0.5f,  ENV_ATK_MAX_MS = 1000.0f;
static constexpr float    ENV_DEC_MIN_MS = 2.0f,  ENV_DEC_MAX_MS = 2000.0f;
static constexpr float    ENV_REL_MIN_MS = 2.0f,  ENV_REL_MAX_MS = 4000.0f;
static constexpr uint16_t BTN_LONG_MS    = 500;    // long-press toggles the ENV page
// RST used as a gate: lower ADS code = higher Eurorack voltage (inverting front-end).
static constexpr int16_t  RST_ON  = 10820;  // ~+1V — gate goes high
static constexpr int16_t  RST_OFF = 11600;  // lower V — gate goes low (hysteresis)

// CV input calibration (measured 2026-04-07; inverting front-end: higher V → lower code)
static constexpr float CV_MOD_ZERO  = 13236.0f, CV_MOD_CPV  = 2414.0f;
static constexpr float CV_ASGN_ZERO = 13240.0f, CV_ASGN_CPV = 2424.0f;
static constexpr float CV_CLK_ZERO  = 13241.0f, CV_CLK_CPV  = 2421.0f;

// Exponential pot map: norm 0..1 → [lo, hi] geometrically (musical for times).
static inline float expoMap(float norm, float lo, float hi) {
    if (norm < 0.0f) norm = 0.0f; else if (norm > 1.0f) norm = 1.0f;
    return lo * powf(hi / lo, norm);
}

// Soft-takeover for the long-press ENV page. On the ENV page CHAOS/CHAR/DEPTH
// change meaning (enable / Attack / Decay), so on a page switch their pots stay
// inactive until each crosses (or matches) the target's stored value.
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

// ─── setup ────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(10, OUTPUT);
    digitalWriteFast(10, HIGH);   // disable Audio Shield SD card CS (shared pin)

    Wire.setSDA(18); Wire.setSCL(19);
    Wire.begin(); Wire.setClock(400000);

    Wire1.setSDA(17); Wire1.setSCL(16);
    Wire1.begin(); Wire1.setClock(400000);

    AudioMemory(12);   // one stereo engine: 12 blocks is comfortable

    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);
    codec.volume(0.7);
    codec.lineOutLevel(LINE_OUT_LEVEL);
    ampL.gain(1.0f);
    ampR.gain(1.0f);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);

    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay();
    display.display();

    engine.voice().setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
    engine.voice().setAlgo(algos[0]);   // start with Rössler
}

// ─── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // Pots — light EMA smoothing to steady params and the pickup comparison.
    static float ps[4]; static bool psInit = false;
    int praw[4] = { readPot(PIN_CHAOS), readPot(PIN_RATE), readPot(PIN_CHAR), readPot(PIN_DEPTH) };
    if (!psInit) { for (int i = 0; i < 4; i++) ps[i] = praw[i]; psInit = true; }
    for (int i = 0; i < 4; i++) ps[i] += (praw[i] - ps[i]) * 0.2f;
    float p1n = ps[0] / 1023.0f, p2 = ps[1], p3n = ps[2] / 1023.0f, p4n = ps[3] / 1023.0f;

    // CV inputs (ADS1115)
    int16_t cv[4];
    adsReadAll(cv);

    // ── Button: short press = next algorithm, long press = toggle ENV page ──
    static uint8_t  algoIdx   = 0;
    static bool     lastBtn   = false;
    static uint32_t btnDownMs = 0;
    static bool     envPage   = false;   // false = main params, true = ENV config
    static Pickup3  pagePickup;
    // The ENV page turns CHAOS into the envelope on/off switch. Latch it out
    // until the pot is actually moved on this page — see the ENV branch below.
    static float    envSwitchRef  = 0.0f;
    static bool     envSwitchLive = false;
    bool btn = !digitalRead(PIN_BTN);
    if (btn && !lastBtn) btnDownMs = millis();
    if (!btn && lastBtn) {
        uint32_t held = millis() - btnDownMs;
        if (held >= BTN_LONG_MS) {
            envPage = !envPage;                          // CHAOS/CHAR/DEPTH change meaning…
            pickup3_arm(pagePickup, p1n, p3n, p4n);      // …so hold them until re-caught
            envSwitchRef = p1n; envSwitchLive = false;   // and don't flip ENV on entry
        } else if (held > 20) {                          // debounce short press
            algoIdx = (algoIdx + 1) % N_ALGOS;
            engine.voice().setAlgo(algos[algoIdx]);
            AudioProcessorUsageMaxReset();   // peak CPU is per-algorithm
        }
    }
    lastBtn = btn;

    // CV calibration (inverting front-end: higher Eurorack voltage → lower ADS code)
    float modVolts  = (CV_MOD_ZERO  - cv[0]) / CV_MOD_CPV;
    float asgnVolts = (CV_ASGN_ZERO - cv[1]) / CV_ASGN_CPV;
    float clkVolts  = (CV_CLK_ZERO  - cv[2]) / CV_CLK_CPV;

    // ── Persistent control state, stored as 0..1 norms so they survive algo and
    //    page switches. On the main page CHAOS/CHAR/DEPTH (p1/p3/p4) feed chaos/
    //    char/depth; on the ENV page CHAOS becomes the envelope ON/OFF switch and
    //    CHAR/DEPTH become Attack/Decay. Page-dependent pots use soft-takeover.
    static float ctlChaosNorm = 0.5f, ctlCharNorm = 0.3f, ctlDepthNorm = 0.8f;
    static float ctlAdNorm    = 0.3f, ctlSrNorm   = 0.4f;
    static bool  envEnabled   = false;   // gate-driven AD/SR envelope off by default
    if (!envPage) {
        if (pickup3_update(pagePickup, 0, p1n, ctlChaosNorm)) ctlChaosNorm = p1n;
        if (pickup3_update(pagePickup, 1, p3n, ctlCharNorm))  ctlCharNorm  = p3n;
        if (pickup3_update(pagePickup, 2, p4n, ctlDepthNorm)) ctlDepthNorm = p4n;
    } else {
        // CHAOS pot is the enable switch (deadband around centre); CHAR = AD macro
        // (attack+decay), DEPTH = SR macro (sustain+release), with soft-takeover.
        //
        // The switch stays inert until the pot is moved on this page. Reading it
        // on entry meant that long-pressing to look at the ENV page while CHAOS
        // happened to sit above 55% switched the envelope on there and then —
        // and with nothing patched to RST that is silence you did not ask for.
        if (!envSwitchLive && fabsf(p1n - envSwitchRef) > 0.05f) envSwitchLive = true;
        if (envSwitchLive) {
            if (p1n > 0.55f)      envEnabled = true;
            else if (p1n < 0.45f) envEnabled = false;
        }
        if (pickup3_update(pagePickup, 1, p3n, ctlAdNorm))    ctlAdNorm    = p3n;
        if (pickup3_update(pagePickup, 2, p4n, ctlSrNorm))    ctlSrNorm    = p4n;
    }

    // ── Apply controls to the active algorithm ──
    ChaosBase* algo = engine.voice().algo();
    float chaos = 0.0f, charV = 0.0f, rateDisp = 0.0f;
    if (algo) {
        chaos = algo->chaosMin + ctlChaosNorm * (algo->chaosMax - algo->chaosMin);
        chaos = constrain(chaos + modVolts * algo->modScale,
                          algo->modMin, algo->modMax);
        charV = algo->charMin + ctlCharNorm * (algo->charMax - algo->charMin);

        // Pitch, in simulated time units per second: the pot spans the algorithm's
        // range and V/Oct multiplies it. Turning that into a safe step size and a
        // step count is Voice's job (ChaosBase::scheduleFor), so the same request
        // produces the same pitch at any sample rate.
        float potRate = algo->simRateMin + (p2 / 1023.0f) * (algo->simRateMax - algo->simRateMin);
        float simRate = potRate * powf(2.0f, clkVolts + asgnVolts);

        // setParams writes several floats. Each store is atomic, but the *set* is
        // not: the audio ISR can preempt between them and integrate a whole block
        // from a mixed parameter set. Mostly that is inaudible, since consecutive
        // sets differ by a pot's worth of smoothing — but a new high alpha landing
        // against a not-yet-written beta can put Chua in its unbounded corner when
        // neither the old nor the new setting was there, and one block is thousands
        // of RK4 steps, plenty for a runaway the player never asked for.
        // Hold off the audio ISR for the handful of stores instead.
        AudioNoInterrupts();
        engine.voice().setParams(chaos, charV, simRate);
        AudioInterrupts();
        rateDisp = engine.voice().effectiveDt();
    }

    // DEPTH = output amplitude; on the ENV page CHAR/DEPTH are the AD/SR macros.
    float depth = 0.1f + ctlDepthNorm * 0.9f;
    ampL.gain(depth);
    ampR.gain(depth);
    float atkMs   = expoMap(ctlAdNorm, ENV_ATK_MIN_MS, ENV_ATK_MAX_MS);  // AD → attack
    float decMs   = expoMap(ctlAdNorm, ENV_DEC_MIN_MS, ENV_DEC_MAX_MS);  // AD → decay
    float sustain = ctlSrNorm;                                           // SR → sustain level
    float relMs   = expoMap(ctlSrNorm, ENV_REL_MIN_MS, ENV_REL_MAX_MS);  // SR → release
    engine.voice().setEnvADSR(atkMs, decMs, sustain, relMs);
    engine.voice().setEnvEnabled(envEnabled);

    // RST used as a GATE: rising edge re-inits the attractor (percussive transient)
    // and opens the envelope; held high it sustains, and it releases when RST falls.
    // A short trigger gives an AD-style hit; a sustained gate gives full ASR.
    static bool gateHigh = false;
    bool prevGate = gateHigh;
    if (!gateHigh && cv[3] < RST_ON)      gateHigh = true;
    else if (gateHigh && cv[3] > RST_OFF) gateHigh = false;
    if (gateHigh && !prevGate && algo) {
        AudioNoInterrupts();       // same reason: init() writes 3-6 state floats,
        algo->init();              // and half a reset is not a point on any orbit
        AudioInterrupts();
    }
    engine.voice().setEnvGate(gateHigh);

    // Phase plot ring buffer — sampled every loop iteration
    #define PLOT_W  128
    #define PLOT_H   32
    #define TRAIL    96
    static uint8_t trailX[TRAIL], trailY[TRAIL];
    static uint8_t trailHead = 0;
    if (algo) {
        float px = (engine.voice().getX() - algo->xMin) * (PLOT_W - 1) / algo->xRange;
        float py = (engine.voice().getY() - algo->yMin) * (PLOT_H - 1) / algo->yRange;
        // constrain() compares, and every comparison against NaN is false, so a
        // NaN would pass straight through to the cast. The divergence guards make
        // that transient rather than permanent, but plot it as 0 regardless.
        if (!isfinite(px)) px = 0.0f;
        if (!isfinite(py)) py = 0.0f;
        trailX[trailHead] = (uint8_t)constrain(px, 0, PLOT_W - 1);
        trailY[trailHead] = (uint8_t)constrain(PLOT_H - 1 - py, 0, PLOT_H - 1);
        trailHead = (trailHead + 1) % TRAIL;
    }

    // Display update (~10 fps)
    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 100) {
        lastDisp = millis();
        display.clearDisplay();

        // Phase space plot (top 32 rows)
        for (uint8_t i = 0; i < TRAIL; i++) {
            display.drawPixel(trailX[i], trailY[i], SSD1306_WHITE);
        }

        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        // Peak audio-ISR load since the last algorithm change, top-right over
        // the plot. Oversampling buys pitch with CPU, so this is the number
        // that says how close a patch is to overrunning the audio budget.
        {
            int cpu = (int)(AudioProcessorUsageMax() + 0.5f);
            if (cpu > 999) cpu = 999;
            // '!' prefix if the load governor is throttling the step count — the
            // pitch is running flat to keep the audio ISR inside its slice.
            bool governed = engine.voice().loadScale() < 0.999f;
            int digits = (cpu >= 100) ? 3 : (cpu >= 10 ? 2 : 1);
            int w = (digits + 1 + (governed ? 1 : 0)) * 6;   // digits + '%' (+ '!'), 6 px/char
            display.fillRect(127 - w, 0, w + 1, 8, SSD1306_BLACK);
            display.setCursor(128 - w, 0);
            if (governed) display.print('!');
            display.print(cpu); display.print('%');
        }

        if (algo) {
            // Row 1: algorithm name + (chaos param | ENV on/off)
            display.setCursor(0, 34);
            display.print(algo->name);
            if (!envPage) {
                display.setCursor(78, 34);
                display.print(algo->chaosLabel); display.print(':');
                display.print(chaos, 1);
            } else {
                display.setCursor(66, 34);
                display.print(envEnabled ? "ENV:ON" : "ENV:OFF");
            }

            if (!envPage) {
                // Row 2: char param + effective rate
                display.setCursor(0, 44);
                display.print(algo->charLabel); display.print(':');
                display.print(charV, 2);
                display.setCursor(72, 44);
                display.print("dt:"); display.print(rateDisp, 4);
                // Row 3: depth + peak levels
                float lv = peakL.available() ? peakL.read() : 0.0f;
                float rv = peakR.available() ? peakR.read() : 0.0f;
                display.setCursor(0, 54);
                display.print("dp:"); display.print(depth, 2);
                display.setCursor(72, 54);
                display.print("L"); display.print((int)(lv * 9));
                display.print(" R"); display.print((int)(rv * 9));
            } else {
                // ENV page: AD (attack/decay) and SR (sustain level/release)
                display.setCursor(0, 44);
                display.print("A:"); display.print(atkMs, 0);
                display.print(" D:"); display.print(decMs, 0);
                display.setCursor(0, 54);
                display.print("S:"); display.print((int)(sustain * 100)); display.print('%');
                display.print(" R:");
                if (relMs >= 1000.0f) { display.print(relMs / 1000.0f, 1); display.print('s'); }
                else                  { display.print(relMs, 0); display.print("ms"); }
            }
        }

        display.display();
    }

    // CV outputs: active algorithm state → X and Y jacks
    if (algo) {
        dacWriteVolts(0, constrain(engine.voice().getX() * algo->cvScaleX, -4.9f, 4.9f));
        dacWriteVolts(1, constrain(engine.voice().getY() * algo->cvScaleY, -4.9f, 4.9f));
    }
}
