# MKI x ES EDU KICK DRUM — Definitive Netlist

**Purpose:** Authoritative netlist for the solderable breadboard design.

**Sources (in order of authority):**
1. Production schematic (PDF p50) — circuit topology and designators
2. Tutorial text (PDF p9-32) — component values and functional descriptions
3. BOM (PDF p3-5) — component counts and values
4. Build guide text (PDF p49) — power supply designators

**Known BOM printing errors:** The BOM omits 1x 5.6nF capacitor and 1x 22K
resistor that are both described in the tutorial text AND present on the
production schematic.

**Rev 1.1 correction (2026-04-20):** Re-read production schematic p50
revealed topology errors in pitch envelope (Blocks 6/8/9). Previously-inferred
components R10 (10K) did not exist; R20 is 1K (not 100K); a new R11 (10K)
in series with TUNE DECAY was missed; a new VD5 clamp on VT3 base was missed;
R24 (100K) sources from ENV_BUF (not SMOOTH). The "smoothing network" is
actually a push-pull compensation from VT4.emitter, NOT from the PNP base
side. See Block 6 for full revised topology.

---

## CORRECTED BOM

### Resistors (24 total)
| Qty | Value | Designators | Role |
|-----|-------|-------------|------|
| 2 | 1M | R5, R23 | Oscillator bridge, smoothing discharge |
| 1 | 470K | R28 | Decay feedback to CAP_MID |
| 1 | 120K | R3 | Accent parallel limiter |
| 4 | 100K | R13, R16, R18, R24 | Accent input, comp threshold, trigger divider, pitch mod base drive |
| 2 | 47K | R25, R26 | Decay buffer input + feedback |
| 1 | 39K | R8 | Gate HP filter discharge |
| 2 | 33K | R1, R4 | Distortion GND ref, comparator threshold |
| 1 | **22K** | **R12** | **Accent series — MISSING FROM PRINTED BOM** |
| 1 | 14K | R9 | Trigger voltage divider bottom |
| 2 | 10K | R11, R27 | TUNE DECAY series, pitch baseline |
| 1 | 2K | R22 | Pitch depth ceiling |
| 4 | 1K | R21, R2, R20, R7 | Output series, accent CV protection, pitch CV protection, 1 unconfirmed |
| 2 | 10R | R14, R15 | Power supply series filtering |

### Capacitors (15 total)
| Qty | Value | Type | Designators | Role |
|-----|-------|------|-------------|------|
| 2 | 47uF | Electrolytic | C2, C3 | Power supply bulk filter |
| 1 | 220nF | Film | C6 | Pitch envelope timing |
| 6 | 100nF | Ceramic | C1, C4, C5, C13, C14, C15 | IC decoupling + power bypass |
| 3 | 15nF | Film | C10, C11, C12 | Oscillator x2, tone filter x1 |
| 1 | 10nF | Film | C8 | Gate HP filter input |
| 1 | **5.6nF** | **Film** | **C9** | **Envelope smoothing — MISSING FROM PRINTED BOM** |
| 1 | 3.3nF | Film | C7 | Distortion HF smoothing |

### Semiconductors
| Qty | Type | Designators | Role |
|-----|------|-------------|------|
| 7 | 1N4148 | VD3, VD4, VD5, VD6, VD7, VD8, VD9 | Signal diodes (see block details) |
| 2 | 1N5819 | VD1, VD2 | Power reverse polarity protection |
| 2 | BC558 (PNP) | VT1, VT3 | Accent limiter, pitch envelope discharge |
| 3 | BC548 (NPN) | VT2, VT4, VT5 | Accent buffer, envelope buffer, pitch modulator |
| 2 | TL072 | DA1, DA2 | 4 op-amp halves total |

### Potentiometers (7 total)
| Qty | Value | Taper | Designator | Label | Wiring |
|-----|-------|-------|------------|-------|--------|
| 1 | 1M | B (lin) | P1 | DECAY | Variable R: parallel with R26 |
| 1 | 250K | B (lin) | P2 | PITCH | Variable R: wiper+end to R27 |
| 2 | 100K | B (lin) | P5, P6 | TUNE DECAY, PITCH CV | See blocks 6/8 |
| 1 | 100K | A (log) | P3 | DISTORTION | Variable R in feedback |
| 1 | 50K | B (lin) | P4 | TONE | Variable R: series with C12 |
| 1 | 10K | B (lin) | P7 | TUNE DEPTH | Variable R: Q1 emitter path |

### Connectors (5 total)
| Qty | Type | Designator | Label |
|-----|------|------------|-------|
| 1 | Switched mono jack | XS1 | TRIGGER IN |
| 1 | Switched mono jack | XS2 | ACCENT CV |
| 1 | Switched mono jack | XS3 | PITCH CV |
| 1 | Switched mono jack | XS4 | OUTPUT |
| 1 | 2x5 pin header | XP1 | Eurorack power |

---

## NET NAMES

| Net | Description |
|-----|-------------|
| VCC | +12V filtered supply rail |
| VEE | -12V filtered supply rail |
| GND | Ground (0V) |
| GATE_IN | Gate input jack tip |
| HP_OUT | High-pass filter output (comparator + input) |
| COMP_INV | Comparator inverting input (threshold) |
| COMP_OUT | Comparator output |
| ACC_IN | Accent node (after 100K from comparator) |
| ACC_EMIT | PNP emitter / NPN base node |
| ACC_TRIG | Accented trigger output (NPN emitter) |
| ~~TRIG_POS~~ | ~~Removed — no blocking diode in trigger path (see Block 3 note)~~ |
| OSC_NONINV | Oscillator op-amp non-inverting input |
| OSC_INV | Oscillator op-amp inverting input |
| OSC_OUT | Oscillator op-amp output |
| CAP_MID | Node between oscillator's two 15nF caps |
| PITCH_BOT | Bottom of pitch pot (to baseline resistor) |
| DECAY_INV | Decay buffer inverting input |
| DECAY_OUT | Decay buffer output |
| ENV_CAP | Envelope capacitor top node (VD7 cathode, C6, VT4.B, R11 pin1) |
| INT_DECAY | Intermediate node after R11, before TUNE DECAY pot CCW |
| VT3_E | PNP emitter (after P5 pot wiper+CW) — was part of ENV_CAP in rev 1.0 |
| VT3_B | PNP base (P6 wiper + VD5 cathode) — replaces old PCV_ATT |
| ENV_BUF | Envelope buffer output (VT4 emitter) — drives R24 and C9 |
| SMOOTH | Push-pull compensation node (C9, VD8, R23) — does NOT drive any transistor |
| PCV_BASE | Pitch modulator (VT5) base |
| VT5_E | Pitch modulator (VT5) emitter |
| DEPTH_MID | Between R22 (2K) and P7 TUNE DEPTH pot |
| PITCH_CV_IN_1K | Between R20 (1K) and P6 PITCH CV pot CW end |
| PITCH_CV_IN | Pitch CV jack tip (XS3) |
| ACCENT_CV_IN | Accent CV jack tip |
| TONE_OUT | Tone filter output (distortion + input) |
| DIST_INV | Distortion inverting input |
| DIST_OUT | Distortion output |
| OUTPUT_J | Output jack tip |

---

## NETLIST BY FUNCTIONAL BLOCK

### BLOCK 1: Gate-to-Trigger Converter
**Source: p13-14 (tutorial), p50 (schematic)**

Converts a sustained gate input into a short trigger pulse using a high-pass
filter and op-amp comparator. The HP filter differentiates the gate edge;
the comparator converts it to a clean +12V trigger pulse.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C8 | 10nF | GATE_IN | HP_OUT | — |
| R8 | 39K | HP_OUT | GND | — |
| VD6 (1N4148) | — | GND (anode) | HP_OUT (cathode) | — |
| R16 | 100K | VCC | COMP_INV | — |
| R4 | 33K | COMP_INV | GND | — |
| DA1A (TL072) | — | HP_OUT (+in) | COMP_INV (-in) | COMP_OUT (out) |

**Notes:**
- VD6 clamps negative spikes at the comparator non-inv input (prevents
  op-amp latch-up on falling gate edge)
- Threshold voltage: 12V × 33K/(33K+100K) ≈ 3.0V
- Comparator outputs +12V when HP_OUT > 3V, -12V otherwise
- The -12V low state is blocked by VD5 (see Block 3)

---

### BLOCK 2: Accent CV Section
**Source: p28-29 (tutorial), p50 (schematic)**

Modulates the trigger amplitude using an external accent CV. A PNP emitter
follower sets a voltage floor proportional to the CV. The 22K/120K network
ensures a usable trigger even at 0V CV, while limiting the maximum amplitude.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R13 | 100K | COMP_OUT | ACC_IN | — |
| R3 | 120K | ACC_IN | GND | — |
| R12 | 22K | ACC_IN | ACC_EMIT | — |
| R2 | 1K | ACCENT_CV_IN (XS2 tip) | ACC_PROT | Series protection for PNP base |
| VD9 (1N4148) | — | GND (anode) | ACC_PROT (cathode) | Clamps negative CV input |
| VT1 (BC558 PNP) | — | ACC_PROT (B) | GND (C) | ACC_EMIT (E) |
| VT2 (BC548 NPN) | — | ACC_EMIT (B) | VCC (C) | ACC_TRIG (E) |

**Notes:**
- R12 (22K) is **missing from the printed BOM** but described on p29 and
  visible on the production schematic
- R2 (1K) is series base protection for VT1; VD9 clamps negative CV to -0.6V.
  Both confirmed from production schematic p2.
- At 0V accent CV: ~2V trigger spike at output (p29)
- At max CV: kick volume does not exceed the no-CV maximum (p29)
- 120K acts as maximum resistance for the divider, capping trigger amplitude
- VT2 (NPN buffer) provides low-impedance output for driving both
  the oscillator trigger and envelope charge paths
- XS2 jack tip → R2 (1K) → ACC_PROT node → VD9 clamp to GND → VT1 base

---

### BLOCK 3: Trigger Routing
**Source: p13 (tutorial), p2 (production schematic)**

Routes the accented trigger to the oscillator input through a voltage divider.
The 100K/14K divider scales 12V down to ~1.4V.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| R18 | 100K | ACC_TRIG | OSC_NONINV |
| R9 | 14K | OSC_NONINV | GND |

**Verification:** 12V × 14K/(100K+14K) = 1.47V ≈ "around 1.4V" (p13) ✓

**Note:** Earlier netlist versions included VD5 (1N4148) as a blocking diode
here. Production schematic confirms this diode is actually in the pitch
envelope charge path (Block 6, designated VD7/VD4 — see that block).

---

### BLOCK 4: Bridged-T Oscillator
**Source: p9-12, p15 (tutorial), p50 (schematic)**

The core sound source. A bridged-T network with an op-amp produces a
decaying sine wave when triggered. Frequency is set by the resistance to
ground at CAP_MID (the node between the two 15nF caps).

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| C10 | 15nF | OSC_OUT | CAP_MID | — |
| C11 | 15nF | CAP_MID | OSC_INV | — |
| R5 | 1M | OSC_OUT | OSC_INV | — |
| P2 (250K B) | PITCH | CAP_MID (pin 1) | PITCH_BOT (wiper+pin 3) | — |
| R27 | 10K | PITCH_BOT | GND | — |
| DA1B (TL072) | — | OSC_NONINV (+in) | OSC_INV (-in) | OSC_OUT (out) |

**Notes:**
- P2 wired as variable resistor (wiper shorted to pin 3)
- R27 (10K) sets minimum resistance → max frequency ~110Hz (p15)
- At P2 max (260K total): frequency drops to deep sub-bass
- R5 (1M bridge) determines oscillation decay rate and amplitude
- CAP_MID also receives connections from: VT5 collector (pitch CV),
  R28 (decay feedback), R23 (smoothing)

---

### BLOCK 5: Decay Feedback
**Source: p16-18 (tutorial), p50 (schematic)**

An inverting buffer feeds the oscillator output back to CAP_MID through a
large resistor, replenishing energy lost each cycle. The 1M DECAY pot controls
how much energy is fed back, extending the oscillation duration.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R25 | 47K | OSC_OUT | DECAY_INV | — |
| R26 | 47K | DECAY_INV | DECAY_OUT | — |
| P1 (1M B) | DECAY | DECAY_INV (pin 1) | DECAY_OUT (pin 2) | — |
| R28 | 470K | DECAY_OUT | CAP_MID | — |
| DA2A (TL072) | — | GND (+in) | DECAY_INV (-in) | DECAY_OUT (out) |

**Notes:**
- P1 is in parallel with R26 (47K feedback resistor)
- P1 wired with pin 1 and pin 2 only (wiper shorted to pin 2)
- At P1 min (0Ω): gain = 0 → no feedback → natural short decay
- At P1 max (1M): gain = -(47K‖1M)/47K ≈ -0.955 → near-sustained
- "kick doesn't go into full drone mode at the maximum setting" (p18) ✓
- R28 (470K) limits the feedback injection to avoid overdriving the oscillator

---

### BLOCK 6: Pitch Envelope Generator (REVISED rev 1.1)
**Source: p21-22 (tutorial), p50 (production schematic — authoritative)**

The trigger charges C6 (220nF) through VD7. The envelope discharges through
**R11 (10K) + P5 (100K TUNE DECAY)** in series, then through the PNP (VT3)
to GND. VT4 (NPN emitter follower) buffers the envelope voltage. The PNP's
base is driven by the PITCH CV attenuator (P6) with a VD5 clamp, setting
the discharge floor.

**Schematic designator mapping (rev 1.1: transistors now match schematic; other IDs still use netlist-local names):**

Transistors: **aligned with schematic** — VT1 PNP (accent), VT2 NPN (accent buffer), VT3 PNP (envelope discharge), VT4 NPN (envelope buffer), VT5 NPN (pitch modulator).

Pots (netlist uses P-numbers; schematic uses R-numbers):

| Netlist | Schematic | Function |
|---|---|---|
| P1 | R1 | DECAY (1M) |
| P2 | R2 | PITCH (250K) |
| P3 | R6 | DISTORTION |
| P4 | R7 | TONE (50K) |
| P5 | R5 | TUNE DECAY (100K) |
| P6 | R3 | PITCH CV (100K) |
| P7 | R4 | TUNE DEPTH |

Block-6/7 resistor/diode mapping where numbers differ:

| Netlist | Schematic | Component |
|---|---|---|
| VD7 | VD4 | Envelope charge diode |
| VD5 (new) | VD5 | PNP base negative clamp |
| C6 | C8 | Envelope cap (220nF) |
| R11 (new, 10K) | R18 | TUNE DECAY series |
| R24 (100K) | R19 | Pitch mod base drive |
| R22 (2K) | R24 | Pitch depth series |
| R20 (1K) | R20 | PITCH CV input series (match) |

**Components:**

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VD7 (1N4148) | — | ACC_TRIG (anode) | ENV_CAP (cathode) | — |
| C6 | 220nF | ENV_CAP | GND | — |
| VT4 (BC548 NPN) | — | ENV_CAP (B) | VCC (C) | ENV_BUF (E) |
| **R11 (NEW, 10K)** | 10K | ENV_CAP | INT_DECAY | — |
| P5 (100K B) | TUNE DECAY | INT_DECAY (CCW) | VT3_E (wiper+CW shorted) | floating (none) |
| VT3 (BC558 PNP) | — | VT3_B (B) | GND (C) | VT3_E (E) |
| **VD5 (NEW, 1N4148)** | — | GND (anode) | VT3_B (cathode) | — |
| P6 (100K B) | PITCH CV | PITCH_CV_IN_1K (CW) | VT3_B (wiper) | GND (CCW) |
| R20 | **1K** (was 100K) | XS3 tip | PITCH_CV_IN_1K | — |

**Notes:**
- **Charge path:** ACC_TRIG → VD7 → ENV_CAP → C6 → GND (fast; ~55us to peak)
- **Discharge path:** ENV_CAP → R11 (10K) → P5 (0-100K variable) → VT3.E → VT3.C → GND
  - Total discharge R = 10K to 110K through PNP
  - At P5 fully CW: 10K (fastest decay)
  - At P5 fully CCW: 110K (slowest decay)
- **VT3 (PNP) conducts** when VT3.E > VT3.B + 0.6V. When ENV_CAP discharges
  to near-CV-level, VT3 cuts off → discharge stops → creates CV-controlled floor.
- **VT4 (NPN emitter follower)** buffers ENV_CAP → ENV_BUF without loading C6.
  Base on ENV_CAP directly (no input R). Collector to VCC. Emitter = ENV_BUF output.
- **ENV_BUF drives three things** (see Blocks 7 and merged Block 9):
  1. R24 (100K) → PCV_BASE → VT5 base (pitch modulation)
  2. C9 (5.6nF) → SMOOTH → push-pull compensation (see Block 9 below)
  3. (nothing else — SMOOTH does NOT feed VT5 base as old netlist claimed)
- **P5 (TUNE DECAY)** wired as 2-terminal variable resistor: CCW + {wiper+CW
  shorted} used, third terminal floating. 2 wires to control deck.
- **P6 (PITCH CV)** wired as 3-terminal voltage divider: CW from R20/jack,
  CCW to GND, wiper to VT3.B (through VD5 clamp).
- **VD5 orientation:** stripe (cathode) toward VT3.B, anode to GND. Clamps
  negative pitch CV to -0.6V (protects PNP B-E junction from reverse breakdown
  with negative CV inputs).
- **R20 was wrongly inferred as 100K in rev 1.0.** Schematic shows 1K — this
  is ESD/short protection only, not attenuation. The P6 divider does the
  attenuation.

---

### BLOCK 7: Pitch Modulator Transistor (REVISED rev 1.1)
**Source: p19-20, p22 (tutorial), p50 (schematic)**

VT5 (NPN) provides voltage-controlled resistance from
CAP_MID to ground, modulating the oscillator frequency. Its base is driven
**directly from ENV_BUF through R24 (100K)** — NOT from the smoothed node
as the previous netlist claimed. The smoothing network (Block 9) is a
separate push-pull compensation into CAP_MID, not in the base drive path.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VT5 (BC548 NPN) | — | PCV_BASE (B) | CAP_MID (C) | VT5_E (E) |
| R24 | 100K | **ENV_BUF** (was SMOOTH) | PCV_BASE | — |
| R22 | 2K | VT5_E | DEPTH_MID | — |
| P7 (10K B) | TUNE DEPTH | DEPTH_MID (CCW + wiper shorted) | GND (CW) | floating (none) |

**Notes:**
- **R24 (100K) source is ENV_BUF directly**, not SMOOTH — corrected from rev 1.0.
  This means the pitch modulation follows the raw envelope tail, not a smoothed
  version.
- R22 (2K) is in SERIES between VT5.E and P7 (not a direct path to GND).
  Effective resistance to GND from VT5.E = R22 (2K) + P7_position (0-10K).
- P7 (TUNE DEPTH) wired as 2-terminal variable R: CCW + wiper shorted, CW to GND.
  - Fully CW (wiper at CW): P7 contributes 0Ω → total emitter R = 2K → max depth
  - Fully CCW (wiper at CCW): P7 contributes 10K → total emitter R = 12K → min depth
- R22 (2K) sets minimum degeneration → maximum depth ceiling (~250Hz swing, p22)
- VT5 collector connects to CAP_MID in parallel with the pitch pot chain

---

### BLOCK 8: Pitch CV Input — MERGED INTO BLOCK 6 (rev 1.1)

This block is now part of Block 6. Summary of changes from rev 1.0:
- **R10 (10K) DELETED** — this resistor does not exist on the production schematic.
- **R20 is 1K** (not 100K as rev 1.0 inferred) — series ESD/short protection only.
- **P6 (PITCH CV) is a 3-terminal voltage divider** going directly to VT3.B
  (no intermediate R10). CW from jack, CCW to GND, wiper to VT3.B.
- **VD5 (1N4148) added** — clamp on VT3.B to GND, stripe toward VT3.B.

See Block 6 for full revised topology.

---

### BLOCK 9: Push-Pull Compensation to CAP_MID (REVISED rev 1.1)
**Source: p24-25 (tutorial), p50 (schematic)**

An AC-coupled compensation path from VT4.emitter (ENV_BUF) into CAP_MID.
This block does NOT smooth the base drive of VT5 (that's a direct path via
R24). Instead it injects a small compensating signal into CAP_MID itself.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| C9 | 5.6nF | ENV_BUF | SMOOTH |
| VD8 (1N4148) | — | SMOOTH (anode) | GND (cathode) |
| R23 | 1M | SMOOTH | CAP_MID |

**Notes:**
- C9 is **missing from the printed BOM** but explicitly described on p24
  ("additional capacitor") and visible on the production schematic as C9 5.6nF.
- **VD8 orientation:** anode = SMOOTH, cathode = GND (stripe toward GND).
  Clamps POSITIVE excursions of SMOOTH to ~+0.6V.
- **Function (per tutorial p25):** C9 AC-couples the ENV_BUF signal to SMOOTH.
  When ENV_BUF drops (envelope decaying), C9 forces SMOOTH to drop too. VD8
  prevents SMOOTH from rising above +0.6V when ENV_BUF jumps up on next
  trigger. The negative-going excursion at SMOOTH is then bled into CAP_MID
  via R23 (1M), giving CAP_MID a slight "push" that compensates for the
  bridge-T's natural droop — extending the audible oscillation tail.
- **SMOOTH does NOT drive any transistor base** (VT5 base comes from ENV_BUF
  via R24, not from SMOOTH — corrected from rev 1.0).

---

### BLOCK 10: Tone Filter
**Source: p30 (tutorial), p50 (schematic)**

Passive RC low-pass filter on the oscillator output. The 50K pot sets
the cutoff frequency. At max resistance: fc ≈ 212Hz, killing the initial
click transient.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| P4 (50K B) | TONE | OSC_OUT (pin 1) | TONE_OUT (wiper+pin 3) |
| C12 | 15nF | TONE_OUT | GND |

**Notes:**
- P4 wired as variable resistor (wiper shorted to pin 3)
- fc = 1/(2π × 50K × 15nF) = 212Hz ≈ "220Hz" (p30) ✓
- At P4 min (0Ω): cap shorted to output → no filtering → full click
- The tone filter feeds directly into the distortion stage (DA2B non-inv)

---

### BLOCK 11: Distortion / Output Buffer
**Source: p31-32 (tutorial), p50 (schematic)**

Non-inverting amplifier with diode-limited gain. At pot minimum: unity
buffer. As the pot increases, gain rises for small signals (<0.6V) while
the diodes clamp large signals, creating soft-clipping distortion.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R1 | 33K | DIST_INV | GND | — |
| P3 (100K A) | DISTORTION | DIST_OUT (pin 1) | DIST_INV (pin 2) | — |
| VD3 (1N4148) | — | DIST_INV (anode) | DIST_OUT (cathode) | — |
| VD4 (1N4148) | — | DIST_OUT (anode) | DIST_INV (cathode) | — |
| C7 | 3.3nF | DIST_OUT | DIST_INV | — |
| DA2B (TL072) | — | TONE_OUT (+in) | DIST_INV (-in) | DIST_OUT (out) |
| R21 | 1K | DIST_OUT | OUTPUT_J | — |

**Notes:**
- P3 wired as variable resistor (wiper shorted to pin 2)
- At P3=0: direct feedback → gain = 1 (clean buffer)
- At P3=max: gain ≈ (100K+33K)/33K ≈ 4x for signals below ±0.6V
- Above ±0.6V: diodes conduct → gain drops to ~1 → soft clipping
- C7 (3.3nF) provides HF bypass in the feedback, softening distortion edges
- R21 (1K) provides output series protection
- VD3/VD4 are anti-parallel (opposite polarities)

---

### BLOCK 12: Power Supply
**Source: p49 (build guide), p50 (schematic)**

Standard Eurorack ±12V power supply with reverse polarity protection,
series resistor filtering, and decoupling.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| VD1 (1N5819) | — | +12V_raw (anode) | VCC (cathode) |
| VD2 (1N5819) | — | VEE (anode) | -12V_raw (cathode) |
| R14 | 10R | VCC (after VD1) | VCC_filt |
| R15 | 10R | VEE (after VD2) | VEE_filt |
| C2 | 47uF | VCC | GND |
| C3 | 47uF | GND | VEE (observe polarity!) |
| C1, C4 | 100nF | VCC/VEE | GND |

**IC Decoupling (placed close to IC power pins):**
| Component | Value | Location |
|-----------|-------|----------|
| C13 | 100nF | DA1 pin 8 (V+) to GND |
| C14 | 100nF | DA1 pin 4 (V-) to GND |
| C15 | 100nF | DA2 pin 8 (V+) to GND |
| C5 | 100nF | DA2 pin 4 (V-) to GND |

**Notes:**
- R14/R15 (10R) also act as slow-blow fuses (p49)
- VD1/VD2: 1N5819 Schottky chosen for low forward voltage drop
- 6x 100nF total (C1, C4, C5, C13, C14, C15) matches BOM qty
- For the breadboard design: place decoupling caps as close to IC
  power pins as physically possible

---

### BLOCK 13: Unconfirmed Components

These components are in the BOM and on the production schematic but their
exact placement is not described in the tutorial text.

| Component | Value | Likely Role |
|-----------|-------|-------------|
| ~~R2~~ | ~~1K~~ | **RESOLVED:** Accent CV input series protection (Block 2) |
| R7 | 1K | Input protection or pull-down (likely on XS1 or XS3) |
| ~~VD9 (1N4148)~~ | — | **RESOLVED:** Accent CV input negative clamp (Block 2) |

**Notes:**
- The BOM lists 1K x3. R21 (output series) and R2 (accent CV protection) are placed.
  R7 remains unconfirmed.
- The BOM lists 1N4148 x7. All 7 now placed: VD3-VD8 + VD9 (accent CV clamp).

---

## COMPLETE NET LIST (Net-Centric View)

```
NET GND (0V):
  R8.2, R4.2, R9.2, R27.2, R1.2, R3.2,
  C6.2, C12.2,
  P6.CCW(PITCH CV), P4.wiper+pin3(TONE), P7.CW(TUNE DEPTH),
  VD5.anode, VD6.anode, VD8.cathode, VD9.anode,
  VT1.C(collector), VT3.C(collector), DA2A.+(noninv),
  Power: C2.-, C3.+, C1/C4/C5/C13/C14/C15 (decoupling)

NET VCC (+12V):
  R16.1,
  VT2.C(collector), VT4.C(collector),
  DA1.pin8(V+), DA2.pin8(V+),
  Power: VD1.cathode, R14, C2.+, decoupling caps

NET VEE (-12V):
  DA1.pin4(V-), DA2.pin4(V-),
  Power: VD2.anode, R15, C3.-, decoupling caps

NET GATE_IN:
  XS1.tip, C8.1

NET HP_OUT:
  C8.2, R8.1, VD6.cathode, DA1A.+(noninv)

NET COMP_INV:
  R16.2, R4.1, DA1A.-(inv)

NET COMP_OUT:
  DA1A.out, R13.1

NET ACC_IN:
  R13.2, R3.1, R12.1

NET ACC_EMIT:
  R12.2, VT1.E(emitter), VT2.B(base)

NET ACC_TRIG:
  VT2.E(emitter), R18.1, VD7.anode

NET OSC_NONINV:
  R18.2, R9.1, DA1B.+(noninv)

NET OSC_INV:
  C11.2, R5.1, DA1B.-(inv)

NET OSC_OUT:
  C10.1, R5.2, R25.1, P4.pin1(TONE), DA1B.out

NET CAP_MID:
  C10.2, C11.1, P2.pin1(PITCH),
  VT5.C(collector), R28.2, R23.2

NET PITCH_BOT:
  P2.wiper+pin3, R27.1

NET DECAY_INV:
  R25.2, R26.1, P1.pin1, DA2A.-(inv)

NET DECAY_OUT:
  R26.2, P1.pin2+wiper, R28.1, DA2A.out

NET ENV_CAP:
  VD7.cathode, C6.1, VT4.B(base), R11.1

NET INT_DECAY:
  R11.2, P5.CCW

NET VT3_E:
  P5.wiper+CW shorted, VT3.E(emitter)

NET VT3_B:
  P6.wiper, VD5.cathode, VT3.B(base)

NET ENV_BUF:
  VT4.E(emitter), C9.1, R24.1

NET SMOOTH:
  C9.2, VD8.anode, R23.1

NET PCV_BASE:
  R24.2, VT5.B(base)

NET VT5_E:
  VT5.E(emitter), R22.1

NET DEPTH_MID:
  R22.2, P7.CCW+wiper shorted

NET PITCH_CV_IN:
  XS3.tip, R20.1

NET PITCH_CV_IN_1K:
  R20.2, P6.CW

NET ACCENT_CV_IN:
  XS2.tip, R2.1

NET ACC_PROT:
  R2.2, VD9.cathode, VT1.B(base)

NET TONE_OUT:
  P4.wiper+pin3, C12.1, DA2B.+(noninv)

NET DIST_INV:
  R1.1, P3.pin2+wiper, VD3.anode, VD4.cathode,
  C7.2, DA2B.-(inv)

NET DIST_OUT:
  P3.pin1, VD3.cathode, VD4.anode, C7.1,
  DA2B.out, R21.1

NET OUTPUT_J:
  R21.2, XS4.tip
```

---

## OP-AMP PIN ASSIGNMENTS

Both ICs are TL072 dual op-amps in DIP-8 package.

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

### DA2 (TL072 #2): Decay Buffer + Distortion
| Pin | Function | Net |
|-----|----------|-----|
| 1 | OUT A | DECAY_OUT |
| 2 | IN- A | DECAY_INV |
| 3 | IN+ A | GND |
| 4 | V- | VEE |
| 5 | IN+ B | TONE_OUT |
| 6 | IN- B | DIST_INV |
| 7 | OUT B | DIST_OUT |
| 8 | V+ | VCC |

**Note:** The op-amp half assignments (A vs B) are inferred from the
production schematic layout. The circuit functions identically regardless
of which half is used for which function within the same IC, but the pin
numbers above match the production PCB routing.

---

## DISCREPANCY LOG

### BOM Omissions (components missing from printed BOM)
| Component | Value | Evidence |
|-----------|-------|---------|
| R12 | 22K | Described p29, shown on schematic p50, visible on breadboard diagrams p29 |
| C9 | 5.6nF | Described p24, shown on schematic p50, labeled on PCB silkscreen p51 |

### Components Not Described in Tutorial
| Component | Value | In BOM? | On Schematic? | Role |
|-----------|-------|---------|---------------|------|
| R14, R15 | 10R | Yes | Yes | Power supply filtering |
| R2, R7 | 1K | Yes (3x total) | Yes | Unknown — likely input protection |
| C2, C3 | 47uF | Yes | Yes | Power supply bulk filter |
| C1, C4, C5, C13-C15 | 100nF | Yes (6x) | Yes | IC decoupling |
| VD1, VD2 | 1N5819 | Yes | Yes | Reverse polarity protection |
| VD9 | 1N4148 | Yes (7x total) | Yes | Unknown — likely CV input protection |

### Tutorial vs Production Schematic Differences
| Item | Tutorial | Production | Resolution |
|------|----------|------------|------------|
| R to GND in oscillator | 51K (original 808) | 250K pot + 10K | Tutorial explains the change on p15 |
| Accent section topology | Builds up progressively p28-29 | Final version with all components | Use final version (p29 + schematic) |
| Pitch CV input details | "variable voltage divider" only | R20 (1K) + P6 divider + VD5 clamp on VT3.B | Rev 1.1: R10 does NOT exist; R20 is 1K not 100K |
| Pitch envelope discharge | Single PNP with pot on collector | R11 (10K) + P5 pot series on PNP emitter side | Rev 1.1: discharge path is on emitter, not collector |
| Pitch mod base drive | Via SMOOTH node | Direct from ENV_BUF via R24 | Rev 1.1: smoothing is push-pull to CAP_MID only |

---

## SIGNAL FLOW SUMMARY

```
                                     ┌──────────────────┐
  GATE IN ──→ HP Filter ──→ Comparator ──→ 100K ──→ Accent ──→ NPN Buffer
  (XS1)       C8/R8/VD6     DA1A/R16/R4     R13     R12/R3     VT1/VT2
                                                      VT1←── ACCENT CV (XS2)
                                                        │
                                    ┌───────────────────┤
                                    │                   │
                                    ▼                   ▼
                              Trigger Path         Envelope Path
                              R18/R9               VD7 → ENV_CAP/C6/VT4
                                    │                   │
                                    ▼          ┌────────┼────────┐
                              OSC_NONINV       ▼        ▼        ▼
                                    │     R11→P5→    VT4(NPN)  R24(100K)
                                    │     VT3(PNP)   buffer     │
                                    ▼     (discharge)  │        ▼
                              Oscillator   │         ENV_BUF  PCV_BASE
                              DA1B/C10/C11 ▼           │        │
                              R5/P2/R27   GND      ┌───┤        ▼
                                    │   (PITCH CV  │   │    VT5(NPN)
                                    │    floor via │   │    pitch mod
                                    │    VT3.B)    ▼   ▼    │
                              OSC_OUT             C9  R24   VT5.C
                                │                  │         │
                                │                  ▼         ▼
                                │              SMOOTH       CAP_MID
                                │              (VD8 clamp)   ↑
                                │              R23──────────→┘ push-pull
                                │
                                │              PITCH CV (XS3)
                                │              → R20(1K) → P6 divider → VD5 clamp → VT3.B
                                │
                          ┌─────┼─────┐
                          │     │     │
                          ▼     ▼     ▼
                       Decay  Tone  (feedback)
                       R25    P4     to CAP_MID
                       DA2A   C12    via R28
                       R26          (via 470K)
                       P1
                       R28→CAP_MID
                          │
                          ▼
                     TONE_OUT
                          │
                          ▼
                    Distortion/Buffer
                    DA2B/R1/P3
                    VD3/VD4/C7
                          │
                          ▼
                    R21 → OUTPUT (XS4)
```
