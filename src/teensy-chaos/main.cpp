#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "teensy-chaos/pins.h"

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
        rateMin    = 0.002f; rateMax  = 0.1f;
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
        rateMin    = 0.002f; rateMax  = 0.15f;
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
        rateMin    = 0.001f; rateMax  = 0.003f;
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
class AudioChaosEngine : public AudioStream {
public:
    AudioChaosEngine() : AudioStream(0, nullptr) {}

    void setAlgo(ChaosBase* a) {
        if (a == algo_) return;
        if (a) a->init();      // initialise state before making live
        dcL_ = dcR_ = 0.0f;   // flush DC history on switch
        algo_ = a;             // atomic pointer store
    }

    ChaosBase* algo() const { return algo_; }
    float getX() const { ChaosBase* a = algo_; return a ? a->getX() : 0.0f; }
    float getY() const { ChaosBase* a = algo_; return a ? a->getY() : 0.0f; }

    void update() override {
        ChaosBase* a = algo_;   // single atomic load — consistent for this block
        if (!a) return;
        audio_block_t* bL = allocate();
        if (!bL) return;
        audio_block_t* bR = allocate();
        if (!bR) { release(bL); return; }

        float gL = a->gainL, gR = a->gainR;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            a->stepSample();
            float outL = tanhf(a->getX() * gL) * 32000.0f;
            float outR = tanhf(a->getY() * gR) * 32000.0f;
            outL -= dcL_; dcL_ += outL * 0.0007f;
            outR -= dcR_; dcR_ += outR * 0.0007f;
            bL->data[i] = (int16_t)outL;
            bR->data[i] = (int16_t)outR;
        }
        transmit(bL, 0); transmit(bR, 1);
        release(bL); release(bR);
    }

private:
    ChaosBase* algo_ = nullptr;
    float dcL_ = 0.0f, dcR_ = 0.0f;
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
        rateMin    = 0.001f; rateMax  = 0.008f;
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
        rateMin    = 0.005f; rateMax  = 0.10f;
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
        rateMin    = 0.002f; rateMax  = 0.10f;
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
    // Pots
    int p1 = readPot(PIN_CHAOS);
    int p2 = readPot(PIN_RATE);
    int p3 = readPot(PIN_CHAR);
    int p4 = readPot(PIN_DEPTH);

    // CV inputs (ADS1115)
    int16_t cv[4];
    adsReadAll(cv);

    // Algorithm selection — button cycles through all algorithms
    static uint8_t  algoIdx   = 0;
    static bool     lastBtn   = false;
    static uint32_t lastBtnMs = 0;
    bool btn = !digitalRead(PIN_BTN);
    if (btn && !lastBtn && millis() - lastBtnMs > 50) {
        algoIdx = (algoIdx + 1) % N_ALGOS;
        engine.setAlgo(algos[algoIdx]);
        lastBtnMs = millis();
    }
    lastBtn = btn;

    // CV calibration (measured 2026-04-07)
    // Input circuit is inverting: higher Eurorack voltage → lower ADS code
    float modVolts  = (13236.0f - cv[0]) / 2414.0f;  // MOD:  0V=13236, 2414 codes/V
    float asgnVolts = (13240.0f - cv[1]) / 2424.0f;  // ASGN: 0V=13240, 2424 codes/V
    float clkVolts  = (13241.0f - cv[2]) / 2421.0f;  // CLK:  0V=13241, 2421 codes/V
    // RST: cv[3] < 10820 = above ~1V (code 10820 corresponds to +1V threshold)

    // Apply controls to active algorithm — ranges read from metadata
    ChaosBase* algo = engine.algo();
    float chaos = 0.0f, rate = 0.0f, charV = 0.0f;
    if (algo) {
        chaos = algo->chaosMin + (p1 / 1023.0f) * (algo->chaosMax - algo->chaosMin);
        rate  = algo->rateMin  + (p2 / 1023.0f) * (algo->rateMax  - algo->rateMin);
        charV = algo->charMin  + (p3 / 1023.0f) * (algo->charMax  - algo->charMin);
        chaos = constrain(chaos + modVolts * algo->modScale,
                          algo->chaosMin - 2.0f, algo->chaosMax + 2.0f);
        rate  = constrain(rate * powf(2.0f, clkVolts + asgnVolts),
                          algo->rateMin * 0.5f, algo->rateMax * 2.0f);
        algo->setParams(chaos, rate, charV);
    }

    float depth = 0.1f + (p4 / 1023.0f) * 0.9f;
    ampL.gain(depth);
    ampR.gain(depth);

    // RST: rising edge above ~1V resets active algorithm to initial conditions
    static int16_t lastRst = 0;
    if (cv[3] < 10820 && lastRst >= 10820) {
        if (algo) algo->init();
    }
    lastRst = cv[3];

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

        if (algo) {
            // Row 1: algorithm name + chaos param
            display.setCursor(0, 34);
            display.print(algo->name);
            display.setCursor(72, 34);
            display.print(algo->chaosLabel); display.print(':');
            display.print(chaos, 1);

            // Row 2: char param + rate
            display.setCursor(0, 44);
            display.print(algo->charLabel); display.print(':');
            display.print(charV, 2);
            display.setCursor(72, 44);
            display.print("dt:"); display.print(rate, 4);

            // Row 3: depth + peak levels
            float lv = peakL.available() ? peakL.read() : 0.0f;
            float rv = peakR.available() ? peakR.read() : 0.0f;
            display.setCursor(0, 54);
            display.print("dp:"); display.print(depth, 2);
            display.setCursor(72, 54);
            display.print("L"); display.print((int)(lv * 9));
            display.print(" R"); display.print((int)(rv * 9));
        }

        display.display();
    }

    // CV outputs: active algorithm state → X and Y jacks
    if (algo) {
        dacWriteVolts(0, constrain(engine.getX() * algo->cvScaleX, -4.9f, 4.9f));
        dacWriteVolts(1, constrain(engine.getY() * algo->cvScaleY, -4.9f, 4.9f));
    }
}
