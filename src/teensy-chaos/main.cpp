#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "teensy-chaos/pins.h"

// ---------------------------------------------------------------------------
// ADS1115 — Wire1 (SDA=17, SCL=16), address 0x48
// Single-ended reads on AIN0-AIN3, ±4.096V range, single-shot mode.
// ---------------------------------------------------------------------------
#define ADS_ADDR 0x48
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

// Read all 4 single-ended channels into out[0..3].
// Mux bits for AIN0-GND..AIN3-GND are 0b100..0b111.
static void adsReadAll(int16_t out[4]) {
    for (int ch = 0; ch < 4; ch++) {
        adsWriteCfg(0b100 + ch);
        delayMicroseconds(1200);  // 860 SPS → ~1.16ms per conversion
        out[ch] = adsReadConv();
    }
}

// ---------------------------------------------------------------------------
// Rössler oscillator as a Teensy AudioStream (stereo: x→L, y→R)
// ---------------------------------------------------------------------------
class AudioRossler : public AudioStream {
public:
    AudioRossler() : AudioStream(0, nullptr) {}

    void setParams(float chaos, float rate, float character) {
        c_  = chaos;
        dt_ = rate;
        a_  = character;
    }

    void reset() {
        x_ = 0.1f; y_ = 0.0f; z_ = 0.0f;
        dcL_ = 0.0f; dcR_ = 0.0f;
    }

    // Read last computed state for CV output (safe to call from loop())
    float getX() const { return x_; }
    float getY() const { return y_; }

    void update() override {
        audio_block_t *blockL = allocate();
        if (!blockL) return;
        audio_block_t *blockR = allocate();
        if (!blockR) { release(blockL); return; }

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            float dx1 = -y_ - z_;
            float dy1 = x_ + a_ * y_;
            float dz1 = b_ + z_ * (x_ - c_);
            float x2 = x_ + 0.5f * dt_ * dx1, y2 = y_ + 0.5f * dt_ * dy1, z2 = z_ + 0.5f * dt_ * dz1;
            float dx2 = -y2 - z2, dy2 = x2 + a_ * y2, dz2 = b_ + z2 * (x2 - c_);
            float x3 = x_ + 0.5f * dt_ * dx2, y3 = y_ + 0.5f * dt_ * dy2, z3 = z_ + 0.5f * dt_ * dz2;
            float dx3 = -y3 - z3, dy3 = x3 + a_ * y3, dz3 = b_ + z3 * (x3 - c_);
            float x4 = x_ + dt_ * dx3, y4 = y_ + dt_ * dy3, z4 = z_ + dt_ * dz3;
            float dx4 = -y4 - z4, dy4 = x4 + a_ * y4, dz4 = b_ + z4 * (x4 - c_);
            x_ += dt_ / 6.0f * (dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_ / 6.0f * (dy1 + 2*dy2 + 2*dy3 + dy4);
            z_ += dt_ / 6.0f * (dz1 + 2*dz2 + 2*dz3 + dz4);
            float outL = tanhf(x_ * gain_) * 32000.0f;
            float outR = tanhf(y_ * gain_) * 32000.0f;
            outL -= dcL_; dcL_ += outL * 0.0007f;
            outR -= dcR_; dcR_ += outR * 0.0007f;
            blockL->data[i] = (int16_t)outL;
            blockR->data[i] = (int16_t)outR;
        }
        transmit(blockL, 0); transmit(blockR, 1);
        release(blockL); release(blockR);
    }

private:
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    float a_ = 0.2f, b_ = 0.2f, c_ = 5.7f;
    float dt_ = 0.05f, gain_ = 0.12f;
    float dcL_ = 0.0f, dcR_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Lorenz attractor as a Teensy AudioStream (stereo: x→L, y→R)
// dx=sigma(y-x), dy=x(rho-z)-y, dz=xy-beta*z
// ---------------------------------------------------------------------------
class AudioLorenz : public AudioStream {
public:
    AudioLorenz() : AudioStream(0, nullptr) {}

    void setParams(float chaos, float rate, float character) {
        rho_   = chaos;      // rho:   20–32 (chaos parameter)
        dt_    = rate;       // dt:    integration step
        sigma_ = character;  // sigma: 8–14 (character)
    }

    void reset() {
        x_ = 0.1f; y_ = 0.0f; z_ = 0.0f;
        dcL_ = 0.0f; dcR_ = 0.0f;
    }

    float getX() const { return x_; }
    float getY() const { return z_; }  // z on right: smoother, always positive

    void update() override {
        audio_block_t *blockL = allocate();
        if (!blockL) return;
        audio_block_t *blockR = allocate();
        if (!blockR) { release(blockL); return; }

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            float dx1 = sigma_ * (y_ - x_);
            float dy1 = x_ * (rho_ - z_) - y_;
            float dz1 = x_ * y_ - beta_ * z_;
            float x2 = x_ + 0.5f*dt_*dx1, y2 = y_ + 0.5f*dt_*dy1, z2 = z_ + 0.5f*dt_*dz1;
            float dx2 = sigma_*(y2-x2), dy2 = x2*(rho_-z2)-y2, dz2 = x2*y2-beta_*z2;
            float x3 = x_ + 0.5f*dt_*dx2, y3 = y_ + 0.5f*dt_*dy2, z3 = z_ + 0.5f*dt_*dz2;
            float dx3 = sigma_*(y3-x3), dy3 = x3*(rho_-z3)-y3, dz3 = x3*y3-beta_*z3;
            float x4 = x_ + dt_*dx3, y4 = y_ + dt_*dy3, z4 = z_ + dt_*dz3;
            float dx4 = sigma_*(y4-x4), dy4 = x4*(rho_-z4)-y4, dz4 = x4*y4-beta_*z4;
            x_ += dt_/6.0f*(dx1+2*dx2+2*dx3+dx4);
            y_ += dt_/6.0f*(dy1+2*dy2+2*dy3+dy4);
            z_ += dt_/6.0f*(dz1+2*dz2+2*dz3+dz4);
            float outL = tanhf(x_ * gain_) * 32000.0f;
            // z is always positive (~0–50), centre it before output
            float outR = tanhf((z_ - rho_) * gain_) * 32000.0f;
            outL -= dcL_; dcL_ += outL * 0.0007f;
            outR -= dcR_; dcR_ += outR * 0.0007f;
            blockL->data[i] = (int16_t)outL;
            blockR->data[i] = (int16_t)outR;
        }
        transmit(blockL, 0); transmit(blockR, 1);
        release(blockL); release(blockR);
    }

private:
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    float sigma_ = 10.0f, beta_ = 2.667f, rho_ = 28.0f;
    float dt_ = 0.002f, gain_ = 0.05f;
    float dcL_ = 0.0f, dcR_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Audio objects
// ---------------------------------------------------------------------------
AudioRossler         rossler;
AudioLorenz          lorenz;
AudioMixer4          mixL;
AudioMixer4          mixR;
AudioAmplifier       ampL;
AudioAmplifier       ampR;
AudioOutputI2S       audioOut;
AudioAnalyzePeak     peakL;
AudioAnalyzePeak     peakR;
AudioControlSGTL5000 codec;

// Both algorithms feed mixer; active one gets gain=1, inactive gets gain=0
AudioConnection      patchRosL(rossler, 0, mixL, 0);
AudioConnection      patchRosR(rossler, 1, mixR, 0);
AudioConnection      patchLorL(lorenz,  0, mixL, 1);
AudioConnection      patchLorR(lorenz,  1, mixR, 1);
AudioConnection      patchMixL(mixL, 0, ampL, 0);
AudioConnection      patchMixR(mixR, 0, ampR, 0);
AudioConnection      patchConnL(ampL, 0, audioOut, 0);
AudioConnection      patchConnR(ampR, 0, audioOut, 1);
AudioConnection      patchPeakL(ampL, 0, peakL, 0);
AudioConnection      patchPeakR(ampR, 0, peakR, 0);

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ---------------------------------------------------------------------------
// MCP4822 DAC
// Calibrated: Vout = code * 0.002431 - 4.972  (both X and Y match ±10mV)
// Range: -4.97V (code 0) to +4.98V (code 4095), 0V at code ~2044
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
void setup() {
    // Disable Audio Shield SD card CS (shared with OLED CS on pin 10)
    pinMode(10, OUTPUT);
    digitalWriteFast(10, HIGH);

    // Wire (I2C0) — Audio Shield SGTL5000 control
    Wire.setSDA(18); Wire.setSCL(19);
    Wire.begin(); Wire.setClock(400000);

    // Wire1 (I2C1) — ADS1115 CV inputs
    Wire1.setSDA(17); Wire1.setSCL(16);
    Wire1.begin(); Wire1.setClock(400000);

    AudioMemory(20);

    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);
    codec.volume(0.7);
    codec.lineOutLevel(29);
    ampL.gain(1.0);
    ampR.gain(1.0);
    // Start with Rössler active
    mixL.gain(0, 1.0f); mixL.gain(1, 0.0f);
    mixR.gain(0, 1.0f); mixR.gain(1, 0.0f);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);

    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay();
    display.display();
}

void loop() {
    // Read controls every loop — parameters update continuously
    int p1 = readPot(PIN_CHAOS);
    int p2 = readPot(PIN_RATE);
    int p3 = readPot(PIN_CHAR);
    int p4 = readPot(PIN_DEPTH);

    int16_t cv[4];
    adsReadAll(cv);

    // Algorithm selection — button press cycles Rössler → Lorenz → ...
    static uint8_t algo = 0;  // 0=Rossler, 1=Lorenz
    static bool lastBtn = false;
    static uint32_t lastBtnMs = 0;
    bool btn = !digitalRead(PIN_BTN);
    if (btn && !lastBtn && millis() - lastBtnMs > 50) {
        algo = (algo + 1) % 2;
        mixL.gain(0, algo == 0 ? 1.0f : 0.0f);
        mixL.gain(1, algo == 1 ? 1.0f : 0.0f);
        mixR.gain(0, algo == 0 ? 1.0f : 0.0f);
        mixR.gain(1, algo == 1 ? 1.0f : 0.0f);
        lastBtnMs = millis();
    }
    lastBtn = btn;

    float chaos, rate, charV;
    float depth = 0.1f + (p4 / 1023.0f) * 0.9f;

    // MOD CV: bipolar volts (calibrated: 0V=13236, 2414 codes/V)
    float modVolts  = (13236.0f - cv[0]) / 2414.0f;
    // ASGN CV: bipolar rate modulation (calibrated: 0V=13240, 2424 codes/V)
    float asgnVolts = (13240.0f - cv[1]) / 2424.0f;
    // CLK CV: V/Oct (calibrated: 0V=13241, 2421 codes/V)
    float clkVolts  = (13241.0f - cv[2]) / 2421.0f;

    if (algo == 0) {
        // Rössler: c 2–8, dt 0.002–0.1, a 0.1–0.4
        chaos = 2.0f  + (p1 / 1023.0f) * 6.0f;
        rate  = 0.002f + (p2 / 1023.0f) * 0.098f;
        charV = 0.1f  + (p3 / 1023.0f) * 0.3f;
        chaos = constrain(chaos + modVolts, 2.0f, 9.0f);
        rate  = constrain(rate * powf(2.0f, clkVolts + asgnVolts), 0.001f, 0.15f);
        rossler.setParams(chaos, rate, charV);
    } else {
        // Lorenz: rho 24–32, dt 0.001–0.003, sigma 6–14
        chaos = 24.0f  + (p1 / 1023.0f) * 8.0f;
        rate  = 0.001f + (p2 / 1023.0f) * 0.002f;
        charV = 6.0f   + (p3 / 1023.0f) * 8.0f;
        chaos = constrain(chaos + modVolts * 2.0f, 20.0f, 36.0f);
        rate  = constrain(rate * powf(2.0f, clkVolts + asgnVolts), 0.0005f, 0.004f);
        lorenz.setParams(chaos, rate, charV);
    }

    ampL.gain(depth);
    ampR.gain(depth);

    // RST: rising edge above ~1V snaps active algorithm to initial conditions
    static int16_t lastRst = 0;
    if (cv[3] < 10820 && lastRst >= 10820) {
        if (algo == 0) rossler.reset(); else lorenz.reset();
    }
    lastRst = cv[3];

    /* --- CALIBRATION SCREEN (retired 2026-04-07, data saved in code) -------
    // CV input calibration: zero=13236-13242, scale=2414-2424 codes/V
    // DAC calibration: Vout = code*0.002431 - 4.972, 0V at code ~2044
    ----------------------------------------------------------------------- */

    // Phase plot ring buffer — sampled every loop iteration
    // Rössler x: ~-11 to +13, y: ~-11 to +11  |  Lorenz x: ~-20 to +20, y: ~-25 to +25
    #define PLOT_W   128
    #define PLOT_H   32
    #define TRAIL    96
    static uint8_t trailX[TRAIL], trailY[TRAIL];
    static uint8_t trailHead = 0;
    {
        float cx, cy, xrange, yrange, xmin, ymin;
        if (algo == 0) {
            cx = rossler.getX(); cy = rossler.getY();
            xmin = -11.0f; xrange = 24.0f;
            ymin = -11.0f; yrange = 22.0f;
        } else {
            cx = lorenz.getX(); cy = lorenz.getY();
            xmin = -20.0f; xrange = 40.0f;
            ymin =   0.0f; yrange = 55.0f;  // z axis: always positive ~0–50
        }
        float px = (cx - xmin) * (PLOT_W - 1) / xrange;
        float py = (cy - ymin) * (PLOT_H - 1) / yrange;
        trailX[trailHead] = (uint8_t)constrain(px, 0, PLOT_W - 1);
        trailY[trailHead] = (uint8_t)constrain(PLOT_H - 1 - py, 0, PLOT_H - 1);
        trailHead = (trailHead + 1) % TRAIL;
    }

    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 100) {
        lastDisp = millis();

        display.clearDisplay();

        // Phase space plot
        for (uint8_t i = 0; i < TRAIL; i++) {
            display.drawPixel(trailX[i], trailY[i], SSD1306_WHITE);
        }

        // Parameter rows below plot
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        // Row 1: algorithm name + primary chaos param
        display.setCursor(0, 34);
        display.print(algo == 0 ? "ROSSLER" : "LORENZ ");
        display.setCursor(64, 34);
        display.print(algo == 0 ? "c:" : "r:"); display.print(chaos, 1);

        // Row 2: char param + rate
        display.setCursor(0, 44);
        display.print(algo == 0 ? "a:" : "s:"); display.print(charV, 2);
        display.setCursor(64, 44);
        display.print("dt:"); display.print(rate, algo == 0 ? 3 : 4);

        // Row 3: depth + peak
        float lv = peakL.available() ? peakL.read() : 0.0f;
        float rv = peakR.available() ? peakR.read() : 0.0f;
        display.setCursor(0, 54);
        display.print("dp:"); display.print(depth, 2);
        display.setCursor(64, 54);
        display.print("L"); display.print((int)(lv * 9));
        display.print(" R"); display.print((int)(rv * 9));

        display.display();
    }

    // CV outputs: active algorithm x→X, y→Y
    float cvX = (algo == 0) ? rossler.getX() * 0.5f  : lorenz.getX() * 0.25f;
    float cvY = (algo == 0) ? rossler.getY() * 0.5f  : (lorenz.getY() - chaos) * 0.15f;
    dacWriteVolts(0, constrain(cvX, -4.9f, 4.9f));
    dacWriteVolts(1, constrain(cvY, -4.9f, 4.9f));
}
