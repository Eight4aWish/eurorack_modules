# Teensy 4.1 — `teensy_chaos`

Chaotic / fractal synthesis module exploiting the Teensy 4.1's 600 MHz
Cortex-M7 with hardware FPU. Stereo audio output via Teensy Audio Shield
(SGTL5000 codec, I2S).

> **Implemented firmware (current).** The algorithm suite and groups below are
> the *design roadmap*; the shipping firmware (`src/teensy_chaos/main.cpp`)
> currently has **6 continuous-ODE algorithms** — Rössler, Van der Pol, Lorenz,
> Chua, Duffing, Coupled Rössler — cycled by short-pressing BTN. `config.h`'s
> 14-algorithm/group tables are not yet wired up. Recent changes:
> - **V/Oct by oversampling.** Pitch = simulated-time advanced per audio sample.
>   Each algorithm has a safe base step `dtBase`; CLK V/Oct (and ASGN, stacked)
>   raise pitch by running more integration sub-steps per sample rather than
>   enlarging `dt`, so even stiff systems (Lorenz/Chua) track 1 V/oct over
>   several octaves without diverging.
> - **Per-algorithm oversampling ceiling.** Oversampling is the one control that
>   buys pitch with CPU, and a step is not the same price in every system: a
>   Duffing step costs ~6x a Rössler step (three `cosf` calls per RK4 step), and
>   Chua / Coupled Rössler ~2x. The former global `OVERSAMPLE_MAX` was therefore
>   simultaneously unsafe for the expensive systems and needlessly tight for the
>   cheap ones, so the ceiling now lives on `ChaosBase::oversampleMax` and is set
>   per algorithm to land each one at a similar share of the per-sample budget:
>
>   | Algorithm | cyc/step | `oversampleMax` | Octaves above `dtBase` | Est. peak CPU |
>   | --- | ---: | ---: | ---: | ---: |
>   | Rössler | ~88 | 64 | 6 | 46% |
>   | Van der Pol | ~83 | 64 | 6 | 43% |
>   | Lorenz | ~89 | 64 | 6 | 46% |
>   | Chua | ~178 | 32 | 5 | 46% |
>   | Coupled Rössler | ~178 | 32 | 5 | 46% |
>   | Duffing | ~543 | 8 | 3 | 36% |
>
>   Costs are static counts from the emitted Cortex-M7 code (instructions, `vdiv`
>   penalty and libm calls), against a 13,605-cycle budget per sample at 600 MHz
>   / 44.1 kHz plus ~570 cycles fixed per-sample overhead (two `tanhf`, the DC
>   blockers and the envelope). Chua and Coupled Rössler previously reached ~88%
>   and Duffing an estimated ~260% — i.e. Duffing could overrun the audio budget
>   at high V/Oct. Estimates, not measurements: raise a cap only against the
>   on-screen CPU figure below.
> - **Peak CPU readout.** Top-right of the OLED, over the phase plot: peak audio
>   -ISR load (`AudioProcessorUsageMax()`) as a percentage, reset on every
>   algorithm change so the figure always describes what is currently running.
> - **Attractor DSP extracted to `libs/chaos_core/`.** `ChaosBase`, the six
>   algorithms and the panel-order registry now live in a library whose only
>   dependency is `<math.h>` — no Arduino, no HAL, no audio library — so it
>   builds unchanged for Teensy 4.1, STM32H7 / Daisy and host compilers.
>   `src/teensy_chaos/main.cpp` is now purely the platform layer: audio graph,
>   ADS1115, MCP4822, OLED and control loop. Verified behaviour-preserving:
>   trajectories are bit-identical to the pre-extraction code across a sweep of
>   each algorithm's parameter range. See
>   [`libs/chaos_core/README.md`](../libs/chaos_core/README.md).
> - **Divergence guard on all six algorithms.** Rössler diverged to NaN with
>   CHAR (`a`) above ≈0.38 — the top ~7% of that pot — for CHAOS ≥ 3 at any RATE,
>   and having no `isfinite` reset it stayed dead (silence on the audio outs, a
>   stuck rail on X/Y) until the algorithm was changed. Every `stepSample()` now
>   ends by testing its state against a per-algorithm `divergeBound` and
>   re-seeding via `init()` if it has escaped, so an unstable corner is a brief
>   glitch rather than a dead voice. Van der Pol and Chua already had ad-hoc
>   guards; those keep their tuned bounds and exact semantics, now expressed
>   through the shared helpers. Verified over a 9x9x5 parameter sweep per
>   algorithm: no algorithm reaches a non-finite state, no guard fires at nominal
>   settings, and stable-region trajectories are bit-identical to before.
>
>   Note this makes those corners *survivable*, not musical — so the ranges were
>   then corrected too, below.
> - **Parameter ranges corrected from measurement.** The guard is a backstop; the
>   ranges themselves should not reach a region with no bounded attractor. Two
>   changes:
>   - **Rössler `charMax` 0.40 → 0.36.** Above a≈0.385 Rössler escapes to
>     infinity for c ≥ 3. This is a property of the ODE, not of RK4 — it escapes
>     at a `dt` 500x smaller too — so `ChaosVanDerPol`'s trick of clamping `dt`
>     against the parameter cannot help; the range has to exclude it.
>   - **Per-algorithm MOD limits replace the global ±2.** `chaosMin - 2` put
>     Rössler at c = 0 and Coupled Rössler at c = 0.5, both of which diverge at
>     *any* CHAR setting including the defaults — reachable with about −2 V into
>     MOD on a default patch. `ChaosBase::modMin`/`modMax` now carry measured
>     limits per algorithm, always covering at least the pot's own range:
>
>   | Algorithm | Pot range | Old (±2) | Measured safe | Now |
>   | --- | --- | --- | --- | --- |
>   | Rössler | 2.0–8.0 | 0.0–10.0 | 1.25–10.50 | 1.5–10.0 |
>   | Van der Pol | 0.1–8.0 | −1.9–10.0 | −1.88–8.66 | −1.5–8.5 |
>   | Lorenz | 24–32 | 22–34 | well beyond | 22–34 |
>   | Chua | 8.0–11.0 | 6.0–13.0 | 5.0–(see below) | 6.0–11.0 |
>   | Duffing | 0.1–0.8 | −1.9–2.8 | beyond ±2 | −1.9–2.8 |
>   | Coupled Rössler | 2.0–8.0 | 0.0–10.0 | 0.50–13.75 | 1.0–13.0 |
>
>   Verified by sweeping the firmware's own control path — CHAOS x CHAR x RATE
>   pots against the full ±5 V MOD range, 1584 settings per algorithm: zero guard
>   trips on five of six, down from divergence on Rössler and Coupled Rössler.
> - **Chua CHAR clamped against CHAOS.** Chua loses its bounded attractor *inside
>   its own pot range* — above a≈9.25 it stays bounded only while b clears a floor
>   that rises with a — so no MOD limit helps and no range change is cheap:
>   `chaosMax`≈9.25 costs 58% of the CHAOS travel, `charMin`≈14.75 costs 69% of
>   CHAR and puts canonical b=14.286 out of reach. `ChaosChua::charInUse()` clamps
>   the pair instead, `b ≥ 12.0 + 1.6·(a − 9.25)`, keeping both pots at full
>   travel. Measured across the MOD-reachable range at `dtBase`:
>
>   | a | ≤9.25 | 9.50 | 10.00 | 10.50 | 11.00 |
>   | --- | --- | --- | --- | --- | --- |
>   | min stable b | 12.00 | 12.35 | 13.15 | 13.95 | 14.75 |
>
>   Verified over the full reachable space (73,629 combinations of a, b and dt):
>   zero guard trips, and the clamp never engages below a=9.25. It is the ODE and
>   not RK4 — a=11, b=14 escapes at a `dt` 1024× smaller — so clamping `dt` the
>   way Van der Pol does cannot help. Turning CHAR below the floor at high CHAOS
>   therefore stops changing the sound; the panel shows the value in force, not
>   the pot's.
> - **Control path hardened against the audio ISR and the I²C bus.**
>   `setParams()` writes several floats and `init()` writes the whole state; the
>   audio ISR could preempt either and integrate a block from a mixed set. For
>   Chua that is not cosmetic — a new high `a` against the old `b` is the
>   unbounded corner the clamp exists to prevent — so both now run inside
>   `AudioNoInterrupts()`. On the ADS1115 side, conversions are now waited on by
>   polling the config register's OS bit instead of a fixed 1200 µs (only ~3%
>   clear of the nominal 1.163 ms, so a slow conversion returned the *previous
>   channel's* value), and a failed I²C read holds the last good sample instead of
>   yielding 0xFFFF — which read as ~+5.5 V on every input, slamming the chaos
>   parameter, maxing oversampling and firing a spurious RST gate.
> - **Audio-ISR load governor.** The `oversampleMax` figures above are static
>   estimates, and an overrun latches: `update()` runs in the audio ISR, which
>   preempts `loop()`, and only `loop()` can lower the step rate — so a block that
>   overruns starves the control loop and the module looks frozen, dead panel and
>   stuck CV, until it is power-cycled. `update()` now times itself and throttles
>   the step count above 75% of the block budget, recovering below 50%. It should
>   never engage; if it does, that algorithm's `oversampleMax` is too high. A `!`
>   before the CPU figure says it is active — the pitch is running flat.
> - **Onboard gate-driven AD/SR envelope (VCA), off by default.** A two-macro
>   envelope (pico-Env / Plaits style) shapes the output: **AD** = attack + decay
>   front, **SR** = sustain level + release tail. When **off** (default) the VCA
>   is fully open and the module behaves exactly as before (drone). When **on**,
>   RST acts as a **gate** — a rising edge re-inits the attractor and opens the
>   envelope, it sustains while held, and releases when RST falls. A short trigger
>   gives an AD-style hit; a sustained gate gives full attack/sustain/release.
> - **Long-press BTN = ENV page.** While on the ENV page: **CHAOS** = envelope
>   ON/OFF switch (low = off, high = on), **CHAR** = AD macro, **DEPTH** = SR macro
>   (pots soft-takeover so values aren't snapped on the page switch). Short press
>   still cycles algorithm.

## Hardware — 10 HP

```
┌──────────────────┐
│    OLED 128x64   │  SSD1306, SPI
│                  │
├────────┬─────────┤
│        │   BTN   │
│        │  X out  │  CV out 1 — attractor x-axis
│  MOD   │  CHAOS  │  CV in / pot
│  ASGN  │  RATE   │  CV in / pot
│  CLK   │  CHAR   │  CV in / pot
│  RST   │  DEPTH  │  CV in / pot
│  Y out │  L out  │  CV out 2 / audio L
│        │  R out  │  audio R
│        │  A in   │  audio in
└────────┴─────────┘
```

### I/O Summary

| Label  | Type              | Notes                                        |
|--------|-------------------|----------------------------------------------|
| CHAOS  | Pot               | Bifurcation / chaos parameter                |
| RATE   | Pot               | Integration step / frequency                 |
| CHAR   | Pot               | Character / secondary algorithm parameter    |
| DEPTH  | Pot               | Depth / mix                                  |
| BTN    | Digital           | Short press: next algo. Long press: group    |
| MOD    | CV in (ADS1115)   | Chaos modulation — bipolar mod of CHAOS      |
| ASGN   | CV in (ADS1115)   | Assignable mod target (selectable via menu)  |
| CLK    | CV in (ADS1115)   | Clock / V-Oct — locks rate or tracks pitch   |
| RST    | CV in (ADS1115)   | Gate/trigger — re-inits attractor + drives the envelope |
| X      | CV out (MCP4822)  | Attractor x-axis / oscillator 1              |
| Y      | CV out (MCP4822)  | Attractor y-axis / oscillator 2              |
| L OUT  | Audio out (I2S)   | Attractor x-axis (audio rate)                |
| R OUT  | Audio out (I2S)   | Attractor y-axis (audio rate)                |
| A IN   | Audio in (I2S)    | Line in via SGTL5000                         |
| OLED   | I2C display       | Algorithm name, phase-space plot, param bars |

Pin assignments TBD — see `include/teensy_chaos/pins.h` once hardware is
finalised.

## Algorithm Suite

Algorithms are grouped by character to aid live navigation.

> Planned expansion into fractal/digital percussion and a MIDI/trigger drum-kit
> mode is captured in [TEENSY_CHAOS_FRACTALBITS.md](TEENSY_CHAOS_FRACTALBITS.md).

### Group 1 — Melodic (pitched, tuneable)

These have a clear oscillatory core. Frequency is controllable via RATE
(integration step size) and can track V/Oct on CLK when not clocked.

| #  | Algorithm   | Equations                                                        | Key parameter (CHAOS)      | Character                            |
|----|-------------|------------------------------------------------------------------|----------------------------|--------------------------------------|
| 1  | Rossler     | dx=-y-z, dy=x+ay, dz=b+z(x-c)                                  | c (bifurcation ~2-8)      | Warm, musical period-doubling        |
| 2  | Van der Pol | dx=y, dy=mu(1-x^2)y - x                                         | mu (nonlinearity ~0.1-10) | Clean sine to gritty relaxation      |
| 3  | Duffing     | dx=y, dy=-delta*y - alpha*x - beta*x^3 + gamma*cos(omega*t)     | gamma (drive amplitude)    | Driven resonance, FM-like sidebands  |
| 4  | Sprott-A    | dx=y, dy=-x+yz, dz=1-y^2                                        | (initial conditions)      | Minimal flow, delicate chaos         |

### Group 2 — Percussive (burst, transient, clockable)

Best used with RST triggering or CLK input. Produce finite bursts or
gritty noise-like textures.

| #  | Algorithm   | Equations                                                        | Key parameter (CHAOS)      | Character                            |
|----|-------------|------------------------------------------------------------------|----------------------------|--------------------------------------|
| 5  | Logistic    | x[n+1] = r * x[n] * (1 - x[n])                                 | r (bifurcation ~2.5-4.0)  | Classic cascade, digital crunch      |
| 6  | Henon       | x[n+1]=1-a*x[n]^2+y[n], y[n+1]=b*x[n]                          | a (chaos ~0.5-1.4)        | 2D strange attractor, crunchy        |
| 7  | Mandelbrot  | z[n+1] = z[n]^2 + c, sonify orbit until escape                  | c (real + imag via P1/P3)  | Finite burst, natural decay          |
| 8  | Julia       | z[n+1] = z[n]^2 + c, fixed c, varying z0                        | c (real + imag via P1/P3)  | Burst with different exploration     |

### Group 3 — Texture / Drone (evolving, spatial, stereo)

Rich evolving timbres. Less pitched, more about movement and density.

| #  | Algorithm       | Equations                                                    | Key parameter (CHAOS)      | Character                            |
|----|-----------------|--------------------------------------------------------------|----------------------------|--------------------------------------|
| 9  | Lorenz          | dx=sigma(y-x), dy=x(rho-z)-y, dz=xy-beta*z                 | rho (~20-32)               | Two-lobe switching, aggressive       |
| 10 | Coupled Rossler | Two Rossler systems, cross-coupled: dx1+=k(x2-x1) etc       | k (coupling ~0-0.5)       | True stereo, beating to unison       |
| 11 | Ikeda           | x[n+1]=1+u(x*cos(t)-y*sin(t)), t=0.4-6/(1+x^2+y^2)        | u (chaos ~0.5-1.0)        | Dense spirals from nonlinear optics  |
| 12 | Standard Map    | p[n+1]=p+K*sin(theta), theta[n+1]=theta+p[n+1]              | K (kick strength ~0-8)    | Kicked rotator, area-preserving      |
| 13 | Chua            | dx=alpha(y-x-f(x)), dy=x-y+z, dz=-beta*y                   | alpha (~8-16)              | Electronic / metallic double-scroll  |
| 14 | Cell Automata   | 1D rule (30,110,etc) → wavetable or bitstream                | rule number (0-255)        | Digital noise / evolving pattern     |

## Control Mapping Detail

### CHAOS — Bifurcation / Chaos Parameter

Primary parameter for each algorithm (see tables above). Sweeps from
ordered/periodic through period-doubling into full chaos. The musical sweet
spot is near the transition.

MOD adds bipolar modulation to this parameter.

### RATE — Rate / Frequency

- **Continuous oscillators (1-4, 9-10, 13):** Integration step size dt.
  Smaller = lower pitch. Can be mapped to V/Oct via CLK for melodic playing.
- **Discrete maps (5-6, 11-12, 14):** Iteration rate or sample-and-hold
  divisor. Controls pitch / density of the output.
- **Fractal orbits (7-8):** Iteration rate. Faster = higher pitch of the
  burst.

### CHAR — Character / Secondary

Algorithm-dependent second parameter:

| Algorithm       | CHAR controls           |
|-----------------|-------------------------|
| Rossler         | a (spiral tightness)    |
| Van der Pol     | (reserved / output mix) |
| Duffing         | drive frequency omega   |
| Logistic/Henon  | output filtering        |
| Lorenz          | sigma                   |
| Coupled Rossler | individual c offset     |
| Mandelbrot      | c imaginary component   |
| Julia           | c imaginary component   |
| Chua            | beta                    |
| Cell Automata   | bit depth / scan rate   |

### BTN — Navigation

- **Short press:** Cycle to next algorithm.
- **Long press (>500 ms):** Toggle the **ENV page** — CHAOS becomes the envelope
  ON/OFF switch, CHAR becomes the AD macro and DEPTH the SR macro for the onboard
  envelope (pots soft-takeover so they don't snap on the switch). *(Roadmap:
  group navigation once groups exist.)*

### CLK — Clock / V-Oct

- **Unclocked (no signal):** RATE sets rate freely.
- **Clock detected:** Rate locks to clock period. For melodic algorithms
  this quantises pitch; for percussive algorithms it sets repetition rate.
- Detection threshold: ~1V rising edge.

### RST — Gate / Trigger

- Used as a **gate** (with hysteresis ~1 V on / lower V off). A rising edge
  resets all state variables to initial conditions **and** opens the onboard
  envelope (see below); the level is held while RST stays high and releases when
  it falls.
- The re-init creates a percussive transient as the trajectory diverges from the
  starting point back toward the attractor; the envelope shapes it into a hit.
- A short trigger gives an AD-style one-shot; a sustained gate gives full
  attack/sustain/release. Useful for rhythmic use — clock CLK, gate/trigger RST.

### AD/SR Envelope (VCA)

- Audio-rate gate-driven envelope (linear attack, exponential decay/release) on
  the stereo output, **off by default**. Two macros (pico-Env / Plaits style):
  **AD** = attack + decay, **SR** = sustain level + release.
- **Off:** VCA fully open — the module is a drone/free-running voice (original
  behaviour). RST still re-inits the attractor (percussive transient) but the
  output isn't VCA-shaped.
- **On:** opens on an RST rising edge (attack → decay to the sustain level),
  **sustains while RST is held**, then **releases** when RST falls. A short
  trigger collapses this into an AD-style hit.
- Configured on the **ENV page** (long-press BTN): CHAOS = ON/OFF (low/high),
  CHAR = AD macro (attack ~0.5–1000 ms, decay ~2–2000 ms), DEPTH = SR macro
  (sustain 0–100 %, release ~2 ms–4 s).

### MOD — Chaos Modulation

Bipolar modulation of CHAOS parameter. Summed with pot value, clamped to
valid range. An LFO here sweeps through bifurcation cascades automatically.

### ASGN — Assignable

Target selectable via OLED menu (long press BTN to access):
- RATE modulation (rate/pitch)
- CHAR modulation (character)
- Stereo width / axis rotation
- Output amplitude

## Stereo Output Strategy

| Type                  | Left channel         | Right channel        |
|-----------------------|----------------------|----------------------|
| 3-variable continuous | x state variable     | y state variable     |
| Coupled Rossler       | Oscillator 1 output  | Oscillator 2 output  |
| 2D discrete maps      | x dimension          | y dimension          |
| Fractal orbits        | Re(z) orbit          | Im(z) orbit          |
| 1D maps (Logistic)    | x[n] direct          | x[n] one-pole filtered (pseudo-stereo) |

## OLED Display Layout

```
┌────────────────────────────┐
│ MELODIC      Rossler    1/4│  Group name, algo name, position
│                            │
│        ·  · ··             │
│      ·        ·            │  Phase-space plot (x vs y)
│     ·    +     ·           │  Real-time attractor trace
│      ·        ·            │
│        · ·  ·              │
│                            │
│ C=5.7  dt=0.04  a=0.2     │  Parameter values
│ ████░░  ██████  ███░░░    │  CHAOS   RATE    CHAR bars
└────────────────────────────┘
```

- Top line: group, algorithm name, position in group (e.g. 1/4).
- Middle: real-time phase-space plot. Ring buffer of last ~200 points,
  oldest points fade. Visually stunning for Rossler/Lorenz spirals.
- Bottom: parameter names with current values + bar graph.

## Audio Architecture

Uses Teensy Audio Library for I2S output to the Audio Shield (SGTL5000).

```
                    ┌─────────────┐
  Algorithm ──x──>  │ DC block    │──> AudioOutputI2S (L)
  (per sample)      │ (one-pole   │
              y──>  │  high-pass) │──> AudioOutputI2S (R)
                    └─────────────┘
```

- Custom `AudioStream` subclass runs the selected algorithm's `process()`
  in `update()`, filling the 128-sample audio block.
- DC blocking: single-pole HPF at ~5 Hz removes attractor offset.
- Output scaling: soft-clip (tanh) to keep within -1.0 to +1.0.
- Sample rate: 44100 Hz (Audio Shield default).

### Integration Methods

- **Continuous systems:** RK4 (4th-order Runge-Kutta) per sample. At
  600 MHz the FPU handles this in ~1 us per sample — well within the
  ~22 us budget at 44.1 kHz.
- **Discrete maps:** Direct iteration, optionally oversampled with
  linear interpolation for anti-aliasing.
- **Fractal orbits:** Iterate until escape (|z| > bailout) or max
  iterations, output orbit samples. When orbit ends, optionally restart
  with perturbed c (auto-trigger) or wait for RST.

## Code Structure

```
src/teensy_chaos/
  main.cpp              — Setup, audio routing, control loop, OLED refresh
  algorithms.h          — ChaosAlgorithm base struct, registry
  algorithms.cpp        — Algorithm implementations (all 14)
  display.h             — OLED rendering (phase plot, parameter bars, nav)
  display.cpp           — Display implementation

include/teensy_chaos/
  pins.h                — Pin assignments (pots, button, CV inputs)
  config.h              — Algorithm count, parameter ranges, defaults
```

### Algorithm Interface

```cpp
struct ChaosAlgorithm {
    const char* name;
    const char* group;           // "MELODIC", "PERCUSSIVE", "TEXTURE"
    uint8_t     group_index;     // position within group

    float state[4];              // state variables (up to 4)
    float out_l, out_r;          // output samples

    void  init();                // reset to initial conditions
    void  process(float dt,      // integration step / iteration rate
                  float param1,  // chaos / bifurcation (CHAOS + MOD)
                  float param2); // character / secondary (CHAR)
    // Scaling hints for display
    float x_min, x_max;         // expected output range for normalisation
    float y_min, y_max;
};
```

## Open Questions

- [ ] Pin assignments — depends on panel layout and PCB routing.
- [ ] CV input conditioning — direct ADC or external ADC (ADS1115)?
      Teensy ADC is 10-bit; may want 12-bit for V/Oct tracking.
- [ ] V/Oct calibration — needed if CV1 is used melodically.
- [ ] OLED refresh rate — aim for ~30 fps phase plot without blocking
      audio. Page-at-a-time strategy (as in ksoloti_elements) or
      partial update.
- [ ] Parameter save/recall — store last-used algorithm + settings in
      EEPROM?
- [ ] Additional algorithms — the framework supports adding more easily.
      Candidates: Thomas attractor, Aizawa, Chen, Halvorsen.
