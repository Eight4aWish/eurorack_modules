#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4728.h>
#include <cmath>  // For sin()
#include "pins.h"

Adafruit_MCP4728 mcp;

// ===== Pins ===== (centralized in include/esp32oscclk/pins.h)

// ===== Clock levels (12-bit DAC) =====
const int clock_high = 1500;
const int clock_low  = 0;

// ===== ADC page update cadence (was for web; keep if you like for future use) =====
const int UPDATE_INTERVAL_MS = 2000;

// ===== State =====
int potValue   = 0;
int cvValue    = 2000;
int switch1    = 0;   // down
int switch2    = 0;   // up
int counterb   = 1;
int counterc   = 1;
int delay_value= 125; // will be recalculated
int value      = 0;
int countera   = 0;

// ===== Range mapping =====
struct RangeMapping {
  int   min_val_inclusive;
  int   max_val_exclusive;
  float output_val;
};

RangeMapping lookupTable[] = {
  { 0,   47,  1479.98f}, { 47,  113, 1396.91f}, {113, 149, 1318.51f},
  {149, 186, 1244.51f}, {186, 222, 1174.66f}, {222, 257, 1108.73f},
  {257, 290, 1046.50f}, {290, 326, 987.77f},  {326, 365, 932.33f},
  {365, 402, 880.00f},  {402, 438, 830.61f},  {438, 474, 830.61f},
  {474, 510, 783.99f},  {510, 546, 739.99f},  {546, 582, 698.46f},
  {582, 620, 659.26f},  {620, 658, 622.25f},  {658, 696, 587.33f},
  {696, 732, 554.37f},  {732, 768, 523.25f},  {768, 805, 493.88f},
  {805, 841, 466.16f},  {841, 880, 440.00f},  {880, 915, 415.30f},
  {915, 950, 392.00f},  {950, 989, 369.99f},  {989, 1027, 349.23f},
  {1027, 1065, 329.63f},{1065, 1101, 311.13f},{1101, 1139, 293.66f},
  {1139, 1178, 277.18f},{1178, 1214, 261.63f},{1214, 1249, 246.94f},
  {1249, 1288, 233.08f},{1288, 1326, 220.00f},{1326, 1362, 207.65f},
  {1362, 1399, 196.00f},{1399, 1436, 185.00f},{1436, 1472, 174.61f},
  {1472, 1506, 164.81f},{1506, 1546, 155.56f},{1546, 1588, 146.83f},
  {1588, 1624, 138.59f},{1624, 1660, 130.81f},{1660, 1698, 123.47f},
  {1698, 1733, 116.54f},{1733, 1768, 110.00f},{1768, 1804, 103.83f},
  {1804, 1842, 98.00f}, {1842, 1875, 92.50f}, {1875, 1911, 87.31f},
  {1911, 1952, 82.41f}, {1952, 1985, 77.78f}, {1985, 2017, 73.42f},
  {2017, 2059, 69.30f}, {2059, 2102, 65.41f}, {2102, 2140, 61.74f},
  {2140, 2175, 58.27f}, {2175, 2207, 55.00f}, {2207, 2243, 51.91f},
  {2243, 2280, 49.00f}, {2280, 2318, 46.25f}, {2318, 2354, 43.65f},
  {2354, 2391, 41.20f}, {2391, 2429, 38.89f}, {2429, 2465, 36.71f},
  {2465, 2505, 34.65f}, {2505, 2543, 32.70f}, {2543, 2579, 30.87f},
  {2579, 2617, 29.14f}, {2617, 2653, 27.50f}, {2653, 2691, 25.96f},
  {2691, 2728, 24.50f}, {2728, 2766, 23.12f}, {2766, 2804, 21.83f},
  {2804, 2843, 20.60f}, {2843, 2883, 19.45f}, {2883, 2923, 18.35f},
  {2923, 2965, 17.32f}, {2965, 3005, 16.35f}, {3005, 3051, 15.43f},
  {3051, 3099, 14.57f}, {3099, 3143, 13.75f}, {3143, 3191, 12.98f},
  {3191, 3243, 12.26f}, {3243, 3298, 11.57f}, {3298, 3351, 10.92f},
  {3351, 3403, 10.31f}, {3403, 3462, 9.73f},  {3462, 3522, 9.19f},
  {3522, 3585, 8.67f},  {3585, 3649, 8.19f},
};
const int   lookupTableSize          = sizeof(lookupTable) / sizeof(lookupTable[0]);
const float NO_MATCH_DEFAULT_OUTPUT  = 440.0f;

float getValueFromRange(int inputValue) {
  for (int i = 0; i < lookupTableSize; ++i) {
    if (inputValue >= lookupTable[i].min_val_inclusive &&
        inputValue <  lookupTable[i].max_val_exclusive) {
      return lookupTable[i].output_val;
    }
  }
  return NO_MATCH_DEFAULT_OUTPUT;
}

// ===== Waveform engine =====
const int   WAVEFORM_TABLE_SIZE       = 512;
const float TARGET_SAMPLE_RATE_HZ     = 10250.0f;
const unsigned long SAMPLE_PERIOD_US  = (unsigned long)(1000000.0 / TARGET_SAMPLE_RATE_HZ);

enum WaveformType {
  SINE,
  TRIANGLE,
  SAWTOOTH_RISING,
  SQUARE_50,
  NUM_WAVEFORMS
};
WaveformType currentWaveform      = SINE;
WaveformType lastSelectedWaveform = SINE;
float        waveformFrequencyHz  = 1.0f;

uint16_t waveformTable[WAVEFORM_TABLE_SIZE];
float    phase          = 0.0f;
float    phaseIncrement = 0.0f;
unsigned long lastUpdateMicros = 0;

void populateWaveformTable(WaveformType type) {
  switch (type) {
    case SINE:
      for (int i = 0; i < WAVEFORM_TABLE_SIZE; i++) {
        waveformTable[i] = (uint16_t)(((sin(2.0 * PI * (double)i / WAVEFORM_TABLE_SIZE) + 1.0) * 0.5) * 4095.0);
      }
      break;
    case TRIANGLE:
      for (int i = 0; i < WAVEFORM_TABLE_SIZE; i++) {
        double val = (i < WAVEFORM_TABLE_SIZE / 2)
                      ? (double)i / (double)(WAVEFORM_TABLE_SIZE / 2)
                      : 2.0 - ((double)i / (double)(WAVEFORM_TABLE_SIZE / 2));
        uint32_t v = (uint32_t)(val * 4095.0);
        waveformTable[i] = (v > 4095) ? 4095 : (uint16_t)v;
      }
      waveformTable[0] = 0;
      break;
    case SAWTOOTH_RISING:
      for (int i = 0; i < WAVEFORM_TABLE_SIZE; i++) {
        waveformTable[i] = (uint16_t)((((double)i) / (WAVEFORM_TABLE_SIZE - 1)) * 4095.0);
      }
      break;
    case SQUARE_50:
      for (int i = 0; i < WAVEFORM_TABLE_SIZE; i++) {
        waveformTable[i] = (i < WAVEFORM_TABLE_SIZE / 2) ? 0 : 4095;
      }
      break;
    default:
      for (int i = 0; i < WAVEFORM_TABLE_SIZE; i++) waveformTable[i] = 2048;
      break;
  }
}

// ===== I2C =====
const uint8_t MCP4728_ADDRESS = 0x60;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("ESP32 MCP4728 Oscillator (no web) Starting...");

  // I2C init
  Wire.begin();
  Wire.setClock(1000000); // 1 MHz; drop to 400000 if unstable wiring

  if (!mcp.begin(MCP4728_ADDRESS)) {
    Serial.println("MCP4728 not found. Halt.");
    while (1) delay(10);
  }
  Serial.println("MCP4728 Found.");

  // ADC config
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // ~3.3V full-scale

  // Initial DAC state
  mcp.setChannelValue(MCP4728_CHANNEL_A, clock_low);
  mcp.setChannelValue(MCP4728_CHANNEL_B, clock_low);
  mcp.setChannelValue(MCP4728_CHANNEL_C, clock_low);

  // Populate first waveform & phase increment
  populateWaveformTable(currentWaveform);
  lastSelectedWaveform = currentWaveform;
  phaseIncrement = (waveformFrequencyHz * (float)WAVEFORM_TABLE_SIZE) / TARGET_SAMPLE_RATE_HZ;

  Serial.println("Setup complete.");
}

void loop() {
  // Read controls
  potValue = analogRead(PIN_POT);
  cvValue  = analogRead(PIN_CV);
  switch2  = analogRead(PIN_SW_UP);
  switch1  = analogRead(PIN_SW_DOWN);

  // Map pot to a musically useful clock delay range (clamped)
  // Original: delay_value = 250 - ((4096 - potValue) / 20);
  // This could go negative; clamp to e.g. 5..250 ms.
  delay_value = 250 - ((4096 - potValue) / 20);
  delay_value = constrain(delay_value, 5, 250);

  // === OSC MODE (when switch1 > threshold) ===
  if (switch1 > 500) {
    // Waveform selection from pot
    int segmentSize = 4096 / (int)NUM_WAVEFORMS;
    WaveformType selectedWaveform = (WaveformType)(potValue / segmentSize);
    if (selectedWaveform >= NUM_WAVEFORMS) selectedWaveform = (WaveformType)((int)NUM_WAVEFORMS - 1);

    if (selectedWaveform != lastSelectedWaveform) {
      currentWaveform = selectedWaveform;
      populateWaveformTable(currentWaveform);
      lastSelectedWaveform = currentWaveform;
      phase = 0.0f; // avoid clicks on waveform change
    }

    // Frequency from CV (via your lookup table)
    waveformFrequencyHz = getValueFromRange(cvValue);
    phaseIncrement = (waveformFrequencyHz * (float)WAVEFORM_TABLE_SIZE) / TARGET_SAMPLE_RATE_HZ;

    // Timed DAC updates on Channel C
    unsigned long now = micros();
    if (now - lastUpdateMicros >= SAMPLE_PERIOD_US) {
      // keep schedule tight even if we overrun a bit
      lastUpdateMicros += SAMPLE_PERIOD_US;

      phase += phaseIncrement;
      if (phase >= (float)WAVEFORM_TABLE_SIZE) phase -= (float)WAVEFORM_TABLE_SIZE;

      uint16_t cvdacValue = waveformTable[(uint16_t)phase];
      (void)mcp.setChannelValue(MCP4728_CHANNEL_C, cvdacValue);
    }
  }

  // === CLOCK MODE (when switch2 > threshold) ===
  if (switch2 > 500) {
    mcp.setChannelValue(MCP4728_CHANNEL_B, clock_high);
    if (counterc == 8) {
      mcp.setChannelValue(MCP4728_CHANNEL_A, clock_high);
      counterc = 0;
    }
    counterb++;
    counterc++;
    delay(delay_value / 8);
    mcp.setChannelValue(MCP4728_CHANNEL_A, clock_low);
    mcp.setChannelValue(MCP4728_CHANNEL_B, clock_low);
    delay(delay_value / 8);
  }
}
 