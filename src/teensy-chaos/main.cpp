#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "teensy-chaos/pins.h"

// ---------------------------------------------------------------------------
// Rössler oscillator as a Teensy AudioStream (stereo: x→L, y→R)
// ---------------------------------------------------------------------------
class AudioRossler : public AudioStream {
public:
    AudioRossler() : AudioStream(0, nullptr) {}

    void setParams(float chaos, float rate, float character) {
        c_  = chaos;      // bifurcation: ~2 (periodic) to ~8 (chaotic)
        dt_ = rate;       // integration step: controls pitch
        a_  = character;  // spiral tightness (~0.1–0.4)
    }

    void update() override {
        audio_block_t *blockL = allocate();
        if (!blockL) return;
        audio_block_t *blockR = allocate();
        if (!blockR) { release(blockL); return; }

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            // RK4 integration
            float dx1 = -y_ - z_;
            float dy1 = x_ + a_ * y_;
            float dz1 = b_ + z_ * (x_ - c_);

            float x2 = x_ + 0.5f * dt_ * dx1;
            float y2 = y_ + 0.5f * dt_ * dy1;
            float z2 = z_ + 0.5f * dt_ * dz1;
            float dx2 = -y2 - z2;
            float dy2 = x2 + a_ * y2;
            float dz2 = b_ + z2 * (x2 - c_);

            float x3 = x_ + 0.5f * dt_ * dx2;
            float y3 = y_ + 0.5f * dt_ * dy2;
            float z3 = z_ + 0.5f * dt_ * dz2;
            float dx3 = -y3 - z3;
            float dy3 = x3 + a_ * y3;
            float dz3 = b_ + z3 * (x3 - c_);

            float x4 = x_ + dt_ * dx3;
            float y4 = y_ + dt_ * dy3;
            float z4 = z_ + dt_ * dz3;
            float dx4 = -y4 - z4;
            float dy4 = x4 + a_ * y4;
            float dz4 = b_ + z4 * (x4 - c_);

            x_ += dt_ / 6.0f * (dx1 + 2*dx2 + 2*dx3 + dx4);
            y_ += dt_ / 6.0f * (dy1 + 2*dy2 + 2*dy3 + dy4);
            z_ += dt_ / 6.0f * (dz1 + 2*dz2 + 2*dz3 + dz4);

            // Soft-clip and scale to int16 range
            float outL = tanhf(x_ * gain_) * 32000.0f;
            float outR = tanhf(y_ * gain_) * 32000.0f;

            // DC blocking (one-pole high-pass)
            outL = outL - dcL_;
            dcL_ += outL * 0.0007f;
            outR = outR - dcR_;
            dcR_ += outR * 0.0007f;

            blockL->data[i] = (int16_t)outL;
            blockR->data[i] = (int16_t)outR;
        }

        transmit(blockL, 0);
        transmit(blockR, 1);
        release(blockL);
        release(blockR);
    }

private:
    // State variables
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
    // Parameters
    float a_ = 0.2f, b_ = 0.2f, c_ = 5.7f;
    float dt_ = 0.05f;
    float gain_ = 0.12f;
    // DC blocking state
    float dcL_ = 0.0f, dcR_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Audio objects
// ---------------------------------------------------------------------------
AudioInputI2S        audioIn;
AudioAmplifier       ampL;
AudioAmplifier       ampR;
AudioOutputI2S       audioOut;
AudioAnalyzePeak     peakL;
AudioAnalyzePeak     peakR;
AudioControlSGTL5000 codec;

// Mono right input → both outputs (with software gain)
AudioConnection      patchInL(audioIn, 1, ampL, 0);
AudioConnection      patchInR(audioIn, 1, ampR, 0);
AudioConnection      patchOutL(ampL, 0, audioOut, 0);
AudioConnection      patchOutR(ampR, 0, audioOut, 1);
// Tap input for level metering (post-gain)
AudioConnection      patchPeakL(ampL, 0, peakL, 0);
AudioConnection      patchPeakR(ampR, 0, peakR, 0);

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
#define OLED_CS  10
#define OLED_DC   9
#define OLED_RST  6
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ---------------------------------------------------------------------------
// MCP4822 DAC (direct GPIO CS)
// ---------------------------------------------------------------------------
#define PIN_CS_DAC 33

void dacWrite(uint8_t ch, uint16_t val) {
    // Bit 15: A/B, Bit 13: GA (0=2x), Bit 12: SHDN (1=active)
    uint16_t frame = (ch ? 0x8000 : 0) | 0x1000 | (val & 0x0FFF);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWriteFast(PIN_CS_DAC, LOW);
    SPI.transfer16(frame);
    digitalWriteFast(PIN_CS_DAC, HIGH);
    SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// Triangle wave state
// ---------------------------------------------------------------------------
uint16_t dacValA = 0;
uint16_t dacValB = 4095;
bool dirA = true;   // true = rising
bool dirB = false;  // false = falling
const uint16_t DAC_STEP = 4;

// ---------------------------------------------------------------------------
void setup() {
    // Disable Audio Shield SD card — its CS is also on pin 10 (shared with
    // OLED_CS).  Drive it HIGH early so the SD card stays deselected and
    // doesn't contend on the SPI bus.  The Teensy 4.1 built-in SD card uses
    // SDIO (a separate bus) and is unaffected.
    pinMode(10, OUTPUT);
    digitalWriteFast(10, HIGH);

    AudioMemory(12);

    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);     // 0-15: 5 = default (~1.33 Vpp full-scale)
    codec.volume(0.7);
    codec.lineOutLevel(29);   // 29 = ~1.29 Vpp (conservative)

    ampL.gain(1.0);           // unity — no software boost
    ampR.gain(1.0);

    // Button
    pinMode(PIN_BTN, INPUT_PULLUP);

    // DAC CS
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);

    // OLED
    if (display.begin(SSD1306_SWITCHCAPVCC)) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(16, 4);
        display.println("TEENSY");
        display.setCursor(20, 24);
        display.println("CHAOS");
        display.setTextSize(1);
        display.setCursor(4, 48);
        display.print("Audio passthrough");
        display.display();
    }
}

void loop() {
    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 100) {
        lastDisp = millis();

        int p1 = readPot(PIN_POT1);
        int p2 = readPot(PIN_POT2);
        int p3 = readPot(PIN_POT3);
        int p4 = readPot(PIN_POT4);
        bool btn = !digitalRead(PIN_BTN);  // active-low

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        display.setCursor(0, 0);
        display.print("P1:"); display.print(p1);
        display.setCursor(64, 0);
        display.print("P2:"); display.print(p2);

        display.setCursor(0, 12);
        display.print("P3:"); display.print(p3);
        display.setCursor(64, 12);
        display.print("P4:"); display.print(p4);

        display.setCursor(0, 24);
        display.print("BTN:"); display.print(btn ? "PRESSED" : "open");

        display.setCursor(0, 36);
        display.print("PASS L:");
        display.print(peakL.available() ? peakL.read() : 0.0f, 2);
        display.print(" R:");
        display.print(peakR.available() ? peakR.read() : 0.0f, 2);

        display.display();
    }
}
