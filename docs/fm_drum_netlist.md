# MKI x ES EDU FM DRUM — Definitive Netlist

**Purpose:** Authoritative netlist for the n8synth solderable breadboard layout.

**Sources (in order of authority):**
1. Tutorial schematics (PDF p9-45) — circuit topology built step by step
2. Full production schematic (PDF p3) — complete circuit (very dense)
3. BOM (PDF p4-6) — component counts and values

**IC assignments:**
- DA1 = TL074 (quad op-amp): Carrier VCO (sections A, B) + Modulator VCO (sections C, D)
- DA2 = TL072 (dual op-amp): Gate-to-Trigger Comparator (A) + VCA (B)

**IC orientation on n8synth:**
- DA1 (TL074): **Normal orientation** — pin 4 (VCC) on left near +12V, pin 11 (VEE) on right near -12V
- DA2 (TL072): **Rotated 180°** — pin 8 (VCC) on left near +12V, pin 4 (VEE) on right near -12V

**Board requirement:** ~100 on-board components. Will need **two n8synth boards**.

**Known BOM notes:**
- 2× 1N5819 Schottky and 2× 10Ω are power supply components — **dropped for n8synth**.
- Tutorial explicitly shows ~14 of the 18× 1N4148. Remaining 4 are likely input protection diodes at jacks in the production circuit.
- 2× 47uF electrolytic are power bypass caps — placed near ICs on board.
- 4× 100nF ceramic are IC decoupling caps (2 per IC, one per supply rail).

---

## CORRECTED BOM

### Resistors (54 total; drop 2× 10Ω for n8synth = 52 on-board)
| Qty | Value | Designators | Role |
|-----|-------|-------------|------|
| 4 | 1M | R20, R29, R42, R52 | VCO reset bias ×2, VCA trim baseline, tune CV bias |
| 3 | 470K | R36, R41, R50 | Impact NPN pull-up, VCA emitter bias, decay CV discharge |
| 1 | 330K | R48 | Tune CV input attenuator |
| 1 | 200K | R39 | VCA feedback |
| 16 | 100K | R3, R6, R13, R14, R16, R17, R22, R23, R25, R26, R27, R32, R43, R44, R49, R53 | See blocks |
| 1 | 68K | R15 | Carrier integrator→schmitt coupling |
| 2 | 51K | R12, R24 | VCO bias dividers (carrier + modulator) |
| 1 | 47K | R10 | Pitch env mixer resistor |
| 2 | 39K | R1, R35 | Gate HP discharge, click injection |
| 3 | 33K | R5, R31, R33 | Comp feedback, tune limiter, carrier CV mixer |
| 1 | 18K | R37 | Pulse wave VCA input scaling |
| 7 | 10K | R4, R21, R30, R34, R45, R46, R47 | Trigger, VCO reset bridges ×2, waveshaper, HPF bias ×2, XOR pull-down |
| 1 | 5K6 | R2 | Accent threshold reference |
| 1 | 4K7 | R7 | Decay minimum discharge |
| 2 | 2K | R38, R51 | Waveshaper diode bias, impact trigger envelope |
| 4 | 1K | R9, R54, R55, R56 | Pitch env discharge + production protection ×3 (verify against PCB) |
| 1 | 470 | R8 | Pitch envelope charge series |
| 1 | 47 | R40 | VCA waveshaper coupling |
| 2 | 10 | — | **Power supply series — DROPPED for n8synth** |

### Trimmer (1 total)
| Qty | Value | Designator | Role |
|-----|-------|------------|------|
| 1 | 2M | RV1 (W205) | VCA DC offset compensation |

### Capacitors (14 total)
| Qty | Value | Type | Designators | Role |
|-----|-------|------|-------------|------|
| 2 | 47uF | Electrolytic | C13, C14 | Power supply bypass (near ICs) |
| 1 | 1uF | Electrolytic | C3 | Decay envelope timing |
| 1 | 470nF | Film | C4 | Pitch envelope timing |
| 4 | 100nF | Ceramic | C9, C10, C11, C12 | IC decoupling (2 per IC) |
| 1 | 15nF | Film | C8 | HPF DC-blocking output |
| 1 | 10nF | Film | C1 | Gate HP coupling cap |
| 1 | 5.6nF | Film | C5 | Impact distortion envelope |
| 1 | 2.2nF | Film | C6 | Modulator integrator cap |
| 2 | 470pF | Ceramic | C7, C15 | Sallen-Key HPF caps |
| 1 | 330pF | Ceramic | C2 | Carrier integrator cap |

### Diodes (20 total; drop 2× 1N5819 for n8synth = 18 on-board)
| Qty | Type | Designators | Role |
|-----|------|-------------|------|
| 18 | 1N4148 | D1-D18 | Signal diodes (see blocks) |
| 2 | 1N5819 | — | **Power protection — DROPPED for n8synth** |

### Transistors (13 total)
| Qty | Type | Designators | Role |
|-----|------|-------------|------|
| 8 | BC548B (NPN) | Q1-Q8 | VCO osc ×2, VCO reset ×2, VCA, impact distortion, HPF, accent buffer |
| 5 | BC558 (PNP) | Q9-Q13 | Accent limiter, XOR gate, decay CV, tune CV carrier, tune CV modulator |

### Potentiometers (6 total — on control deck)
| Qty | Value | Taper | Designator | Label |
|-----|-------|-------|------------|-------|
| 1 | 1M | B (lin) | POT1 | TUNE DEPTH |
| 1 | 500K | A (log) | POT2 | DECAY |
| 1 | 250K | B (lin) | POT3 | TUNE DECAY |
| 2 | 100K | A (log) | POT4, POT5 | TUNE CARRIER, TUNE MODULATOR |
| 1 | 100K | B (lin) | POT6 | TUNE CV INT |

### Jacks (5 total — on control deck)
| Qty | Type | Designator | Label |
|-----|------|------------|-------|
| 1 | Switched mono | J1 | GATE IN |
| 1 | Switched mono | J2 | ACCENT CV |
| 1 | Switched mono | J3 | DECAY CV |
| 1 | Switched mono | J4 | TUNE CV |
| 1 | Switched mono | J5 | OUTPUT |

### Switches (2 total — on control deck)
| Qty | Type | Designator | Label |
|-----|------|------------|-------|
| 1 | SPDT | SW1 | FM ON/OFF |
| 1 | SPDT | SW2 | SINE/PULSE |

### ICs (2 total)
| Qty | Type | Designator | Sections |
|-----|------|------------|----------|
| 1 | TL074 | DA1 | A,B: Carrier VCO; C,D: Modulator VCO |
| 1 | TL072 | DA2 | A: Gate comparator; B: VCA |

---

## NET NAMES

| Net | Description |
|-----|-------------|
| VCC | +12V supply rail |
| VEE | -12V supply rail |
| GND | Ground (0V) |
| GATE_IN | Gate input jack tip |
| HP_OUT | HP filter output → comparator |
| COMP_OUT | Comparator output (raw trigger) |
| ACC_NODE | Accent limiting node |
| UNACC_TRIG | Unaccented trigger (full amplitude) |
| ACC_TRIG | Accented trigger (amplitude limited by accent CV) |
| CARRIER_CV | Carrier VCO control voltage input |
| CAR_INT_INV | Carrier integrator inverting input |
| CAR_INT_NINV | Carrier integrator non-inv input |
| CAR_INT_OUT | Carrier integrator output = carrier triangle |
| CAR_SCH_INV | Carrier schmitt trigger inverting input |
| CAR_SCH_NINV | Carrier schmitt trigger non-inv input |
| CAR_SCH_OUT | Carrier schmitt trigger output = carrier square |
| MOD_CV | Modulator VCO control voltage input |
| MOD_INT_INV | Modulator integrator inverting input |
| MOD_INT_NINV | Modulator integrator non-inv input |
| MOD_INT_OUT | Modulator integrator output = modulator triangle |
| MOD_SCH_OUT | Modulator schmitt trigger output = modulator square |
| SINE_OUT | Waveshaper output (approx sine wave) |
| PITCH_CAP | Pitch envelope capacitor node |
| PITCH_OUT | Pitch envelope output (after isolation diode) |
| DECAY_CAP | Decay envelope capacitor node |
| VCA_INV | VCA op-amp inverting input |
| VCA_OUT | VCA output |
| XOR_OUT | XOR pulse gate output |
| HPF_OUT | High pass filter output (after DC blocking cap) |
| FM_SW_COM | FM on/off switch common terminal |
| TYPE_SW_COM | Sine/pulse switch common terminal |
| TUNE_MIX | Tune/pitch/modulator mixer junction |
| ACCENT_CV_IN | Accent CV jack tip |
| DECAY_CV_IN | Decay CV jack tip |
| TUNE_CV_IN | Tune CV jack tip |
| OUTPUT_J | Output jack tip |

---

## OP-AMP PIN ASSIGNMENTS

### DA1 — TL074 (Normal orientation on n8synth)

```
         ┌──U──┐
OUT_A  1 │     │ 14  OUT_D        ← Carrier integrator out / Mod schmitt out
IN_A-  2 │     │ 13  IN_D-
IN_A+  3 │     │ 12  IN_D+
VCC    4 │     │ 11  VEE         ← +12V left / -12V right
IN_B+  5 │     │ 10  IN_C+
IN_B-  6 │     │  9  IN_C-
OUT_B  7 │     │  8  OUT_C       ← Carrier schmitt out / Mod integrator out
         └─────┘
```

| Section | Function | Pin (+) | Pin (-) | Pin (out) |
|---------|----------|---------|---------|-----------|
| A | Carrier Integrator | 3 | 2 | 1 |
| B | Carrier Schmitt Trigger | 5 | 6 | 7 |
| C | Modulator Integrator | 10 | 9 | 8 |
| D | Modulator Schmitt Trigger | 12 | 13 | 14 |
| Power | | 4 (VCC) | 11 (VEE) | — |

### DA2 — TL072 (Rotated 180° on n8synth)

Physical pin layout on board (top to bottom):
```
Left column:  5(IN_B+), 6(IN_B-), 7(OUT_B), 8(VCC)    ← VCA section + power
Right column: 4(VEE),   3(IN_A+), 2(IN_A-), 1(OUT_A)   ← Comparator section + power
```

| Section | Function | Pin (+) | Pin (-) | Pin (out) |
|---------|----------|---------|---------|-----------|
| A | Gate-to-Trigger Comparator | 3 | 2 | 1 |
| B | Crude VCA | 5 | 6 | 7 |
| Power | | 8 (VCC) | 4 (VEE) | — |

---

## NETLIST BY FUNCTIONAL BLOCK

### BLOCK 1: Gate-to-Trigger Converter
**Source: p20 (tutorial). Reused block from kick/snare (identical topology).**

Converts sustained gate input to a short trigger pulse via HP filter + comparator.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| C1 | 10nF | GATE_IN | HP_OUT | HP coupling cap |
| R1 | 39K | HP_OUT | GND | HP discharge |
| R3 | 100K | VCC | COMP_INV (DA2 pin 2) | Threshold top |
| R5 | 33K | COMP_OUT | DA2 pin 3 (+) | Positive feedback (hysteresis) |
| R4 | 10K | HP_OUT | DA2 pin 3 (+) | Signal scaling |
| R2 | 5K6 | DA2 pin 3 (+) | GND | Threshold reference (with accent CV) |
| DA2-A | TL072 | pin 3 (+) | pin 2 (-) | pin 1 (out) = COMP_OUT |

---

### BLOCK 2: Accent CV Section
**Source: p20 (tutorial). Same pattern as kick/snare accent.**

Limits trigger amplitude based on external accent CV. PNP sets voltage floor,
NPN buffers the output. Creates both accented and unaccented trigger outputs.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R6 | 100K | COMP_OUT | ACC_NODE | — |
| Q9 (BC558 PNP) | — | ACCENT_CV_IN (B) | GND (C) | ACC_NODE (E) |
| Q8 (BC548 NPN) | — | ACC_NODE (B) | VCC (C) | ACC_TRIG (E) |

**Outputs:**
- UNACC_TRIG: from COMP_OUT (may need additional path — verify against PCB)
- ACC_TRIG: from Q8 emitter (amplitude limited)
- ACCENT_CV_IN: J2 jack tip → Q9 base

---

### BLOCK 3: Decay Envelope
**Source: p18 (tutorial)**

Simple diode-cap-pot envelope. Trigger charges cap instantly through diode;
cap discharges slowly through pot + minimum resistor.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| D1 (1N4148) | — | ACC_TRIG (anode) | DECAY_CAP (cathode) | Charge diode |
| C3 | 1uF | DECAY_CAP | GND | Envelope cap (electrolytic, observe polarity) |
| R7 | 4K7 | DECAY_CAP | GND (via POT2) | Min discharge (series with pot) |

**Connections:**
- POT2 (500K DECAY): wiper+pin3 to R7, pin1 to DECAY_CAP
- DECAY_CAP drives VCA control (Q5 base path)
- Decay CV (Block 13) also connects to DECAY_CAP node

---

### BLOCK 4: Pitch Envelope
**Source: p24-25 (tutorial)**

Trigger charges pitch cap through diode + small series resistor (470Ω softens
attack). Cap discharges through tune decay pot + 1K min. Output isolated by
diode so pitch only bends downward from base tune CV.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R8 | 470 | UNACC_TRIG | D2 anode | Charge series (softens attack) |
| D2 (1N4148) | — | R8 (anode) | PITCH_CAP (cathode) | Charge diode |
| C4 | 470nF | PITCH_CAP | GND | Pitch envelope cap |
| R9 | 1K | PITCH_CAP | GND (via POT3) | Min discharge |
| D3 (1N4148) | — | PITCH_CAP (anode) | PITCH_OUT (cathode) | Output isolation — only passes when env > tune CV |
| R10 | 47K | PITCH_OUT | TUNE_MIX (via POT1) | Pitch env to mixer |

**Connections:**
- POT3 (250K TUNE DECAY): controls pitch env discharge rate
- POT1 (1M TUNE DEPTH): in series between R10 and TUNE_MIX, attenuates pitch env depth
- PITCH_OUT → R10 → POT1 → TUNE_MIX

---

### BLOCK 5: Carrier VCO
**Source: p9-11 (tutorial). Uses DA1 sections A (integrator) and B (schmitt trigger).**

909-style triangle VCO. Integrator + schmitt trigger with NPN current switch.
Generates triangle (from integrator) and square (from schmitt) waves.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R13 | 100K | CARRIER_CV | CAR_INT_INV (DA1 pin 2) | CV to integrator (-) |
| R14 | 100K | CARRIER_CV | CAR_INT_NINV (DA1 pin 3) | CV to integrator (+) |
| R12 | 51K | CAR_INT_NINV | GND | Bias divider (sets ~CV/3 at +) |
| C2 | 330pF | CAR_INT_INV | CAR_INT_OUT (DA1 pin 1) | Integrator feedback cap |
| R15 | 68K | CAR_INT_OUT | CAR_SCH_INV (DA1 pin 6) | Integrator→schmitt coupling |
| R16 | 100K | CAR_SCH_OUT (DA1 pin 7) | CAR_SCH_NINV (DA1 pin 5) | Schmitt + feedback |
| R17 | 100K | CAR_SCH_OUT | CAR_INT_INV | Feedback to integrator |
| Q1 (BC548 NPN) | — | CAR_SCH_OUT (B) | CAR_INT_INV (C) | GND (E) |

**Notes:**
- Q1: when schmitt goes high, NPN conducts, sinking current from integrator(-) to GND.
  This doubles the discharge rate, causing the integrator to ramp back up.
- CAR_INT_OUT = carrier triangle wave output
- CAR_SCH_OUT = carrier square wave output
- DA1-A: pin 3 (+) = CAR_INT_NINV, pin 2 (-) = CAR_INT_INV, pin 1 (out) = CAR_INT_OUT
- DA1-B: pin 5 (+) = CAR_SCH_NINV, pin 6 (-) = CAR_SCH_INV, pin 7 (out) = CAR_SCH_OUT

---

### BLOCK 6: Carrier VCO Reset
**Source: p22 (tutorial)**

Resets carrier VCO phase on trigger by discharging integrator cap through
NPN + 10K bridge. Ensures consistent attack transient on every hit.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R20 | 1M | VCC | Q2 base | Trigger bias (trigger pulls base high) |
| R21 | 10K | Q2 collector | Q2 emitter | Bridge resistor across C2 |
| Q2 (BC548 NPN) | — | R20 (B) | CAR_INT_OUT side of C2 (C) | CAR_INT_INV side of C2 (E) |

**Notes:**
- When trigger goes high, Q2 conducts, shorting C2 through 10K
- Resets integrator to ~CV/2, so every hit starts from same phase point
- R20 connects to trigger path (UNACC_TRIG or VCC with trigger switching)

---

### BLOCK 7: Modulator VCO
**Source: p31-32 (tutorial). Clone of carrier with 2 differences.**

Uses DA1 sections C (integrator) and D (schmitt trigger). Differences from carrier:
1. C6 = 2.2nF (vs 330pF) — shifts frequency range much lower
2. R25 = 100K (vs 68K) — wider schmitt thresholds for stronger output

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R22 | 100K | MOD_CV | MOD_INT_INV (DA1 pin 9) | CV to integrator (-) |
| R23 | 100K | MOD_CV | MOD_INT_NINV (DA1 pin 10) | CV to integrator (+) |
| R24 | 51K | MOD_INT_NINV | GND | Bias divider |
| C6 | 2.2nF | MOD_INT_INV | MOD_INT_OUT (DA1 pin 8) | Integrator cap (7× bigger = lower freq) |
| R25 | 100K | MOD_INT_OUT | DA1 pin 13 (-) | Integrator→schmitt (wider threshold) |
| R26 | 100K | MOD_SCH_OUT (DA1 pin 14) | DA1 pin 12 (+) | Schmitt + feedback |
| R27 | 100K | MOD_SCH_OUT | MOD_INT_INV | Feedback to integrator |
| Q3 (BC548 NPN) | — | MOD_SCH_OUT (B) | MOD_INT_INV (C) | GND (E) |

**Notes:**
- DA1-C: pin 10 (+), pin 9 (-), pin 8 (out) = MOD_INT_OUT (modulator triangle)
- DA1-D: pin 12 (+), pin 13 (-), pin 14 (out) = MOD_SCH_OUT (modulator square)

---

### BLOCK 8: Modulator VCO Reset
**Source: p22 pattern applied to modulator**

Same as carrier reset but for modulator integrator cap C6.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R29 | 1M | VCC | Q4 base | Trigger bias |
| R30 | 10K | Q4 collector | Q4 emitter | Bridge resistor across C6 |
| Q4 (BC548 NPN) | — | R29 (B) | MOD_INT_OUT side of C6 (C) | MOD_INT_INV side of C6 (E) |

---

### BLOCK 9: Carrier CV Mixer
**Source: p25, p32 (tutorial)**

Passive mixer combining tune pot, pitch envelope, and modulator into CARRIER_CV.
33K from +12V limits tune pot max to ~9V (leaves headroom for pitch envelope).

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R31 | 33K | VCC | POT4 top | Limits tune pot max voltage to ~9V |
| R33 | 33K | POT4 wiper | CARRIER_CV | Tune to carrier CV mixer |
| R32 | 100K | FM_SW_COM | CARRIER_CV | Modulator→carrier FM coupling |
| D4 (1N4148) | — | GND (anode) | CARRIER_CV (cathode) | Clamp — prevents CV going negative |

**Connections:**
- POT4 (100K TUNE CARRIER): top to R31, wiper to R33
- TUNE_MIX from pitch env (Block 4) connects to CARRIER_CV via D3 isolation
- FM_SW_COM from FM switch (Block 18) connects via R32
- CARRIER_CV drives carrier VCO (Block 5)

---

### BLOCK 10: Tri-to-Sine Waveshaper
**Source: p12-13 (tutorial)**

Anti-parallel diodes + voltage divider soft-clip the carrier triangle into
approximate sine wave. 10K/2K divider scales triangle from ~10Vpp down to
~1.6Vpp — just right for gradual diode conduction.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R34 | 10K | CAR_INT_OUT | SINE_NODE | Input scaling resistor |
| D5 (1N4148) | — | SINE_NODE (anode) | SINE_NODE (cathode) | Anti-parallel pair |
| D6 (1N4148) | — | SINE_NODE (cathode) | SINE_NODE (anode) | (reverse of D5) |
| R38 | 2K | SINE_NODE | GND | Diode bias / scaling reference |

**Output:** SINE_OUT from SINE_NODE (shaped sine wave)

---

### BLOCK 11: Impact Distortion
**Source: p27-28 (tutorial)**

NPN bridges the 10K waveshaper input resistor on accented hits. Driven by
a short envelope (5.6nF + 470K decay). Reduces waveshaper scaling momentarily,
creating asymmetric clipping = sharp attack + extra harmonics.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| D7 (1N4148) | — | ACC_TRIG (anode) | C5 (cathode) | Trigger to distortion envelope |
| C5 | 5.6nF | D7 cathode | IMPACT_NODE | Distortion envelope cap |
| R51 | 2K | IMPACT_NODE | GND | Envelope discharge |
| R36 | 470K | VCC | Q6 collector | NPN pull-up |
| Q6 (BC548 NPN) | — | IMPACT_NODE (B) | R34/SINE_NODE junction (C) | CAR_INT_OUT side of R34 (E) |

**Notes:**
- Q6 bridges R34 (10K): when conducting, reduces effective input scaling
- Driven by accented trigger for volume-dependent impact
- Effect is subtle but adds 909-style sharp transient

---

### BLOCK 12: Added Click
**Source: p29 (tutorial)**

Injects trigger pulse directly into waveshaper output via diode + 39K.
Adds initial click/transient to the drum sound.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| D8 (1N4148) | — | UNACC_TRIG (anode) | CLICK_NODE (cathode) | Isolation — blocks -12V when trigger inactive |
| R35 | 39K | CLICK_NODE | SINE_OUT | Injection resistor (39K sweet spot) |

**Notes:**
- Uses unaccented trigger (VCA already handles accent-based volume)
- Diode prevents trigger's -12V idle state from affecting waveshaper
- 39K chosen for subtle but noticeable click without overpowering sound

---

### BLOCK 13: Decay CV
**Source: p41 (tutorial)**

PNP transistor between decay envelope discharge path and GND creates a
voltage floor. Cap can only discharge to the CV level at PNP base.
470K bypass allows slow continued discharge past the floor.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| Q11 (BC558 PNP) | — | DECAY_CV_IN (B) | GND (C) | DECAY_CAP (E) |
| R50 | 470K | DECAY_CAP | GND | Slow bypass (gradual discharge past CV floor) |

**Connections:**
- DECAY_CV_IN: J3 jack tip → Q11 base
- Q11 emitter connects to DECAY_CAP node (Block 3)
- High CV = higher floor = shorter effective decay
- 470K allows eventual full discharge regardless of CV

---

### BLOCK 14: Crude VCA + DC Offset Trim
**Source: p14-17 (tutorial). Uses DA2-B.**

NPN transistor as voltage-controlled "fake resistor" at op-amp inverting input.
Base current (from waveshaper) modulated by collector current (from envelope).
200K feedback sets gain. 2M trimmer + 1M compensate NPN reverse current offset.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R40 | 47 | TYPE_SW_COM | VCA_BASE_NODE | Waveshaper/VCA coupling |
| Q5 (BC548 NPN) | — | VCA_BASE_NODE (B) | VCA_INV (DA2 pin 6) (C) | VCA_EMIT (E) |
| R41 | 470K | VCA_EMIT | GND | Emitter bias |
| R39 | 200K | VCA_OUT (DA2 pin 7) | VCA_INV (DA2 pin 6) | Feedback |
| RV1 | 2M trimmer | VCA_INV (DA2 pin 6) | RV1 wiper | DC offset trim |
| R42 | 1M | RV1 wiper | GND | Trim baseline |
| DA2-B | TL072 | pin 5 (+) = GND | pin 6 (-) = VCA_INV | pin 7 (out) = VCA_OUT |

**Notes:**
- DA2-B (+) pin 5 connects to GND (or via trimmer)
- VCA gain is controlled by envelope voltage at Q5 base (via decay envelope path)
- DECAY_CAP voltage controls Q5 base current through the VCA coupling network
- TYPE_SW_COM selects sine or pulse wave input (from Block 15)

---

### BLOCK 15: Sine/Pulse Switch
**Source: p39 (tutorial)**

SPDT switch selects between sine wave output and XOR pulse output as VCA input.
Pulse path has extra 18K scaling resistor (pulse is much louder than sine).

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| R37 | 18K | HPF_OUT | SW2 terminal 2 | Pulse wave input scaling |
| SW2 (SPDT) | — | SINE_OUT (term 1) | TYPE_SW_COM (common) | R37/HPF_OUT (term 2) |

**Notes:**
- SW2 position 1 (SINE): SINE_OUT → TYPE_SW_COM → R40 → VCA
- SW2 position 2 (PULSE): HPF_OUT → R37 → TYPE_SW_COM → R40 → VCA
- 18K + 47Ω scales pulse signal down to ~20mVpp range suitable for VCA NPN

---

### BLOCK 16: XOR Pulse Gate
**Source: p34-35 (tutorial)**

Discrete XOR gate from 4 diodes + PNP transistor. Takes carrier and modulator
square waves, outputs ring-mod-like pulse with semi-chaotic pulse width.
Sounds harsh and metallic — ideal for cymbal/hi-hat synthesis.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| D9 (1N4148) | — | CAR_SCH_OUT (anode) | Q10 emitter (cathode) | — |
| D10 (1N4148) | — | Q10 base (anode) | CAR_SCH_OUT (cathode) | — |
| D11 (1N4148) | — | Q10 emitter (anode) | MOD_SCH_OUT (cathode) | — |
| D12 (1N4148) | — | MOD_SCH_OUT (anode) | Q10 base (cathode) | — |
| Q10 (BC558 PNP) | — | see diodes (B) | see diodes (E) | XOR_COLL (C) |
| R43 | 100K | Q10 emitter | Q10 base (junction) | Bias path |
| R45 | 10K | XOR_OUT | GND (or VEE) | Output pull-down |

**Notes:**
- XOR truth table: output HIGH when exactly one input is HIGH
- A (carrier sq) and B (modulator sq) connect through diode pairs to PNP base/emitter
- XOR_OUT goes to HPF (Block 17) for hi-hat/cymbal filtering

---

### BLOCK 17: High Pass Filter
**Source: p37 (tutorial)**

Sallen-Key HPF removes low-end from XOR pulse for cymbal/hi-hat sounds.
Transistor-based (NPN emitter follower as active element). 12 dB/oct rolloff
starting at ~7.5 kHz with resonance bump. 15nF output cap removes DC offset.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| C7 | 470pF | XOR_OUT | HPF_NODE_A | HPF input cap 1 |
| C15 | 470pF | HPF_NODE_A | Q7 base | HPF cap 2 |
| R44 | 100K | HPF_NODE_A | Q7 emitter | Sallen-Key feedback (was 200K in tutorial — see note) |
| Q7 (BC548 NPN) | — | C15 (B) | VCC (C) | HPF_EMIT (E) |
| R46 | 10K | HPF_EMIT | VEE_BIAS | Emitter bias 1 |
| R47 | 10K | VEE_BIAS | VEE | Emitter bias 2 |
| C8 | 15nF | HPF_EMIT | HPF_OUT | DC-blocking output cap |

**Notes:**
- Tutorial p37 shows 200K feedback, but BOM has only 1× 200K (used in VCA).
  Production circuit may use 100K here. Verify against PCB.
- 10K + 10K bias chain sets NPN operating point between supply rails
- HPF_OUT → R37 (18K scaling) → SW2 (sine/pulse switch)

---

### BLOCK 18: FM On/Off Switch
**Source: p33 (tutorial)**

SPDT switch selects between modulator output and GND for the FM path.
Prevents carrier from crashing into supply rail when modulator CV goes negative.

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| SW1 (SPDT) | — | MOD_INT_OUT (term 1) | FM_SW_COM (common) | GND (term 2) |

**Connections:**
- SW1 position 1 (FM ON): modulator triangle → FM_SW_COM → R32 → CARRIER_CV
- SW1 position 2 (FM OFF): GND → FM_SW_COM → R32 → CARRIER_CV (no modulation)
- D4 (Block 9) clamps CARRIER_CV to prevent negative voltages

---

### BLOCK 19: Tune CV
**Source: p43-44 (tutorial)**

External CV controls both VCO frequencies simultaneously via PNP transistors
inserted between tune pots and GND. PNP emitters mirror base voltage,
shifting the voltage divider point. Diode + 1M compensate for PNP Vbe offset.

| Component | Value | Pin 1 | Pin 2 | Pin 3 |
|-----------|-------|-------|-------|-------|
| R48 | 330K | TUNE_CV_IN | TUNE_CV_ATT | Input attenuator (with POT6) |
| D13 (1N4148) | — | TUNE_CV_BIAS (anode) | PNP_BASES (cathode) | Vbe compensation diode |
| R52 | 1M | TUNE_CV_BIAS | VEE | Ensures current through D13 for consistent Vbe drop |
| Q12 (BC558 PNP) | — | PNP_BASES (B) | GND (C) | CARRIER_TUNE_BOT (E) |
| Q13 (BC558 PNP) | — | PNP_BASES (B) | GND (C) | MOD_TUNE_BOT (E) |
| R49 | 100K | POT6 wiper | TUNE_CV_ATT | Tune CV INT pot connection |

**Connections:**
- POT6 (100K TUNE CV INT): controls how much external CV affects tuning
- Q12 emitter connects to bottom of POT4 (TUNE CARRIER pot) and R31/R33 network
- Q13 emitter connects to bottom of POT5 (TUNE MODULATOR pot)
- External CV at J4 → R48 → POT6 → D13 → PNP bases → both tune pot grounds

---

### BLOCK 20: IC Decoupling
**Placed near each IC on board.**

| Component | Value | Pin 1 | Pin 2 | Notes |
|-----------|-------|-------|-------|-------|
| C9 | 100nF | VCC (DA1 pin 4) | GND | DA1 positive supply bypass |
| C10 | 100nF | VEE (DA1 pin 11) | GND | DA1 negative supply bypass |
| C11 | 100nF | VCC (DA2 pin 8) | GND | DA2 positive supply bypass |
| C12 | 100nF | VEE (DA2 pin 4) | GND | DA2 negative supply bypass |
| C13 | 47uF | VCC | GND | Bulk positive supply bypass (electrolytic) |
| C14 | 47uF | VEE | GND | Bulk negative supply bypass (electrolytic) |

---

## BOM CROSS-CHECK

### Resistor count verification
| Value | BOM Qty | Placed | Designators | OK? |
|-------|---------|--------|-------------|-----|
| 1M | 4 | 4 | R20, R29, R42, R52 | ✓ |
| 470K | 3 | 3 | R36, R41, R50 | ✓ |
| 330K | 1 | 1 | R48 | ✓ |
| 200K | 1 | 1 | R39 | ✓ (see HPF note) |
| 100K | 16 | 16 | R3, R6, R13, R14, R16, R17, R22, R23, R25, R26, R27, R32, R43, R44, R49, R53 | ✓ |
| 68K | 1 | 1 | R15 | ✓ |
| 51K | 2 | 2 | R12, R24 | ✓ |
| 47K | 1 | 1 | R10 | ✓ |
| 39K | 2 | 2 | R1, R35 | ✓ |
| 33K | 3 | 3 | R5, R31, R33 | ✓ |
| 18K | 1 | 1 | R37 | ✓ |
| 10K | 7 | 7 | R4, R21, R30, R34, R45, R46, R47 | ✓ |
| 5K6 | 1 | 1 | R2 | ✓ |
| 4K7 | 1 | 1 | R7 | ✓ |
| 2K | 2 | 2 | R38, R51 | ✓ |
| 1K | 4 | 1+3 | R9 + R54,R55,R56 (protection — verify PCB) | ⚠ |
| 470 | 1 | 1 | R8 | ✓ |
| 47 | 1 | 1 | R40 | ✓ |
| 10 | 2 | 0 | — (dropped for n8synth) | — |

### Diode count verification
| Type | BOM Qty | Placed | Designators | OK? |
|------|---------|--------|-------------|-----|
| 1N4148 | 18 | 13+1+4 | D1-D13 placed + D14 (gate clamp?) + D15-D18 (jack protection?) | ⚠ |
| 1N5819 | 2 | 0 | — (dropped for n8synth) | — |

**Unresolved:**
- 3× 1K resistors (R54-R56): likely production protection — base current limiters or output series. Verify against PCB.
- 4-5× 1N4148 diodes (D14-D18): likely input protection at jacks and/or additional clamping. Verify against PCB.
- HPF feedback: tutorial shows 200K but BOM only has 1× 200K (used in VCA). Production circuit may use 100K in HPF (which would require 17× 100K, conflicting with BOM's 16). Needs PCB verification.

---

## SIGNAL FLOW DIAGRAM

```
GATE IN ─→ [HP + Comparator] ─→ COMP_OUT ─┬→ [Accent CV] ─→ ACC_TRIG
            (Block 1, DA2-A)               │                    │
                                           └→ UNACC_TRIG       │
                                                │               │
                              ┌─────────────────┤               │
                              │                 │               │
                              ▼                 ▼               ▼
                     [Pitch Envelope]    [VCO Resets]    [Decay Envelope]
                      (Block 4)          (Blocks 6,8)    (Block 3)
                         │                  │ │              │
                         ▼                  │ │              │
                    PITCH_OUT               │ │         DECAY_CAP
                         │                  │ │              │
          ┌──────────────┤                  │ │         [Decay CV]
          │              │                  │ │          (Block 13)
          │     ┌────────┘                  │ │              │
          │     │                           │ │              ▼
          │     ▼                           ▼ ▼        VCA control
          │  [Carrier CV Mixer] ←── [FM Switch] ←── [Modulator VCO]
          │   (Block 9)             (Block 18)      (Blocks 7,8, DA1-C,D)
          │     │                                        │
          │     ▼                                        │ (square wave)
          │  [Carrier VCO] ─── carrier triangle          │
          │   (Blocks 5,6, DA1-A,B)    │                 │
          │                 │          │                 │
          │      carrier sq │          ▼                 │
          │                 │  [Waveshaper] ──→ SINE ──→ [Sine/Pulse SW]
          │                 │   (Blocks 10-12)           (Block 15, SW2)
          │                 │                                │
          │                 │                                ▼
          │                 └─→ [XOR Gate] ─→ [HPF] ─→ PULSE path
          │                      (Block 16)   (Block 17)
          │                          ▲
          │                          │ (mod square)
          │                     MOD_SCH_OUT
          │
          │                                    TYPE_SW_COM
          │                                        │
          │                                        ▼
          └──────────────────────────────────→ [VCA] ─→ OUTPUT
                                               (Block 14, DA2-B)
```

### Board split suggestion
**Board 1 (signal generation):** Blocks 1-2 (trigger/accent), 3-4 (envelopes),
5-6 (carrier VCO + reset), 9 (carrier CV mixer), 10-12 (waveshaper/distortion/click).
Contains DA1 (TL074). End near top of board.

**Board 2 (modulator + output):** Blocks 7-8 (modulator VCO + reset), 14 (VCA),
15 (switch), 16 (XOR), 17 (HPF), 18 (FM switch), 13 (decay CV), 19 (tune CV).
Contains DA2 (TL072). Start near top.

Inter-board connections: CARRIER_CV, CAR_INT_OUT, CAR_SCH_OUT, SINE_OUT,
ACC_TRIG, UNACC_TRIG, DECAY_CAP, PITCH_OUT, VCC, VEE, GND.
