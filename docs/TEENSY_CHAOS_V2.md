# teensy_chaos v2 — settled decisions

> **Status: design record, not implemented.** This captures what was settled in
> design discussion, what is deliberately parked until the hardware is in hand,
> and the hardware facts behind both. The shipping firmware remains the
> six-algorithm CV chaos oscillator described in [TEENSY_CHAOS.md](TEENSY_CHAOS.md).

## Scope

v2 is **oscillator algorithms only** — a large suite of continuous, pitched,
V/Oct-tracking chaotic systems with one voice and one control surface.

The [Fractal Bits](TEENSY_CHAOS_FRACTALBITS.md) plan (fractal/digital percussion,
a MIDI/trigger drum kit, an Oscillator|Kit menu) is **out of scope for v2** and
that document is now historical. Dropping it removes the two-level menu, the
four-voice polyphony, the per-mode envelope split, the USB MIDI requirement, and
the osc/perc/both suitability tagging. It also settles the CPU budget: one voice,
so all headroom goes to audio quality rather than being divided across kit pieces.

## What the current controls actually are

Before anything is renamed, the firmware's real behaviour (`src/teensy_chaos/main.cpp`):

| Panel label | Actually does | CV |
| --- | --- | --- |
| CHAOS | primary bifurcation parameter, `chaosMin..chaosMax`, then `+ modVolts × modScale` clamped to `modMin..modMax` | MOD |
| RATE | pitch, mapped **linearly** over the algorithm's range (was per-sample `dt`; now `simRateMin..simRateMax` per second) | CLK + ASGN |
| CHAR | secondary parameter, `charMin..charMax` | **none** |
| DEPTH | output amplitude only — `ampL/ampR.gain(0.1 + 0.9·norm)` | **none** |

- **ASGN is not assignable.** It is summed with CLK inside one `powf(2, clkVolts + asgnVolts)`,
  so it is a second V/Oct — a transpose input. The two jacks are indistinguishable
  to the firmware.
- **CLK does no clock detection.** `cv[2]` is only ever read as V/Oct volts;
  TEENSY_CHAOS.md's "rate locks to clock period" is unimplemented.
- **RST is a gate**, not a reset. The re-init is a side effect of the rising edge.
- `CV_GATE_THRESH` in `include/teensy_chaos/pins.h` is dead — a 10-bit threshold
  left from a design that read CV on the Teensy's own ADC, before the ADS1115.

## Settled control surface

Rename to what the controls are, and give the two silent ones a job:

| Was | Becomes | Notes |
| --- | --- | --- |
| DEPTH (pot) | **LEVEL** | output trim; pot only, no CV — the envelope is the VCA |
| RATE (pot) | **TUNE** | base pitch, **exponential taper** (see below) |
| CHAOS (pot) | CHAOS | unchanged |
| CHAR (pot) | CHAR | unchanged |
| MOD (CV) | **CHAOS CV** | already correct |
| ASGN (CV) | **CHAR CV** | dedicated, no assignment mechanism needed |
| CLK (CV) | **V/OCT** | what it is |
| RST (CV) | **GATE** | what it is |

**Why ASGN becomes CHAR and not something assignable.** Once RATE is understood
as pitch, the destination list collapses: RATE is already V/Oct's, CHAOS already
has MOD, and LEVEL as a CV destination duplicates an external VCA. CHAR is the
only gap, so an assignment mechanism would be UI for a choice of one. The real
cost either way is the metadata: `ChaosBase` carries `modScale`, `modMin` and
`modMax` for the chaos parameter only, and CHAR needs the equivalent measured
per algorithm before any CV can safely drive it.

### TUNE must be exponential

`main.cpp:459` maps the pot linearly into `dt`, but pitch is exponential in `dt`.
On Rössler (0.002–0.1) that puts **~1 octave in the top half of the knob and
~2.6 octaves in the bottom tenth** — all the useful travel crushed against one end.
`expoMap()` already exists in `main.cpp` for the envelope times and is the fix:

```c
float potRate = expoMap(p2 / 1023.0f, algo->simRateMin, algo->simRateMax);
```

Safe on all six (every `simRateMin` > 0), and it changes neither range nor stability.

Total travel per algorithm, which is also why Lorenz feels static:

| Algorithm | RATE range (per-sample `dt` at 44.1 kHz) | Octaves |
| --- | --- | ---: |
| Van der Pol | 0.002–0.15 | 6.2 |
| Rössler / Coupled Rössler | 0.002–0.10 | 5.6 |
| Duffing | 0.005–0.10 | 4.3 |
| Chua | 0.001–0.008 | 3.0 |
| **Lorenz** | **0.001–0.003** | **1.6** |

### Lorenz needs a range rework

Two independent causes of "never much beyond noise", both range choices:

1. **The CHAOS pot never crosses a bifurcation.** ρ 24–32 sits entirely above the
   chaos onset near 24.74, so there is no periodic setting anywhere on the knob.
   The classic periodic windows are far higher (≈99.5–100.8, ≈145–166, a stable
   limit cycle above ≈313) and reaching them needs `divergeBound` above 200 and
   probably a smaller `dtBase` — an experiment, not a range edit.
2. **1.6 octaves of RATE travel**, against Rössler's 5.6.

### Van der Pol's CHAR is dead

`setParams` ends `(void)charV;`. The reason is real: Van der Pol is the only
member of the suite that **is not chaotic** — a 2D autonomous system has a limit
cycle and nowhere to explore. Two ways out:

- **Free:** output mix between x and y (its y swings ~6× its x, so it morphs
  smooth → spiky).
- **Better, not free:** force it — `dy = mu(1-x²)y - x + A·cos(ωt)`, CHAR = drive
  amplitude. The forced Van der Pol is genuinely chaotic. Costs a `cosf` per step,
  so `maxStepsPerSecond` would drop toward Duffing's.

### Chua

α ≈ 10.5 is a good place to play, and the drop-off there is the documented floor:
bounded only while `b ≥ 12.0 + 1.6·(a − 9.25)`, so at α = 10.5 the **bottom half of
the CHAR pot** is the re-seeding region. This is deliberately not clamped
(`libs/chaos_core/README.md`) — the stutter is the character. Worth showing on the
display rather than enforcing. Note also that MOD is pinned `modMin = 6, modMax = 11`,
so CV can only pull α *down* from 10.5, never up.

## Audio quality direction

**Raising `maxStepsPerSecond` buys pitch range, not quality.** At a given pitch the step
count is already determined; bigger caps only mean higher notes.

The unused lever is `main.cpp:464`: at or below `dtBase` the engine runs **exactly
one RK4 step per audio sample**, with `dt` stretched to match the pitch. The same
pitch could come from N steps of `dt/N` — identical simulated time, N× the accuracy.
And N steps per output sample *is* running the simulation at N × 44.1 kHz.

Two changes, in order:

1. **Constant-rate simulation.** Always run N steps at `dt = desiredDt / N` instead
   of the minimum. CPU becomes roughly constant regardless of pitch, so N is a
   single dial to turn until the load meter reads the target. Top pitch is unchanged
   (`dt` reaches `dtBase` at maximum, as today).
2. **Decimation filter.** A halfband cascade from the N× internal rate to the output.
   Without it, running at 16× just discards 15 of every 16 samples and aliases
   exactly as before.

**There is currently no anti-aliasing anywhere.** The graph is `engine → ampL/R → I2S`
and nothing else; `tanhf(state * gain)` is a saturator generating fresh harmonics on
top. Chua's double-scroll switching and Van der Pol's relaxation edges are
near-discontinuous, so there is real energy above Nyquist folding back — plausibly
part of why Lorenz reads as noise.

Pitch and quality share one budget (total steps = quality N × pitch oversampling,
bounded by `STEPS_ABS_MAX` and the load governor), so N necessarily gives way at the
top of the V/Oct range. That is musically fine: `dt` is already small relative to the
orbit period up there.

**N must be auditionable, not baked in.** Some of the current character is coarse
integration and unfiltered foldover. The precedent is the reverted Chua clamp: clean
lost to character, on purpose.

## Platform

Undecided, and deliberately so — `chaos_core` depends only on `<math.h>`, so the port
cost is the ~600-line platform layer, not the DSP.

**Clock speed does not decide it.** Measured ~88 cycles/step for Rössler (the host
benchmark corroborates the static estimate). Against 13,605 cycles/sample at
600 MHz / 44.1 kHz that is ~150 RK4 steps per sample at full load; at 480 MHz,
~120. At the 16× constant rate above, ~10% on Teensy 4.1 against ~13% on an
STM32H750. Same Cortex-M7, same FPU, 25% clock difference, nothing near a cliff.
The premise "what a 600 MHz chip can do" is really "what a fast M7 with an FPU can
do", and both candidates are on the right side of that line.

So the decision rests on **I/O, codec and UI**, not MHz.

### Candidates

| | Teensy 4.1 (own board) | Alchemy Lab V2 | Daisy Patch.Init() | o_C T4.1 |
| --- | --- | --- | --- | --- |
| MCU | 600 MHz i.MXRT1062 | 480 MHz H750 | 480 MHz H750 | 600 MHz i.MXRT1062 |
| Audio | SGTL5000 16/44.1 | 24-bit / 96 kHz | 24-bit / 96 kHz | SGTL5000 16/44.1 |
| Pots | as designed | **6, each with a 16-LED ring** | 4 | **none** (encoders) |
| CV in | ADS1115, ~200 Hz | 6 jacks, 16-bit | 12 ADC in, 16-bit | 8 in, external 16-bit ADC |
| Display | OLED + phase plot | 102 RGB LEDs | none | OLED |

- **o_C T4.1 is out on the control surface, not on audio.** It *does* have I2S audio
  in/out via SGTL5000 (added by Stoffregen and DJ Phazer) — but it has no pots, and
  CHAOS and CHAR are exactly the two controls that want absolute position rather than
  a detented encoder. Hardware is CC BY-SA 4.0 with KiCad sources; its **CV input
  stage is well worth copying** — an inverting op-amp with the offset from a precision
  2.5 V reference rather than a divider off the rail, which is why its zero point does
  not drift with the supply.
- Notably, the T4.1 revision **moved away from reading CV on the Teensy's own ADC**,
  adding a discrete 16-bit part "free from the on-chip digital noise of a CPU"
  (~14 bits in practice). The classic managed only ~1 kHz effective after the
  averaging needed to clean the on-chip reading up.

### If staying on Teensy: the CV path is the fix, not the MCU

The ADS1115 is configured single-shot at 860 SPS, four channels read sequentially
(`main.cpp:44-81`) — ~1.16 ms per conversion, **~5 ms per round, ~200 Hz per channel**,
and `loop()` blocks on it. That means V/Oct steps at 200 Hz, gates jitter by up to
5 ms, and audio-rate modulation is impossible. Meanwhile the MCU idles at 17–36%.

**The fix is a fast SPI ADC, not direct-to-pin.** The problem was never that the
converter is external — it is that it is I2C at 860 SPS. Requirement, derived from the
front end below: 1 Eurorack volt → 0.302 V at the ADC, so **1 cent = 0.25 mV**, needing
~13.7 bits ENOB over 3.3 V before margin. That rules out 12-bit. Candidates:
**MCP3564R** (24-bit, 4-ch, 153.6 ksps, internal reference), **ADS131M04** (24-bit,
4-ch simultaneous — removes the inter-channel skew), **ADS8688** (16-bit, 8-ch,
built-in ±10.24 V ranges, would delete the front-end scaling). Give it its own SPI
bus; SPI0 already carries the OLED and MCP4822.

For breadboard proving, an **MCP3208** (DIP-16, 100 ksps) is enough: 12-bit averaged
64× is ~15 effective bits at 1.5 kHz, and the thing being proven is the SPI/DMA path.

### Existing analog front end (derived from the calibration constants)

PGA = 001 is ±4.096 V full scale, so exactly **8000 codes per volt** at the ADS pin.
With `CV_MOD_ZERO = 13236` and `CV_MOD_CPV = 2414`:

```
V_pin = 1.654 − 0.302 × V_euro
```

| Eurorack in | At the pin |
| ---: | ---: |
| +5 V | 0.15 V |
| 0 V | 1.65 V |
| −5 V | 3.16 V |
| ±5.5 V | rails at 0 / 3.31 V |

So the divider **already targets 0–3.3 V** and needs no change for a 3.3 V converter —
`pins.h` says as much. The inversion cannot be passive, so an op-amp stage is already
there and the node is low-impedance. What is missing is **protection**: a ±12 V patch
error puts about −2 V or +5.3 V on the pin, and the Teensy is not 5 V tolerant. Per
channel: ~1 kΩ series into the pin, plus a **Schottky** clamp to 3V3 and GND (BAT54S).
Silicon's 0.7 V drop would let the pin reach 4.0 V, which is still over.

To confirm before committing: with nothing patched the pin should read ~1.65 V; with
+5 V patched, ~0.15 V; and trace whether an op-amp output or a resistor junction drives
it (the latter needs a buffer added).

### If moving to Alchemy Lab

Built on an Electrosmith Daisy submodule; SDK at `hermetic-modular/alchemy-sdk`, MIT.
**The ten jacks are four different classes** — "six reassignable CV jacks" is true only
on the input side:

| Jack | Type | Out bits | Out latency | In bits | In latency |
| --- | --- | --- | --- | --- | --- |
| J1, J2 | Codec In, **AC-coupled** | — | — | 1 (trigger) | audio block |
| J3–J6 | ADC in ↔ MCP4728 I²C DAC | 12 | ~70 µs | 16 | audio rate |
| J7, J8 | ADC in ↔ STM32 DAC1 | 12 | **<1 µs** | 16 | audio rate |
| J9, J10 | **Codec Out, DC-coupled** | **24** | audio block | — | — |

CV input front end is `V_pin = 1.65 − 0.165 × V_jack`, i.e. **full scale ±10 V** — half
the ADC span used at ±5 V, but 16 bits over ±10 V is still 3277 codes/volt (~2.7 codes
per cent), better than the ADS1115's 2414. Engine block is 24 samples.

Two consequences for this module specifically:

- **J1/J2 cannot be GATE.** AC coupling blocks DC; sustained gates last only until the
  cap debiases, and the envelope sustains while held.
- **J9/J10 are audio out *or* CV out**, not both — they are the only codec outputs.

The mapping fits exactly, with nothing spare:

| Need | Jack |
| --- | --- |
| Stereo audio out (X, Y) | J9, J10 |
| Line in | J1 or J2 |
| CHAOS CV, CHAR CV, V/Oct, GATE | 4 × J3–J8 |
| X, Y CV out (12-bit, as the MCP4822 today) | 2 × J3–J8 |

**Six pots delete the ENV page.** P1–P6 as CHAOS, TUNE, CHAR, LEVEL, AD, SR means every
control is live with no pages, and `envPage`, the `Pickup3` soft-takeover struct,
`envSwitchRef`/`envSwitchLive` and the branched display all disappear.

Framework features that retire planned work: **`CvRouter`** is per-pot CV routing with
attenuation and bipolar conversion (the assignable-CV feature, already written);
**`VirtualButton().Selector().Colors().Bind()`** is the algorithm selector with LED
feedback, preset persistence and host editing; **`Presets`** is a wear-levelled 16-slot
CRC'd flash store with a two-pot save/load gesture behind the B2+B3 Settings chord
(slot selector on pot 2, action on pot 3).

The **phase plot does not survive** — 6 rings of 16 (13 in the arc) plus 6 button
accents cannot render an X/Y trajectory. The closest equivalent is driving ring
brightness and colour from block-rate DSP state, which the SDK explicitly encourages.

Caveats: the SDK is beta ("APIs, surface names, and on-disk preset formats may change");
V1 and V2 boards differ materially (V1 has no MCP4728, PCA9557 or DG411).

## Presets carry the model, and norms travel

`presets.Manage(buttons)` stores one byte per stateful button, so a slot is a whole
patch — algorithm plus every pot position. Keep it that way rather than making each
algorithm remember its own knobs: with six live pots, per-model settings would leave
**six uncaught pots on every model change**, which is the friction the six-pot layout
exists to remove.

Cross-model recall comes free because every control is stored as a **norm** and mapped
through the active algorithm's range at use time. 0.7 CHAOS means "70% through this
algorithm's bifurcation range" whichever algorithm is loaded, and because those ranges
are individually bench-calibrated, a norm carried across models lands somewhere musical
rather than somewhere unstable. The cheapest version needs no presets at all: the norms
already survive algorithm switches on purpose, so changing algorithm keeps the knobs
and the sound morphs under your hands.

## Portability work done (2026-09-01)

Two pieces landed ahead of the move, so that bench work on the Teensy carries over
to the target hardware rather than being rewritten.

**Rate figures are per second, not per sample.** `dt` means simulated time advanced
per audio sample, so pitch is `dt x sampleRate` — which made `dtBase` carry two
meanings at once: the largest numerically-safe integration *step* (a property of the
equations) and the top of the pot's pitch range (not sample-rate independent at all).
Left alone, every range tuned at 44.1 kHz would have come out ~1.1 octaves sharp at
96 kHz, and the per-sample step cap would have been over twice as permissive as the
CPU budget it was measured against — the block would overrun and the governor would
be left to catch it.

`dtBase` is now the stability limit only. `simRateMin/Max` carry the pitch range in
simulated time units per second, `maxStepsPerSecond` the CPU ceiling per second, and
`ChaosBase::scheduleFor(simRate, sampleRate)` turns a request into a step size and a
fractional step count. The characterisation harness asserts both properties: identity
with the old per-sample formula at 44.1 kHz, and pitch invariance across 44.1 / 48 /
96 kHz.

**`chaos_core::Voice`.** The attractor, the oversampling schedule, the gate-driven
AD/SR envelope, DC blocking and the output limiter now live in the library and render
to float buffers. Sample rate is a parameter throughout — envelope times and the DC
blocker's corner are specified in real units and converted internally, so the same
voice runs at 44.1 or 96 kHz untouched. (The DC blocker's fixed 0.0007 coefficient was
the same class of bug: ~4.9 Hz at 44.1 kHz, ~10.7 Hz at 96 kHz.)

`src/teensy_chaos/main.cpp` keeps only what is genuinely Teensy: the `AudioStream`
callback and its blocks, the `ARM_DWT_CYCCNT` load governor, the critical section
around `setParams()`, and the float→int16 conversion. Firmware builds clean; flash
41,756 B, RAM1 variables 13,152 B.

The constant-rate simulation and decimation work therefore belongs in `Voice`, where
it will transfer — with the caveat that **N still has to be re-tuned after the move**,
since 44.1 and 96 kHz start with different amounts of foldover to remove.

## Parked until the hardware is in hand

- **Model-select UX.** Leading candidate: B1 enters select mode, all six rings show a
  bank of 4 (`DrawSelector`, `ZoneGeometry::Region`, `avail_mask` blanking empty slots),
  giving 24 slots laid out spatially; turn any pot to choose, B1 confirms. Legible and
  physical, but the pot used to choose comes back uncaught — acceptable in principle
  since only that one pot is affected, and only testable by hand.
- **N**, by ear, against the current sound.
- **The algorithm list.** The Sprott catalogue (A–S) is polynomial, cheap, and
  structurally varied — a dozen algorithms in Rössler's cost class. Plus the named 3D
  attractors (Chen, Lü, Halvorsen, Aizawa, Dadras, Rikitake, Nosé–Hoover), Thomas'
  cyclically symmetric attractor (Duffing-priced, three `sinf`), and the forced Van der
  Pol. **16 is the practical ceiling** — that is what `DrawSlotIndicator` encodes and
  what a person can read at a glance.

## Still to measure

- **On-device peak CPU at full oversampling.** Duffing with TUNE at maximum and +3 V on
  V/Oct; Chua at +5 V. Watch for the `!` governor prefix. This is the only honest input
  for retuning `maxStepsPerSecond` — the static estimates predicted Duffing at 36% and a
  partial reading showed 17%, consistent with ~2.6 steps rather than the cap's 8.
- **Whether Alchemy Lab ships V1 or V2.**

## Tooling

`libs/chaos_core/tools/characterise.cpp` sweeps each algorithm on a host compiler and
prints the measured constructor values — per-step cost, guard trips, and the
`atanh(0.90)/median` gains — against what `Attractors.h` declares. It reproduces the
six shipped gains to within a fraction of a percent, so it can be trusted to
characterise new ones. See `libs/chaos_core/README.md`.
