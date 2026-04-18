registerLayout("EDU Snare Drum", {
  "title": "MKI x ES EDU SNARE DRUM",
  "source": "docs/snare_drum_netlist.md",
  "board": "n8synth 10HP",
  "revision": "0.2",
  "notes": "Snare drum layout. 3x TL072 (rotated 180), 7 transistors, two signal paths (drum + noise) merged at mixer. Power conditioning handled by n8synth board rows 37-40. BOM discrepancy: mixer uses 8th 100K (R38) not in printed BOM.",
  "stages": [
    {
      "id": 1,
      "name": "ICs + Decoupling",
      "desc": "Install DA1, DA2, DA3 (all rotated 180deg). Add 6x 100nF decoupling caps close to each IC power pin.",
      "test": "Verify +12V on DA1/DA2/DA3 pin 8 (row 7/18/31 col e) via pwrL even rows. Verify -12V on pin 4 (row 4/15/28 col f) via pwrR even rows.",
      "color": "#888"
    },
    {
      "id": 2,
      "name": "Gate-to-Trigger",
      "desc": "HP filter (C15/R16) differentiates gate edge. Comparator (DA1A) with R6/R17 threshold converts to clean trigger. VD3 clamps negative spikes. Identical to kick drum.",
      "test": "Inject +5V gate at XS4 (TRIGGER IN). Probe COMP_OUT (row 7 right) for +12V pulse.",
      "color": "#e67e22"
    },
    {
      "id": 3,
      "name": "Accent + Trigger Routing",
      "desc": "R7 feeds COMP_OUT to accent network. VT1 (PNP) sets floor from accent CV; VT3 (NPN) buffers. VD4 blocks -12V. R25/R27 divide trigger for oscillator.",
      "test": "Probe ACC_TRIG (row 10 left). Should see shaped trigger pulse. ~1V at OSC_NONINV (row 4 left).",
      "color": "#e74c3c"
    },
    {
      "id": 4,
      "name": "Bridged-T Oscillator",
      "desc": "C12/C13 (33nF pair), R3 (910K) + R28 (1K) bridges, P5 (tune pot) + R34 (470R floor). Core drum sound, 100-200Hz.",
      "test": "With trigger working, probe OSC_OUT (row 6 left). Should hear decaying sine. P5 sets pitch.",
      "color": "#2ecc71"
    },
    {
      "id": 5,
      "name": "Decay Feedback",
      "desc": "DA2A inverts OSC_OUT via R13/R14. P1 (250K) sets feedback gain. R4 (470K) feeds back to CAP_MID, extending oscillation.",
      "test": "Turn DECAY pot. Oscillation should sustain longer at higher settings.",
      "color": "#3498db"
    },
    {
      "id": 6,
      "name": "Pitch Attack Envelope",
      "desc": "VD5 charges C10 (470nF) from COMP_OUT (full trigger). P4+R29 set charge rate (attack). R19/R15/VT4 divider drives pitch NPN. R35 (330R) sets max frequency ceiling.",
      "test": "Probe ENV_CAP (row 12 left). Decaying voltage on trigger. Hear pitch sweep down. P4 varies attack intensity.",
      "color": "#9b59b6"
    },
    {
      "id": 7,
      "name": "Pitch CV Buffer",
      "desc": "DA3A buffers attenuated pitch CV (P3 voltage divider) to isolate from envelope discharge path. R8 provides input protection.",
      "test": "Apply CV at XS2 (TUNE CV). P3 varies pitch effect. Higher CV = higher base pitch.",
      "color": "#1abc9c"
    },
    {
      "id": 8,
      "name": "Noise Generator",
      "desc": "VT5 (BC548 reverse-biased) creates white noise via avalanche breakdown. DA2B amplifies x46 (1M/22K). C1 AC-couples.",
      "test": "Probe NOISE_AMP_OUT (row 17 left). Should see uniform white noise signal.",
      "color": "#f39c12"
    },
    {
      "id": 9,
      "name": "Swing VCA + Noise Envelope",
      "desc": "VT6 NPN transistor VCA modulates noise volume. C14 AC-couples input, R2 biases. VD7 charges C11 (470nF) from COMP_OUT. P2 (100K A) sets both VCA collector load and decay rate. VD6 + C17/C18 clean output.",
      "test": "With noise + trigger, probe VCA_COLL (row 24). Should see percussive noise burst. P2 sets snare wire decay.",
      "color": "#e91e63"
    },
    {
      "id": 10,
      "name": "Snappy CV",
      "desc": "VT2 (PNP) provides CV-controlled discharge path for noise envelope. R22 (22K) limits discharge rate. At 0V CV = fast snappy burst.",
      "test": "Apply CV at XS3 (SNAPPY CV). Higher CV = longer noise tail, lower = tighter snare wires.",
      "color": "#795548"
    },
    {
      "id": 11,
      "name": "High Pass Filter",
      "desc": "2nd-order Sallen-Key HPF (C19/C20 1nF, R12 100K). VT7 emitter follower with R23 to -12V for headroom. R24 (22K) feedback adds resonance. C16 AC-couples output. fc ~3.4 kHz.",
      "test": "Probe HPF_AC (row 34). Noise should have lows removed, resonant bite around 3.4 kHz.",
      "color": "#00bcd4"
    },
    {
      "id": 12,
      "name": "Mixer + Output",
      "desc": "DA3B inverting summer: R38 (100K) drum input, R26 (10K) noise input, R18 (33K) feedback. R30 (1K) output series protection.",
      "test": "Full signal at XS5 (OUTPUT). Both drum body and snare wire noise audible. ~10Vpp.",
      "color": "#8bc34a"
    }
  ],
  "ics": [
    {
      "id": "DA1",
      "value": "TL072",
      "label": "Comparator + Oscillator",
      "stage": 1,
      "rotation": 180,
      "pins": [
        {
          "n": 5,
          "r": 4,
          "c": "e",
          "net": "OSC_NONINV"
        },
        {
          "n": 4,
          "r": 4,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 6,
          "r": 5,
          "c": "e",
          "net": "OSC_INV"
        },
        {
          "n": 3,
          "r": 5,
          "c": "f",
          "net": "HP_OUT"
        },
        {
          "n": 7,
          "r": 6,
          "c": "e",
          "net": "OSC_OUT"
        },
        {
          "n": 2,
          "r": 6,
          "c": "f",
          "net": "COMP_INV"
        },
        {
          "n": 8,
          "r": 7,
          "c": "e",
          "net": "VCC"
        },
        {
          "n": 1,
          "r": 7,
          "c": "f",
          "net": "COMP_OUT"
        }
      ]
    },
    {
      "id": "DA2",
      "value": "TL072",
      "label": "Decay Buffer + Noise Amp",
      "stage": 1,
      "rotation": 180,
      "pins": [
        {
          "n": 5,
          "r": 15,
          "c": "e",
          "net": "NOISE_BIAS"
        },
        {
          "n": 4,
          "r": 15,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 6,
          "r": 16,
          "c": "e",
          "net": "NOISE_AMP_INV"
        },
        {
          "n": 3,
          "r": 16,
          "c": "f",
          "net": "GND"
        },
        {
          "n": 7,
          "r": 17,
          "c": "e",
          "net": "NOISE_AMP_OUT"
        },
        {
          "n": 2,
          "r": 17,
          "c": "f",
          "net": "DECAY_INV"
        },
        {
          "n": 8,
          "r": 18,
          "c": "e",
          "net": "VCC"
        },
        {
          "n": 1,
          "r": 18,
          "c": "f",
          "net": "DECAY_OUT"
        }
      ]
    },
    {
      "id": "DA3",
      "value": "TL072",
      "label": "Pitch CV Buffer + Mixer",
      "stage": 1,
      "rotation": 180,
      "pins": [
        {
          "n": 5,
          "r": 28,
          "c": "e",
          "net": "GND"
        },
        {
          "n": 4,
          "r": 28,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 6,
          "r": 29,
          "c": "e",
          "net": "MIX_INV"
        },
        {
          "n": 3,
          "r": 29,
          "c": "f",
          "net": "PCV_BUF_IN"
        },
        {
          "n": 7,
          "r": 30,
          "c": "e",
          "net": "MIX_OUT"
        },
        {
          "n": 2,
          "r": 30,
          "c": "f",
          "net": "PCV_BUF_OUT"
        },
        {
          "n": 8,
          "r": 31,
          "c": "e",
          "net": "VCC"
        },
        {
          "n": 1,
          "r": 31,
          "c": "f",
          "net": "PCV_BUF_OUT"
        }
      ]
    }
  ],
  "twoPins": [
    {
      "_comment": "=== STAGE 1: IC DECOUPLING (6x 100nF) ==="
    },
    {
      "id": "C2",
      "type": "C",
      "value": "100nF",
      "r1": 7,
      "c1": "a",
      "r2": 7,
      "c2": "pwrL",
      "stage": 1
    },
    {
      "id": "C3",
      "type": "C",
      "value": "100nF",
      "r1": 4,
      "c1": "g",
      "r2": 3,
      "c2": "pwrR",
      "stage": 1
    },
    {
      "id": "C4",
      "type": "C",
      "value": "100nF",
      "r1": 18,
      "c1": "a",
      "r2": 19,
      "c2": "pwrL",
      "stage": 1
    },
    {
      "id": "C5",
      "type": "C",
      "value": "100nF",
      "r1": 15,
      "c1": "g",
      "r2": 15,
      "c2": "pwrR",
      "stage": 1
    },
    {
      "id": "C6",
      "type": "C",
      "value": "100nF",
      "r1": 31,
      "c1": "a",
      "r2": 31,
      "c2": "pwrL",
      "stage": 1
    },
    {
      "id": "C7",
      "type": "C",
      "value": "100nF",
      "r1": 28,
      "c1": "g",
      "r2": 27,
      "c2": "pwrR",
      "stage": 1
    },
    {
      "_comment": "=== STAGE 2: GATE-TO-TRIGGER (Block 1) ==="
    },
    {
      "id": "C15",
      "type": "C",
      "value": "10nF",
      "r1": 3,
      "c1": "g",
      "r2": 5,
      "c2": "g",
      "stage": 2,
      "_note": "GATE_IN (row 3R via JPS) to HP_OUT (DA1A pin3, row 5f)"
    },
    {
      "id": "R16",
      "type": "R",
      "value": "39K",
      "r1": 5,
      "c1": "h",
      "r2": 5,
      "c2": "pwrR",
      "stage": 2,
      "_note": "HP_OUT to GND"
    },
    {
      "id": "VD3",
      "type": "D",
      "value": "1N4148",
      "r1": 1,
      "c1": "pwrR",
      "r2": 5,
      "c2": "i",
      "stage": 2,
      "_note": "GND(anode, pwrR row 1) to HP_OUT(cathode, row 5R). 4-row span"
    },
    {
      "id": "R6",
      "type": "R",
      "value": "100K",
      "r1": 2,
      "c1": "pwrL",
      "r2": 2,
      "c2": "a",
      "stage": 2,
      "_note": "VCC through R6 to row 2L. Jumper JW1 bridges row 2L to COMP_INV at row 6R"
    },
    {
      "id": "R17",
      "type": "R",
      "value": "33K",
      "r1": 6,
      "c1": "h",
      "r2": 7,
      "c2": "pwrR",
      "stage": 2,
      "_note": "COMP_INV to GND. Threshold divider with R6"
    },
    {
      "_comment": "=== STAGE 3: ACCENT + TRIGGER ROUTING (Blocks 2+3) ==="
    },
    {
      "id": "R7",
      "type": "R",
      "value": "100K",
      "r1": 7,
      "c1": "g",
      "r2": 8,
      "c2": "g",
      "stage": 3,
      "_note": "COMP_OUT (row 7R) to ACC_IN (row 8R)"
    },
    {
      "id": "R5",
      "type": "R",
      "value": "120K",
      "r1": 8,
      "c1": "i",
      "r2": 9,
      "c2": "pwrR",
      "stage": 3,
      "_note": "ACC_IN to GND"
    },
    {
      "id": "R20",
      "type": "R",
      "value": "22K",
      "r1": 8,
      "c1": "h",
      "r2": 9,
      "c2": "h",
      "stage": 3,
      "_note": "ACC_IN (row 8R) to ACC_EMIT (row 9R)"
    },
    {
      "id": "VD4",
      "type": "D",
      "value": "1N4148",
      "r1": 10,
      "c1": "a",
      "r2": 11,
      "c2": "a",
      "stage": 3,
      "_note": "ACC_TRIG(anode, row 10L) to TRIG_POS(cathode, row 11L). Blocking diode"
    },
    {
      "id": "R25",
      "type": "R",
      "value": "10K",
      "r1": 11,
      "c1": "b",
      "r2": 4,
      "c2": "b",
      "stage": 3,
      "_note": "TRIG_POS (row 11L) to OSC_NONINV (row 4L, DA1B pin5)"
    },
    {
      "id": "R27",
      "type": "R",
      "value": "1K",
      "r1": 4,
      "c1": "a",
      "r2": 5,
      "c2": "pwrL",
      "stage": 3,
      "_note": "OSC_NONINV to GND. Voltage divider bottom with R25"
    },
    {
      "_comment": "=== STAGE 4: OSCILLATOR (Block 4) ==="
    },
    {
      "id": "C12",
      "type": "C",
      "value": "33nF",
      "r1": 6,
      "c1": "d",
      "r2": 8,
      "c2": "d",
      "stage": 4,
      "_note": "OSC_OUT (row 6L) to CAP_MID (row 8L)"
    },
    {
      "id": "C13",
      "type": "C",
      "value": "33nF",
      "r1": 8,
      "c1": "c",
      "r2": 5,
      "c2": "c",
      "stage": 4,
      "_note": "CAP_MID (row 8L) to OSC_INV (row 5L, DA1B pin6)"
    },
    {
      "id": "R3",
      "type": "R",
      "value": "910K",
      "r1": 6,
      "c1": "a",
      "r2": 5,
      "c2": "a",
      "stage": 4,
      "_note": "OSC_OUT to OSC_INV bridge"
    },
    {
      "id": "R28",
      "type": "R",
      "value": "1K",
      "r1": 6,
      "c1": "b",
      "r2": 5,
      "c2": "b",
      "stage": 4,
      "_note": "OSC_OUT to OSC_INV bridge (1K)"
    },
    {
      "id": "R34",
      "type": "R",
      "value": "470R",
      "r1": 8,
      "c1": "a",
      "r2": 9,
      "c2": "pwrL",
      "stage": 4,
      "_note": "CAP_MID/TUNE bottom to GND. P5(TUNE) connects row 8L via JPS"
    },
    {
      "_comment": "=== STAGE 5: DECAY FEEDBACK (Block 5) ==="
    },
    {
      "id": "R13",
      "type": "R",
      "value": "47K",
      "r1": 6,
      "c1": "c",
      "r2": 17,
      "c2": "g",
      "stage": 5,
      "_note": "OSC_OUT (row 6L) to DECAY_INV (row 17R, DA2A pin2). Long span - use wire if needed"
    },
    {
      "id": "R14",
      "type": "R",
      "value": "47K",
      "r1": 17,
      "c1": "h",
      "r2": 18,
      "c2": "g",
      "stage": 5,
      "_note": "DECAY_INV to DECAY_OUT (DA2A pin1, row 18R)"
    },
    {
      "id": "R4",
      "type": "R",
      "value": "470K",
      "r1": 18,
      "c1": "h",
      "r2": 8,
      "c2": "b",
      "stage": 5,
      "_note": "DECAY_OUT (row 18R) to CAP_MID (row 8L). Very long span - will need wire"
    },
    {
      "_comment": "=== STAGE 6: PITCH ATTACK ENVELOPE (Block 6) ==="
    },
    {
      "id": "VD5",
      "type": "D",
      "value": "1N4148",
      "r1": 20,
      "c1": "h",
      "r2": 21,
      "c2": "h",
      "stage": 6,
      "_note": "COMP_OUT(anode, row 20R via JW_COMPOUT from 7R) to DIODE_OUT(cathode, row 21R). P4.1 also on 21R"
    },
    {
      "id": "R29",
      "type": "R",
      "value": "1K",
      "r1": 12,
      "c1": "i",
      "r2": 14,
      "c2": "g",
      "stage": 6,
      "_note": "WIPER_OUT (row 12R, P4.w) to ENV_CAP (row 14R). Attack series R"
    },
    {
      "id": "C10",
      "type": "C",
      "value": "470nF",
      "r1": 14,
      "c1": "h",
      "r2": 13,
      "c2": "pwrR",
      "stage": 6,
      "_note": "ENV_CAP (row 14R) to GND. Pitch envelope timing cap"
    },
    {
      "id": "R19",
      "type": "R",
      "value": "27K",
      "r1": 14,
      "c1": "i",
      "r2": 13,
      "c2": "g",
      "stage": 6,
      "_note": "ENV_CAP (row 14R) to ENV_DIV (row 13R)"
    },
    {
      "id": "R15",
      "type": "R",
      "value": "47K",
      "r1": 13,
      "c1": "h",
      "r2": 30,
      "c2": "g",
      "stage": 6,
      "_note": "ENV_DIV to PCV_BUF_OUT (DA3A output, row 30R). Long span"
    },
    {
      "id": "R35",
      "type": "R",
      "value": "330R",
      "r1": 14,
      "c1": "b",
      "r2": 13,
      "c2": "pwrL",
      "stage": 6,
      "_note": "PITCH_NPN_E (VT4 emitter) to GND. Min frequency floor"
    },
    {
      "_comment": "=== STAGE 7: PITCH CV BUFFER (Block 7) ==="
    },
    {
      "id": "R8",
      "type": "R",
      "value": "100K",
      "r1": 34,
      "c1": "g",
      "r2": 33,
      "c2": "g",
      "stage": 7,
      "_note": "PITCH_CV_IN (XS2 via JPS, row 34R) through R8 to P3 input (row 33R). Series protection"
    },
    {
      "_comment": "=== STAGE 8: NOISE GENERATOR (Block 8) ==="
    },
    {
      "id": "R9",
      "type": "R",
      "value": "100K",
      "r1": 20,
      "c1": "pwrL",
      "r2": 19,
      "c2": "a",
      "stage": 8,
      "_note": "VCC to VT5 emitter (reversed). Noise source supply"
    },
    {
      "id": "R10",
      "type": "R",
      "value": "100K",
      "r1": 21,
      "c1": "a",
      "r2": 21,
      "c2": "pwrL",
      "stage": 8,
      "_note": "NOISE_COLL (VT5 collector) to GND. Collector load"
    },
    {
      "id": "C1",
      "type": "C",
      "value": "1uF",
      "r1": 21,
      "c1": "b",
      "r2": 15,
      "c2": "b",
      "stage": 8,
      "_note": "NOISE_COLL (row 21L) to NOISE_BIAS (row 15L, DA2B pin5). AC coupling"
    },
    {
      "id": "R11",
      "type": "R",
      "value": "100K",
      "r1": 15,
      "c1": "c",
      "r2": 15,
      "c2": "pwrL",
      "stage": 8,
      "_note": "NOISE_BIAS to GND. DC bias for amp input"
    },
    {
      "id": "R1",
      "type": "R",
      "value": "1M",
      "r1": 17,
      "c1": "a",
      "r2": 16,
      "c2": "a",
      "stage": 8,
      "_note": "NOISE_AMP_OUT (row 17L) to NOISE_AMP_INV (row 16L). Feedback R"
    },
    {
      "id": "R21",
      "type": "R",
      "value": "22K",
      "r1": 16,
      "c1": "b",
      "r2": 17,
      "c2": "pwrL",
      "stage": 8,
      "_note": "NOISE_AMP_INV to GND. Gain-setting R (gain = 1 + 1M/22K = 46)"
    },
    {
      "_comment": "=== STAGE 9: VCA + NOISE ENVELOPE (Blocks 9+10) ==="
    },
    {
      "id": "C14",
      "type": "C",
      "value": "33nF",
      "r1": 17,
      "c1": "b",
      "r2": 22,
      "c2": "b",
      "stage": 9,
      "_note": "NOISE_AMP_OUT (row 17L) to VCA_BASE (row 22L). AC coupling"
    },
    {
      "id": "R2",
      "type": "R",
      "value": "1M",
      "r1": 22,
      "c1": "a",
      "r2": 22,
      "c2": "pwrL",
      "stage": 9,
      "_note": "VCA_BASE to VCC. Bias for VT6"
    },
    {
      "id": "VD6",
      "type": "D",
      "value": "1N4148",
      "r1": 19,
      "c1": "pwrR",
      "r2": 24,
      "c2": "i",
      "stage": 9,
      "_note": "GND(anode, pwrR row 19) to VCA_COLL(cathode, row 24R). 5-row span"
    },
    {
      "id": "C17",
      "type": "C",
      "value": "2.2nF",
      "r1": 24,
      "c1": "h",
      "r2": 23,
      "c2": "pwrR",
      "stage": 9,
      "_note": "VCA_COLL to GND. HF filter"
    },
    {
      "id": "C18",
      "type": "C",
      "value": "2.2nF",
      "r1": 23,
      "c1": "i",
      "r2": 21,
      "c2": "pwrR",
      "stage": 9,
      "_note": "VCA_COLL (row 23R via JW10 bridge) to GND. HF filter (2nd)"
    },
    {
      "id": "VD7",
      "type": "D",
      "value": "1N4148",
      "r1": 7,
      "c1": "i",
      "r2": 25,
      "c2": "g",
      "stage": 9,
      "_note": "COMP_OUT(anode, row 7R) to NOISE_ENV_CAP(cathode, row 25R). Long span"
    },
    {
      "id": "C11",
      "type": "C",
      "value": "470nF",
      "r1": 25,
      "c1": "i",
      "r2": 25,
      "c2": "pwrR",
      "stage": 9,
      "_note": "NOISE_ENV_CAP to GND. Noise envelope timing cap"
    },
    {
      "_comment": "=== STAGE 10: SNAPPY CV (Block 10 continued) ==="
    },
    {
      "id": "R22",
      "type": "R",
      "value": "22K",
      "r1": 27,
      "c1": "a",
      "r2": 27,
      "c2": "pwrL",
      "stage": 10,
      "_note": "VT2 collector (snappy discharge) to GND"
    },
    {
      "_comment": "=== STAGE 11: HIGH PASS FILTER (Block 11) ==="
    },
    {
      "id": "C19",
      "type": "C",
      "value": "1nF",
      "r1": 24,
      "c1": "g",
      "r2": 33,
      "c2": "b",
      "stage": 11,
      "_note": "VCA_COLL (row 24R) to HPF_A (row 33L). 1st HPF cap"
    },
    {
      "id": "C20",
      "type": "C",
      "value": "1nF",
      "r1": 33,
      "c1": "c",
      "r2": 32,
      "c2": "c",
      "stage": 11,
      "_note": "HPF_A (row 33L) to VT7 base area (row 32L)"
    },
    {
      "id": "R12",
      "type": "R",
      "value": "100K",
      "r1": 33,
      "c1": "a",
      "r2": 33,
      "c2": "pwrL",
      "stage": 11,
      "_note": "HPF_A to GND"
    },
    {
      "id": "R23",
      "type": "R",
      "value": "22K",
      "r1": 34,
      "c1": "a",
      "r2": 34,
      "c2": "pwrR",
      "stage": 11,
      "_note": "HPF_EMIT (VT7 emitter, row 34L) to VEE (-12V). Headroom bias"
    },
    {
      "id": "R24",
      "type": "R",
      "value": "22K",
      "r1": 34,
      "c1": "b",
      "r2": 33,
      "c2": "d",
      "stage": 11,
      "_note": "HPF_EMIT to HPF_A. Resonance feedback"
    },
    {
      "id": "C16",
      "type": "C",
      "value": "10nF",
      "r1": 34,
      "c1": "c",
      "r2": 35,
      "c2": "a",
      "stage": 11,
      "_note": "HPF_EMIT to HPF_AC (row 35L). AC coupling output"
    },
    {
      "_comment": "=== STAGE 12: MIXER + OUTPUT (Block 12) ==="
    },
    {
      "id": "R38",
      "type": "R",
      "value": "100K",
      "r1": 26,
      "c1": "b",
      "r2": 29,
      "c2": "d",
      "stage": 12,
      "_note": "OSC_OUT (row 26L via JW_OSCMIX from row 6) to MIX_INV (row 29L). Drum body input to mixer"
    },
    {
      "id": "R26",
      "type": "R",
      "value": "10K",
      "r1": 35,
      "c1": "b",
      "r2": 29,
      "c2": "b",
      "stage": 12,
      "_note": "HPF_AC (row 35L) to MIX_INV (row 29L). Noise input to mixer"
    },
    {
      "id": "R18",
      "type": "R",
      "value": "33K",
      "r1": 29,
      "c1": "c",
      "r2": 30,
      "c2": "c",
      "stage": 12,
      "_note": "MIX_INV (row 29L) to MIX_OUT (row 30L). Mixer feedback R"
    },
    {
      "id": "R30",
      "type": "R",
      "value": "1K",
      "r1": 30,
      "c1": "a",
      "r2": 36,
      "c2": "a",
      "stage": 12,
      "_note": "MIX_OUT (row 30L) to OUTPUT_J (row 36L). Output series protection"
    }
  ],
  "transistors": [
    {
      "id": "VT1",
      "subtype": "PNP",
      "value": "BC558",
      "eR": 9,
      "eC": "i",
      "bR": 10,
      "bC": "i",
      "cR": 11,
      "cC": "i",
      "stage": 3,
      "_note": "E=ACC_EMIT(9R), B=ACCENT_CV_IN(10R, from XS1 via JPS), C=GND(11R, wire to gndR). PNP accent limiter"
    },
    {
      "id": "VT3",
      "subtype": "NPN",
      "value": "BC548",
      "eR": 10,
      "eC": "c",
      "bR": 9,
      "bC": "c",
      "cR": 8,
      "cC": "c",
      "stage": 3,
      "_note": "E=ACC_TRIG(10L), B=ACC_EMIT(9L via jumper from 9R), C=VCC(8L, wire to +12v). NPN accent buffer"
    },
    {
      "id": "VT4",
      "subtype": "NPN",
      "value": "BC548",
      "eR": 14,
      "eC": "c",
      "bR": 13,
      "bC": "c",
      "cR": 12,
      "cC": "c",
      "stage": 6,
      "_note": "E=PITCH_NPN_E(14L, to R35/GND), B=ENV_DIV(13L via jumper from 13R), C=CAP_MID(12L via jumper to 8L). Pitch modulation NPN"
    },
    {
      "id": "VT5",
      "subtype": "NPN",
      "value": "BC548",
      "eR": 19,
      "eC": "b",
      "bR": 20,
      "bC": "b",
      "cR": 21,
      "cC": "c",
      "stage": 8,
      "_note": "WIRED BACKWARDS! E=VCC(via R9, 19L), B=floating/GND(20L), C=NOISE_COLL(21L). Reverse-biased for avalanche noise"
    },
    {
      "id": "VT6",
      "subtype": "NPN",
      "value": "BC548",
      "eR": 23,
      "eC": "c",
      "bR": 22,
      "bC": "c",
      "cR": 23,
      "cC": "g",
      "stage": 9,
      "_note": "E=GND(23L\u2192gndL), B=VCA_BASE(22L), C=VCA_COLL(23R\u2192row 24R). Swing VCA. CHALLENGE: emitter and collector same row different sides"
    },
    {
      "id": "VT2",
      "subtype": "PNP",
      "value": "BC558",
      "eR": 25,
      "eC": "j",
      "bR": 26,
      "bC": "j",
      "cR": 27,
      "cC": "j",
      "stage": 10,
      "_note": "E=NOISE_ENV_CAP(25R), B=SNAPPY_CV_IN(26R from XS3), C=R22(27R\u219227L via jumper). PNP snappy gate"
    },
    {
      "id": "VT7",
      "subtype": "NPN",
      "value": "BC548",
      "eR": 34,
      "eC": "d",
      "bR": 32,
      "bC": "d",
      "cR": 32,
      "cC": "pwrL",
      "stage": 11,
      "_note": "E=HPF_EMIT(34L), B=HPF input(32L from C20), C=VCC. Emitter follower. CHALLENGE: B and C same row - may need to split across rows"
    }
  ],
  "jumpers": [
    {
      "id": "JW1",
      "r1": 2,
      "c1": "b",
      "r2": 6,
      "c2": "g",
      "label": "R6\u2192COMP_INV",
      "stage": 2,
      "_note": "Connect R6 output (row 2L) to COMP_INV (row 6R, DA1A pin2)"
    },
    {
      "id": "JW2",
      "r1": 9,
      "c1": "d",
      "r2": 9,
      "c2": "g",
      "label": "ACC_EMIT L\u2194R",
      "stage": 3,
      "_note": "Bridge ACC_EMIT across centre gap so VT3 base (9L) connects to VT1 emitter (9R)"
    },
    {
      "id": "JW3",
      "r1": 11,
      "c1": "i",
      "r2": 11,
      "c2": "pwrR",
      "label": "VT1.C\u2192GND",
      "stage": 3,
      "_note": "VT1 collector (row 11R) to GND rail"
    },
    {
      "id": "JW4",
      "r1": 13,
      "c1": "i",
      "r2": 13,
      "c2": "b",
      "label": "ENV_DIV L\u2194R",
      "stage": 6,
      "_note": "Bridge ENV_DIV so R19 output (13R) reaches VT4 base (13L)"
    },
    {
      "id": "JW5",
      "r1": 12,
      "c1": "b",
      "r2": 8,
      "c2": "b",
      "label": "VT4.C\u2192CAP_MID",
      "stage": 6,
      "_note": "CHALLENGE: VT4 collector (12L via row 12c) to CAP_MID (8L). May need separate wire"
    },
    {
      "id": "JW6",
      "r1": 16,
      "c1": "g",
      "r2": 17,
      "c2": "pwrR",
      "label": "DA2A.+\u2192GND",
      "stage": 5,
      "_note": "DA2A non-inv input (pin 3, row 16R) to GND"
    },
    {
      "id": "JW7",
      "r1": 28,
      "c1": "a",
      "r2": 29,
      "c2": "pwrL",
      "label": "DA3B.+\u2192GND",
      "stage": 12,
      "_note": "DA3B non-inv input (pin 5, row 28L) to GND"
    },
    {
      "id": "JW8",
      "r1": 30,
      "c1": "g",
      "r2": 31,
      "c2": "g",
      "label": "PCV_BUF follower",
      "stage": 7,
      "_note": "Connect DA3A output (pin1, row 31R) to DA3A inv input (pin2, row 30R) for unity follower"
    },
    {
      "id": "JW9",
      "r1": 23,
      "c1": "b",
      "r2": 23,
      "c2": "pwrL",
      "label": "VT6.E\u2192GND",
      "stage": 9,
      "_note": "VCA transistor emitter to GND"
    },
    {
      "id": "JW10",
      "r1": 23,
      "c1": "h",
      "r2": 24,
      "c2": "j",
      "label": "VT6.C\u2192VCA_COLL",
      "stage": 9,
      "_note": "VCA collector (row 23R) to VCA_COLL node (row 24R)"
    },
    {
      "id": "JW11",
      "r1": 27,
      "c1": "i",
      "r2": 27,
      "c2": "b",
      "label": "VT2.C L\u2194R",
      "stage": 10,
      "_note": "VT2 collector (27R) to R22 (27L)"
    },
    {
      "id": "JW_COMPOUT",
      "r1": 7,
      "c1": "j",
      "r2": 20,
      "c2": "g",
      "label": "COMP_OUT\u2192VD5",
      "stage": 6,
      "_note": "Carry COMP_OUT (row 7R) to VD5 anode area (row 20R). Long wire"
    },
    {
      "id": "JW_OSCMIX",
      "r1": 6,
      "c1": "d",
      "r2": 26,
      "c2": "a",
      "label": "OSC_OUT\u2192mixer",
      "stage": 12,
      "_note": "Carry OSC_OUT (row 6L) to R38 area (row 26L). Long wire for drum body to mixer"
    }
  ],
  "powerWires": [
    {
      "r1": 7,
      "c1": "b",
      "r2": 6,
      "c2": "pwrL",
      "label": "DA1 VCC",
      "stage": 1
    },
    {
      "r1": 4,
      "c1": "h",
      "r2": 4,
      "c2": "pwrR",
      "label": "DA1 VEE",
      "stage": 1
    },
    {
      "r1": 18,
      "c1": "b",
      "r2": 18,
      "c2": "pwrL",
      "label": "DA2 VCC",
      "stage": 1
    },
    {
      "r1": 15,
      "c1": "h",
      "r2": 16,
      "c2": "pwrR",
      "label": "DA2 VEE",
      "stage": 1
    },
    {
      "r1": 31,
      "c1": "b",
      "r2": 30,
      "c2": "pwrL",
      "label": "DA3 VCC",
      "stage": 1
    },
    {
      "r1": 28,
      "c1": "h",
      "r2": 28,
      "c2": "pwrR",
      "label": "DA3 VEE",
      "stage": 1
    },
    {
      "r1": 8,
      "c1": "d",
      "r2": 8,
      "c2": "pwrL",
      "label": "VT3.C\u2192VCC",
      "stage": 3
    },
    {
      "_removed": "VT7.C→VCC power wire removed — VT7 collector connects directly to pwrL row 32 via cC pin. Any wire from row 32L would short VT7 base to VCC."
    }
  ],
  "jpsWires": [
    {
      "id": "XS4",
      "label": "TRIGGER IN",
      "row": 3,
      "col": "h",
      "stage": 2
    },
    {
      "id": "XS1",
      "label": "ACCENT CV",
      "row": 10,
      "col": "j",
      "stage": 3
    },
    {
      "id": "P5.1",
      "label": "TUNE pin1",
      "row": 8,
      "col": "c",
      "stage": 4
    },
    {
      "id": "P5.w",
      "label": "TUNE wiper+GND",
      "row": 8,
      "col": "d",
      "stage": 4,
      "_note": "CHALLENGE: P5 wiper shorted to pin3. Pin1 to CAP_MID, wiper/pin3 to R34 then GND"
    },
    {
      "id": "P1.1",
      "label": "DECAY pin1",
      "row": 17,
      "col": "i",
      "stage": 5
    },
    {
      "id": "P1.w",
      "label": "DECAY wiper",
      "row": 18,
      "col": "i",
      "stage": 5
    },
    {
      "id": "P4.1",
      "label": "ATTACK pin1",
      "row": 21,
      "col": "j",
      "stage": 6,
      "_note": "DIODE_OUT (row 21R). VD5 cathode also on 21R. Pot resistance leads to P4.w"
    },
    {
      "id": "P4.w",
      "label": "ATTACK wiper",
      "row": 12,
      "col": "h",
      "stage": 6,
      "_note": "WIPER_OUT (row 12R). R29 bridges from here to ENV_CAP on row 14R"
    },
    {
      "id": "XS2",
      "label": "TUNE CV IN",
      "row": 34,
      "col": "i",
      "stage": 7
    },
    {
      "id": "P3.1",
      "label": "PITCH CV pin1",
      "row": 34,
      "col": "j",
      "stage": 7
    },
    {
      "id": "P3.w",
      "label": "PITCH CV wiper",
      "row": 29,
      "col": "g",
      "stage": 7
    },
    {
      "id": "P3.3",
      "label": "PITCH CV gnd",
      "row": 29,
      "col": "pwrR",
      "stage": 7
    },
    {
      "id": "P2.1",
      "label": "SNAPPY pin1",
      "row": 25,
      "col": "j",
      "stage": 9,
      "_note": "NOISE_ENV_CAP to P2. P2 wiper/pin2 to VCA_COLL"
    },
    {
      "id": "P2.w",
      "label": "SNAPPY wiper",
      "row": 24,
      "col": "j",
      "stage": 9
    },
    {
      "id": "XS3",
      "label": "SNAPPY CV IN",
      "row": 26,
      "col": "i",
      "stage": 10
    },
    {
      "id": "XS5",
      "label": "AUDIO OUTPUT",
      "row": 36,
      "col": "b",
      "stage": 12
    }
  ],
  "netLabels": [
    {
      "r": 2,
      "side": "L",
      "name": "VCC_R6",
      "stage": 2
    },
    {
      "r": 3,
      "side": "R",
      "name": "GATE_IN",
      "stage": 2
    },
    {
      "r": 4,
      "side": "L",
      "name": "OSC_NONINV",
      "stage": 1
    },
    {
      "r": 5,
      "side": "L",
      "name": "OSC_INV",
      "stage": 1
    },
    {
      "r": 5,
      "side": "R",
      "name": "HP_OUT",
      "stage": 2
    },
    {
      "r": 6,
      "side": "L",
      "name": "OSC_OUT",
      "stage": 1
    },
    {
      "r": 6,
      "side": "R",
      "name": "COMP_INV",
      "stage": 2
    },
    {
      "r": 7,
      "side": "R",
      "name": "COMP_OUT",
      "stage": 2
    },
    {
      "r": 8,
      "side": "L",
      "name": "CAP_MID",
      "stage": 4
    },
    {
      "r": 8,
      "side": "R",
      "name": "ACC_IN",
      "stage": 3
    },
    {
      "r": 9,
      "side": "R",
      "name": "ACC_EMIT",
      "stage": 3
    },
    {
      "r": 10,
      "side": "L",
      "name": "ACC_TRIG",
      "stage": 3
    },
    {
      "r": 11,
      "side": "L",
      "name": "TRIG_POS",
      "stage": 3
    },
    {
      "r": 12,
      "side": "R",
      "name": "WIPER_OUT",
      "stage": 6
    },
    {
      "r": 14,
      "side": "R",
      "name": "ENV_CAP",
      "stage": 6
    },
    {
      "r": 21,
      "side": "R",
      "name": "DIODE_OUT",
      "stage": 6
    },
    {
      "r": 26,
      "side": "L",
      "name": "OSC_MIX",
      "stage": 12
    },
    {
      "r": 13,
      "side": "R",
      "name": "ENV_DIV",
      "stage": 6
    },
    {
      "r": 14,
      "side": "L",
      "name": "PITCH_NPN_E",
      "stage": 6
    },
    {
      "r": 15,
      "side": "L",
      "name": "NOISE_BIAS",
      "stage": 8
    },
    {
      "r": 16,
      "side": "L",
      "name": "NOISE_AMP_INV",
      "stage": 8
    },
    {
      "r": 17,
      "side": "L",
      "name": "NOISE_AMP_OUT",
      "stage": 8
    },
    {
      "r": 17,
      "side": "R",
      "name": "DECAY_INV",
      "stage": 5
    },
    {
      "r": 18,
      "side": "R",
      "name": "DECAY_OUT",
      "stage": 5
    },
    {
      "r": 21,
      "side": "L",
      "name": "NOISE_COLL",
      "stage": 8
    },
    {
      "r": 22,
      "side": "L",
      "name": "VCA_BASE",
      "stage": 9
    },
    {
      "r": 24,
      "side": "R",
      "name": "VCA_COLL",
      "stage": 9
    },
    {
      "r": 25,
      "side": "R",
      "name": "NOISE_ENV_CAP",
      "stage": 9
    },
    {
      "r": 27,
      "side": "L",
      "name": "VT2_COLL",
      "stage": 10
    },
    {
      "r": 29,
      "side": "L",
      "name": "MIX_INV",
      "stage": 12
    },
    {
      "r": 29,
      "side": "R",
      "name": "PCV_BUF_IN",
      "stage": 7
    },
    {
      "r": 30,
      "side": "L",
      "name": "MIX_OUT",
      "stage": 12
    },
    {
      "r": 30,
      "side": "R",
      "name": "PCV_BUF_OUT",
      "stage": 7
    },
    {
      "r": 33,
      "side": "L",
      "name": "HPF_A",
      "stage": 11
    },
    {
      "r": 34,
      "side": "L",
      "name": "HPF_EMIT",
      "stage": 11
    },
    {
      "r": 35,
      "side": "L",
      "name": "HPF_AC",
      "stage": 11
    },
    {
      "r": 36,
      "side": "L",
      "name": "OUTPUT_J",
      "stage": 12
    }
  ],
  "_challenges": [
    "R4 (470K decay feedback) spans from row 18R to row 8L \u2014 very long. Consider using a jumper wire.",
    "R13 (47K decay input) spans from row 6L to row 17R \u2014 long. May need wire assist.",
    "R15 (47K env divider) spans from row 13R to row 30R \u2014 very long across board.",
    "JW_COMPOUT spans row 7R to row 20R (13 rows) \u2014 long wire carrying COMP_OUT to VD5 anode.",
    "VD7 spans from row 7R to row 25R \u2014 very long. Use wire to carry COMP_OUT to noise env area.",
    "R25 (10K trigger divider) spans from row 11L to row 4L \u2014 7 rows. Doable with leads.",
    "C19 (1nF HPF) spans from row 24R to row 33L \u2014 across centre gap AND long distance. Wire needed.",
    "JW_OSCMIX spans row 6L to row 26L (20 rows) \u2014 long wire carrying OSC_OUT to mixer R38.",
    "VT6 (VCA NPN) has E and C on same row different sides \u2014 may need creative placement.",
    "VT7 (HPF follower) has B and C listed on same row \u2014 split across rows if needed.",
    "P5 (TUNE) connects to CAP_MID. With wiper+pin3 shorted, both pins go to row 8L area.",
    "COMP_OUT (row 7R) feeds three destinations: accent R7, pitch env JW_COMPOUT, noise env VD7. All 3 share row 7 right side.",
    "VD3 clamp diode spans 4 rows (pwrR row 1 to 5,i). VD6 clamp diode spans 5 rows (pwrR row 19 to 24,i). Both doable with 1N4148 leads.",
    "C18 (2.2nF) signal leg on row 23R (VCA_COLL via JW10), GND on pwrR row 21. 2-row span.",
    "The spare board mount strip (rows 37-40 area) can be used for overflow if needed."
  ]
});