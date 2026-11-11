# Teensy 4.1 — `teensy_chaos`: Secret

**Secret**, the chaotic-attractor oscillator, on the Teensy Chaos hardware: Teensy 4.1,
Teensy Audio Shield (SGTL5000), a 128×64 SSD1306 OLED on SPI, four CV inputs through an
ADS1115 and two CV outputs from an MCP4822. 10 HP.

The DSP is the same as Secret's on the Hermetic Modular Alchemy Lab
([`eurorack_daisy_patch_init/daisy_chaos`](https://github.com/Eight4aWish/eurorack_daisy_patch_init/tree/main/daisy_chaos)):
twelve RK4 models in a `chaos_core::Voice`, played by pitch in Hz, so V/OCT plays notes and
TAME runs from free chaos to a locked tone. [`libs/chaos_core/`](../libs/chaos_core/) is a
copy of that library; its README says which commit and how to update it. What Secret is
and how TAME works is in
[`docs/SECRET.md`](https://github.com/Eight4aWish/eurorack_daisy_patch_init/blob/main/docs/SECRET.md).

It replaced the **Chaos** firmware on 2026-11-11. Chaos had six of these models, played
by a simulated-time rate rather than a note, and its source and this file's old contents
are at tag `teensy-chaos-final`.

## Panel

The jacks keep the jobs they had under Chaos. Bottom to top, with the pot beside each:

| Jack | Job | Pot | Job |
| --- | --- | --- | --- |
| RST | GATE | DEPTH | TAME, none (0) to full (1) |
| CLK | V/OCT | CHAR | the secondary parameter |
| ASGN | TUNE CV, 1 V/oct like CLK | RATE | TUNE, 27.5–880 Hz, exponential |
| MOD | CHAOS CV, ±5 V sweeps the knob from its centre to either end | CHAOS | the bifurcation parameter |

- **BTN, short press**: the next of twelve models, in Secret's order: Rossler, Van der Pol,
  Lorenz, Chua, Duffing, Coupled Rossler, Pendulum, Lorenz-Lu-Chen, Moore-Spiegel,
  Brusselator, Colpitts, Hindmarsh-Rose.
- **BTN, long press**: the ENV page. There CHAOS chooses **Drone** (below 45% of its travel) or
  **Gated** (above 55%), CHAR sets attack + decay and DEPTH sustain
  + release. RATE stays TUNE on both pages. On a page switch the other three pots wait to
  be turned past their stored value before they take over (pickup), and CHAOS does not
  switch the mode until it has been moved on the ENV page.
- **X, Y**: the attractor's raw state as CV, ±5 V. **Audio L and R** carry X and Y,
  through the soft limiter, at the eurorack ±5 V level (line out 13).
- **Audio in**: not used yet.

In **Drone** (the default) the VCA is open and GATE is ignored. In **Gated** a rising edge
on GATE starts a note: each channel's attack waits for its next zero crossing, and the
attractor is re-seeded only from silence, so a gate during a release retriggers without a
jump. Gate thresholds: on at about +1.0 V, off at about +0.7 V.

TAME has no CV jack here; on the Alchemy Lab it has one (J6). ASGN has no counterpart
there: it is a second V/OCT input, summed with CLK.

## Screen

- Top half: the phase-space plot (X against Y), the model number top left, and the peak
  audio-update load since the last model change top right. `!` before the load figure
  means the governor (below) is holding the pitch down.
- Below it, the model name (`GATE` at the right when Gated), CHAOS and CHAR in the model's
  own units, the pitch playing, and TAME. The pitch is the one actually playing, so it
  reads lower than the note asked for when a model's step cap holds it.
- The ENV page shows the mode, attack, decay, sustain and release.
- After 20 s with no knob or button moved, the screen goes fully off, display and charge
  pump, and the first touch wakes it. The noise test
  (`eurorack_electronics/docs/noise_test.md`) measured the panel, when powered, at about
  +11 dB on the output noise, a 5.7 kHz line at −76 dB; screen off, the output is at
  −85.5 dB(A).

## Step caps and the governor

Pitch above what a model's safe step reaches is bought with more RK4 steps per sample,
up to a per-model cap (`maxStepsPerSecond`), measured on the Alchemy Lab at 480 MHz. The
bench build runs each model at its cap before audio starts and prints the load over USB
serial:

```sh
pio run -e teensy_chaos_bench -t upload   # in the foreground; reflash teensy_chaos after
```

On 2026-11-11, at 600 MHz, 44.1 kHz and 128-sample blocks:

| Model | Steps/sample at the cap | Cycles/step | Load, TAME 0 | Load, TAME 1 |
| --- | ---: | ---: | ---: | ---: |
| Rossler | 18.4 | 241 | 33% | 49% |
| Van der Pol | 19.0 | 181 | 25% | 43% |
| Lorenz | 25.9 | 251 | 48% | 48% |
| Chua | 22.2 | 279 | 46% | 46% |
| Duffing | 9.3 | 590 | 40% | 49% |
| Coupled Rossler | 12.0 | 429 | 38% | 49% |
| Pendulum | 4.1 | 1404 | 42% | 50% |
| Lorenz-Lu-Chen | 17.0 | 399 | 50% | 50% |
| Moore-Spiegel | 12.2 | 427 | 38% | 50% |
| Brusselator | 6.1 | 912 | 41% | 47% |
| Colpitts | 5.9 | 970 | 42% | 48% |
| Hindmarsh-Rose | 11.6 | 472 | 40% | 51% |

Cycles per step are within a few percent of the Alchemy Lab's, and the Teensy has 25% more
cycles, so every model at its cap takes at most about half a block. The caps are used as
they are, which keeps the pitch range the same as Secret's. (Lorenz, Chua and
Lorenz-Lu-Chen have no TAME gain, so TAME costs them nothing.)

The governor stays as a backstop. The audio update runs in an interrupt that preempts the
control loop, so a block that overran would starve the loop for good: dead panel, stuck
CV. The update times itself, and above 75% of a block it runs fewer steps (the pitch goes
flat), recovering below 50%.

## Build and upload

```sh
pio run -e teensy_chaos
pio run -e teensy_chaos -t upload   # in the foreground
```

The env uses the Teensy Loader app (`teensy-gui`) and PlatformIO's arm64 GCC
(`scripts/arm64_toolchain.py`), since the Teensy platform's own loader and compiler are
x86_64 only.

## Calibration

In `src/teensy_chaos/main.cpp`, measured 2026-04-07. The CV front end inverts, so a higher
voltage reads as a lower ADS1115 code.

| Input | 0 V code | Codes per volt |
| --- | ---: | ---: |
| MOD | 13236 | 2414 |
| ASGN | 13240 | 2424 |
| CLK | 13241 | 2421 |

GATE (RST) goes high below code 10820 and low above 11600. The MCP4822 outputs follow
`Vout = code × 0.002431 − 4.972`.

## Hardware notes

- Pin assignments are in [`include/teensy_chaos/pins.h`](../include/teensy_chaos/pins.h).
- Pin 10 is the OLED's chip select and the Audio Shield's SD card's too; the SD card is held
  off in `setup()`.
- The ADS1115 is on Wire1 (SDA 17, SCL 16); the codec on Wire (SDA 18, SCL 19). A failed
  I²C read holds the last good value, so one bus hiccup is not a 5 V spike on every input.
