# Teensy 4.1 — `teensy-chaos`

Chaotic / fractal synthesis module exploiting the Teensy 4.1's 600 MHz
Cortex-M7 with hardware FPU. Stereo audio output via Teensy Audio Shield
(SGTL5000 codec, I2S).

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
| RST    | CV in (ADS1115)   | Reset / trigger — snaps to initial conditions|
| X      | CV out (MCP4822)  | Attractor x-axis / oscillator 1              |
| Y      | CV out (MCP4822)  | Attractor y-axis / oscillator 2              |
| L OUT  | Audio out (I2S)   | Attractor x-axis (audio rate)                |
| R OUT  | Audio out (I2S)   | Attractor y-axis (audio rate)                |
| A IN   | Audio in (I2S)    | Line in via SGTL5000                         |
| OLED   | I2C display       | Algorithm name, phase-space plot, param bars |

Pin assignments TBD — see `include/teensy-chaos/pins.h` once hardware is
finalised.

## Algorithm Suite

Algorithms are grouped by character to aid live navigation.

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

- **Short press:** Cycle to next algorithm within current group.
- **Long press (>500 ms):** Cycle to next group (Melodic -> Percussive ->
  Texture -> Melodic).

### CLK — Clock / V-Oct

- **Unclocked (no signal):** RATE sets rate freely.
- **Clock detected:** Rate locks to clock period. For melodic algorithms
  this quantises pitch; for percussive algorithms it sets repetition rate.
- Detection threshold: ~1V rising edge.

### RST — Reset / Trigger

- Rising edge resets all state variables to initial conditions.
- Creates a percussive transient as the trajectory diverges from the
  starting point back toward the attractor.
- Useful for rhythmic use — clock CLK, trigger RST on downbeats.

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
src/teensy-chaos/
  main.cpp              — Setup, audio routing, control loop, OLED refresh
  algorithms.h          — ChaosAlgorithm base struct, registry
  algorithms.cpp        — Algorithm implementations (all 14)
  display.h             — OLED rendering (phase plot, parameter bars, nav)
  display.cpp           — Display implementation

include/teensy-chaos/
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
      audio. Page-at-a-time strategy (as in ksoloti-elements) or
      partial update.
- [ ] Parameter save/recall — store last-used algorithm + settings in
      EEPROM?
- [ ] Additional algorithms — the framework supports adding more easily.
      Candidates: Thomas attractor, Aizawa, Chen, Halvorsen.
