// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "chaos_core/Registry.h"   // ChaosBase, the six attractors, algos[] / N_ALGOS
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
// Single AudioStream. setAlgo() swaps the active ChaosBase* at any time;
// pointer reads/writes are word-sized and atomic on Cortex-M7.
//
// Pitch comes from `stepsPerSample_`: the attractor is advanced that many
// integration steps per audio sample (fractional via an accumulator), so V/Oct
// raises pitch by oversampling at a safe dt rather than enlarging dt itself.
//
// An audio-rate envelope acts as a VCA on the output, driven by the RST input
// used as a GATE. Two macros shape it (pico-Env / Plaits style): AD = attack +
// decay front, SR = sustain level + release tail. Disabled by default — then the
// VCA stays fully open and the module is a free-running drone. A short trigger on
// RST gives an AD-style hit; a sustained gate gives full attack/sustain/release.
class AudioChaosEngine : public AudioStream {
public:
    enum EnvStage : uint8_t { ENV_OPEN, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_CLOSED };

    AudioChaosEngine() : AudioStream(0, nullptr) {}

    void setAlgo(ChaosBase* a) {
        if (a == algo_) return;
        if (a) a->init();      // initialise state before making live
        dcL_ = dcR_ = 0.0f;   // flush DC history on switch
        stepAcc_ = 0.0f;
        loadScale_ = 1.0f;     // the throttle described the old algorithm's cost
        algo_ = a;             // atomic pointer store
    }

    ChaosBase* algo() const { return algo_; }
    float getX() const { ChaosBase* a = algo_; return a ? a->getX() : 0.0f; }
    float getY() const { ChaosBase* a = algo_; return a ? a->getY() : 0.0f; }

    // Guard the ceiling and NaN as well as the floor. A NaN would pass a plain
    // `s < 1` test, and then stall the integrator for good: stepAcc_ becomes NaN,
    // so (int)stepAcc_ is always 0, no step ever runs, and only setAlgo() clears
    // it. STEPS_ABS_MAX sits well above any algorithm's oversampleMax, so it
    // bounds garbage without ever trimming a legitimate setting.
    void  setStepsPerSample(float s) {
        if (!(s >= 1.0f))            s = 1.0f;            // false for NaN too
        else if (s > STEPS_ABS_MAX)  s = STEPS_ABS_MAX;
        stepsPerSample_ = s;
    }
    void  setEnvEnabled(bool e)      { envEnabled_ = e; }   // off → VCA stays open (drone)
    void  setEnvGate(bool g)         { envGate_ = g; }      // RST used as a gate
    EnvStage envStage() const        { return envStage_; }
    float loadScale() const          { return loadScale_; }   // < 1 → governor throttling

    // Attack linear; decay/release exponential (~ -60 dB). Decay approaches the
    // sustain level, release falls from there to zero.
    void setEnvADSR(float atkMs, float decMs, float sustain, float relMs) {
        const float sr = 44100.0f / 1000.0f;
        float atkSamp = atkMs * sr; if (atkSamp < 1.0f) atkSamp = 1.0f;
        float decSamp = decMs * sr; if (decSamp < 1.0f) decSamp = 1.0f;
        float relSamp = relMs * sr; if (relSamp < 1.0f) relSamp = 1.0f;
        envAtkInc_ = 1.0f / atkSamp;
        envDecMul_ = expf(-6.908f / decSamp);   // ln(0.001) ≈ -6.908
        envRelMul_ = expf(-6.908f / relSamp);
        envSus_    = (sustain < 0.0f) ? 0.0f : (sustain > 1.0f ? 1.0f : sustain);
    }

    void update() override {
        const uint32_t tStart = ARM_DWT_CYCCNT;
        ChaosBase* a = algo_;   // single atomic load — consistent for this block
        if (!a) return;
        audio_block_t* bL = allocate();
        if (!bL) return;
        audio_block_t* bR = allocate();
        if (!bR) { release(bL); return; }

        // Enable / gate-edge handling (block rate — inaudible latency).
        if (!envEnabled_) {
            // Disabled: VCA fully open (original drone behaviour), ignore the gate.
            // Hold the edge detector low rather than tracking the live gate, so a
            // gate that is already high when the envelope is switched on still
            // reads as a rising edge and starts the note straight away, instead
            // of leaving the voice closed until the gate next cycles.
            envStage_ = ENV_OPEN; envLevel_ = 1.0f; envGatePrev_ = false;
        } else {
            if (envStage_ == ENV_OPEN) { envStage_ = ENV_CLOSED; envLevel_ = 0.0f; }
            bool g = envGate_;
            if (g && !envGatePrev_)       envStage_ = ENV_ATTACK;   // gate rising → (re)trigger
            else if (!g && envGatePrev_ && envStage_ != ENV_CLOSED) envStage_ = ENV_RELEASE; // gate falling
            envGatePrev_ = g;
        }

        float gL = a->gainL, gR = a->gainR;
        float steps = stepsPerSample_ * loadScale_;
        if (steps < 1.0f) steps = 1.0f;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            // Advance the attractor by `steps` integration steps this sample.
            stepAcc_ += steps;
            int n = (int)stepAcc_;
            stepAcc_ -= (float)n;
            for (int k = 0; k < n; k++) a->stepSample();

            envAdvance();

            float outL = tanhf(a->getX() * gL) * 32000.0f;
            float outR = tanhf(a->getY() * gR) * 32000.0f;
            outL -= dcL_; dcL_ += outL * 0.0007f;
            outR -= dcR_; dcR_ += outR * 0.0007f;
            outL *= envLevel_; outR *= envLevel_;
            bL->data[i] = (int16_t)outL;
            bR->data[i] = (int16_t)outR;
        }
        transmit(bL, 0); transmit(bR, 1);
        release(bL); release(bR);
        governLoad(ARM_DWT_CYCCNT - tStart);
    }

private:
    // Load governor. The per-algorithm oversampleMax ceilings are estimates from
    // static instruction counts, so a mis-set one could still let a block overrun
    // its slice — and that failure latches. This update() runs in the audio ISR,
    // which preempts loop(); only loop() can lower the step rate, so once a block
    // takes longer than a block period the control loop is starved and can never
    // wind the rate back. The module looks frozen: dead panel, stuck CV.
    //
    // So measure the block and throttle the step count if it runs long. A note
    // that goes flat under an extreme patch beats a module that needs a power
    // cycle. It should never engage in normal use — if it does, the algorithm's
    // oversampleMax is set too high. `!` on the OLED next to the CPU figure says
    // it is active.
    inline void governLoad(uint32_t cycles) {
        const uint32_t budget = AUDIO_BLOCK_SAMPLES * (F_CPU_ACTUAL / 44100u);
        float sc = loadScale_;
        if (cycles > (budget >> 1) + (budget >> 2)) {          // over 75% of the block
            sc *= 0.85f;                                       // ~4 blocks (12 ms) to halve
            if (sc < 0.02f) sc = 0.02f;
        } else if (cycles < (budget >> 1) && sc < 1.0f) {      // under 50%: creep back
            sc += 0.02f;
            if (sc > 1.0f) sc = 1.0f;
        }
        loadScale_ = sc;
    }

    inline void envAdvance() {
        switch (envStage_) {
            case ENV_ATTACK:
                envLevel_ += envAtkInc_;
                if (envLevel_ >= 1.0f) { envLevel_ = 1.0f; envStage_ = ENV_DECAY; }
                break;
            case ENV_DECAY:
                envLevel_ = envSus_ + (envLevel_ - envSus_) * envDecMul_;
                if (envLevel_ - envSus_ <= 0.0008f) { envLevel_ = envSus_; envStage_ = ENV_SUSTAIN; }
                break;
            case ENV_SUSTAIN: envLevel_ = envSus_; break;
            case ENV_RELEASE:
                envLevel_ *= envRelMul_;
                if (envLevel_ <= 0.0008f) { envLevel_ = 0.0f; envStage_ = ENV_CLOSED; }
                break;
            case ENV_OPEN:   envLevel_ = 1.0f; break;
            case ENV_CLOSED: envLevel_ = 0.0f; break;
        }
    }

    ChaosBase* algo_ = nullptr;
    float dcL_ = 0.0f, dcR_ = 0.0f;
    float stepsPerSample_ = 1.0f, stepAcc_ = 0.0f;
    static constexpr float STEPS_ABS_MAX = 256.0f;   // backstop above every oversampleMax
    volatile float loadScale_ = 1.0f;                // load-governor throttle, 0.02..1
    // Envelope (VCA) — gate-driven AD/SR
    volatile bool envEnabled_ = false;   // off by default — module is a drone voice
    volatile bool envGate_ = false;      // RST gate level
    bool  envGatePrev_ = false;
    EnvStage envStage_ = ENV_OPEN;
    float envLevel_  = 1.0f;
    float envAtkInc_ = 1.0f;      // per-sample linear attack increment
    float envDecMul_ = 0.999f;    // per-sample exponential decay-to-sustain
    float envRelMul_ = 0.999f;    // per-sample exponential release
    float envSus_    = 0.0f;      // sustain level
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
    codec.lineOutLevel(29);
    ampL.gain(1.0f);
    ampR.gain(1.0f);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);

    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay();
    display.display();

    engine.setAlgo(algos[0]);   // start with Rössler
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
            engine.setAlgo(algos[algoIdx]);
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
    ChaosBase* algo = engine.algo();
    float chaos = 0.0f, charV = 0.0f, rateDisp = 0.0f;
    if (algo) {
        chaos = algo->chaosMin + ctlChaosNorm * (algo->chaosMax - algo->chaosMin);
        chaos = constrain(chaos + modVolts * algo->modScale,
                          algo->modMin, algo->modMax);
        charV = algo->charMin + ctlCharNorm * (algo->charMax - algo->charMin);

        // Pitch: desired simulated-time-per-audio-sample from RATE + V/Oct, then
        // realise it as a safe step size × oversampling so it stays stable.
        float potDt    = algo->rateMin + (p2 / 1023.0f) * (algo->rateMax - algo->rateMin);
        float desiredDt = potDt * powf(2.0f, clkVolts + asgnVolts);
        desiredDt = constrain(desiredDt, 1.0e-5f, algo->dtBase * algo->oversampleMax);
        float stepDt, steps;
        if (desiredDt <= algo->dtBase) { stepDt = desiredDt;     steps = 1.0f; }
        else                           { stepDt = algo->dtBase;  steps = desiredDt / algo->dtBase; }
        rateDisp = desiredDt;

        // setParams writes several floats. Each store is atomic, but the *set* is
        // not: the audio ISR can preempt between them and integrate a whole block
        // from a mixed parameter set. Mostly that is inaudible, since consecutive
        // sets differ by a pot's worth of smoothing — but for Chua it is not
        // cosmetic. A new high alpha against the not-yet-written beta is exactly
        // the unbounded corner charInUse() exists to keep out of reach, and one
        // block is thousands of RK4 steps, plenty for an exponential runaway.
        // Hold off the audio ISR for the handful of stores instead.
        AudioNoInterrupts();
        algo->setParams(chaos, stepDt, charV);
        engine.setStepsPerSample(steps);
        AudioInterrupts();
        // Report what is actually in force. Chua clamps CHAR against CHAOS to
        // stay inside its bounded region, so below that floor the pot and the
        // running value part company and the panel should follow the latter.
        charV = algo->charInUse(chaos, charV);
    }

    // DEPTH = output amplitude; on the ENV page CHAR/DEPTH are the AD/SR macros.
    float depth = 0.1f + ctlDepthNorm * 0.9f;
    ampL.gain(depth);
    ampR.gain(depth);
    float atkMs   = expoMap(ctlAdNorm, ENV_ATK_MIN_MS, ENV_ATK_MAX_MS);  // AD → attack
    float decMs   = expoMap(ctlAdNorm, ENV_DEC_MIN_MS, ENV_DEC_MAX_MS);  // AD → decay
    float sustain = ctlSrNorm;                                           // SR → sustain level
    float relMs   = expoMap(ctlSrNorm, ENV_REL_MIN_MS, ENV_REL_MAX_MS);  // SR → release
    engine.setEnvADSR(atkMs, decMs, sustain, relMs);
    engine.setEnvEnabled(envEnabled);

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
    engine.setEnvGate(gateHigh);

    // Phase plot ring buffer — sampled every loop iteration
    #define PLOT_W  128
    #define PLOT_H   32
    #define TRAIL    96
    static uint8_t trailX[TRAIL], trailY[TRAIL];
    static uint8_t trailHead = 0;
    if (algo) {
        float px = (engine.getX() - algo->xMin) * (PLOT_W - 1) / algo->xRange;
        float py = (engine.getY() - algo->yMin) * (PLOT_H - 1) / algo->yRange;
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
            bool governed = engine.loadScale() < 0.999f;
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
        dacWriteVolts(0, constrain(engine.getX() * algo->cvScaleX, -4.9f, 4.9f));
        dacWriteVolts(1, constrain(engine.getY() * algo->cvScaleY, -4.9f, 4.9f));
    }
}
