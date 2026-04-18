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

---

## CORRECTED BOM

### Resistors (24 total)
| Qty | Value | Designators | Role |
|-----|-------|-------------|------|
| 2 | 1M | R5, R23 | Oscillator bridge, smoothing discharge |
| 1 | 470K | R28 | Decay feedback to CAP_MID |
| 1 | 120K | R3 | Accent parallel limiter |
| 5 | 100K | R13, R16, R18, R20, R24 | Various (see block details) |
| 2 | 47K | R25, R26 | Decay buffer input + feedback |
| 1 | 39K | R8 | Gate HP filter discharge |
| 2 | 33K | R1, R4 | Distortion GND ref, comparator threshold |
| 1 | **22K** | **R12** | **Accent series — MISSING FROM PRINTED BOM** |
| 1 | 14K | R9 | Trigger voltage divider bottom |
| 2 | 10K | R10, R27 | Pitch CV to PNP base, pitch baseline |
| 1 | 2K | R22 | Pitch depth ceiling |
| 3 | 1K | R21, R2, R7 | Output series, accent CV protection, 1 unconfirmed |
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
| 7 | 1N4148 | VD3-VD9 | Signal diodes (see block details) |
| 2 | 1N5819 | VD1, VD2 | Power reverse polarity protection |
| 2 | BC558 (PNP) | VT1, VT5 | Accent limiter, pitch CV floor |
| 3 | BC548 (NPN) | VT2, VT3, VT4 | Accent buffer, envelope buffer, pitch CV |
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
| ENV_CAP | Envelope capacitor top node |
| VT5_C | PNP collector (to TUNE DECAY pot) |
| ENV_BUF | Envelope buffer output (VT3 emitter) |
| SMOOTH | Smoothed envelope node |
| PCV_BASE | Pitch CV NPN transistor base |
| VT4_E | Pitch CV NPN emitter |
| DEPTH_MID | Between tune depth pot and 2K |
| PCV_ATT | Pitch CV attenuated (pot wiper to PNP base) |
| PITCH_CV_IN | Pitch CV jack tip |
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
- CAP_MID also receives connections from: VT4 collector (pitch CV),
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

### BLOCK 6: Pitch Envelope Generator
**Source: p21-22 (tutorial), p50 (schematic)**

A simple RC envelope: the trigger charges C6 (220nF) through a diode, and
it discharges through the PNP transistor (VT5) and TUNE DECAY pot. VT3
(NPN buffer) taps the envelope voltage without loading the cap. VT5 acts
as a voltage-controlled gate in the discharge path (see Block 8).

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VD7 (1N4148) | — | ACC_TRIG (anode) | ENV_CAP (cathode) | — |
| C6 | 220nF | ENV_CAP | GND | — |
| VT3 (BC548 NPN) | — | ENV_CAP (B) | VCC (C) | ENV_BUF (E) |
| VT5 (BC558 PNP) | — | PCV_ATT (B) | VT5_C (C) | ENV_CAP (E) |
| P5 (100K B) | TUNE DECAY | VT5_C (pin 1) | GND (wiper+pin 3) | — |

**Notes:**
- Charge path: ACC_TRIG → VD7 → ENV_CAP → C6 → GND (fast charge)
- Discharge path: ENV_CAP → VT5(E→C) → P5 → GND (rate set by P5)
- VT5 (PNP) conducts only when ENV_CAP > PCV_ATT + 0.6V:
  creates a voltage floor set by the pitch CV (see Block 8)
- VT3 (NPN emitter follower) buffers ENV_CAP → ENV_BUF
  without draining the cap. Collector to VCC, emitter = output.
- P5 wired as variable resistor (wiper to pin 3)

---

### BLOCK 7: Pitch CV Control Transistor
**Source: p19-20, p22 (tutorial), p50 (schematic)**

VT4 provides voltage-controlled resistance from CAP_MID to ground, modulating
the oscillator frequency. Its base is driven from the smoothed envelope
(through R24). When base voltage rises, VT4 opens, lowering CAP_MID
resistance, raising oscillator frequency.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| VT4 (BC548 NPN) | — | PCV_BASE (B) | CAP_MID (C) | VT4_E (E) |
| R24 | 100K | SMOOTH | PCV_BASE | — |
| P7 (10K B) | TUNE DEPTH | VT4_E (pin 1) | DEPTH_MID (wiper+pin 3) | — |
| R22 | 2K | DEPTH_MID | GND | — |

**Notes:**
- R24 (100K) protects VT4 base from excessive current
- R22 (2K) sets minimum emitter resistance → max frequency ceiling ~250Hz (p22)
- P7 (TUNE DEPTH) varies emitter degeneration:
  at 0Ω: max depth (full pitch sweep), at 10K: min depth
- P7 wired as variable resistor (wiper to pin 3)
- VT4 collector connects to CAP_MID in parallel with the pitch pot chain

---

### BLOCK 8: Pitch CV Input
**Source: p26-27 (tutorial), p50 (schematic)**

External pitch CV sets the envelope floor (the base pitch the envelope
decays to). The CV goes through a 100K input resistor and a variable voltage
divider (P6) to the PNP (VT5) base. When ENV_CAP drops to this CV-set
floor, VT5 cuts off and discharge stops.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R20 | 100K | PITCH_CV_IN | P6 pin 1 | — |
| P6 (100K B) | PITCH CV | R20 (pin 1) | PCV_ATT (wiper) | GND (pin 3) |
| R10 | 10K | PCV_ATT | VT5 base | — |

**Notes:**
- R20 (100K) provides input protection / impedance matching
- P6 wired as voltage divider: pin 1 from CV input, pin 3 to GND,
  wiper = attenuated CV output
- R10 (10K) limits base current into VT5
- At P6 max: full CV → high floor → shallow pitch sweep
- At P6 min: 0V → VT5 base at 0V → envelope decays to near-zero
- XS3 jack tip = PITCH_CV_IN
- **Note:** R20 and R10 are inferred from the production schematic and
  component counts. The tutorial text only describes the pot as a
  "variable voltage divider."

---

### BLOCK 9: Smoothed Pitch Envelope
**Source: p24-25 (tutorial), p50 (schematic)**

A 5.6nF cap smooths the pitch envelope's tail, preventing an abrupt
transition to the base pitch. The diode allows fast initial charging
but forces slow discharge through the 1M resistor.

| Component | Value | Pin 1 | Pin 2 |
|-----------|-------|-------|-------|
| C9 | 5.6nF | ENV_BUF | SMOOTH |
| VD8 (1N4148) | — | SMOOTH (anode) | GND (cathode) |
| R23 | 1M | SMOOTH | CAP_MID |

**Notes:**
- C9 is **missing from the printed BOM** but explicitly described on p24
  ("additional capacitor") and visible on the production schematic
- Charge: ENV_BUF rises → current through C9 → VD8 → GND (fast)
- Discharge: VD8 reverse biased → current must go through R23 (1M)
  to CAP_MID (slow)
- R23 connects to CAP_MID (not GND) for push/pull compensation (p25):
  "while the collector voltage is high, we give the 5.6nF capacitor a
  little push"
- SMOOTH also drives R24 (100K) → PCV_BASE → VT4 (pitch control)

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
  R8.2, R4.2, R9.2, R27.2, R22.2(via DEPTH_MID), R1.2, R3.2,
  C6.2, C12.2,
  P5.wiper+pin3(TUNE DECAY), P6.pin3(PITCH CV), P4.wiper+pin3(TONE),
  VD6.anode, VD8.cathode, VD9.anode,
  VT1.C(collector), DA2A.+(noninv),
  Power: C2.-, C3.+, C1/C4/C5/C13/C14/C15 (decoupling)

NET VCC (+12V):
  R16.1,
  VT2.C(collector), VT3.C(collector),
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
  VT4.C(collector), R28.2, R23.2

NET PITCH_BOT:
  P2.wiper+pin3, R27.1

NET DECAY_INV:
  R25.2, R26.1, P1.pin1, DA2A.-(inv)

NET DECAY_OUT:
  R26.2, P1.pin2+wiper, R28.1, DA2A.out

NET ENV_CAP:
  VD7.cathode, C6.1, VT3.B(base), VT5.E(emitter)

NET VT5_C:
  VT5.C(collector), P5.pin1(TUNE DECAY)

NET ENV_BUF:
  VT3.E(emitter), C9.1

NET SMOOTH:
  C9.2, VD8.anode, R23.1, R24.1

NET PCV_BASE:
  R24.2, VT4.B(base)

NET VT4_E:
  VT4.E(emitter), P7.pin1(TUNE DEPTH)

NET DEPTH_MID:
  P7.wiper+pin3, R22.1

NET PITCH_CV_IN:
  XS3.tip, R20.1

NET PCV_POT_IN:
  R20.2, P6.pin1(PITCH CV)

NET PCV_ATT:
  P6.wiper, R10.1

NET VT5_BASE:
  R10.2, VT5.B(base)

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
| Pitch CV input details | "variable voltage divider" only | Includes R20 (100K) + R10 (10K) | Use schematic version with protection Rs |

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
                              VD5 → R18/R9         VD7 → C6/VT3
                                    │                   │
                                    ▼                   ▼
                              OSC_NONINV            ENV_BUF
                                    │                   │
                                    ▼                   ▼
                              Oscillator           Smoothing
                              DA1B/C10/C11         C9/VD8/R23
                              R5/P2/R27                │
                                    │              ┌────┤
                                    │              │    ▼
                              OSC_OUT          R23→CAP_MID  R24→VT4(NPN)→CAP_MID
                                │                            ↑
                                │                       PITCH CV (XS3)
                                │                       R20→P6→R10→VT5(PNP)
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
