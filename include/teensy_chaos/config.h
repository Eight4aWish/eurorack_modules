#pragma once
#include <cstdint>

// ---------- Algorithm groups ----------
enum AlgoGroup : uint8_t {
    GROUP_MELODIC    = 0,
    GROUP_PERCUSSIVE = 1,
    GROUP_TEXTURE    = 2,
    GROUP_COUNT      = 3
};

// Total number of algorithms
constexpr uint8_t ALGO_COUNT = 14;

// Algorithms per group
constexpr uint8_t ALGO_PER_GROUP[] = { 4, 4, 6 };

// ---------- Audio ----------
constexpr float   SAMPLE_RATE     = 44100.0f;
constexpr uint8_t AUDIO_BLOCK_SIZE = 128;  // Teensy Audio Library default

// ---------- Parameter ranges ----------
// Default dt range for continuous oscillators (mapped from POT2)
constexpr float DT_MIN = 0.001f;
constexpr float DT_MAX = 0.12f;

// DC-blocking high-pass filter coefficient (~5 Hz at 44.1 kHz)
// alpha = 1 - (2*pi*fc / fs)
constexpr float DC_BLOCK_COEFF = 0.99929f;

// Soft-clip output scaling (pre-tanh gain)
constexpr float OUTPUT_GAIN = 0.15f;

// ---------- Controls ----------
constexpr uint16_t BTN_LONG_PRESS_MS  = 500;
constexpr uint16_t BTN_DEBOUNCE_MS    = 50;
constexpr uint16_t CLOCK_THRESHOLD_MV = 1000;  // ~1V rising edge

// ---------- Display ----------
constexpr uint8_t  OLED_FPS           = 30;
constexpr uint16_t OLED_REFRESH_MS    = 1000 / OLED_FPS;
constexpr uint16_t PHASE_PLOT_POINTS  = 200;  // ring buffer size
