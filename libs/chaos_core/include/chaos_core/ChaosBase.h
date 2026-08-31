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
        // Absolute limits the chaos parameter may be driven to once MOD CV is
        // added. These are not decoration: several systems lose their bounded
        // attractor just outside the pot range — Rossler below c~1.25, Coupled
        // Rossler below c~0.5 — so a CV that overshoots turns the voice into a
        // repeating re-seed. Always at least [chaosMin, chaosMax], so the pot's
        // own range stays reachable; beyond that, measured per algorithm.
        float modMin = 0.0f, modMax = 1.0f;
        // Pre-tanh amplitude scale. The host writes tanhf(state * gain) * 32000,
        // so the tanh is a soft limiter and these are a voicing decision, not a
        // safety one — nothing can exceed full scale whatever they are set to.
        //
        // Set by measurement so all six algorithms sit at the same level: gain =
        // atanh(0.90) / median peak |state| over a 5x5x3 sweep of the pot range.
        // The MEDIAN matters. Calibrating on the maximum drags everything down —
        // and for Chua the maximum is its divergence spike (max|x| lands exactly
        // on divergeBound), which is not a thing anyone plays. So normal settings
        // land at 90% of full scale and the loud corners saturate into the tanh,
        // which is what it is there for. L and R are measured separately because
        // getX() and getY() are different state variables with different extents
        // (Van der Pol's y swings 6x its x).
        float gainL    = 0.12f, gainR = 0.12f;
        float xMin = -1.0f, xRange = 2.0f;     // plot window
        float yMin = -1.0f, yRange = 2.0f;
        float cvScaleX = 0.5f, cvScaleY = 0.5f; // state → ±5V CV
        // Divergence guard. RK4 runs away at the edges of some parameter ranges,
        // and once the state is non-finite every later step inherits it: the
        // voice goes silent with X/Y stuck on a rail until the algorithm is
        // changed. stepSample() tests its state against this bound and re-seeds
        // via init() instead, turning a dead module into a brief glitch.
        //
        // Set well above the attractor's natural extent. Divergence is
        // exponential, so a runaway crosses any threshold within a handful of
        // samples, while a healthy trajectory never approaches one — the cost of
        // a generous bound is a few more samples of garbage, the cost of a tight
        // one is re-seeding a perfectly good trajectory.
        float divergeBound = 1.0e3f;

        virtual ~ChaosBase() {}
        virtual void  init()                                          = 0;
        virtual void  setParams(float chaos, float rate, float charV) = 0;
        virtual void  stepSample()                                    = 0;
        virtual float getX() const                                    = 0;
        virtual float getY() const                                    = 0;

    protected:
        // True once `v` has left the region any healthy trajectory stays in.
        // Apply to the variable `divergeBound` was chosen for.
        bool diverged(float v) const {
            return !isfinite(v) || fabsf(v) > divergeBound;
        }
        // The unrecoverable case only, without the magnitude test — for state
        // that legitimately ranges wider than the bound (Chua's z reaches past
        // the value that catches a runaway in x, and Van der Pol's y past the
        // one that catches x), where a shared bound would reset healthy runs.
        static bool nonFinite(float v) { return !isfinite(v); }
    };

}  // namespace chaos_core
