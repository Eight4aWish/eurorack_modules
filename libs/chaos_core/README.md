# chaos_core

Platform-independent chaotic-attractor DSP: the `ChaosBase` interface, six
continuous-ODE attractors integrated with RK4, and the panel-order registry.

The only dependency is `<math.h>`. Nothing here includes Arduino, a vendor HAL,
or an audio library, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy
/ STM32H7, or a host compiler for offline testing. Integration into an audio
callback, CV/gate I/O, calibration and UI all belong to the platform layer that
owns these objects — see [`src/teensy_chaos/main.cpp`](../../src/teensy_chaos/main.cpp)
for the reference consumer.

## Files

- `include/chaos_core/ChaosBase.h` — abstract base: four pure-virtual methods plus
  the metadata each algorithm publishes about itself
- `include/chaos_core/Attractors.h` — the six attractors, RK4 per `stepSample()`
- `include/chaos_core/Registry.h` / `src/Registry.cpp` — `algos[]` and `N_ALGOS`,
  the shipping set in panel order

## ChaosBase

Subclasses fill in the metadata fields in their constructor and implement
`init()`, `setParams(chaos, rate, charV)`, `stepSample()`, `getX()`, `getY()`.

| Field | Meaning |
| --- | --- |
| `name`, `chaosLabel`, `charLabel` | display strings |
| `chaosMin/Max`, `rateMin/Max`, `charMin/Max` | parameter ranges the panel maps onto |
| `dtBase` | largest numerically-safe integration step |
| `divergeBound` | magnitude past which the state is treated as diverged |
| `oversampleMax` | ceiling on integration steps per audio sample (see below) |
| `modScale` | chaos-parameter units per volt of MOD CV |
| `modMin`, `modMax` | absolute limits MOD CV may drive the chaos parameter to |
| `gainL`, `gainR` | pre-saturation amplitude scale for the audio outs |
| `xMin`, `xRange`, `yMin`, `yRange` | plot window |
| `cvScaleX`, `cvScaleY` | state → ±5 V CV scaling |

The platform layer reads these rather than hard-coding any of it, so adding an
algorithm needs no changes outside this library.

## Pitch, `dtBase` and `oversampleMax`

RK4 diverges if `dt` grows past what a given system tolerates, so pitch above
what `dtBase` reaches is produced by running **more integration steps per audio
sample**, not by enlarging `dt`. That buys pitch with CPU, and a step is not the
same price in every system, so the ceiling is per-algorithm:

| Algorithm | X / Y outputs | cyc/step (M7) | `oversampleMax` |
| --- | --- | ---: | ---: |
| Rössler | x, y | ~88 | 64 |
| Van der Pol | x, y | ~83 | 64 |
| Lorenz | x, z−ρ (centred) | ~89 | 64 |
| Chua | x, z | ~178 | 32 |
| Duffing | x, y | ~543 | 8 |
| Coupled Rössler | x₁, x₂ (the other oscillator) | ~178 | 32 |

Costs are static counts from emitted Cortex-M7 code. See
[`docs/TEENSY_CHAOS.md`](../../docs/TEENSY_CHAOS.md) for the budget arithmetic
and for the on-screen CPU figure to raise a cap against.

## Usage

```cpp
#include "chaos_core/Registry.h"
using namespace chaos_core;

ChaosBase* algo = algos[0];          // Rössler
algo->init();
algo->setParams(/*chaos*/ 5.7f, /*rate (dt)*/ 0.05f, /*char*/ 0.2f);

// per audio sample, `steps` from the V/Oct oversampling calculation
for (int k = 0; k < steps; k++) algo->stepSample();
float outL = algo->getX(), outR = algo->getY();
```

`setParams` may be called at any rate; it is just field assignment plus, in
some algorithms, a stability clamp on `dt`. Swapping the active `ChaosBase*`
is safe on a core with word-sized atomic pointer stores, but call `init()`
before making a new one live.

## Divergence guard

RK4 runs away at the edges of some parameter ranges — Rössler above `a`≈0.38,
Chua at its `chaosMax`. Once the state is non-finite every later step inherits
it, so the voice goes silent with X/Y stuck on a rail until the algorithm is
changed. Every `stepSample()` therefore ends by testing its state and re-seeding
via `init()` if it has escaped, which turns a dead module into a brief glitch.

Two tests are available to subclasses:

- `diverged(v)` — non-finite **or** `|v| > divergeBound`. Apply to the variable
  the bound was chosen for.
- `nonFinite(v)` — the unrecoverable case only. For state that legitimately
  ranges wider than that bound: Chua's `z` exceeds the value that catches a
  runaway in `x`, as does Van der Pol's `y`, so bounding them would reset
  healthy trajectories.

Bounds sit well above each attractor's measured extent, because divergence is
exponential — a runaway crosses any threshold within a handful of samples, while
a healthy trajectory never approaches one. Verified across a 9x9x5 sweep of every
algorithm's parameter space (8.1M steps each): no algorithm reaches a non-finite
state, no guard fires at nominal settings, and stable-region trajectories are
bit-identical to the unguarded code.

## Parameter ranges are a safety boundary, not just taste

Several of these systems lose their bounded attractor just outside — and in one
case inside — the range the panel exposes. The guard makes that survivable, but
it is a backstop: the ranges themselves have to exclude it, and they are set
from measurement.

- `charMin`/`charMax`, `chaosMin`/`chaosMax` bound what the pots reach.
- `modMin`/`modMax` bound what MOD CV can add on top. They are always at least
  `[chaosMin, chaosMax]`, so the pot's own range stays reachable, but they are
  **not** a fixed margin — Rössler escapes below c≈1.25 and Coupled Rössler
  below c≈0.5, while Lorenz and Duffing are stable far past ±2.

Note this is genuinely a range problem, not a step-size one. Rössler above
a≈0.385 escapes even at a `dt` 500× smaller than `dtBase`, so the Van der Pol
trick of clamping `dt` against the parameter (`setParams`) cannot help; only
excluding the parameter value can.

### Chua — the one case a range cannot fix

Chua loses its bounded attractor **inside its own pot range**, so unlike the
others it cannot be fixed by moving a limit: above a≈9.25 the attractor stays
bounded only while b clears a floor that rises with a. Excluding that corner by
range means either `chaosMax` ≈ 9.25 (losing 58% of the CHAOS travel, including
part of the double-scroll band) or `charMin` ≈ 14.75 (losing 69% of CHAR, and
putting canonical b=14.286 out of reach).

`ChaosChua::charInUse()` clamps the *pair* instead, which keeps both pots at
full travel. The floor is measured at `dtBase` across the whole MOD-reachable
range and is linear in a to within the sweep resolution:

| a | ≤9.25 | 9.50 | 10.00 | 10.50 | 11.00 |
| --- | --- | --- | --- | --- | --- |
| min stable b | 12.00 | 12.35 | 13.15 | 13.95 | 14.75 |

giving `b ≥ 12.0 + 1.6·(a − 9.25)`. It engages on 12.5% of the CHAR plane and
never below a=9.25. Verified over the full reachable space — a across
`[modMin, modMax]`, b across the CHAR pot, dt from `rateMin` to `dtBase`, 73,629
combinations — with zero guard trips. `divergeBound` stays as a backstop behind
it.

This is genuinely the ODE and not the integration: a=11, b=14 escapes at a `dt`
1024× smaller too, so `ChaosVanDerPol`'s trick of clamping `dt` against the
parameter cannot help. Only the parameter pair can.

Because the clamp makes the running b differ from the requested one, algorithms
that clamp override `charInUse()` and the firmware displays its result, so the
panel shows what is actually running.

## Adding an algorithm

Subclass `ChaosBase` in `Attractors.h`, set the metadata in the constructor
(including `oversampleMax`, scaled by the new system's per-step cost), then add
an instance to `Registry.cpp` and bump `N_ALGOS`. No platform file changes.

## Notes

- Single-precision throughout; assumes a hardware FPU.
- Not thread-safe. `stepSample()` is expected to run in one audio context while
  `setParams()` is called from a control context — the field writes are
  word-sized, and a torn parameter update is at worst one sample of a stale
  coefficient.
- Compatible with Teensy 4.1, STM32H7 / Daisy, and host builds.

See the project's root `README.md` and `docs/TEENSY_CHAOS.md` for the module,
its I/O map and the algorithm-suite roadmap.
