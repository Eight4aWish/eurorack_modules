// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "teensy_chaos/pins.h"

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

static int16_t adsReadConv() {
    Wire1.beginTransmission(ADS_ADDR);
    Wire1.write(ADS_REG_CONV);
    Wire1.endTransmission(false);
    Wire1.requestFrom((int)ADS_ADDR, 2);
    uint8_t msb = Wire1.read();
    uint8_t lsb = Wire1.read();
    return (int16_t)((uint16_t(msb) << 8) | lsb);
}

static void adsReadAll(int16_t out[4]) {
    for (int ch = 0; ch < 4; ch++) {
        adsWriteCfg(0b100 + ch);
        delayMicroseconds(1200);  // 860 SPS → ~1.16ms per conversion
        out[ch] = adsReadConv();
    }
}

// ─── ChaosBase ────────────────────────────────────────────────────────────────
// Abstract base for all chaotic algorithms. Subclasses populate metadata fields
// in their constructors and implement the four pure-virtual methods.
class ChaosBase {
public:
    const char* name       = "?";
    const char* chaosLabel = "c";   // display label for CHAOS param
    const char* charLabel  = "a";   // display label for CHAR param
    float chaosMin = 0.0f,   chaosMax = 1.0f;
    float rateMin  = 0.001f, rateMax  = 0.1f;
    float charMin  = 0.0f,   charMax  = 1.0f;
    // Largest numerically-safe integration step. Pitch above what `dtBase`
    // alone can reach is produced by oversampling (multiple steps per audio
    // sample) instead of growing dt, so V/Oct tracks without diverging.
    float dtBase   = 0.05f;
    // Ceiling on those extra steps. Oversampling is the one control that buys
    // pitch with CPU, so the cap has to be per-algorithm: a Duffing step costs
    // ~6x a Rossler step (three cosf calls), and a single global cap is either
    // unsafe for the expensive systems or needlessly tight for the cheap ones.
    // Values are set so every algorithm tops out at a similar share of the
    // per-sample cycle budget. Raise one only against a measured CPU figure.
    float oversampleMax = 64.0f;
    float modScale = 1.0f;          // chaos-param units per volt of MOD CV
    float gainL    = 0.12f, gainR = 0.12f;  // pre-tanh amplitude scale
    float xMin = -1.0f, xRange = 2.0f;     // plot window
    float yMin = -1.0f, yRange = 2.0f;
    float cvScaleX = 0.5f, cvScaleY = 0.5f; // state → ±5V CV

    virtual ~ChaosBase() {}
    virtual void  init()                                          = 0;
    virtual void  setParams(float chaos, float rate, float charV) = 0;
    virtual void  stepSample()                                    = 0;
    virtual float getX() const                                    = 0;
    virtual float getY() const                                    = 0;
};

// ─── ChaosRossler ─────────────────────────────────────────────────────────────
// dx = -y - z,  dy = x + a*y,  dz = b + z*(x - c)
// CHAOS = c (bifurcation, 2–8),  CHAR = a (spiral tightness, 0.1–0.4)
class ChaosRossler : public ChaosBase {
public:
    ChaosRossler() {
        name       = "ROSSLER";
        chaosLabel = "c"; charLabel = "a";
        chaosMin   = 2.0f;   chaosMax = 8.0f;
        rateMin    = 0.002f; rateMax  = 0.1f;   dtBase = 0.1f;
        oversampleMax = 64.0f;   // ~85 cyc/step
        charMin    = 0.1f;   charMax  = 0.4f;
        modScale   = 1.0f;
        gainL      = 0.12f;  gainR    = 0.12f;
        xMin       = -11.0f; xRange   = 24.0f;
        yMin       = -11.0f; yRange   = 22.0f;
        cvScaleX   = 0.50f;  cvScaleY = 0.50f;
    }
    void init() override { x_ = 0.1f; y_ = 0.0f; z_ = 0.0f; }
    void setParams(float chaos, float rate, float charV) override {
        c_ = chaos; dt_ = rate; a_ = charV;
    }
    void stepSample() override {
        float dx1 = -y_ - z_;
        float dy1 = x_ + a_*y_;
        float dz1 = b_ + z_*(x_ - c_);
        float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1, z2 = z_ + 0.5f*dt_*dz1;
        float dx2 = -y2 - z2, dy2 = x2 + a_*y2, dz2 = b_ + z2*(x2 - c_);
        float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2, z3 = z_ + 0.5f*dt_*dz2;
        float dx3 = -y3 - z3, dy3 = x3 + a_*y3, dz3 = b_ + z3*(x3 - c_);
        float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3, z4 = z_ + dt_*dz3;
        float dx4 = -y4 - z4, dy4 = x4 + a_*y4, dz4 = b_ + z4*(x4 - c_);
        x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
        y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
        z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
    }
    float getX() const override { return x_; }
    float getY() const override { return y_; }
private:
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    float a_ = 0.2f, b_ = 0.2f, c_ = 5.7f;
    float dt_ = 0.05f;
};

// ─── ChaosVanDerPol ───────────────────────────────────────────────────────────
// dx/dt = y,   dy/dt = mu*(1 - x^2)*y - x
// CHAOS = mu (nonlinearity, 0.1–8): low = near-sine, high = relaxation osc
// Start on limit cycle (x=2, y=0) so amplitude is correct from first sample.
class ChaosVanDerPol : public ChaosBase {
public:
    ChaosVanDerPol() {
        name       = "VAN DER POL";
        chaosLabel = "u"; charLabel = "a";
        chaosMin   = 0.1f;   chaosMax = 8.0f;
        rateMin    = 0.002f; rateMax  = 0.15f;  dtBase = 0.15f;
        oversampleMax = 64.0f;   // ~83 cyc/step
        charMin    = 0.0f;   charMax  = 1.0f;  // reserved
        modScale   = 1.0f;
        gainL      = 0.45f;  gainR    = 0.20f;
        xMin       = -3.0f;  xRange   = 6.0f;
        yMin       = -8.0f;  yRange   = 16.0f;
        cvScaleX   = 2.00f;  cvScaleY = 0.60f;
    }
    void init() override { x_ = 2.0f; y_ = 0.0f; }
    void setParams(float chaos, float rate, float charV) override {
        mu_ = chaos;
        // Cap dt for numerical stability: VdP stiffness ∝ mu; RK4 diverges if dt*mu too large
        dt_ = fminf(rate, 1.0f / (mu_ + 2.0f));
        (void)charV;
    }
    void stepSample() override {
        float dx1 = y_;
        float dy1 = mu_*(1.0f - x_*x_)*y_ - x_;
        float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1;
        float dx2 = y2, dy2 = mu_*(1.0f - x2*x2)*y2 - x2;
        float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2;
        float dx3 = y3, dy3 = mu_*(1.0f - x3*x3)*y3 - x3;
        float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3;
        float dx4 = y4, dy4 = mu_*(1.0f - x4*x4)*y4 - x4;
        x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
        y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
        // Safety net: reset if numerics diverge (edge case at extreme mu+dt)
        if (!isfinite(x_) || !isfinite(y_) || fabsf(x_) > 20.0f) {
            x_ = 2.0f; y_ = 0.0f;
        }
    }
    float getX() const override { return x_; }
    float getY() const override { return y_; }
private:
    float x_ = 2.0f, y_ = 0.0f;
    float mu_ = 1.0f, dt_ = 0.05f;
};

// ─── ChaosLorenz ──────────────────────────────────────────────────────────────
// dx = sigma*(y-x),  dy = x*(rho-z)-y,  dz = x*y - beta*z
// CHAOS = rho (bifurcation, 24–32),  CHAR = sigma (8–14)
// getY() returns z-rho (centred around 0) for both audio and plot.
class ChaosLorenz : public ChaosBase {
public:
    ChaosLorenz() {
        name       = "LORENZ";
        chaosLabel = "r"; charLabel = "s";
        chaosMin   = 24.0f;  chaosMax = 32.0f;
        rateMin    = 0.001f; rateMax  = 0.003f;  dtBase = 0.003f;
        oversampleMax = 64.0f;   // ~89 cyc/step
        charMin    = 6.0f;   charMax  = 14.0f;
        modScale   = 2.0f;
        gainL      = 0.05f;  gainR    = 0.05f;
        xMin       = -20.0f; xRange   = 40.0f;
        yMin       = -28.0f; yRange   = 55.0f;  // z-rho: ≈ -28 to +27
        cvScaleX   = 0.25f;  cvScaleY = 0.15f;
    }
    void init() override { x_ = 0.1f; y_ = 0.0f; z_ = 0.0f; }
    void setParams(float chaos, float rate, float charV) override {
        rho_ = chaos; dt_ = rate; sigma_ = charV;
    }
    void stepSample() override {
        float dx1 = sigma_*(y_ - x_);
        float dy1 = x_*(rho_ - z_) - y_;
        float dz1 = x_*y_ - beta_*z_;
        float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1, z2 = z_ + 0.5f*dt_*dz1;
        float dx2 = sigma_*(y2-x2), dy2 = x2*(rho_-z2)-y2, dz2 = x2*y2-beta_*z2;
        float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2, z3 = z_ + 0.5f*dt_*dz2;
        float dx3 = sigma_*(y3-x3), dy3 = x3*(rho_-z3)-y3, dz3 = x3*y3-beta_*z3;
        float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3, z4 = z_ + dt_*dz3;
        float dx4 = sigma_*(y4-x4), dy4 = x4*(rho_-z4)-y4, dz4 = x4*y4-beta_*z4;
        x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
        y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
        z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
    }
    float getX() const override { return x_; }
    float getY() const override { return z_ - rho_; }  // centred: audio + plot
private:
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    float sigma_ = 10.0f, beta_ = 2.667f, rho_ = 28.0f;
    float dt_ = 0.002f;
};

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
        algo_ = a;             // atomic pointer store
    }

    ChaosBase* algo() const { return algo_; }
    float getX() const { ChaosBase* a = algo_; return a ? a->getX() : 0.0f; }
    float getY() const { ChaosBase* a = algo_; return a ? a->getY() : 0.0f; }

    void  setStepsPerSample(float s) { stepsPerSample_ = (s < 1.0f) ? 1.0f : s; }
    void  setEnvEnabled(bool e)      { envEnabled_ = e; }   // off → VCA stays open (drone)
    void  setEnvGate(bool g)         { envGate_ = g; }      // RST used as a gate
    EnvStage envStage() const        { return envStage_; }

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
        ChaosBase* a = algo_;   // single atomic load — consistent for this block
        if (!a) return;
        audio_block_t* bL = allocate();
        if (!bL) return;
        audio_block_t* bR = allocate();
        if (!bR) { release(bL); return; }

        // Enable / gate-edge handling (block rate — inaudible latency).
        if (!envEnabled_) {
            // Disabled: VCA fully open (original drone behaviour), ignore the gate.
            envStage_ = ENV_OPEN; envLevel_ = 1.0f; envGatePrev_ = envGate_;
        } else {
            if (envStage_ == ENV_OPEN) { envStage_ = ENV_CLOSED; envLevel_ = 0.0f; }
            bool g = envGate_;
            if (g && !envGatePrev_)       envStage_ = ENV_ATTACK;   // gate rising → (re)trigger
            else if (!g && envGatePrev_ && envStage_ != ENV_CLOSED) envStage_ = ENV_RELEASE; // gate falling
            envGatePrev_ = g;
        }

        float gL = a->gainL, gR = a->gainR;
        float steps = stepsPerSample_;
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
    }

private:
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

// ─── ChaosChua ────────────────────────────────────────────────────────────────
// Chua circuit — double-scroll attractor.
// dx = alpha*(y - x - f(x)),  dy = x - y + z,  dz = -beta*y
// f(x): piecewise-linear Chua diode, negative slope in centre region.
// CHAOS = alpha (8–16),  CHAR = beta (20–35)
// Audio: x→L, z→R  (y amplitude is tiny, ~±0.5, not suitable for audio)
class ChaosChua : public ChaosBase {
public:
    ChaosChua() {
        name       = "CHUA";
        chaosLabel = "a"; charLabel = "b";
        chaosMin   = 8.0f;   chaosMax = 11.0f;   // double-scroll bounded ~8.5–10.5
        rateMin    = 0.001f; rateMax  = 0.008f;  dtBase = 0.008f;
        oversampleMax = 32.0f;   // ~178 cyc/step - 2x a Rossler step
        charMin    = 12.0f;  charMax  = 16.0f;   // canonical 14.286 near centre
        modScale   = 1.0f;
        gainL      = 0.28f;  gainR    = 0.25f;
        xMin       = -5.0f;  xRange   = 10.0f;
        yMin       = -6.0f;  yRange   = 12.0f;  // z axis for phase plot
        cvScaleX   = 1.30f;  cvScaleY = 1.00f;
    }
    void init() override { x_ = 0.5f; y_ = 0.0f; z_ = 0.0f; }
    void setParams(float chaos, float rate, float charV) override {
        alpha_ = chaos; dt_ = rate; beta_ = charV;
    }
    void stepSample() override {
        float h1 = chuaF(x_);
        float dx1 = alpha_*(y_ - x_ - h1),  dy1 = x_ - y_ + z_,  dz1 = -beta_*y_;
        float x2 = x_+0.5f*dt_*dx1, y2 = y_+0.5f*dt_*dy1, z2 = z_+0.5f*dt_*dz1;
        float h2 = chuaF(x2);
        float dx2 = alpha_*(y2 - x2 - h2), dy2 = x2 - y2 + z2, dz2 = -beta_*y2;
        float x3 = x_+0.5f*dt_*dx2, y3 = y_+0.5f*dt_*dy2, z3 = z_+0.5f*dt_*dz2;
        float h3 = chuaF(x3);
        float dx3 = alpha_*(y3 - x3 - h3), dy3 = x3 - y3 + z3, dz3 = -beta_*y3;
        float x4 = x_+dt_*dx3, y4 = y_+dt_*dy3, z4 = z_+dt_*dz3;
        float h4 = chuaF(x4);
        float dx4 = alpha_*(y4 - x4 - h4), dy4 = x4 - y4 + z4, dz4 = -beta_*y4;
        x_ += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
        y_ += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
        z_ += dt_/6.0f*(dz1 + 2*dz2 + 2*dz3 + dz4);
        // Guard: reset if trajectory escapes the attractor
        if (!isfinite(x_) || !isfinite(z_) || fabsf(x_) > 8.0f) {
            x_ = 0.5f; y_ = 0.0f; z_ = 0.0f;
        }
    }
    float getX() const override { return x_; }
    float getY() const override { return z_; }
private:
    inline float chuaF(float x) const {
        if (x >  1.0f) return m1_*x + (m0_ - m1_);
        if (x < -1.0f) return m1_*x - (m0_ - m1_);
        return m0_*x;
    }
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    float alpha_ = 9.0f, beta_ = 14.286f;
    // Standard double-scroll slopes: m0 inner, m1 outer — BOTH must be negative.
    // With m0=-8/7, m1=-5/7 the system has three equilibria at x=0 and x=±1.5.
    static constexpr float m0_ = -8.0f / 7.0f;
    static constexpr float m1_ = -5.0f / 7.0f;
    float dt_ = 0.005f;
};

// ─── ChaosDuffing ─────────────────────────────────────────────────────────────
// Forced nonlinear oscillator — double-well potential with periodic drive.
// Autonomous 3-variable form: track phase φ = ω·t as a state variable.
// dx = y,   dy = -δy - αx - βx³ + γcos(φ),   dφ = ω
// α=-1, β=1 (double-well), δ=0.3 (damping) — fixed.
// CHAOS = γ (drive amplitude, 0.1–0.8): low = periodic, high = chaotic
// CHAR  = ω (drive frequency, 0.8–1.4): sets the base pitch
// Audio: x→L, y→R. Frequency ≈ ω·dt·44100 / 2π Hz.
class ChaosDuffing : public ChaosBase {
public:
    ChaosDuffing() {
        name       = "DUFFING";
        chaosLabel = "g"; charLabel  = "w";
        chaosMin   = 0.1f;   chaosMax = 0.8f;
        rateMin    = 0.005f; rateMax  = 0.10f;  dtBase = 0.10f;
        oversampleMax = 8.0f;    // ~543 cyc/step - 3x cosf, 6x a Rossler step
        charMin    = 0.8f;   charMax  = 1.4f;
        modScale   = 0.35f;
        gainL      = 0.55f;  gainR    = 0.55f;
        xMin       = -2.0f;  xRange   = 4.0f;
        yMin       = -2.5f;  yRange   = 5.0f;
        cvScaleX   = 3.00f;  cvScaleY = 2.50f;
    }
    void init() override { x_ = 1.0f; y_ = 0.0f; phi_ = 0.0f; }
    void setParams(float chaos, float rate, float charV) override {
        gamma_ = chaos; dt_ = rate; omega_ = charV;
    }
    void stepSample() override {
        float c1 = cosf(phi_);
        float dx1 = y_;
        float dy1 = -delta_*y_ - alpha_*x_ - beta_*x_*x_*x_ + gamma_*c1;
        float x2 = x_+0.5f*dt_*dx1, y2 = y_+0.5f*dt_*dy1;
        float p2 = phi_ + 0.5f*dt_*omega_;
        float c2 = cosf(p2);
        float dx2 = y2;
        float dy2 = -delta_*y2 - alpha_*x2 - beta_*x2*x2*x2 + gamma_*c2;
        float x3 = x_+0.5f*dt_*dx2, y3 = y_+0.5f*dt_*dy2;
        // p3 = p2 (midpoint forcing phase is the same for both RK4 k2 and k3 stages)
        float c3 = c2;
        float dx3 = y3;
        float dy3 = -delta_*y3 - alpha_*x3 - beta_*x3*x3*x3 + gamma_*c3;
        float x4 = x_+dt_*dx3, y4 = y_+dt_*dy3;
        float p4 = phi_ + dt_*omega_;
        float c4 = cosf(p4);
        float dx4 = y4;
        float dy4 = -delta_*y4 - alpha_*x4 - beta_*x4*x4*x4 + gamma_*c4;
        x_   += dt_/6.0f*(dx1 + 2*dx2 + 2*dx3 + dx4);
        y_   += dt_/6.0f*(dy1 + 2*dy2 + 2*dy3 + dy4);
        phi_ += dt_*omega_;
        if (phi_ > 6.28318f) phi_ -= 6.28318f;  // keep phi in [0, 2π)
    }
    float getX() const override { return x_; }
    float getY() const override { return y_; }
private:
    float x_ = 1.0f, y_ = 0.0f, phi_ = 0.0f;
    float gamma_ = 0.4f, omega_ = 1.2f, dt_ = 0.05f;
    static constexpr float alpha_ = -1.0f;  // double-well: negative linear term
    static constexpr float beta_  =  1.0f;  // positive cubic term
    static constexpr float delta_ =  0.3f;  // damping
};

// ─── ChaosCoupledRossler ──────────────────────────────────────────────────────
// Two Rössler systems with symmetric x-coupling.
// dx1 = -y1 - z1 + k(x2-x1),   dy1 = x1 + a·y1,   dz1 = b + z1(x1-c)
// dx2 = -y2 - z2 + k(x1-x2),   dy2 = x2 + a·y2,   dz2 = b + z2(x2-c)
// CHAOS = c (bifurcation, 2–8, shared), CHAR = k (coupling, 0.0–0.5)
// At low k: two detuned oscillators beating. At high k: synchronise.
// Oscillators start at different ICs to ensure phase diversity.
// Audio: x1→L, x2→R — true stereo output.
class ChaosCoupledRossler : public ChaosBase {
public:
    ChaosCoupledRossler() {
        name       = "CPLROSSLER";
        chaosLabel = "c"; charLabel  = "k";
        chaosMin   = 2.0f;   chaosMax = 8.0f;
        rateMin    = 0.002f; rateMax  = 0.10f;  dtBase = 0.10f;
        oversampleMax = 32.0f;   // ~178 cyc/step - two coupled systems
        charMin    = 0.0f;   charMax  = 0.5f;
        modScale   = 1.0f;
        gainL      = 0.10f;  gainR    = 0.10f;
        xMin       = -13.0f; xRange   = 26.0f;
        yMin       = -11.0f; yRange   = 22.0f;
        cvScaleX   = 0.45f;  cvScaleY = 0.45f;
    }
    void init() override {
        x1_=0.1f; y1_=0.0f; z1_=0.0f;
        x2_=0.5f; y2_=0.2f; z2_=0.0f;  // offset IC for phase diversity
    }
    void setParams(float chaos, float rate, float charV) override {
        c_ = chaos; dt_ = rate; k_ = charV;
    }
    void stepSample() override {
        // Derivatives — both oscillators coupled via x
        auto deriv = [this](float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            float& dx, float& dy, float& dz) {
            dx = -y1 - z1 + k_*(x2 - x1);
            dy =  x1 + a_*y1;
            dz =  b_ + z1*(x1 - c_);
            (void)y2; (void)z2;
        };
        float dx1a, dy1a, dz1a, dx2a, dy2a, dz2a;
        deriv(x1_,y1_,z1_, x2_,y2_,z2_, dx1a,dy1a,dz1a);
        deriv(x2_,y2_,z2_, x1_,y1_,z1_, dx2a,dy2a,dz2a);

        float x1b=x1_+0.5f*dt_*dx1a, y1b=y1_+0.5f*dt_*dy1a, z1b=z1_+0.5f*dt_*dz1a;
        float x2b=x2_+0.5f*dt_*dx2a, y2b=y2_+0.5f*dt_*dy2a, z2b=z2_+0.5f*dt_*dz2a;
        float dx1b, dy1b, dz1b, dx2b, dy2b, dz2b;
        deriv(x1b,y1b,z1b, x2b,y2b,z2b, dx1b,dy1b,dz1b);
        deriv(x2b,y2b,z2b, x1b,y1b,z1b, dx2b,dy2b,dz2b);

        float x1c=x1_+0.5f*dt_*dx1b, y1c=y1_+0.5f*dt_*dy1b, z1c=z1_+0.5f*dt_*dz1b;
        float x2c=x2_+0.5f*dt_*dx2b, y2c=y2_+0.5f*dt_*dy2b, z2c=z2_+0.5f*dt_*dz2b;
        float dx1c, dy1c, dz1c, dx2c, dy2c, dz2c;
        deriv(x1c,y1c,z1c, x2c,y2c,z2c, dx1c,dy1c,dz1c);
        deriv(x2c,y2c,z2c, x1c,y1c,z1c, dx2c,dy2c,dz2c);

        float x1d=x1_+dt_*dx1c, y1d=y1_+dt_*dy1c, z1d=z1_+dt_*dz1c;
        float x2d=x2_+dt_*dx2c, y2d=y2_+dt_*dy2c, z2d=z2_+dt_*dz2c;
        float dx1d, dy1d, dz1d, dx2d, dy2d, dz2d;
        deriv(x1d,y1d,z1d, x2d,y2d,z2d, dx1d,dy1d,dz1d);
        deriv(x2d,y2d,z2d, x1d,y1d,z1d, dx2d,dy2d,dz2d);

        x1_ += dt_/6.0f*(dx1a + 2*dx1b + 2*dx1c + dx1d);
        y1_ += dt_/6.0f*(dy1a + 2*dy1b + 2*dy1c + dy1d);
        z1_ += dt_/6.0f*(dz1a + 2*dz1b + 2*dz1c + dz1d);
        x2_ += dt_/6.0f*(dx2a + 2*dx2b + 2*dx2c + dx2d);
        y2_ += dt_/6.0f*(dy2a + 2*dy2b + 2*dy2c + dy2d);
        z2_ += dt_/6.0f*(dz2a + 2*dz2b + 2*dz2c + dz2d);
    }
    float getX() const override { return x1_; }
    float getY() const override { return x2_; }
private:
    float x1_=0.1f, y1_=0.0f, z1_=0.0f;
    float x2_=0.5f, y2_=0.2f, z2_=0.0f;
    float c_=5.7f, k_=0.05f, dt_=0.05f;
    static constexpr float a_ = 0.2f;
    static constexpr float b_ = 0.2f;
};

// ─── Algorithm registry ───────────────────────────────────────────────────────
ChaosRossler         algoRossler;
ChaosVanDerPol       algoVanDerPol;
ChaosLorenz          algoLorenz;
ChaosChua            algoChua;
ChaosDuffing         algoDuffing;
ChaosCoupledRossler  algoCoupledRossler;

ChaosBase* algos[] = {
    &algoRossler, &algoVanDerPol, &algoLorenz,
    &algoChua, &algoDuffing, &algoCoupledRossler
};
constexpr uint8_t N_ALGOS = 6;

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
    bool btn = !digitalRead(PIN_BTN);
    if (btn && !lastBtn) btnDownMs = millis();
    if (!btn && lastBtn) {
        uint32_t held = millis() - btnDownMs;
        if (held >= BTN_LONG_MS) {
            envPage = !envPage;                          // CHAOS/CHAR/DEPTH change meaning…
            pickup3_arm(pagePickup, p1n, p3n, p4n);      // …so hold them until re-caught
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
        if (p1n > 0.55f)      envEnabled = true;
        else if (p1n < 0.45f) envEnabled = false;
        if (pickup3_update(pagePickup, 1, p3n, ctlAdNorm))    ctlAdNorm    = p3n;
        if (pickup3_update(pagePickup, 2, p4n, ctlSrNorm))    ctlSrNorm    = p4n;
    }

    // ── Apply controls to the active algorithm ──
    ChaosBase* algo = engine.algo();
    float chaos = 0.0f, charV = 0.0f, rateDisp = 0.0f;
    if (algo) {
        chaos = algo->chaosMin + ctlChaosNorm * (algo->chaosMax - algo->chaosMin);
        chaos = constrain(chaos + modVolts * algo->modScale,
                          algo->chaosMin - 2.0f, algo->chaosMax + 2.0f);
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

        algo->setParams(chaos, stepDt, charV);
        engine.setStepsPerSample(steps);
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
    if (gateHigh && !prevGate && algo) algo->init();   // attractor re-init on attack
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
            int digits = (cpu >= 100) ? 3 : (cpu >= 10 ? 2 : 1);
            int w = (digits + 1) * 6;   // digits + '%', 6 px per char at size 1
            display.fillRect(127 - w, 0, w + 1, 8, SSD1306_BLACK);
            display.setCursor(128 - w, 0);
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
