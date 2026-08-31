#pragma once
// chaos_core — platform-independent chaotic-attractor DSP.
//
// Nothing here touches Arduino, a HAL, or an audio library: the only dependency
// is <math.h>, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy /
// STM32H7, or a host compiler for offline testing. Integration, I/O and UI
// belong to the platform layer that owns these objects.

#include <math.h>

namespace chaos_core {

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

}  // namespace chaos_core
