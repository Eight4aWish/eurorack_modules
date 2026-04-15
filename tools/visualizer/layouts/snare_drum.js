registerLayout("MKI x ES EDU SNARE DRUM", {
  "title": "MKI x ES EDU SNARE DRUM",
  "source": "docs/snare_drum_netlist.md",
  "board": "n8synth 6HP",
  "revision": "0.1",
  "notes": "Auto-generated skeleton \u2014 row/column placements need to be filled in.",
  "stages": [
    {
      "id": 1,
      "name": "ICs + Power",
      "desc": "Install DA1, DA2, DA3 and decoupling caps. Wire IC power pins to rails.",
      "test": "Verify +12V/-12V on IC power pins.",
      "color": "#888"
    },
    {
      "id": 2,
      "name": "Gate-to-Trigger Converter",
      "desc": "Converts a sustained gate into a short trigger pulse. High-pass filter\ndifferentiates the gate edge; comparator converts it to a clean \u00b112V pulse.\nIdentical topology to kick drum.",
      "test": "",
      "color": "#e67e22"
    },
    {
      "id": 3,
      "name": "Accent CV Section",
      "desc": "Modulates trigger amplitude via external accent CV. PNP emitter follower\nsets voltage floor, NPN buffers the result.",
      "test": "",
      "color": "#e74c3c"
    },
    {
      "id": 4,
      "name": "Trigger Routing to Oscillator",
      "desc": "Routes accented trigger to oscillator input through blocking diode and\n10K/1K voltage divider. Different divider ratio from kick (10K/1K vs 100K/14K).",
      "test": "",
      "color": "#2ecc71"
    },
    {
      "id": 5,
      "name": "Bridged-T Percussive Oscillator",
      "desc": "Core drum sound source. Bridged-T network produces decaying sine wave when\ntriggered. Tuned for 100-200 Hz range (snare body). Same topology as kick\nbut with 33nF caps (vs 15nF) for lower starting fre",
      "test": "",
      "color": "#3498db"
    },
    {
      "id": 6,
      "name": "Decay Feedback",
      "desc": "Inverting buffer feeds oscillator output back to CAP_MID, extending decay.\nSame topology as kick but with 250K DECAY pot (vs 1M).",
      "test": "",
      "color": "#9b59b6"
    },
    {
      "id": 7,
      "name": "Pitch Attack Envelope",
      "desc": "RC envelope for pitch modulation. Charges from FULL trigger (COMP_OUT,\nnot accented trigger) for consistent attack at all accent levels.\nNPN transistor provides voltage-controlled resistance from CAP_",
      "test": "",
      "color": "#1abc9c"
    },
    {
      "id": 8,
      "name": "Pitch CV Input + Buffer",
      "desc": "External pitch CV sets the transistor's operating point, keeping it open\nto varying degrees. An op-amp buffer isolates the attenuator impedance\nfrom the envelope discharge path.",
      "test": "",
      "color": "#f39c12"
    },
    {
      "id": 9,
      "name": "Noise Generator",
      "desc": "White noise source using reverse-biased BC548 (avalanche breakdown).\nAC-coupled and amplified \u00d746 by non-inverting op-amp stage.",
      "test": "",
      "color": "#e91e63"
    },
    {
      "id": 10,
      "name": "Swing Type VCA",
      "desc": "NPN transistor-based amplitude modulator (Roland 606/808 style). The\ncollector voltage is set by the envelope CV \u2014 when transistor is in cutoff,\noutput = CV level. Signal enters at base through AC cou",
      "test": "",
      "color": "#00bcd4"
    },
    {
      "id": 11,
      "name": "Noise Envelope + Snappy Control",
      "desc": "Simple RC envelope for noise burst. Charges from full trigger (fast, no\nseries resistance). The 100K A-taper pot does double duty as VCA collector\nload AND envelope discharge path. PNP transistor prov",
      "test": "",
      "color": "#888"
    },
    {
      "id": 12,
      "name": "High Pass Filter",
      "desc": "Second-order Sallen-Key high pass filter removes low end from noise path.\nUses NPN emitter follower (instead of op-amp) as active element. Feedback\nfrom emitter to filter node introduces resonance.",
      "test": "",
      "color": "#e67e22"
    },
    {
      "id": 13,
      "name": "Mixer + Output",
      "desc": "Inverting summing amplifier mixes drum oscillator and filtered noise.\nIndividual input resistors set the gain for each path.",
      "test": "",
      "color": "#e74c3c"
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
          "n": 1,
          "r": null,
          "c": "f",
          "net": "COMP_OUT"
        },
        {
          "n": 2,
          "r": null,
          "c": "f",
          "net": "COMP_INV"
        },
        {
          "n": 3,
          "r": null,
          "c": "f",
          "net": "HP_OUT"
        },
        {
          "n": 4,
          "r": null,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 5,
          "r": null,
          "c": "e",
          "net": "OSC_NONINV"
        },
        {
          "n": 6,
          "r": null,
          "c": "e",
          "net": "OSC_INV"
        },
        {
          "n": 7,
          "r": null,
          "c": "e",
          "net": "OSC_OUT"
        },
        {
          "n": 8,
          "r": null,
          "c": "e",
          "net": "VCC"
        }
      ]
    },
    {
      "id": "DA2",
      "value": "TL072",
      "label": "Decay Buffer + Noise Amplifier",
      "stage": 1,
      "rotation": 180,
      "pins": [
        {
          "n": 1,
          "r": null,
          "c": "f",
          "net": "DECAY_OUT"
        },
        {
          "n": 2,
          "r": null,
          "c": "f",
          "net": "DECAY_INV"
        },
        {
          "n": 3,
          "r": null,
          "c": "f",
          "net": "GND"
        },
        {
          "n": 4,
          "r": null,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 5,
          "r": null,
          "c": "e",
          "net": "NOISE_BIAS"
        },
        {
          "n": 6,
          "r": null,
          "c": "e",
          "net": "NOISE_AMP_INV"
        },
        {
          "n": 7,
          "r": null,
          "c": "e",
          "net": "NOISE_AMP_OUT"
        },
        {
          "n": 8,
          "r": null,
          "c": "e",
          "net": "VCC"
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
          "n": 1,
          "r": null,
          "c": "f",
          "net": "PCV_BUF_OUT"
        },
        {
          "n": 3,
          "r": null,
          "c": "f",
          "net": "PCV_BUF_IN"
        },
        {
          "n": 4,
          "r": null,
          "c": "f",
          "net": "VEE"
        },
        {
          "n": 5,
          "r": null,
          "c": "e",
          "net": "GND"
        },
        {
          "n": 6,
          "r": null,
          "c": "e",
          "net": "MIX_INV"
        },
        {
          "n": 7,
          "r": null,
          "c": "e",
          "net": "MIX_OUT"
        },
        {
          "n": 8,
          "r": null,
          "c": "e",
          "net": "VCC"
        }
      ]
    }
  ],
  "twoPins": [
    {
      "id": "C1",
      "type": "C",
      "value": "1uF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Noise AC coupling"
    },
    {
      "id": "C10",
      "type": "C",
      "value": "470nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Pitch envelope, noise envelope"
    },
    {
      "id": "C11",
      "type": "C",
      "value": "470nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 11,
      "_role": "Pitch envelope, noise envelope"
    },
    {
      "id": "C12",
      "type": "C",
      "value": "33nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 5,
      "_role": "Oscillator \u00d72, VCA input AC coupling"
    },
    {
      "id": "C13",
      "type": "C",
      "value": "33nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 5,
      "_role": "Oscillator \u00d72, VCA input AC coupling"
    },
    {
      "id": "C14",
      "type": "C",
      "value": "33nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 10,
      "_role": "Oscillator \u00d72, VCA input AC coupling"
    },
    {
      "id": "C15",
      "type": "C",
      "value": "10nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 2,
      "_role": "Gate HP filter, HPF AC coupling output"
    },
    {
      "id": "C16",
      "type": "C",
      "value": "10nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "Gate HP filter, HPF AC coupling output"
    },
    {
      "id": "C17",
      "type": "C",
      "value": "2.2nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 10,
      "_role": "VCA output filter \u00d72"
    },
    {
      "id": "C18",
      "type": "C",
      "value": "2.2nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 10,
      "_role": "VCA output filter \u00d72"
    },
    {
      "id": "C19",
      "type": "C",
      "value": "1nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "HPF filter stages \u00d72"
    },
    {
      "id": "C2",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C20",
      "type": "C",
      "value": "1nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "HPF filter stages \u00d72"
    },
    {
      "id": "C21",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power bypass"
    },
    {
      "id": "C22",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power bypass"
    },
    {
      "id": "C3",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C4",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C5",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C6",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C7",
      "type": "C",
      "value": "100nF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 1,
      "_role": "IC decoupling"
    },
    {
      "id": "C8",
      "type": "C",
      "value": "47uF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power supply bulk filter"
    },
    {
      "id": "C9",
      "type": "C",
      "value": "47uF",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power supply bulk filter"
    },
    {
      "id": "R1",
      "type": "R",
      "value": "1M",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Noise amp feedback, VCA base bias"
    },
    {
      "id": "R10",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R11",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R12",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R13",
      "type": "R",
      "value": "47K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 6,
      "_role": "Decay input, decay feedback, pitch env divider"
    },
    {
      "id": "R14",
      "type": "R",
      "value": "47K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 6,
      "_role": "Decay input, decay feedback, pitch env divider"
    },
    {
      "id": "R15",
      "type": "R",
      "value": "47K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Decay input, decay feedback, pitch env divider"
    },
    {
      "id": "R16",
      "type": "R",
      "value": "39K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 2,
      "_role": "Gate HP filter discharge"
    },
    {
      "id": "R17",
      "type": "R",
      "value": "33K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 2,
      "_role": "Comparator threshold, mixer feedback"
    },
    {
      "id": "R18",
      "type": "R",
      "value": "33K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 13,
      "_role": "Comparator threshold, mixer feedback"
    },
    {
      "id": "R19",
      "type": "R",
      "value": "27K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Pitch env divider"
    },
    {
      "id": "R2",
      "type": "R",
      "value": "1M",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 10,
      "_role": "Noise amp feedback, VCA base bias"
    },
    {
      "id": "R20",
      "type": "R",
      "value": "22K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 3,
      "_role": "Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV"
    },
    {
      "id": "R21",
      "type": "R",
      "value": "22K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV"
    },
    {
      "id": "R22",
      "type": "R",
      "value": "22K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 11,
      "_role": "Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV"
    },
    {
      "id": "R23",
      "type": "R",
      "value": "22K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV"
    },
    {
      "id": "R24",
      "type": "R",
      "value": "22K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 12,
      "_role": "Accent series, noise amp gain, HPF emitter, HPF feedback, snappy CV"
    },
    {
      "id": "R25",
      "type": "R",
      "value": "10K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 4,
      "_role": "Trigger divider top, mixer noise input"
    },
    {
      "id": "R26",
      "type": "R",
      "value": "10K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 13,
      "_role": "Trigger divider top, mixer noise input"
    },
    {
      "id": "R27",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 4,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R28",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 5,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R29",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R3",
      "type": "R",
      "value": "910K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 5,
      "_role": "Oscillator bridge"
    },
    {
      "id": "R30",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 13,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R31",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R32",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R33",
      "type": "R",
      "value": "1K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Bridge, trigger div bottom, attack series, output + unconfirmed"
    },
    {
      "id": "R34",
      "type": "R",
      "value": "470R",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 5,
      "_role": "Oscillator tuning to GND"
    },
    {
      "id": "R35",
      "type": "R",
      "value": "330R",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Pitch NPN series (min frequency floor)"
    },
    {
      "id": "R36",
      "type": "R",
      "value": "10R",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power supply series filtering"
    },
    {
      "id": "R37",
      "type": "R",
      "value": "10R",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power supply series filtering"
    },
    {
      "id": "R38",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 13,
      "_role": "Mixer drum input (see BOM discrepancy note)"
    },
    {
      "id": "R4",
      "type": "R",
      "value": "470K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 6,
      "_role": "Decay injection to CAP_MID"
    },
    {
      "id": "R5",
      "type": "R",
      "value": "120K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 3,
      "_role": "Accent node parallel limiter"
    },
    {
      "id": "R6",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 2,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R7",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 3,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R8",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 8,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "R9",
      "type": "R",
      "value": "100K",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 9,
      "_role": "Comp threshold, accent, pitch CV in, noise emitter, noise coll, noise bias, HPF"
    },
    {
      "id": "VD1",
      "type": "D",
      "value": "1N5819",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power reverse polarity protection"
    },
    {
      "id": "VD2",
      "type": "D",
      "value": "1N5819",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Power reverse polarity protection"
    },
    {
      "id": "VD3",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 2,
      "_role": "Signal diodes (see block details)"
    },
    {
      "id": "VD4",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 4,
      "_role": "Signal diodes (see block details)"
    },
    {
      "id": "VD5",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 7,
      "_role": "Signal diodes (see block details)"
    },
    {
      "id": "VD6",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 10,
      "_role": "Signal diodes (see block details)"
    },
    {
      "id": "VD7",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": 11,
      "_role": "Signal diodes (see block details)"
    },
    {
      "id": "VD8",
      "type": "D",
      "value": "1N4148",
      "r1": null,
      "c1": null,
      "r2": null,
      "c2": null,
      "stage": null,
      "_role": "Signal diodes (see block details)"
    }
  ],
  "transistors": [
    {
      "id": "VT1",
      "subtype": "PNP",
      "value": "BC558",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 3
    },
    {
      "id": "VT2",
      "subtype": "PNP",
      "value": "BC558",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 11
    },
    {
      "id": "VT3",
      "subtype": "NPN",
      "value": "BC548",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 3
    },
    {
      "id": "VT4",
      "subtype": "NPN",
      "value": "BC548",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 7
    },
    {
      "id": "VT5",
      "subtype": "NPN",
      "value": "BC548",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 9
    },
    {
      "id": "VT6",
      "subtype": "NPN",
      "value": "BC548",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 10
    },
    {
      "id": "VT7",
      "subtype": "NPN",
      "value": "BC548",
      "eR": null,
      "eC": null,
      "bR": null,
      "bC": null,
      "cR": null,
      "cC": null,
      "stage": 12
    }
  ],
  "jumpers": [],
  "powerWires": [],
  "jpsWires": [],
  "netLabels": [],
  "_netConnections": {
    "C15": [
      "GATE_IN",
      "HP_OUT"
    ],
    "R16": [
      "HP_OUT",
      "GND"
    ],
    "VD3": [
      "GND",
      "HP_OUT"
    ],
    "R6": [
      "VCC",
      "COMP_INV"
    ],
    "R17": [
      "COMP_INV",
      "GND"
    ],
    "DA1A": [
      "HP_OUT",
      "COMP_INV",
      "COMP_OUT"
    ],
    "R7": [
      "COMP_OUT",
      "ACC_IN"
    ],
    "R5": [
      "ACC_IN",
      "GND"
    ],
    "R20": [
      "ACC_IN",
      "ACC_EMIT"
    ],
    "VT1": [
      "ACCENT_CV_IN",
      "GND",
      "ACC_EMIT"
    ],
    "VT3": [
      "ACC_EMIT",
      "VCC",
      "ACC_TRIG"
    ],
    "VD4": [
      "ACC_TRIG",
      "TRIG_POS"
    ],
    "R25": [
      "TRIG_POS",
      "OSC_NONINV"
    ],
    "R27": [
      "OSC_NONINV",
      "GND"
    ],
    "C12": [
      "OSC_OUT",
      "CAP_MID"
    ],
    "C13": [
      "CAP_MID",
      "OSC_INV"
    ],
    "R28": [
      "OSC_OUT",
      "OSC_INV"
    ],
    "R3": [
      "OSC_OUT",
      "OSC_INV"
    ],
    "P5": [
      "CAP_MID",
      "GND"
    ],
    "R34": [
      "P5",
      "GND"
    ],
    "DA1B": [
      "OSC_NONINV",
      "OSC_INV",
      "OSC_OUT"
    ],
    "R13": [
      "OSC_OUT",
      "DECAY_INV"
    ],
    "R14": [
      "DECAY_INV",
      "DECAY_OUT"
    ],
    "P1": [
      "DECAY_INV",
      "DECAY_OUT"
    ],
    "R4": [
      "DECAY_OUT",
      "CAP_MID"
    ],
    "DA2A": [
      "GND",
      "DECAY_INV",
      "DECAY_OUT"
    ],
    "VD5": [
      "COMP_OUT",
      "ENV_CAP"
    ],
    "P4": [
      "VD5",
      "ENV_CAP"
    ],
    "R29": [
      "P4",
      "ENV_CAP"
    ],
    "C10": [
      "ENV_CAP",
      "GND"
    ],
    "R19": [
      "ENV_CAP",
      "ENV_DIV"
    ],
    "R15": [
      "ENV_DIV",
      "PCV_BUF_OUT"
    ],
    "VT4": [
      "ENV_DIV",
      "CAP_MID",
      "PITCH_NPN_E"
    ],
    "R35": [
      "PITCH_NPN_E",
      "GND"
    ],
    "R8": [
      "PITCH_CV_IN",
      "P3"
    ],
    "P3": [
      "R8",
      "PCV_BUF_IN",
      "GND"
    ],
    "DA3A": [
      "PCV_BUF_IN",
      "PCV_BUF_OUT",
      "PCV_BUF_OUT"
    ],
    "VT5": [
      "reversed",
      "VCC",
      "NOISE_COLL"
    ],
    "R9": [
      "VCC",
      "VT5"
    ],
    "R10": [
      "NOISE_COLL",
      "GND"
    ],
    "C1": [
      "NOISE_COLL",
      "NOISE_BIAS"
    ],
    "R11": [
      "NOISE_BIAS",
      "GND"
    ],
    "R1": [
      "NOISE_AMP_OUT",
      "NOISE_AMP_INV"
    ],
    "R21": [
      "NOISE_AMP_INV",
      "GND"
    ],
    "DA2B": [
      "NOISE_BIAS",
      "NOISE_AMP_INV",
      "NOISE_AMP_OUT"
    ],
    "C14": [
      "NOISE_AMP_OUT",
      "VCA_BASE"
    ],
    "R2": [
      "VCA_BASE",
      "VCC"
    ],
    "VT6": [
      "VCA_BASE",
      "VCA_COLL",
      "GND"
    ],
    "VD6": [
      "GND",
      "VCA_COLL"
    ],
    "C17": [
      "VCA_COLL",
      "GND"
    ],
    "C18": [
      "VCA_COLL",
      "GND"
    ],
    "VD7": [
      "COMP_OUT",
      "NOISE_ENV_CAP"
    ],
    "C11": [
      "NOISE_ENV_CAP",
      "GND"
    ],
    "P2": [
      "NOISE_ENV_CAP",
      "VCA_COLL"
    ],
    "VT2": [
      "SNAPPY_CV_IN",
      "GND",
      "NOISE_ENV_CAP"
    ],
    "R22": [
      "VT2",
      "GND"
    ],
    "C19": [
      "VCA_COLL",
      "HPF_A"
    ],
    "C20": [
      "HPF_A",
      "VT7"
    ],
    "R12": [
      "HPF_A",
      "GND"
    ],
    "VT7": [
      "C20",
      "VCC",
      "HPF_EMIT"
    ],
    "R23": [
      "HPF_EMIT",
      "VEE"
    ],
    "R24": [
      "HPF_EMIT",
      "HPF_A"
    ],
    "C16": [
      "HPF_EMIT",
      "HPF_AC"
    ],
    "R38": [
      "OSC_OUT",
      "MIX_INV"
    ],
    "R26": [
      "HPF_AC",
      "MIX_INV"
    ],
    "R18": [
      "MIX_INV",
      "MIX_OUT"
    ],
    "DA3B": [
      "GND",
      "MIX_INV",
      "MIX_OUT"
    ],
    "R30": [
      "MIX_OUT",
      "OUTPUT_J"
    ],
    "VD1": [
      "VCC"
    ],
    "VD2": [
      "VEE"
    ],
    "R36": [
      "VCC",
      "VCC_filt"
    ],
    "R37": [
      "VEE",
      "VEE_filt"
    ],
    "C8": [
      "VCC",
      "GND"
    ],
    "C9": [
      "GND",
      "VEE"
    ],
    "C6": [
      "VCC",
      "GND"
    ],
    "C7": [
      "VEE",
      "GND"
    ]
  },
  "_blockComponentMap": {
    "Block 1: Gate-to-Trigger Converter": [
      "C15",
      "R16",
      "VD3",
      "R6",
      "R17",
      "DA1A"
    ],
    "Block 2: Accent CV Section": [
      "R7",
      "R5",
      "R20",
      "VT1",
      "VT3"
    ],
    "Block 3: Trigger Routing to Oscillator": [
      "VD4",
      "R25",
      "R27"
    ],
    "Block 4: Bridged-T Percussive Oscillator": [
      "C12",
      "C13",
      "R28",
      "R3",
      "P5",
      "R34",
      "DA1B"
    ],
    "Block 5: Decay Feedback": [
      "R13",
      "R14",
      "P1",
      "R4",
      "DA2A"
    ],
    "Block 6: Pitch Attack Envelope": [
      "VD5",
      "P4",
      "R29",
      "C10",
      "R19",
      "R15",
      "VT4",
      "R35"
    ],
    "Block 7: Pitch CV Input + Buffer": [
      "R8",
      "P3",
      "DA3A"
    ],
    "Block 8: Noise Generator": [
      "VT5",
      "R9",
      "R10",
      "C1",
      "R11",
      "R1",
      "R21",
      "DA2B"
    ],
    "Block 9: Swing Type VCA": [
      "C14",
      "R2",
      "VT6",
      "VD6",
      "C17",
      "C18"
    ],
    "Block 10: Noise Envelope + Snappy Control": [
      "VD7",
      "C11",
      "P2",
      "VT2",
      "R22"
    ],
    "Block 11: High Pass Filter": [
      "C19",
      "C20",
      "R12",
      "VT7",
      "R23",
      "R24",
      "C16"
    ],
    "Block 12: Mixer + Output": [
      "R38",
      "R26",
      "R18",
      "DA3B",
      "R30"
    ],
    "Block 13: Power Supply": [
      "VD1",
      "VD2",
      "R36",
      "R37",
      "C8",
      "C9",
      "C6",
      "C7"
    ]
  }
});