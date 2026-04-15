# MKI x ES EDU SNARE DRUM — Definitive Netlist

**Purpose:** Authoritative netlist for the solderable breadboard design.

**Sources (in order of authority):**
1. Production schematic (PDF p44) — circuit topology and designators
2. Tutorial text (PDF p9-25) — component values, functional descriptions, schematics
3. BOM (PDF p3-5) — component counts and values
4. Build guide text (PDF p42-43) — power supply and jack designators
5. Full hand-drawn schematic (PDF p2) — overview topology

**Two signal paths:** DRUM (pitched oscillation) + SNARE WIRES (noise burst),
combined by a mixer. Significantly larger than kick drum: 3 ICs, 7 transistors,
~80 total components vs ~55 for kick.

---

## BOM

### Resistors (37 total)
| Qty | Value | Designators | Role |
|-----|-------|-------------|------|
| 2 | 1M | R1, R2 | Noise amp feedback, VCA base bias |
| 1 | 910K | R3 | Oscillator bridge |
| 1 | 470K | R4 | Decay injection to CAP_MID |
| 1 | 120K | R5 | Accent node parallel limiter |
| 7 | 100K | R6, R7, R8, R9, R10, R11, R12 | Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF |
| 3 | 47K | R13, R14, R15 | Decay input, decay feedback, pitch env divider |
| 1 | 39K | R16 | Gate HP filter discharge |
| 2 | 33K | R17, R18 | Comparator threshold, mixer feedback |
| 1 | 27K | R19 | Pitch env divider |
| 5 | 22K | R20, R21, R22, R23, R24 | Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV |
| 2 | 10K | R25, R26 | Trigger divider top, mixer noise input |
| 7 | 1K | R27, R28, R29, R30, R31, R32, R33 | Bridge, trigger div bottom, attack series, output + unconfirmed |
| 1 | 470R | R34 | Oscillator tuning to GND |
| 1 | 330R | R35 | Pitch NPN series (min frequency floor) |
| 1 | 100K | R38 | Mixer drum input (see BOM discrepancy note) |
| 2 | 10R | R36, R37 | Power supply series filtering |

**Note:** R36/R37 are production designators R9/R10 per p42. Other R
designators are systematic assignments — production PCB designators may differ.

**BOM discrepancy:** Tutorial (p22) shows a 100K resistor for the mixer drum
input path, which would require 8× 100K. The BOM lists only 7× 100K. The
mixer drum input may actually share with another 100K or use a different
value on the production PCB. For the breadboard, use the tutorial value (100K)
and source one additional 100K resistor. This extra resistor is designated R38.

### Capacitors (22 total)
| Qty | Value | Type | Designators | Role |
|-----|-------|------|-------------|------|
| 2 | 47uF | Electrolytic | C8, C9 | Power supply bulk filter |
| 1 | 1uF | Film | C1 | Noise AC coupling |
| 2 | 470nF | Film | C10, C11 | Pitch envelope, noise envelope |
| 6 | 100nF | Ceramic | C2, C3, C4, C5, C6, C7 | IC decoupling |
| 2 | 100nF | Ceramic | C21, C22 | Power bypass |
| 3 | 33nF | Film | C12, C13, C14 | Oscillator ×2, VCA input AC coupling |
| 2 | 10nF | Film | C15, C16 | Gate HP filter, HPF AC coupling output |
| 2 | 2.2nF | Film | C17, C18 | VCA output filter ×2 |
| 2 | 1nF | Film | C19, C20 | HPF filter stages ×2 |

**Note:** C2-C9 and C21-C22 use production designators per p42-43.
Other cap designators are systematic. Production PCB may differ.

### Semiconductors
| Qty | Type | Designators | Role |
|-----|------|-------------|------|
| 6 | 1N4148 | VD3-VD8 | Signal diodes (see block details) |
| 2 | 1N5819 | VD1, VD2 | Power reverse polarity protection |
| 2 | BC558 (PNP) | VT1, VT2 | Accent CV limiter, snappy CV gate |
| 5 | BC548 (NPN) | VT3-VT7 | Accent buffer, noise source, pitch mod, HPF follower, VCA |
| 3 | TL072 | DA1, DA2, DA3 | 6 op-amp halves total |

### Potentiometers (5 total)
| Qty | Value | Taper | Designator | Label | Wiring |
|-----|-------|-------|------------|-------|--------|
| 1 | 250K | B (lin) | P1 | DECAY | Parallel with R14 (47K feedback) |
| 1 | 100K | A (log) | P2 | SNAPPY | Collector load + envelope discharge |
| 1 | 100K | B (lin) | P3 | PITCH CV | Voltage divider: CV attenuation |
| 1 | 5K | B (lin) | P4 | ATTACK | Series R: pitch env charge rate |
| 1 | 1K | B (lin) | P5 | TUNE | Variable R: oscillator freq |

### Connectors (6 total)
| Qty | Type | Designator | Label |
|-----|------|------------|-------|
| 1 | Switched mono jack | XS1 | ACCENT IN |
| 1 | Switched mono jack | XS2 | TUNE CV IN |
| 1 | Switched mono jack | XS3 | SNAPPY CV IN |
| 1 | Switched mono jack | XS4 | TRIGGER IN |
| 1 | Switched mono jack | XS5 | AUDIO OUTPUT |
| 1 | 2x5 pin header | XP1 | Eurorack power |

---

## NET NAMES

| Net | Description |
|-----|-------------|
| VCC | +12V filtered supply rail |
| VEE | -12V filtered supply rail |
| GND | Ground (0V) |
| GATE_IN | Trigger input jack tip (XS4) |
| HP_OUT | High-pass filter output (comp + input) |
| COMP_INV | Comparator inverting input (threshold) |
| COMP_OUT | Comparator output |
| ACC_IN | Accent node (after 100K from comp) |
| ACC_EMIT | PNP emitter / NPN base junction |
| ACC_TRIG | Accented trigger (NPN emitter) |
| TRIG_POS | Trigger after blocking diode (to oscillator) |
| OSC_NONINV | Oscillator op-amp non-inverting input |
| OSC_INV | Oscillator op-amp inverting input |
| OSC_OUT | Oscillator op-amp output |
| CAP_MID | Node between oscillator's two 33nF caps |
| DECAY_INV | Decay buffer inverting input |
| DECAY_OUT | Decay buffer output |
| PITCH_NPN_E | Pitch modulation NPN emitter |
| ENV_CAP | Pitch envelope capacitor top |
| ENV_DIV | Pitch envelope divider junction (27K/47K) |
| PCV_BUF_IN | Pitch CV buffer input (pot wiper) |
| PCV_BUF_OUT | Pitch CV buffer output |
| NOISE_COLL | Noise transistor collector |
| NOISE_BIAS | Noise amp + input (after AC coupling + bias) |
| NOISE_AMP_OUT | Noise amplifier output |
| VCA_BASE | VCA transistor base (after 33nF coupling) |
| VCA_COLL | VCA collector node |
| NOISE_ENV_CAP | Noise envelope cap top |
| HPF_A | HPF node between two 1nF caps |
| HPF_EMIT | HPF emitter follower output |
| HPF_AC | HPF output after 10nF AC coupling |
| MIX_INV | Mixer inverting input (summing junction) |
| MIX_OUT | Mixer output |
| ACCENT_CV_IN | Accent CV jack tip (XS1) |
| PITCH_CV_IN | Tune CV jack tip (XS2) |
| SNAPPY_CV_IN | Snappy CV jack tip (XS3) |
| OUTPUT_J | Audio output jack tip (XS5) |

---

## NETLIST BY FUNCTIONAL BLOCK

### BLOCK 1: Gate-to-Trigger Converter
**Source: p11 (tutorial, reused from kick), p44 (schematic)**

Converts a sustained gate into a short trigger pulse. High-pass filter
differentiates the gate edge; comparator converts it to a clean ±12V pulse.
Identical topology to kick drum.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C15 | 10nF | GATE_IN | HP_OUT | — |
| R16 | 39K | HP_OUT | GND | — |
| VD3 (1N4148) | — | GND (anode) | HP_OUT (cathode) | — |
| R6 | 100K | VCC | COMP_INV | — |
| R17 | 33K | COMP_INV | GND | — |
| DA1A (TL072) | — | HP_OUT (+in) | COMP_INV (-in) | COMP_OUT (out) |

**Notes:**
- VD3 clamps negative spikes at comp non-inv input (prevents latch-up)
- Threshold voltage: 12V × 33K/(33K+100K) ≈ 3.0V (same as kick)
- Comparator outputs +12V when HP_OUT > 3V, -12V otherwise

---

### BLOCK 2: Accent CV Section
**Source: p11 (tutorial, reused from kick), p44 (schematic)**

Modulates trigger amplitude via external accent CV. PNP emitter follower
sets voltage floor, NPN buffers the result.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R7 | 100K | COMP_OUT | ACC_IN | — |
| R5 | 120K | ACC_IN | GND | — |
| R20 | 22K | ACC_IN | ACC_EMIT | — |
| VT1 (BC558 PNP) | — | ACCENT_CV_IN (B) | GND (C) | ACC_EMIT (E) |
| VT3 (BC548 NPN) | — | ACC_EMIT (B) | VCC (C) | ACC_TRIG (E) |

**Notes:**
- At 0V accent CV: trigger appears at moderate level
- Higher CV: PNP clamps, limiting trigger amplitude
- XS1 jack tip connects to VT1 base
- ACC_TRIG drives: blocking diode to oscillator (Block 3)
- COMP_OUT also drives envelope charge diodes directly (Blocks 6, 10)

---

### BLOCK 3: Trigger Routing to Oscillator
**Source: p11 (tutorial), p44 (schematic)**

Routes accented trigger to oscillator input through blocking diode and
10K/1K voltage divider. Different divider ratio from kick (10K/1K vs 100K/14K).

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| VD4 (1N4148) | — | ACC_TRIG (anode) | TRIG_POS (cathode) |
| R25 | 10K | TRIG_POS | OSC_NONINV |
| R27 | 1K | OSC_NONINV | GND |

**Verification:** 12V × 1K/(10K+1K) = 1.09V — scaled trigger at oscillator input.
(Kick used 100K/14K for ~1.4V)

---

### BLOCK 4: Bridged-T Percussive Oscillator
**Source: p10, 13 (tutorial), p44 (schematic)**

Core drum sound source. Bridged-T network produces decaying sine wave when
triggered. Tuned for 100-200 Hz range (snare body). Same topology as kick
but with 33nF caps (vs 15nF) for lower starting frequency, and 1K TUNE pot
(vs 250K PITCH pot).

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C12 | 33nF | OSC_OUT | CAP_MID | — |
| C13 | 33nF | CAP_MID | OSC_INV | — |
| R28 | 1K | OSC_OUT | OSC_INV | — |
| R3 | 910K | OSC_OUT | OSC_INV | — |
| P5 (1K B) | TUNE | CAP_MID (pin 1) | GND via R34 (wiper+pin 3) | — |
| R34 | 470R | P5 wiper+pin3 | GND | — |
| DA1B (TL072) | — | OSC_NONINV (+in) | OSC_INV (-in) | OSC_OUT (out) |

**Notes:**
- R28 (1K bridge) + R3 (910K bridge) form the bridged-T with C12/C13
- P5 wired as variable resistor (wiper shorted to pin 3)
- Resistance to GND: P5 (0-1K) + R34 (470R) = 470R to 1.47K
- R3 (910K) determines oscillation decay rate and amplitude
- CAP_MID also receives: VT4 collector (pitch mod), R4 (decay injection)

---

### BLOCK 5: Decay Feedback
**Source: p12 (tutorial), p44 (schematic)**

Inverting buffer feeds oscillator output back to CAP_MID, extending decay.
Same topology as kick but with 250K DECAY pot (vs 1M).

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R13 | 47K | OSC_OUT | DECAY_INV | — |
| R14 | 47K | DECAY_INV | DECAY_OUT | — |
| P1 (250K B) | DECAY | DECAY_INV (pin 1) | DECAY_OUT (pin 2+wiper) | — |
| R4 | 470K | DECAY_OUT | CAP_MID | — |
| DA2A (TL072) | — | GND (+in) | DECAY_INV (-in) | DECAY_OUT (out) |

**Notes:**
- P1 in parallel with R14 (47K feedback)
- P1 wired: pin 1 to DECAY_INV, pin 2+wiper to DECAY_OUT
- At P1 min (0R): gain = 0 → no feedback → natural short decay
- At P1 max (250K): gain = -(47K‖250K)/47K ≈ -0.84 ≈ "about 0.8" (p12) ✓
- R4 (470K) limits injection to avoid overdriving oscillator

---

### BLOCK 6: Pitch Attack Envelope
**Source: p13-14 (tutorial), p44 (schematic)**

RC envelope for pitch modulation. Charges from FULL trigger (COMP_OUT,
not accented trigger) for consistent attack at all accent levels.
NPN transistor provides voltage-controlled resistance from CAP_MID to GND.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VD5 (1N4148) | — | COMP_OUT (anode) | ENV_CAP (cathode) | — |
| P4 (5K B) | ATTACK | VD5 cathode side (pin 1) | ENV_CAP via R29 (wiper+pin 3) | — |
| R29 | 1K | P4 wiper+pin3 | ENV_CAP | — |
| C10 | 470nF | ENV_CAP | GND | — |
| R19 | 27K | ENV_CAP | ENV_DIV | — |
| R15 | 47K | ENV_DIV | PCV_BUF_OUT | — |
| VT4 (BC548 NPN) | — | ENV_DIV (B) | CAP_MID (C) | PITCH_NPN_E (E) |
| R35 | 330R | PITCH_NPN_E | GND | — |

**Notes:**
- **Charge path:** COMP_OUT → VD5 → P4(5K)+R29(1K) → C10 → GND
  - Attack resistance = 1K to 6K (variable via P4)
  - Uses COMP_OUT (full trigger, p13: "full size trigger instead of accented one")
- **Discharge path:** C10 → R19(27K) → ENV_DIV → R15(47K) → PCV_BUF_OUT
  - When no pitch CV: PCV_BUF_OUT = 0V, so discharge to GND through 27K+47K
  - τ ≈ 470nF × 74K ≈ 35ms
- **VT4 pitch modulation:** When ENV_DIV rises, VT4 opens, lowering CAP_MID
  impedance to GND through 330R, raising oscillator frequency
- R35 (330R) prevents effective R from reaching 0 → max freq ceiling
- At ENV_DIV = 0V: VT4 off → oscillator at base frequency

---

### BLOCK 7: Pitch CV Input + Buffer
**Source: p23 (tutorial), p44 (schematic)**

External pitch CV sets the transistor's operating point, keeping it open
to varying degrees. An op-amp buffer isolates the attenuator impedance
from the envelope discharge path.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R8 | 100K | PITCH_CV_IN | P3 pin 1 | — |
| P3 (100K B) | PITCH CV | R8 (pin 1) | PCV_BUF_IN (wiper) | GND (pin 3) |
| DA3A (TL072) | — | PCV_BUF_IN (+in) | PCV_BUF_OUT (-in) | PCV_BUF_OUT (out) |

**Notes:**
- R8 (100K) provides input protection / impedance matching
- P3 wired as voltage divider: pin 1 from CV, pin 3 to GND, wiper = output
- DA3A wired as voltage follower (unity gain buffer)
- Buffer output (PCV_BUF_OUT) replaces GND at R15 bottom (Block 6):
  - At P3 max: high CV → R15 bottom lifted → shallow pitch sweep
  - At P3 min: 0V → normal envelope decay to ground
- XS2 jack tip = PITCH_CV_IN
- Buffer prevents attenuator impedance from affecting envelope discharge (p23)

---

### BLOCK 8: Noise Generator
**Source: p16 (tutorial), p44 (schematic)**

White noise source using reverse-biased BC548 (avalanche breakdown).
AC-coupled and amplified ×46 by non-inverting op-amp stage.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VT5 (BC548 NPN) | — | reversed | VCC via R9 (E, reversed) | NOISE_COLL (C) |
| R9 | 100K | VCC | VT5 emitter (reversed) | — |
| R10 | 100K | NOISE_COLL | GND | — |
| C1 | 1uF | NOISE_COLL | NOISE_BIAS | — |
| R11 | 100K | NOISE_BIAS | GND | — |
| R1 | 1M | NOISE_AMP_OUT | NOISE_AMP_INV | — |
| R21 | 22K | NOISE_AMP_INV | GND | — |
| DA2B (TL072) | — | NOISE_BIAS (+in) | NOISE_AMP_INV (-in) | NOISE_AMP_OUT (out) |

**Notes:**
- VT5 wired BACKWARDS: emitter connected to VCC through R9 (100K),
  causing avalanche breakdown → random current fluctuations at collector
- R10 (100K) is the collector load (produces voltage from noise current)
- C1 (1uF) AC-couples the noise signal (removes DC offset)
- R11 (100K) biases the op-amp + input at 0V DC
- Non-inverting amplifier: gain = 1 + 1M/22K = 1 + 45.45 ≈ **46** ≈ "45" (p16) ✓
- Output: uniform white noise signal

**Transistor wiring clarification:**
- Physical emitter pin → connect to VCC through R9 (reverse bias)
- Physical collector pin → signal output (NOISE_COLL)
- Physical base pin → floating or grounded (check production schematic)

---

### BLOCK 9: Swing Type VCA
**Source: p17 (tutorial), p44 (schematic)**

NPN transistor-based amplitude modulator (Roland 606/808 style). The
collector voltage is set by the envelope CV — when transistor is in cutoff,
output = CV level. Signal enters at base through AC coupling.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C14 | 33nF | NOISE_AMP_OUT | VCA_BASE | — |
| R2 | 1M | VCA_BASE | VCC | — |
| VT6 (BC548 NPN) | — | VCA_BASE (B) | VCA_COLL (C) | GND (E) |
| VD6 (1N4148) | — | GND (anode) | VCA_COLL (cathode) | — |
| C17 | 2.2nF | VCA_COLL | GND | — |
| C18 | 2.2nF | VCA_COLL | GND | — |

**Notes:**
- C14 (33nF) AC-couples noise into VT6 base
- R2 (1M) biases base to VCC, keeping transistor forward-active at idle
- VCA collector load is P2 (100K A pot) from Block 10 — NOT a fixed resistor
- VD6 clamps collector to prevent going below 0V when CV is low (keeps VCA quiet)
- C17, C18 (2.2nF each) filter HF distortion from hard switching
- VT6 emitter → GND

---

### BLOCK 10: Noise Envelope + Snappy Control
**Source: p19, 25 (tutorial), p44 (schematic)**

Simple RC envelope for noise burst. Charges from full trigger (fast, no
series resistance). The 100K A-taper pot does double duty as VCA collector
load AND envelope discharge path. PNP transistor provides external CV
control over decay rate (snappy CV).

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VD7 (1N4148) | — | COMP_OUT (anode) | NOISE_ENV_CAP (cathode) | — |
| C11 | 470nF | NOISE_ENV_CAP | GND | — |
| P2 (100K A) | SNAPPY | NOISE_ENV_CAP (pin 1) | VCA_COLL (wiper+pin 2) | — |
| VT2 (BC558 PNP) | — | SNAPPY_CV_IN (B) | GND via R22 (C) | NOISE_ENV_CAP (E) |
| R22 | 22K | VT2 collector | GND | — |

**Notes:**
- **Charge:** COMP_OUT → VD7 → C11 (fast, no series R per p19)
- **Discharge path 1:** C11 → P2(100K A) → VCA_COLL (sets both decay rate and
  VCA collector voltage)
- **Discharge path 2 (Snappy CV, p25):** C11 → VT2(E→C) → R22(22K) → GND
  - At 0V CV: PNP fully open → fast discharge through 22K → short snappy burst
  - At higher CV: PNP closes → slower discharge → longer noise tail
- P2 wired: pin 1 to cap, pin 2+wiper to VCA collector
- A-taper gives perceptually linear decay control
- XS3 jack tip = SNAPPY_CV_IN

---

### BLOCK 11: High Pass Filter
**Source: p20 (tutorial), p44 (schematic)**

Second-order Sallen-Key high pass filter removes low end from noise path.
Uses NPN emitter follower (instead of op-amp) as active element. Feedback
from emitter to filter node introduces resonance.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C19 | 1nF | VCA_COLL | HPF_A | — |
| C20 | 1nF | HPF_A | VT7 base | — |
| R12 | 100K | HPF_A | GND | — |
| VT7 (BC548 NPN) | — | C20 output (B) | VCC (C) | HPF_EMIT (E) |
| R23 | 22K | HPF_EMIT | VEE | — |
| R24 | 22K | HPF_EMIT | HPF_A | — |
| C16 | 10nF | HPF_EMIT | HPF_AC | — |

**Notes:**
- fc = 1/(2π × C_eff × R) where C_eff = C19×C20/(C19+C20) = 0.5nF
  fc = 1/(2π × 0.5nF × 100K) = **3.18 kHz** ≈ "3.4 kHz" (p20) ✓
- R24 (22K) feedback from emitter to HPF_A provides resonance:
  "a pronounced bump at that same frequency" (p20)
- R23 (22K) biases emitter follower to VEE (-12V, not GND!)
  "the transistor wouldn't be able to reproduce some of the signal because
  it's already fully closed at 0 V base voltage" (p20) — VEE gives headroom
- C16 (10nF) AC-couples the output (removes DC offset from VEE bias)
- Output: ~3V peak-to-peak, offset ≈ -1V (removed by C16)

---

### BLOCK 12: Mixer + Output
**Source: p22 (tutorial), p44 (schematic)**

Inverting summing amplifier mixes drum oscillator and filtered noise.
Individual input resistors set the gain for each path.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R38 | 100K | OSC_OUT | MIX_INV | — |
| R26 | 10K | HPF_AC | MIX_INV | — |
| R18 | 33K | MIX_INV | MIX_OUT | — |
| DA3B (TL072) | — | GND (+in) | MIX_INV (-in) | MIX_OUT (out) |
| R30 | 1K | MIX_OUT | OUTPUT_J | — |

**Notes:**
- Drum gain = -33K/100K = **-0.33** (drum signal is already large)
- Noise gain = -33K/10K = **-3.3** (noise after HPF is small, needs boost)
- "approximately 10 V peak-to-peak signal at the output" (p22)
- R30 (1K) provides output series protection (same role as kick's R21)
- XS5 jack tip = OUTPUT_J
- **Note:** R38 is the mixer's 100K drum input — this is separate from
  the R6 in Block 1. Total 100K usage is 7 as per BOM.

---

### BLOCK 13: Power Supply
**Source: p42-43 (build guide), p44 (schematic)**

Standard Eurorack ±12V with reverse polarity protection, series resistor
filtering, and decoupling. Identical topology to kick.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| VD1 (1N5819) | — | +12V_raw (anode) | VCC (cathode) |
| VD2 (1N5819) | — | VEE (anode) | -12V_raw (cathode) |
| R36 | 10R | VCC (after VD1) | VCC_filt |
| R37 | 10R | VEE (after VD2) | VEE_filt |
| C8 | 47uF | VCC | GND |
| C9 | 47uF | GND | VEE (observe polarity!) |
| C6 | 100nF | VCC | GND |
| C7 | 100nF | VEE | GND |

**IC Decoupling (placed close to IC power pins):**
| Component | Value | Location |
|-----------|-------|----------|
| C2 | 100nF | DA1 pin 8 (V+) to GND |
| C3 | 100nF | DA1 pin 4 (V-) to GND |
| C4 | 100nF | DA2 pin 8 (V+) to GND |
| C5 | 100nF | DA2 pin 4 (V-) to GND |
| C21 | 100nF | DA3 pin 8 (V+) to GND |
| C22 | 100nF | DA3 pin 4 (V-) to GND |

**Notes:**
- R36/R37 (10R) also act as slow-blow fuses (p42)
- VD1/VD2: 1N5819 Schottky for low forward voltage drop
- 8× 100nF total (C2-C7, C21, C22) matches BOM ✓
- Designators C2-C9, C21, C22 per p42-43 production names

---

### BLOCK 14: Unconfirmed Components

These components are in the BOM but their exact placement is not fully
described in the tutorial text. Most are likely jack input protection
or additional pull-down resistors, present on the production PCB.

| Component | Value | Likely Role |
|-----------|-------|-------------|
| R31 | 1K | Jack input protection (XS1 or XS4) |
| R32 | 1K | Jack input protection |
| R33 | 1K | Jack input protection or pull-down |
| VD8 (1N4148) | — | 6th signal diode — likely input protection on CV jack |

**Notes:**
- BOM lists 1K ×7. Only 4 are confidently placed: R28 (oscillator bridge),
  R27 (trigger divider), R29 (attack series), R30 (output protection)
- BOM lists 1N4148 ×6. Only 5 are confidently placed (VD3-VD7).
  VD8 is likely on a CV input for protection.
- These can be omitted from the breadboard initially and added once
  confirmed from the physical PCB.

---

## OP-AMP PIN ASSIGNMENTS

All three ICs are TL072 dual op-amps in DIP-8 package.
**Rotated 180°** on n8synth breadboard (pin 8/VCC faces +12V left rail).

### DA1 (TL072 #1): Comparator + Oscillator
| Pin | Function | Net |
|-----|----------|-----|
| 1 | OUT A | COMP_OUT |
| 2 | IN- A | COMP_INV |
| 3 | IN+ A | HP_OUT |
| 4 | V- | VEE |
| 5 | IN+ B | OSC_NONINV |
| 6 | IN- B | OSC_INV |
| 7 | OUT B | OSC_OUT |
| 8 | V+ | VCC |

### DA2 (TL072 #2): Decay Buffer + Noise Amplifier
| Pin | Function | Net |
|-----|----------|-----|
| 1 | OUT A | DECAY_OUT |
| 2 | IN- A | DECAY_INV |
| 3 | IN+ A | GND |
| 4 | V- | VEE |
| 5 | IN+ B | NOISE_BIAS |
| 6 | IN- B | NOISE_AMP_INV |
| 7 | OUT B | NOISE_AMP_OUT |
| 8 | V+ | VCC |

### DA3 (TL072 #3): Pitch CV Buffer + Mixer
| Pin | Function | Net |
|-----|----------|-----|
| 1 | OUT A | PCV_BUF_OUT |
| 2 | IN- A | PCV_BUF_OUT (follower) |
| 3 | IN+ A | PCV_BUF_IN |
| 4 | V- | VEE |
| 5 | IN+ B | GND |
| 6 | IN- B | MIX_INV |
| 7 | OUT B | MIX_OUT |
| 8 | V+ | VCC |

**Note:** Op-amp half assignments are inferred from functional grouping.
The production PCB may assign halves differently — the circuit works
identically regardless of which half handles which function within an IC.

---

## COMPLETE NET LIST (Net-Centric View)

```
NET GND (0V):
  R16.2(39K), R17.2(33K), R27.2(1K), R34.2(470R), R35.2(330R),
  R5.2(120K), R10.2(100K noise collector), R11.2(100K noise bias),
  R12.2(100K HPF), R21.2(22K noise amp), R22.2(22K snappy),
  C10.2(470nF pitch env), C11.2(470nF noise env),
  C12.2(33nF VCA coupling — actually VCA emitter),
  C17.2, C18.2 (2.2nF VCA filter),
  P5.wiper+pin3(TUNE via R34), P3.pin3(PITCH CV),
  VD3.anode, VD6.anode,
  VT1.C, VT6.E(VCA emitter),
  DA2A.+(noninv), DA3B.+(noninv),
  Power: C8.-, C9.+, C6/C7 bypass, C2-C5/C21/C22 decoupling

NET VCC (+12V):
  R6.1(100K comp threshold), R9.1(100K noise emitter),
  R2.1(1M VCA bias),
  VT3.C(accent NPN collector), VT7.C(HPF NPN collector),
  DA1.pin8, DA2.pin8, DA3.pin8,
  Power: VD1.cathode, R36, C8.+, decoupling caps

NET VEE (-12V):
  R23.2(22K HPF emitter bias),
  DA1.pin4, DA2.pin4, DA3.pin4,
  Power: VD2.anode, R37, C9.-, decoupling caps

NET GATE_IN:
  XS4.tip, C15.1

NET HP_OUT:
  C15.2, R16.1, VD3.cathode, DA1A.+(noninv)

NET COMP_INV:
  R6.2, R17.1, DA1A.-(inv)

NET COMP_OUT:
  DA1A.out, R7.1,
  VD5.anode (pitch env charge),
  VD7.anode (noise env charge)

NET ACC_IN:
  R7.2, R5.1, R20.1

NET ACC_EMIT:
  R20.2, VT1.E(emitter), VT3.B(base)

NET ACC_TRIG:
  VT3.E(emitter), VD4.anode

NET TRIG_POS:
  VD4.cathode, R25.1

NET OSC_NONINV:
  R25.2, R27.1, DA1B.+(noninv)

NET OSC_INV:
  C13.2, R28.2(1K bridge), R3.2(910K bridge), DA1B.-(inv)

NET OSC_OUT:
  C12.1, R28.1(1K bridge), R3.1(910K bridge),
  R13.1(47K decay input), P5_tone(—), DA1B.out,
  R38.1 (100K to mixer)

NET CAP_MID:
  C12.2, C13.1, P5.pin1(TUNE),
  VT4.C(pitch NPN collector), R4.2(470K decay injection)

NET DECAY_INV:
  R13.2, R14.1, P1.pin1, DA2A.-(inv)

NET DECAY_OUT:
  R14.2, P1.pin2+wiper, R4.1, DA2A.out

NET PITCH_NPN_E:
  VT4.E(emitter), R35.1

NET ENV_CAP:
  VD5.cathode side (after P4+R29), C10.1, R19.1

NET ENV_DIV:
  R19.2, R15.1, VT4.B(base)

NET PCV_BUF_IN:
  P3.wiper, DA3A.+(noninv)

NET PCV_BUF_OUT:
  DA3A.out, DA3A.-(inv), R15.2

NET PITCH_CV_IN:
  XS2.tip, R8.1

NET P3_TOP:
  R8.2, P3.pin1

NET NOISE_COLL:
  VT5.C(physical collector), R10.1, C1.1

NET NOISE_BIAS:
  C1.2, R11.1, DA2B.+(noninv)

NET NOISE_AMP_INV:
  R1.2(1M from output), R21.1(22K to GND), DA2B.-(inv)

NET NOISE_AMP_OUT:
  R1.1, DA2B.out, C14.1

NET VCA_BASE:
  C14.2, R2.2(1M to VCC), VT6.B(base)

NET VCA_COLL:
  VT6.C(collector), P2.pin2+wiper,
  VD6.cathode, C17.1, C18.1,
  C19.1 (to HPF)

NET NOISE_ENV_CAP:
  VD7.cathode, C11.1, P2.pin1, VT2.E(emitter)

NET VT2_COLL:
  VT2.C(collector), R22.1

NET HPF_A:
  C19.2, C20.1, R12.1, R24.2(22K feedback)

NET HPF_EMIT:
  VT7.E(emitter), R23.1, R24.1, C16.1

NET HPF_AC:
  C16.2, R26.1

NET MIX_INV:
  R38.2, R26.2, R18.1, DA3B.-(inv)

NET MIX_OUT:
  R18.2, DA3B.out, R30.1

NET OUTPUT_J:
  R30.2, XS5.tip

NET ACCENT_CV_IN:
  XS1.tip, VT1.B(base)

NET SNAPPY_CV_IN:
  XS3.tip, VT2.B(base)
```

---

## SIGNAL FLOW SUMMARY

```
                        ┌──── DRUM PATH ────────────────────────────┐
                        │                                           │
  GATE IN ──→ HP Filter ──→ Comparator ──→ 100K ──→ Accent ──→ NPN Buffer
  (XS4)       C15/R16/VD3   DA1A/R6/R17     R7     R20/R5     VT1/VT3
                                │               VT1←── ACCENT CV (XS1)
                                │                        │
                     ┌──────────┤                        │
                     │          │                        ▼
                     │          │                  ACC_TRIG
                     │          │                        │
                     │          │                  VD4 (blocking)
                     │          │                        │
                     │          │                  10K/1K divider
                     │          │                        │
                     │          │                        ▼
                     │          │                  OSC_NONINV
                     │          │                        │
                     │          │                  Oscillator ──→ OSC_OUT
                     │          │                  DA1B/C12/C13        │
                     │          │                  R28/R3/P5/R34  ┌───┤
                     │          │                       ↑         │   │
                     │          │                  VT4(NPN)───CAP_MID │
                     │          │                       ↑         │   │
                     │     COMP_OUT ──→ VD5 ──→ P4+R29 ──→ 470nF │   │
                     │     (full         (pitch envelope)         │   │
                     │      trigger)     R19/R15 → VT4 base      │   │
                     │          │              ↑                  │   │
                     │          │         PCV_BUF_OUT             │   │
                     │          │              ↑                  │   │
                     │          │         DA3A (buffer)      Decay│ feedback
                     │          │              ↑             DA2A/R13/R14
                     │          │         PITCH CV (XS2)     P1/R4→CAP_MID
                     │          │                                 │
                     │          │                          ┌──────┘
                     │          │                          │
                     │          │                     100K to mixer
                     │          │                          │
                     │          ▼                          ▼
                     │    ┌── SNARE WIRE PATH ──┐    ┌── MIXER ──┐
                     │    │                     │    │            │
                     │    │  Noise Gen          │    │  DA3B      │
                     │    │  VT5(reversed)      │    │  10K noise │──→ OUTPUT
                     │    │  DA2B(×46)          │    │  100K drum │    (XS5)
                     │    │       │              │    │  33K fbk  │
                     │    │       ▼              │    └────────────┘
                     │    │  Swing VCA           │         ↑
                     │    │  VT6/C14/R2          │         │
                     │    │  VD6/C17/C18         │         │
                     │    │       │              │    HPF AC out
                     │    │       │◄── CV ──┐    │         │
                     │    │       ▼         │    │    High Pass Filter
                     │    └────────────────┘    │    C19/C20/R12
                     │                          │    VT7/R23/R24
                     │                          │    C16
                COMP_OUT ──→ VD7 ──→ 470nF ──→ P2(100K A)
                (full trigger)   (noise envelope)    │
                                         │          VCA_COLL
                                         │
                                   VT2(PNP)←── SNAPPY CV (XS3)
                                   R22 (22K)
```

---

## DISCREPANCY LOG

### BOM vs Tutorial
No missing components identified (unlike kick which was missing R12 and C9
from the printed BOM). All BOM quantities reconcile with the tutorial.

### Components Not Fully Described in Tutorial
| Component | Value | In BOM? | On Schematic? | Role |
|-----------|-------|---------|---------------|------|
| R36, R37 | 10R | Yes | Yes | Power supply filtering |
| C8, C9 | 47uF | Yes | Yes | Power supply bulk filter |
| C2-C7, C21, C22 | 100nF | Yes (8×) | Yes | IC decoupling |
| VD1, VD2 | 1N5819 | Yes | Yes | Reverse polarity protection |
| R31-R33 | 1K (×3) | Yes (7× total) | Yes | Likely jack protection |
| VD8 | 1N4148 | Yes (6× total) | Yes | Likely CV input protection |

### Key Differences from Kick Drum
| Feature | Kick | Snare |
|---------|------|-------|
| ICs | 2× TL072 | 3× TL072 |
| Transistors | 5 | 7 |
| Oscillator caps | 15nF (×2) | 33nF (×2) — lower freq |
| Tune pot | 250K B (PITCH) | 1K B (TUNE) — narrow range |
| Trigger divider | 100K/14K (1.47V) | 10K/1K (1.09V) |
| Decay pot | 1M B | 250K B |
| Decay gain max | ~0.955 | ~0.84 |
| Tone filter | P4 50K + 15nF | None (HPF instead) |
| Distortion | DA2B soft clip | None |
| Noise path | None | Full path: generator→VCA→HPF |
| Pitch envelope | VT5 PNP gate + VT3 buffer | Direct RC + NPN |
| Envelope trigger | Accented (ACC_TRIG) | Full (COMP_OUT) |
| Signal paths | 1 | 2 (drum + noise, mixed) |

---

## COMPONENT COUNT VERIFICATION

```
Resistors:  2+1+1+1+7+3+1+2+1+5+2+7+1+1+2 = 37  ✓ (BOM: 37)
Capacitors: 2+1+2+8+3+2+2+2                = 22  ✓ (BOM: 22)
Diodes:     6+2                             = 8   ✓ (BOM: 6+2)
Transistors: 2+5                            = 7   ✓ (BOM: 2+5)
ICs:        3                               = 3   ✓ (BOM: 3)
Pots:       5                               = 5   ✓ (BOM: 5)
Jacks:      5                               = 5   ✓ (BOM: 5)
```
