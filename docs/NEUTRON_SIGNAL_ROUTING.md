# Behringer Neutron — Signal Routing Reference

> **Source of truth:** *Behringer NEUTRON User Manual* (English, 32 pp.,
> matches firmware V1.2.2). Page references in this document are to that PDF.
>
> **This is not the Proton.** The Proton firmware uses the labels *"Neutron mode"*
> and *"Proton mode"* for its ASR retrigger setting (§15 of the Proton manual),
> which is the only place the lineage is explicit. Architecturally the two are
> different units: the Neutron has **one** VCF (with a dual output tap), **one**
> LFO, **two ADSRs** (no ASRs), an analog **BBD delay**, an **overdrive**
> circuit, and a **32 in / 24 out** patchbay. The Proton has dual VCFs, dual
> LFOs, two ADSRs + two ASRs, a wavefolder (no delay, no overdrive), and a
> 40 in / 24 out patchbay. **Do not infer Proton routing from Neutron behavior
> or vice versa** — that mistake produced the first round of errors in this
> repo's earlier docs.

## 1. Block diagram (default normalled flow)

Per §8 of the manual:

```
   [MIDI / USB] ── 1 V/oct ─┐
                            ▼
   ┌─────────[VCO 1]────────┴────────[VCO 2]────────┐
   │  Tune (±1 / ±10 oct)            Tune           │
   │  Range (8/16/32 / free)         Range          │
   │  Shape (5 waves, blend or sw)   Shape          │
   │  PW                             PW             │
   └─────────────────┬──────────────────────────────┘
                     │
              ┌──────┴──────┐
              │  OSC MIX    │
              └──────┬──────┘
                     │
                     │  + NOISE LEVEL (white noise)
                     │  + EXT INPUT (rear ¼″ TS)
                     ▼
                  [VCF]    ──── tap: VCF 1 (panel-selected mode)
                     │     ──── tap: VCF 2 (complementary mode — see §5)
                     ▼
                [OVERDRIVE]    DRIVE / TONE / LEVEL
                     │
                     ▼
                  [VCA]    CV ← ENV 1 (norm), + VCA BIAS offset
                     │
                     ▼
              [BBD DELAY]   TIME / REPEATS / MIX
                     │
                     ▼
              MAIN OUT (¼″ TRS, balanced)
              + Headphones (independent level)
              + 3.5 mm OUTPUT jack on patchbay
```

### Always-on modulation (no cables patched)

From §8 default-routings table:

| Source | Destination | Depth knob | Notes |
|--------|-------------|-----------|-------|
| LFO (bipolar) | VCF Freq | MOD DEPTH | normalled |
| ENV 2 | VCF Freq | ENV DEPTH | normalled, sums with LFO path |
| ENV 1 | VCA CV | (no knob — VCA BIAS adds offset) | normalled |
| LFO (bipolar) | ATT 2 IN | (passive att) | normalled |
| ATT 2 OUT | ATT 1 IN | (active att, ATT1 CV-controllable) | normalled |
| ATT 2 OUT | PW 1 + PW 2 | — | **PWM via LFO is on by default** |
| LFO (bipolar) | MULT IN | — | so MULT 1 / MULT 2 carry LFO unless something is patched into MULT IN |
| NOISE | S&H IN | — | S&H samples noise by default → "classic" random staircase |
| ASSIGN | ATT 1 CV | — | so ASSIGN-OUT routes through ATT1 by default |
| ENV 2 | INVERT IN | — | so INVERT OUT = −ENV 2 by default |
| E. GATE 1 | E. GATE 2 | — | unless E. GATE 2 has its own patch |
| Kbd CV | OSC 1, OSC 2 | always 1 V/oct | normalled |
| Kbd CV | VCF cutoff | binary KEY TRK button | full 1 V/oct when on |

**Important consequence:** out of the box the Neutron is *already* doing PWM,
filter mod, envelope-on-filter, and S&H-of-noise. A patch-generator that just
sets panel knobs needs to remember these are wired even with no cables.

## 2. Patched-CV behaviour

Standard semi-modular convention applies on every input jack: **plugging a
cable breaks the listed normal**. Examples:

- Patching `VCA IN` cuts the OD → VCA → DELAY chain — the VCA now amplifies
  whatever's at the jack, not the synth's own audio.
- Patching `MULT IN` replaces the LFO normal at MULT 1 / MULT 2.
- Patching `ATT 2 IN` replaces the LFO bipolar input — and that affects
  PW 1 / PW 2 (because PWs are normalled from ATT 2 OUT).
- Patching `S&H IN` replaces the NOISE normal — S&H now samples whatever
  you patched.

The manual does not call out any "replaces vs sums" exceptions on the
Neutron — unlike the Proton's §5.5 patched-Freq-CV behaviour. So treat all
Neutron CV inputs as straight overrides where a normal exists, and as
additive (with the panel knob) where no normal exists.

## 3. CV inputs (32 jacks; §3.2.1, §12)

Voltages are −5 V to +5 V unless stated.

### Oscillators (8 jacks)

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| OSC 1 | 1 V/oct | VCO 1 pitch | MIDI / kbd | Sums with kbd |
| OSC 2 | 1 V/oct | VCO 2 pitch | MIDI / kbd | Sums |
| OSC 1+2 | 1 V/oct | Pitch to both | — | Sums into both |
| INVERT IN | ±9.5 V | Sign-flips at INVERT OUT | ENV 2 | **Override** — INVERT OUT now = −(your patch) |
| SHAPE 1 | −5..+5 V | OSC 1 wave morph (in blend mode) or step (in switch mode) | SHAPE 1 knob | Sums |
| SHAPE 2 | −5..+5 V | OSC 2 wave | SHAPE 2 knob | Sums |
| PW 1 | −5..+5 V | OSC 1 pulse-width | ATT 2 OUT (LFO via ATT 2) | **Override** — replaces LFO-PWM normal |
| PW 2 | −5..+5 V | OSC 2 pulse-width | ATT 2 OUT | **Override** |

**Wave-shape note:** SHAPE jacks behave very differently in **blend** vs.
**switch** mode (per-OSC config — hold OSC n RANGE, press PARAPHONIC to
toggle, §5.3). In blend mode the CV smoothly morphs between adjacent waves;
in switch mode it steps between fixed waveforms.

PW only affects the first two waveshapes (Tone Mod and Square/Pulse) — sine,
triangle and sawtooth ignore it.

### Filter (3 jacks)

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| VCF | audio | Filter audio input | OSC MIX + NOISE + EXT | **Override** — cuts the synth's normal feed into the filter |
| FREQ MOD | −5..+5 V | Cutoff modulation | LFO bipolar (via panel route) | **Override** of LFO; sums with ENV 2·EnvDepth and panel FREQ knob |
| RES | −5..+5 V | Resonance modulation | RESO knob | Sums |

> **One filter, two outputs.** VCF 1 is the panel-selected mode (LPF / BPF /
> HPF). VCF 2 is a complementary tap from the same filter — patching VCF 1 +
> VCF 2 into SUM 1 A/B gives a notch when the panel mode is BPF (per §4.5
> tip). The manual gives this Mode→VCF2 relationship:
>
> | Panel Mode | VCF 2 acts as |
> |------------|---------------|
> | HPF | BPF |
> | BPF | (complementary BPF) |
> | LPF | HPF |
>
> (Diagrams in §4.5 are stylised symbols; verify the exact pairing on the
> bench if it matters for a generator.)

### Overdrive / VCA / Delay (4 jacks)

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| OD IN | audio | Overdrive audio input | VCF output | **Override** |
| VCA IN | audio | VCA audio input | OD output | **Override** |
| VCA CV | −9..+9 V | VCA gain | ENV 1 | **Override** — VCA BIAS still adds offset |
| DELAY IN | audio | BBD delay input | VCA output | **Override** |
| DELAY TIME | −5..+5 V | Modulates delay time | DELAY TIME knob | Sums |

`VCA CV` is the only mod input in the entire patchbay rated **±9 V** rather
than ±5 V — the envelopes output 0..+9 V, so the headroom matches.

### Envelopes (2 jacks)

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| E. GATE 1 | trigger ≥ 1.5 V | Triggers ENV 1 | MIDI gate | **Override** |
| E. GATE 2 | trigger ≥ 1.5 V | Triggers ENV 2 | E. GATE 1 (which itself defaults to MIDI gate) | **Override** |

**Note**: E. GATE 2 is normalled from **E. GATE 1's pre-jack signal**, so
you can drive ENV 1 from MIDI and ENV 2 from a patched gate independently
just by patching E. GATE 2.

### Sample & Hold and LFO (5 jacks)

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| S&H IN | any | What gets sampled | NOISE | **Override** |
| S&H CLOCK | trigger ≥ 3 V | External clock for S&H | S&H RATE knob (internal clock) | **Override** of internal rate |
| LFO RATE | −5..+5 V | Exp rate modulation | LFO RATE knob | Sums |
| LFO SHAPE | −5..+5 V | Wave morph (blend mode) or step (switch mode) | LFO SHAPE knob | Sums |
| LFO TRIG | trigger ≥ 1.6 V | Resets LFO phase | KEY SYNC button (MIDI keys reset) | Adds external trig source |

### Utilities (10 jacks)

| Jack | Range | Function | Normalled from |
|------|-------|----------|----------------|
| MULT IN | any | Splits to MULT 1 + MULT 2 outs | LFO bipolar |
| ATT 1 IN | any | Source for Attenuator 1 (active, has CV) | ATT 2 OUT |
| ATT 1 CV | −5..+5 V | Modulates Att 1 amount | ASSIGN OUT |
| ATT 2 IN | any | Source for Attenuator 2 (passive) | LFO bipolar |
| SLEW IN | any | Slew-rate-limited copy at SLEW OUT | (no MIDI normal — needs a patch to do anything via the SLEW jack) |
| SUM 1 (A) | any | Input A of SUM 1 | — |
| SUM 1 (B) | any | Input B of SUM 1 | — |
| SUM 2 (A) | any | Input A of SUM 2 | — |
| SUM 2 (B) | any | Input B of SUM 2 | — |

(That's 10 utility input jacks — §3.2.1 numbering 68–76.)

> **Slew vs. PORTA TIME:** these are two different slew limiters. PORTA TIME
> is hard-wired to the MIDI-note pitch CV path (the kbd-to-VCO normal). The
> SLEW jack is a free utility you can patch into any signal.

## 4. Output jacks (24; §3.2.1)

| Jack | Carries | Notes |
|------|---------|-------|
| OSC 1 | VCO 1 raw output | max +14 dBu |
| OSC 2 | VCO 2 raw output | max +14 dBu |
| OSC MIX | post OSC MIX knob blend | max +14 dBu |
| VCF 1 | filter, panel-selected mode | max +12 dBu |
| VCF 2 | filter, complementary mode (see §3 Filter table) | max +12 dBu |
| OVERDRIVE | post-overdrive | max +18 dBu |
| VCA | post-VCA, pre-delay | max +18 dBu |
| OUTPUT | post-delay (final) | max +15 dBu — duplicate of rear ¼″ |
| NOISE | white noise generator | max +18 dBu |
| ENV 1 | ADSR 1 envelope CV | 0..+9 V |
| ENV 2 | ADSR 2 envelope CV | 0..+9 V |
| INVERT | sign-flipped INVERT IN | ±9.5 V |
| LFO | bipolar LFO | −5..+5 V |
| LFO UNI | unipolar LFO | 0..+5 V |
| S&H | sample-and-hold output | tracks input up to ±9.5 V |
| MULT 1, MULT 2 | duplicates of MULT IN | ±9.5 V |
| MIDI GATE | MIDI gate as CV | 0..+3.3 V |
| ATT 1 | Attenuator 1 output (CV-controllable amount) | ±9.5 V |
| ATT 2 | Attenuator 2 output (passive amount) | depends on input |
| SLEW | slew-limited SLEW IN | ±9.5 V |
| SUM 1 | SUM 1 (A + B) | ±9.5 V |
| SUM 2 | SUM 2 (A + B) | ±9.5 V |
| ASSIGN | user-assignable: OSC 1 CV / OSC 2 CV / velocity / mod wheel / aftertouch | 0..+5 V (calibratable, §7.2) |

## 5. Switches & buttons

### Oscillator (§3.1.1)

| Control | Type | Effect |
|---------|------|--------|
| RANGE 1 / RANGE 2 | 3-LED button | Cycles 8′ / 16′ / 32′; all-three-lit = ±10-octave free range. Long-press = manual tuning mode (LFO-shape LEDs become a tuner against last MIDI note). |
| OSC SYNC | toggle | Hard-syncs OSC 2 period to OSC 1. Long-press = config menu (assign-out source via RANGE buttons; envelope retrigger via KEY TRK). |
| PARAPHONIC | toggle | When 2 MIDI notes received: OSC 1 = note 1, OSC 2 = note 2. Long-press = poly-chain on/off (§5.9). Inside other long-presses, this button toggles blend-vs-switch for the held section. |

### Filter (§3.1.2)

| Control | Type | Effect |
|---------|------|--------|
| MODE | 3-pos cycling button | LPF / BPF / HPF |
| KEY TRK | toggle | Adds 1 V/oct from MIDI note to filter cutoff |

(No "Soft" button, no "Link" button — those are Proton-only.)

### LFO (§3.1.3)

| Control | Type | Effect |
|---------|------|--------|
| KEY SYNC | toggle | New MIDI note resets LFO phase. Long-press = LFO blend/switch toggle + LFO config |

### Other top panel

- **MIDI IN** indicator LED (next to the VOLUME knob).
- **DELAY**, **OVERDRIVE**, **ENVELOPE 1/2**, **SLEW RATE LIMITER**,
  **ATTENUATORS**, **OUTPUT**, **SAMPLE & HOLD** — all knobs only, no buttons.

### Rear panel

- 4-bit **MIDI channel DIP switch** (1–16; §3.2.2) — no soft setting; this
  is the *only* way to set the channel.
- USB B (class-compliant — no driver), MIDI IN/THRU (5-pin DIN).
- **No SysEx** for parameter control. Only documented SysEx is the **ASSIGN
  OUT calibration sequence** (§7.2): six commands to enter 1 V cal mode,
  bump up/down, save and exit.

## 6. Pots & knob ranges (§3, §12 specs)

### Oscillators

| Knob | Range |
|------|-------|
| TUNE 1 / TUNE 2 | ±1 octave per range step (8′/16′/32′) or ±10 oct in free mode |
| Frequency span | **0.7 Hz – 55 kHz** across all four ranges |
| WIDTH 1 / WIDTH 2 | 0–100 % duty (Tone Mod and Square only) |
| OSC MIX | CCW = OSC 1 only … CW = OSC 2 only |

### Filter

| Knob | Range |
|------|-------|
| FREQ | **10 Hz – 15 kHz** |
| RESO | 0 → self-oscillation |
| MOD DEPTH | 0–100 % of the active source at FREQ MOD jack (LFO normalled) |
| ENV DEPTH | 0–100 % of ENV 2 |
| NOISE | 0–100 % of noise injected into filter feed |
| VCA BIAS | 0–100 % offset added to VCA CV (CW = VCA forced fully open) |

### LFO

| Knob | Range |
|------|-------|
| RATE | **0.01 Hz – 10 kHz** (audio rate!) |
| SHAPE | Sine / Triangle / Sawtooth / Square / **Reverse Sawtooth** |

(Note: Neutron has Reverse Saw; Proton has Ramp. Different fifth shape.)

### Envelopes (both ADSRs)

| Knob | Range |
|------|-------|
| Attack | 300 µs – 5 s (linear) |
| Decay | 2.4 ms – 10 s (exponential) |
| Sustain | 0 V – 9 V |
| Release | 1.5 ms – 6 s (exponential) |

(No Fast/Slow toggle as on the Proton — single time range.)

### Delay (BBD)

| Knob | Range |
|------|-------|
| TIME | **25 ms – 640 ms** |
| REPEATS | 0 → infinite (self-oscillation at max) |
| MIX | 0 = dry … 100 % = wet only |

### Overdrive

| Knob | Range |
|------|-------|
| DRIVE | 0–11 (yes, eleven) — soft-clipping |
| TONE | bipolar tilt EQ (CCW = bass boost, CW = treble) |
| LEVEL | 0 dB → −∞ (final level — *can mute the synth if all the way down*) |

### Sample & Hold / Slew / Attenuators

| Knob | Range |
|------|-------|
| S&H RATE | 0.26 Hz – 28 Hz (or external clock) |
| S&H GLIDE | 500 µs – 1 s |
| SLEW | 1 ms – 3 s rise/fall limit on SLEW IN |
| PORTA TIME | 0–10 s glide between MIDI notes |
| ATTEN 1 | +4 dB to −∞ (active, has CV input) |
| ATTEN 2 | 0 dB to −∞ (passive) |

### Output

| Knob | Notes |
|------|-------|
| VOLUME | Main ¼″ output |
| PHONES LEVEL | Independent — separate rear knob |

## 7. Configurable features (held-button menus)

The Neutron has no SysEx for these — they're all panel button combos
(§5.1–§5.9):

| Function | Combo | Action |
|----------|-------|--------|
| Assign-Out source | hold OSC SYNC | RANGE 1/2 buttons cycle: OSC 1 CV / OSC 2 CV / Velocity / Mod Wheel / Aftertouch (LFO-shape LEDs indicate) |
| Envelope retrigger on/off | hold OSC SYNC, then KEY TRK toggles | KEY TRK LED on = retriggering |
| OSC 1 shape blend↔switch | hold RANGE 1 + press PARAPHONIC | LED throbs (blend) or flashes (switch) |
| OSC 2 shape blend↔switch | hold RANGE 2 + press PARAPHONIC | same |
| LFO shape blend↔switch | hold KEY SYNC + press PARAPHONIC | same |
| Manual OSC 1 tune | hold RANGE 1 | LFO-shape LEDs become tuner; turn TUNE 1 until top-centre LED lights for in-tune-on-last-MIDI-note |
| Manual OSC 2 tune | hold RANGE 2 | same |
| Tune-pot bypass | hold RANGE n + PARAPHONIC | freezes pitch — TUNE knob ignored, perfect tuning |
| Software version | hold OSC SYNC + PARAPHONIC | flashes major.minor.build on the three OSC1 octave LEDs |
| Poly-chain on/off | hold PARAPHONIC | LED slow-flash mono / fast-flash duo |

Stored configuration persists across power cycles.

## 8. Implications for a patch generator

- **Single VCF, single LFO, single VCA.** Far less surface area than the
  Proton. A generator only has to think about *one* filter cutoff/resonance
  pair, one LFO destination network, one VCA shape.
- **PWM and S&H-of-noise are on by default.** Even with no patches, both are
  active. To disable PWM: patch any silent jack into PW 1 / PW 2 (or into
  ATT 2 IN). To disable S&H bubbling: ignore the S&H output, or patch a
  static voltage into S&H IN.
- **VCF mod is single-source (LFO normal or patched FREQ MOD), additive
  with ENV 2.** Same shape as the Proton's Freq-CV behaviour, but with only
  one filter to worry about.
- **VCA has no F/S envelope toggle.** ADSR 1 has a single time range — for
  long pads use Slow → 5/10/9/6 s as upper bounds. For drum-style patches
  the envelope's lower bound (300 µs attack, 2.4 ms decay) is fast enough.
- **LFO at audio rate** (up to 10 kHz) is unusual and powerful — opens up
  through-zero-style FM on the filter or PW. Worth having a generator path
  that uses the LFO as audio when SHAPE is set to sine/triangle.
- **Delay self-oscillates** at REPEATS = max. Treat that as a useful effect
  voice (cosmic feedback) rather than a bug.
- **ASSIGN OUT routes through ATT 1** by default (the chain ASSIGN → ATT1 CV
  is the shipped normal). To use ASSIGN as a clean LFO/velocity voice,
  patch ATT 2 OUT or ATT 1 OUT somewhere useful, and use ASSIGN's amount
  via mod-wheel/velocity to control depth.
- **No CC layer — only Mod Wheel / Sustain / Pitch Bend.** A generator
  cannot remote-control parameter values via MIDI CC — the Neutron only
  responds to Pitch Bend (±2 st fixed in 1.2.2), Mod Wheel, and Sustain.
  Everything else is panel-only or patch-only.
- **MIDI channel is DIP-switch only** (rear of unit). Can't be set in
  software.
- **Pitch bend is hard-coded to ±2 semitones** in 1.2.2 — a generator
  expecting wider bend will have to do its bending in CV (via OSC n CV
  jacks) instead of via MIDI.
- **VCA CV is ±9 V, not ±5 V.** If a generator drives VCA CV from an
  external CV source, the source must be capable of ±9 V or the VCA will
  not fully open.
- **Polyphony**: voice-stealing across poly-chained Neutrons is built-in
  (§5.9). One unit standalone is monophonic (or paraphonic for OSC 1/2
  splits).
- **Auto-calibration** runs at every power-on (§5.4). No manual command.

## 9. SysEx surface

> **Almost nothing.** Unlike the Proton's parameter-rich SysEx table, the
> Neutron manual (V1.2.2) only documents SysEx for the **ASSIGN OUT
> calibration** procedure (§7.2). Six messages:
>
> | SysEx (hex) | Action |
> |-------------|--------|
> | `F0 00 20 32 00 7F 10 20 F7` | Enter ASSIGN-OUT 1 V calibration mode |
> | `F0 00 20 32 00 7F 10 21 F7` | Enter ASSIGN-OUT 4 V calibration mode |
> | `F0 00 20 32 00 7F 10 22 F7` | Decrement output voltage |
> | `F0 00 20 32 00 7F 10 23 F7` | Increment output voltage |
> | `F0 00 20 32 00 7F 10 24 F7` | Save and exit |
>
> Manufacturer ID `00 20 32` matches the Proton (Music Tribe / Behringer);
> the Model ID is absent from this short table. Firmware updates use the DFU
> updater app from `musictri.be`, not SysEx (§7.1).
>
> **For a patch-maker, the implication is: no remote parameter automation.**
> A generator can drive notes/Mod Wheel/Sustain via MIDI but cannot recall
> a "preset" — every voicing is panel and patchbay state, set by hand.

## 10. Open questions / required experiments

### Q1 — VCF 2 mode pairing exact mapping

The §4.5 stylised symbols (Mode = ⌒, VCF 2 = ⌐ etc.) are ambiguous on the
PDF. Bench check: set MODE = LPF, sweep audio through, listen to VCF 2
output — should be HPF. Set MODE = BPF, listen — should be the
"complementary" BPF (different centre or notch?). Set MODE = HPF, VCF 2
should be LPF.

### Q2 — VCA BIAS interaction with patched VCA CV

When VCA CV is patched (overriding ENV 1), does VCA BIAS still add an
offset? Manual implies yes ("CW = VCA forced fully open *regardless of
CV*") but worth confirming with a static voltage on VCA CV.

### Q3 — LFO TRIG vs KEY SYNC priority

If KEY SYNC is on (LFO resets on each MIDI note) AND a gate is patched
into LFO TRIG, do both reset the phase, or does one win? Expected: both
reset (last wins). Verify on a scope.

### Q4 — Audio-rate LFO + filter FM

Claim: LFO → FREQ MOD at audio rate is "FM into the filter." Expected: at
high LFO rate with sine shape, this should produce sidebands around the
filter's cutoff/self-osc frequency. Bench check this is *clean enough* to
be useful (no zipper or quantisation artefacts).

---

## Sources

- **Behringer NEUTRON User Manual** (English, 32 pp., firmware V1.2.2).
- **Proton ↔ Neutron differences:** see `docs/PROTON_SIGNAL_ROUTING.md`.
  The two units share a manufacturer SysEx ID (`00 20 32`) and the
  Proton's "Neutron mode" / "Proton mode" labels in the ASR retrigger
  setting acknowledge the lineage — but architecturally they are
  different synths and should be treated as separate references.
