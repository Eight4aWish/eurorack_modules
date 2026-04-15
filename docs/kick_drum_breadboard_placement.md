# MKI x ES EDU KICK DRUM — n8synth Breadboard Placement

**Platform:** n8synth 10HP solderable breadboard + 6HP 2x6 control deck
**Board capacity:** 36 usable rows (1-36), rows 37-40 reserved for power
**Grid:** Standard breadboard layout — 5 connected holes per half-row
**Columns:** Left half = a,b,c,d,e | centre gap | Right half = f,g,h,i,j
**Power rails:** +12V and GND on LEFT edge, -12V and GND on RIGHT edge

---

## IC PLACEMENT — ROTATED 180 DEGREES

Both TL072s are rotated so VCC (pin 8) faces the +12V rail on the left
and VEE (pin 4) faces the -12V rail on the right. Pin assignments shown
below with IC body straddling columns e (left) and f (right).

### DA1 (TL072 #1) — Rows 9-12

Comparator + Oscillator. Oscillator on left, comparator on right.

```
              LEFT (a-e)                   RIGHT (f-j)
         a     b     c     d     e    f     g     h     i     j
        ┌─────────────────────────┐  ┌─────────────────────────┐
Row  9: │R18.2       R9.1  DA1.5 │  │DA1.4  Cdec        -12V→ │
        │            ↓GND  NONINV│  │VEE    →GND              │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 10: │R5.1        C11.2 DA1.6 │  │DA1.3  C8.2  R8.1  VD6.K│
        │(→r11a)           INV   │  │HP_OUT        ↓GND  ↓GND │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 11: │R5.2  P4w  R25.1 C10.2  DA1.7 │  │DA1.2  JW←r5L             │
        │(←r10a)                 OUT │  │COMPINV                     │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 12: │Cdec              DA1.8 │  │DA1.1  R13.1             │
        │→GND              VCC   │  │COMPOUT (→r13g)           │
        │            +12V→       │  │                          │
        └─────────────────────────┘  └─────────────────────────┘
```

**Pin map:**
| Row | Left col e | Net | Right col f | Net |
|-----|-----------|-----|-------------|-----|
| 9 | pin 5 | OSC_NONINV | pin 4 | VEE |
| 10 | pin 6 | OSC_INV | pin 3 | HP_OUT |
| 11 | pin 7 | OSC_OUT | pin 2 | COMP_INV |
| 12 | pin 8 | VCC | pin 1 | COMP_OUT |


### DA2 (TL072 #2) — Rows 27-30

Decay buffer + Distortion. Distortion on left, decay on right.

```
              LEFT (a-e)                   RIGHT (f-j)
         a     b     c     d     e    f     g     h     i     j
        ┌─────────────────────────┐  ┌─────────────────────────┐
Row 27: │      P4w  C12.1  DA2.5 │  │DA2.4  Cdec        -12V→ │
        │            ↓GND  TONE  │  │VEE    →GND              │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 28: │C7.2 VD4.K VD3.A R1.1  DA2.6 │  │DA2.3  →GND              │
        │                  ↓GND  INV  │  │GND                       │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 29: │JW   C7.1 VD4.A VD3.K  DA2.7 │  │DA2.2  R25.2 R26.1 P1w  │
        │→r31              OUT   │  │DCINV                     │
        ├─────────────────────────┤  ├─────────────────────────┤
Row 30: │Cdec              DA2.8 │  │DA2.1  R26.2 R28.1 P1w   │
        │→GND              VCC   │  │DCOUT                     │
        │            +12V→       │  │                          │
        └─────────────────────────┘  └─────────────────────────┘
```

**Pin map:**
| Row | Left col e | Net | Right col f | Net |
|-----|-----------|-----|-------------|-----|
| 27 | pin 5 | TONE_OUT | pin 4 | VEE |
| 28 | pin 6 | DIST_INV | pin 3 | GND |
| 29 | pin 7 | DIST_OUT | pin 2 | DECAY_INV |
| 30 | pin 8 | VCC | pin 1 | DECAY_OUT |

---

## COMPLETE ROW ALLOCATION

**Legend:**
- `→GND` / `→+12V` / `→-12V` = component lead goes to power rail
- `JW→rN` = jumper wire to row N (same or opposite side)
- `P4w` = wire from JPS control deck (pot/jack signal)
- `(vert)` = component mounted vertically (short span)

### Rows 1-8: Input Stages + CAP_MID (above DA1)

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 1 | *spare* | *spare* |
| 2 | *spare* | *spare* |
| 3 | ACC_TRIG relay: **JW from r16L** (col a), **VD5.A** (col b) | *free* |
| 4 | TRIG_POS: **VD5.K** (col a), **R18.1** (col b) | *free* |
| 5 | COMP_INV node: **R16.2** (col a, R16.1→+12V), **R4.1** (col b, R4.2→GND), **JW→r11R** (col c) | *free* |
| 6 | PITCH_BOT: **R27.1** (col a, JPS P2 wiper wire), **R27.2**→GND (col b) | *free* |
| 7 | CAP_MID ext: **R23.2** (col a), **R28.2** (col b), **JW from r22L** (col c, VT4.C), **JW→r8L** (col d) | *free* |
| 8 | CAP_MID: **C11.1** (col a), **C10.1** (col b), **JPS P2 pin1 wire** (col c), **JW from r7L** (col d) | GATE_IN: **C8.1** (col g), **JPS XS1 wire** (col h) |

**Component spans (rows above DA1):**
- VD5: row 3L → row 4L (1 row, vertical)
- R18: row 4L → row 9L (5 rows = 12.7mm)
- R16: +12V rail → row 5L (rail bridge)
- R4: row 5L → GND rail (rail bridge)
- R27: row 6L → GND rail (rail bridge)
- C11: row 8L → row 10L (2 rows = 5.1mm)
- C10: row 8L → row 11L (3 rows = 7.6mm)
- C8: row 8R → row 10R (2 rows = 5.1mm)

### Rows 9-12: DA1 (see IC diagram above)

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 9 | **R18.2** (col c), **R9.1** (col d, R9.2→GND), **DA1.5** (col e) | **DA1.4** (col f), **C14** (col g, C14.2→GND) |
| 10 | **R5.1** (col a), **C11.2** (col d), **DA1.6** (col e) | **DA1.3** (col f), **C8.2** (col g), **R8.1** (col h, R8.2→GND), **VD6.K** (col i, VD6.A→GND) |
| 11 | **R5.2** (col a), **P4 wire** (col b), **R25.1** (col c), **C10.2** (col d), **DA1.7** (col e) | **DA1.2** (col f), **JW from r5L** (col g) |
| 12 | **C13** (col a, C13.2→GND), **DA1.8** (col e) | **DA1.1** (col f), **R13.1** (col g) |

**Decoupling caps:**
- C14 (100nF): row 9R (VEE pin) → GND rail (right)
- C13 (100nF): row 12L (VCC pin) → GND rail (left)

**Component spans (DA1 area):**
- R5 (1M bridge): row 10L → row 11L (1 row, vertical mount)
- R8: row 10R → GND rail (rail bridge)
- VD6: GND rail → row 10R (rail bridge, anode at GND)
- R9: row 9L → GND rail (rail bridge)

### Rows 13-17: Accent Section (below DA1)

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 13 | *free* | ACC_IN: **R13.2** (col g, vert from r12g), **R12.1** (col h), **R3.1** (col i, R3.2→GND) |
| 14 | **VT2.C** (col c, →+12V rail) | *free* |
| 15 | **VT2.B** ACC_EMIT (col c), **JW→r15R** (col a) | **VT1.E** ACC_EMIT (col h), **R12.2** (col i), **JW from r15L** (col j) |
| 16 | **VT2.E** ACC_TRIG (col c), **JW→r3L** (col b), **VD7.A** (col a) | **VT1.B** ACCENT_CV_IN (col h), **JPS XS2 wire** (col i) |
| 17 | **VT3.C** (col d, →+12V rail) | **VT1.C** (col h, →GND rail) |

**Component spans (accent area):**
- R13: row 12R → row 13R (1 row, vertical mount)
- R12: row 13R → row 15R (2 rows = 5.1mm)
- R3: row 13R → GND rail (rail bridge)
- VT2 (BC548 NPN): rows 14-16 left, col c (E at row 16, B at row 15, C at row 14)
- VT1 (BC558 PNP): rows 15-17 right, col h (E at row 15, B at row 16, C at row 17)
- VD7: row 16L → row 18L (2 rows = 5.1mm)

### Rows 18-21: Envelope + Smoothing

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 18 | ENV_CAP: **VD7.K** (col a), **C6.1** (col b, C6.2→GND), **VT3.B** (col d), **JW→r18R** (col c) | **VT5.E** ENV_CAP (col h), **JW from r18L** (col g) |
| 19 | ENV_BUF: **VT3.E** (col d), **C9.1** (col a) | VT5_BASE: **VT5.B** (col h), **R10.2** (col g) |
| 20 | *free* | VT5_C: **VT5.C** (col h), **JPS P5 wire** (col g) |
| 21 | SMOOTH: **C9.2** (col a), **VD8.A** (col b, VD8.K→GND), **R23.1** (col c), **R24.1** (col d) | *free* |

**Component spans (envelope area):**
- VT3 (BC548 NPN): rows 17-19 left, col d (C at row 17, B at row 18, E at row 19)
- VT5 (BC558 PNP): rows 18-20 right, col h (E at row 18, B at row 19, C at row 20)
- C6 (220nF): row 18L → GND rail (rail bridge)
- C9 (5.6nF): row 19L → row 21L (2 rows = 5.1mm)
- VD8: row 21L → GND rail (rail bridge, cathode at GND)
- R23 (1M): row 21L → row 7L (14 rows — long point-to-point wire)
- R24 (100K): row 21L → row 23L (2 rows = 5.1mm)

### Rows 22-26: Pitch CV + Spare

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 22 | VT4.C: **JW→r7L** (col a, CAP_MID) | PCV_ATT: **R10.1** (col g), **R20.2** (col h), **JPS P6 wiper wire** (col i) |
| 23 | PCV_BASE: **VT4.B** (col b), **R24.2** (col a) | PITCH_CV_IN: **R20.1** (col g), **JPS XS3 wire** (col h) |
| 24 | VT4_E: **VT4.E** (col b), **JPS P7 pin1 wire** (col a) | *free* |
| 25 | DEPTH_MID: **R22.1** (col a, R22.2→GND), **JPS P7 wiper wire** (col b) | *free* |
| 26 | *spare* | *spare* |

**Component spans (pitch CV area):**
- VT4 (BC548 NPN): rows 22-24 left, col b (C at row 22, B at row 23, E at row 24)
- R24: row 21L → row 23L (2 rows = 5.1mm)
- R10: row 22R → row 19R (3 rows = 7.6mm)
- R20: row 23R → row 22R (1 row, vertical mount)
- R22: row 25L → GND rail (rail bridge)

### Rows 27-30: DA2 (see IC diagram above)

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 27 | **JPS P4 wiper wire** (col b), **C12.1** (col c, C12.2→GND), **DA2.5** (col e) | **DA2.4** (col f), **C5** (col g, C5.2→GND) |
| 28 | **C7.2** (col a), **VD4.K** (col b), **VD3.A** (col c), **R1.1** (col d, R1.2→GND), **DA2.6** (col e) | **DA2.3** (col f), **wire→GND** (col g) |
| 29 | **JW→r31L** (col a), **C7.1** (col b), **VD4.A** (col c), **VD3.K** (col d), **DA2.7** (col e) | **DA2.2** (col f), **R25.2** (col g), **R26.1** (col h), **JPS P1 pin1 wire** (col i) |
| 30 | **C15** (col a, C15.2→GND), **DA2.8** (col e) | **DA2.1** (col f), **R26.2** (col g), **R28.1** (col h), **JPS P1 wiper wire** (col i) |

**Decoupling caps:**
- C5 (100nF): row 27R (VEE pin) → GND rail (right)
- C15 (100nF): row 30L (VCC pin) → GND rail (left)

**Component spans (DA2 area):**
- VD3: row 28L → row 29L (1 row, vertical)
- VD4: row 29L → row 28L (1 row, vertical, anti-parallel with VD3)
- C7 (3.3nF): row 28L → row 29L (1 row, vertical mount)
- R1: row 28L → GND rail (rail bridge)
- R26 (47K): row 29R → row 30R (1 row, vertical mount)
- R25 (47K): row 11L → row 29R (18 rows — long point-to-point run)
- R28 (470K): row 30R → row 7L (23 rows — long point-to-point run)

### Rows 31-36: Output + Spare

| Row | Left half (a-e) | Right half (f-j) |
|-----|----------------|-------------------|
| 31 | DIST_OUT ext: **R21.1** (col a), **JW from r29L** (col b), **JPS P3 wire** (col c) | *free* |
| 32 | OUTPUT_J: **R21.2** (col a), **JPS XS4 wire** (col b) | *free* |
| 33 | *spare* | *spare* |
| 34 | *spare — R2 (1K), R7 (1K), VD9 if needed* | *spare* |
| 35 | *spare* | *spare* |
| 36 | *spare* | *spare* |

**Component spans (output area):**
- R21 (1K): row 31L → row 32L (1 row, vertical mount)

---

## JUMPER WIRE LIST

### Short jumpers (same row, across centre gap)
| Wire | From | To | Signal | Purpose |
|------|------|----|--------|---------|
| JW1 | row 15L col a | row 15R col j | ACC_EMIT | Connect VT2.B to VT1.E |
| JW2 | row 18L col c | row 18R col g | ENV_CAP | Connect VT3.B to VT5.E |

### Row-to-row jumpers (same side)
| Wire | From | To | Signal | Rows spanned |
|------|------|----|--------|-------------|
| JW3 | row 5L col c | row 11R col g | COMP_INV | 6 rows, crosses gap |
| JW4 | row 7L col d | row 8L col d | CAP_MID | 1 row (extends net) |
| JW5 | row 16L col b | row 3L col a | ACC_TRIG | 13 rows |
| JW6 | row 22L col a | row 7L col c | VT4.C→CAP_MID | 15 rows |
| JW7 | row 29L col a | row 31L col b | DIST_OUT ext | 2 rows |

### Long-span components (resistors acting as point-to-point wires)
| Component | From | To | Rows spanned | Distance |
|-----------|------|----|-------------|----------|
| R25 (47K) | row 11L col c | row 29R col g | 18 rows | ~46mm |
| R28 (470K) | row 30R col h | row 7L col b | 23 rows | ~58mm |
| R23 (1M) | row 21L col c | row 7L col a | 14 rows | ~36mm |
| R18 (100K) | row 4L col b | row 9L col c | 5 rows | ~13mm |

**Note:** For R25, R28, and R23: extend the component leads or solder
short wire extensions. Route these above the components, keeping them
tidy. All three are high-impedance (47K-1M) so lead length is not
electrically significant.

---

## JPS CONTROL DECK WIRING SUMMARY

These are the signals that need wires from the breadboard to the
control deck edge connector. Exact routing depends on JPS cell
assignment and is left to the builder.

| JPS Cell | Component | Signal A | Row needed | Signal B | Row needed | Signal C | Row needed |
|----------|-----------|----------|------------|----------|------------|----------|------------|
| JPS1 | XS1 TRIGGER | Tip=GATE_IN | row 8R | — | — | Sleeve→D-bus | — |
| JPS2 | XS2 ACCENT | Tip=ACCENT_CV_IN | row 16R | — | — | Sleeve→D-bus | — |
| JPS3 | XS3 PITCH CV | Tip=PITCH_CV_IN | row 23R | — | — | Sleeve→D-bus | — |
| JPS4 | XS4 OUTPUT | Tip=OUTPUT_J | row 32L | — | — | Sleeve→D-bus | — |
| JPS5 | P2 PITCH 250K | pin1=CAP_MID | row 8L | wiper+pin3 | row 6L | — | — |
| JPS6 | P6 PITCH CV 100K | pin1 from R20 | row 22R | wiper=PCV_ATT | row 22R | pin3→D-bus | — |
| JPS7 | P7 TUNE DEPTH 10K | pin1=VT4_E | row 24L | wiper+pin3 | row 25L | — | — |
| JPS8 | P5 TUNE DECAY 100K | pin1=VT5_C | row 20R | wiper+pin3→GND | D-bus | — | — |
| JPS9 | P4 TONE 50K | pin1=OSC_OUT | row 11L | wiper+pin3 | row 27L | — | — |
| JPS10 | P3 DISTORTION 100K | pin1=DIST_OUT | row 31L | wiper+pin2 | row 31L | — | — |
| JPS11 | P1 DECAY 1M | pin1=DECAY_INV | row 29R | wiper+pin2 | row 30R | — | — |

---

## VISUAL OVERVIEW — SIGNAL FLOW ON BOARD

```
Row 1-2:  ·····spare·····························
Row 3:    ACC_TRIG relay──VD5───┐
Row 4:    TRIG_POS──────────R18─┤(to row 9L)
Row 5:    COMP_INV node─────────┤(to row 11R)
Row 6:    PITCH_BOT─R27─GND    │
Row 7:    ═══CAP_MID═══════════╪══(R23, R28, VT4 arrive here)
Row 8:    ═══CAP_MID═══════════╪══(C10, C11, P2 depart here)
          ─────────────────────────────────────── GATE_IN→C8
          ┌────────────────────┐  ┌────────────────────┐
Row 9:    │ OSC_NONINV    p5   │  │  p4    VEE -12V    │
Row 10:   │ OSC_INV       p6   │  │  p3    HP_OUT      │
Row 11:   │ OSC_OUT       p7 DA1  p2    COMP_INV   │
Row 12:   │ VCC +12V      p8   │  │  p1    COMP_OUT    │
          └────────────────────┘  └────────────────────┘
Row 13:                           ACC_IN──R13──(from COMP_OUT)
Row 14:   VT2.C→+12V
Row 15:   VT2.B═══ACC_EMIT═══VT1.E──R12──(from ACC_IN)
Row 16:   VT2.E═ACC_TRIG═VD7↓    VT1.B──ACCENT_CV(JPS)
Row 17:   VT3.C→+12V             VT1.C→GND
Row 18:   VT3.B══ENV_CAP════════VT5.E(jumper L↔R)
          ──C6→GND, VD7↑(from ACC_TRIG)
Row 19:   VT3.E═ENV_BUF──C9↓    VT5.B──R10──(from PCV_ATT)
Row 20:                           VT5.C──P5(JPS)
Row 21:   SMOOTH──C9↑,VD8→GND,R23→CAP_MID,R24↓
Row 22:   VT4.C→CAP_MID(JW)     PCV_ATT──R10,R20
Row 23:   VT4.B──R24↑            PITCH_CV_IN──R20(JPS)
Row 24:   VT4.E──P7(JPS)
Row 25:   DEPTH_MID──R22→GND,P7(JPS)
Row 26:   ·····spare·····························
          ┌────────────────────┐  ┌────────────────────┐
Row 27:   │ TONE_OUT      p5   │  │  p4    VEE -12V    │
Row 28:   │ DIST_INV      p6   │  │  p3    GND         │
Row 29:   │ DIST_OUT      p7 DA2  p2    DECAY_INV  │
Row 30:   │ VCC +12V      p8   │  │  p1    DECAY_OUT   │
          └────────────────────┘  └────────────────────┘
Row 31:   DIST_OUT ext──R21↓,P3(JPS)
Row 32:   OUTPUT_J──R21↑,XS4(JPS)
Row 33-36:·····spare·····························
```

---

## COMPONENT COUNT VERIFICATION

| Category | Expected | Placed | Status |
|----------|----------|--------|--------|
| TL072 (DIP-8) | 2 | 2 | DA1 rows 9-12, DA2 rows 27-30 |
| Signal resistors | 20 | 20 | R1,R3,R4,R5,R8,R9,R10,R12,R13,R16,R18,R20,R21,R22,R23,R24,R25,R26,R27,R28 |
| Signal capacitors | 7 | 7 | C6,C7,C8,C9,C10,C11,C12 |
| Decoupling caps | 4 | 4 | C5,C13,C14,C15 (C1,C4 at power section) |
| 1N4148 diodes | 6 | 6 | VD3,VD4,VD5,VD6,VD7,VD8 |
| BC548 NPN | 3 | 3 | VT2,VT3,VT4 |
| BC558 PNP | 2 | 2 | VT1,VT5 |
| **Total on board** | **44** | **44** | **Uses 32 of 36 rows** |

**Not placed (power section rows 37-40):** R14, R15 (10R), C1, C4 (100nF),
C2, C3 (47uF), VD1, VD2 (1N5819)

**Not placed (unconfirmed):** R2, R7 (1K), VD9 (1N4148) — spare rows 33-36

---

## NOTES FOR CONSTRUCTION

1. **IC orientation:** Both ICs inserted upside-down relative to standard.
   The dot/notch marking pin 1 points DOWNWARD (toward higher row
   numbers). Pin 8 (VCC) faces the +12V rail on the left.

2. **Vertical components:** R5, R13, R26, R21, VD3, VD4, C7, R20 are all
   mounted vertically (1-row span = 2.54mm). Bend one lead 90 degrees,
   insert body upright.

3. **Long-span components:** R25, R28, R23 span 14-23 rows. Extend
   leads with wire if needed. Route above other components.

4. **Transistor orientation:** Verify E-B-C pinout before soldering.
   The flat side of the TO-92 package indicates pin orientation.
   Bend leads to 2.54mm spacing for adjacent row placement.

5. **Decoupling caps:** Solder C13, C14, C5, C15 as close as possible
   to the IC power pins. Ceramic 100nF caps are small enough to
   bridge between the IC pin row and the adjacent power rail.

6. **JPS wiring:** Run wires from the edge connector to the indicated
   rows AFTER all board components are soldered. This allows testing
   each section with clip leads before committing.

7. **Power section:** The Eurorack power header, 1N5819 protection
   diodes, 10R resistors, 47uF electrolytics, and 2 bypass caps
   go in rows 37-40 per the n8synth power layout.
