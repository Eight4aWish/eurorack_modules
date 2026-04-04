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
// Audio objects
// ---------------------------------------------------------------------------
AudioRossler         rossler;
AudioAmplifier       ampL;
AudioAmplifier       ampR;
AudioOutputI2S       audioOut;
AudioAnalyzePeak     peakL;
AudioAnalyzePeak     peakR;
AudioControlSGTL5000 codec;

// Rössler x→L, y→R, DEPTH pot controls amp gain
AudioConnection      patchOutL(rossler, 0, ampL, 0);
AudioConnection      patchOutR(rossler, 1, ampR, 0);
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
// ---------------------------------------------------------------------------
void dacWrite(uint8_t ch, uint16_t val) {
    uint16_t frame = (ch ? 0x8000 : 0) | 0x1000 | (val & 0x0FFF);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWriteFast(PIN_CS_DAC, LOW);
    SPI.transfer16(frame);
    digitalWriteFast(PIN_CS_DAC, HIGH);
    SPI.endTransaction();
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

    AudioMemory(12);

    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);
    codec.volume(0.7);
    codec.lineOutLevel(29);
    ampL.gain(1.0);
    ampR.gain(1.0);

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

    // Scale to Rössler parameters
    float chaos = 2.0f + (p1 / 1023.0f) * 6.0f;      // c:  2.0 – 8.0
    float rate  = 0.002f + (p2 / 1023.0f) * 0.098f;   // dt: 0.002 – 0.1
    float charV = 0.1f + (p3 / 1023.0f) * 0.3f;       // a:  0.1 – 0.4
    float depth = 0.1f + (p4 / 1023.0f) * 0.9f;       // gain: 0.1 – 1.0

    // MOD CV: bipolar modulation of chaos (floating centre ~4600)
    float mod = (cv[0] - 4600.0f) / 28000.0f * 3.0f;
    chaos = constrain(chaos + mod, 2.0f, 9.0f);

    // CLK CV: V/Oct pitch modulation of rate
    // Circuit centre ~13000 codes = 0V; ~2430 codes per Eurorack volt (uncalibrated)
    float clkVolts = (13000.0f - cv[2]) / 2430.0f;
    rate *= powf(2.0f, clkVolts);
    rate = constrain(rate, 0.001f, 0.15f);

    rossler.setParams(chaos, rate, charV);
    ampL.gain(depth);
    ampR.gain(depth);

    // RST: rising edge snaps oscillator back to initial conditions
    static int16_t lastRst = 0;
    if (cv[3] > 8000 && lastRst <= 8000) rossler.reset();
    lastRst = cv[3];

    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 100) {
        lastDisp = millis();

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        // Row 0: algorithm name
        display.setCursor(0, 0);
        display.print("ROSSLER");

        // Row 1: chaos (modulated) and char values
        display.setCursor(0, 10);
        display.print("c:"); display.print(chaos, 2);
        display.setCursor(64, 10);
        display.print("a:"); display.print(charV, 2);

        // Row 2: rate value
        display.setCursor(0, 20);
        display.print("dt:"); display.print(rate, 3);

        // Row 3: DEPTH bar
        display.setCursor(0, 30);
        display.print("DEP");
        display.drawRect(22, 30, 106, 7, SSD1306_WHITE);
        display.fillRect(23, 31, (int)(depth * 104), 5, SSD1306_WHITE);

        // Row 4: L/R peak bars
        float lv = peakL.available() ? peakL.read() : 0.0f;
        float rv = peakR.available() ? peakR.read() : 0.0f;
        display.setCursor(0, 41);  display.print("L");
        display.drawRect(8, 41, 52, 7, SSD1306_WHITE);
        display.fillRect(9, 42, (int)(lv * 50), 5, SSD1306_WHITE);
        display.setCursor(64, 41); display.print("R");
        display.drawRect(72, 41, 52, 7, SSD1306_WHITE);
        display.fillRect(73, 42, (int)(rv * 50), 5, SSD1306_WHITE);

        // Row 5: CV activity (dot = active, dash = idle)
        display.setCursor(0,  54); display.print("MD"); display.print(cv[0] > 8000 ? "*" : "-");
        display.setCursor(32, 54); display.print("AS"); display.print(cv[1] > 8000 ? "*" : "-");
        display.setCursor(64, 54); display.print("CK"); display.print(cv[2] > 8000 ? "*" : "-");
        display.setCursor(96, 54); display.print("RS"); display.print(cv[3] > 8000 ? "*" : "-");

        display.display();
    }

    // Slow triangle LFO on both DAC channels (antiphase) — ~8s period
    {
        uint32_t t = millis() % 8000;
        uint16_t val = (t < 4000) ? (t * 4095 / 4000) : ((8000 - t) * 4095 / 4000);
        dacWrite(0, val);
        dacWrite(1, 4095 - val);
    }
}
