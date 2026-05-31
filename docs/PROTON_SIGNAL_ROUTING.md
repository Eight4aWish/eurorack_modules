# Behringer Proton — Signal Routing Reference

> **Source of truth:** *Behringer PROTON User Manual V0.0* (the version
> shipped with the unit; SysEx Manufacturer ID `00 20 32`, Model ID `00 01 25`).
> All page references in this document are to that PDF unless noted otherwise.
>
> **Lineage:** The Proton is in Behringer's Neutron family of semi-modular
> Eurorack synths — not a Pro-One clone (that's the Behringer **PRO-1**, a
> separate desktop product). The lineage is explicit in the firmware itself:
> the ASR retrigger setting offers two modes named *"Neutron"* (default —
> new notes retrigger the ASRs) and *"Proton"* (notes don't retrigger ASRs
> while at least one note is held). See §15 of the manual.

## 1. Block diagram

Default normalled signal flow when no patch cables are inserted:

```
                       [MIDI / USB / kbd CV] ──── 1 V/oct ───┐
                                                             ▼
   ┌────────────[VCO 1]─────────┬─────────────[VCO 2]────────┐
   │  Tune (±13 st)             │   Tune                     │
   │  Range (32/16/8 ft)        │   Range                    │
   │  Shape (5 waves +          │   Shape                    │
   │   sub-osc) +               │   PW                       │
   │   Sub-Mix                  │   Sub-Mix                  │
   └──────────────┬─────────────┴─────────────┬──────────────┘
                  │                           │
                  └────── Osc Mix knob ───────┘
                              │
                              ▼
                          [Osc Mix]   ── (taps to OSC1 / OSC2 / OSC MIX OUT)
                              │
                              │  + Noise Level (white noise)
                              │  + Ext Level   (front Ext In + rear input)
                              ▼
                       [Wavefolder] (modes: AM, ½, 1, BP)
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
          [VCF 1]                          [VCF 2]
          (LPF/BPF/HPF)                   (LPF/BPF/HPF)
                 │                                   │
            ┌────┴────┬─── Filter Mix knob ──┬───────┘
            │         (CCW=VCF1 … CW=VCF2)   │
            ▼                                ▼
         [VCA 1]                          [VCA 2]
         Bias                             Bias
         CV: ADSR 2                       CV: ADSR 2
            │                                │
            ▼                                ▼
       Main Out / Phones              VCA 2 OUT jack
```

VCA 1 also has its own out jack (does **not** break the main-out connection,
per §6). VCA 2 is fed straight from VCF 2, **not** from the Filter Mix —
useful for stereo splits, surprising for mono use.

### Always-on modulation (no cables patched)

| Source | Destination | Depth knob | Notes |
|--------|-------------|-----------|-------|
| LFO 1 | VCF 1 freq | Mod Depth 1 | normalled |
| LFO 2 | VCF 2 freq | Mod Depth 2 | normalled |
| ADSR 1 | VCF 1 freq | Env Depth 1 | normalled, additive with LFO 1 |
| ADSR 1 | VCF 2 freq | Env Depth 2 | normalled, additive with LFO 2 |
| ADSR 2 | VCA 1 gain | (no knob — Bias adds offset) | normalled |
| ADSR 2 | VCA 2 gain | (no knob — Bias adds offset) | normalled |
| Kbd CV | VCO 1 pitch | always 1 V/oct | normalled |
| Kbd CV | VCO 2 pitch | always 1 V/oct | normalled |
| Kbd CV | VCF 1 / VCF 2 cutoff | binary (Key btn) | full 1 V/oct tracking when enabled |
| Velocity / Mod Wheel / Aftertouch | VCF 1 / 2 freq | secondary depth | one source per VCF, set via VCF Mode long-press; 0 by default (§5.5, §15) |

## 2. Patched-CV precedence — important

Per **§5.5 of the manual**, patching a `VCF n Freq CV` jack **replaces** the
internal LFO source rather than summing with it:

> *"When link is not in use then VCF 1 is modulated by LFO 1 **or** a source
> patched via the patchbay; and VCF 2 by LFO 2 or a source patched via the
> patchbay."*

Mod Depth and Env Depth knobs always scale the active source:

- With nothing patched into `VCF 1 Freq CV`: Mod Depth 1 scales LFO 1.
- With something patched into `VCF 1 Freq CV`: Mod Depth 1 scales **the patched signal** (LFO 1 is overridden).
- Env Depth 1 always scales **ADSR 1**, regardless of what's at the Freq CV jack — ADSR 1 has its own independent path into the cutoff.

This is the opposite of what I previously inferred from third-party
write-ups. **For a patch-maker the rule is: the Freq CV jack is the LFO/external
input — it's a single mod source per filter, with Mod Depth as the attenuator.**

## 3. CV inputs (40 jacks; §12)

Voltages are −5 V to +5 V unless stated. "Override" means inserting a plug
breaks the listed normal.

### Oscillators (§12.1) — 7 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| OSC 1 CV | −5..+5 V | 1 V/oct pitch | MIDI / kbd | Sums with kbd CV |
| OSC 2 CV | −5..+5 V | 1 V/oct pitch | MIDI / kbd | Sums |
| OSC 1+2 CV | −5..+5 V | 1 V/oct pitch to both | — | Sums into both |
| PW 1 | −5..+5 V | Pulse-width mod, VCO 1 | Width 1 knob | Sums |
| PW 2 | −5..+5 V | Pulse-width mod, VCO 2 | Width 2 knob | Sums |
| Shape 1 | −5..+5 V | Sweeps shape (or blends in blend mode) | Shape 1 knob | Sums |
| Shape 2 | −5..+5 V | Sweeps shape | Shape 2 knob | Sums |

### Wavefolder (§12.2) — 3 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| WF In | audio | Wavefolder audio input | Osc Mix | **Override** — replaces internal Osc Mix feed |
| WF Folds | −5..+5 V | Folds amount | Folds knob | Sums |
| WF Sym | −5..+5 V | Asymmetric folding | Sym knob | Sums (no effect in AM or BP modes — §4.1, §4.3) |

### Filters (§12.3) — 6 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| VCF 1 In | audio | VCF 1 audio input | Wavefolder out | **Override** — replaces wavefolder feed |
| VCF 1 Freq | −5..+5 V | Cutoff modulation | LFO 1 | **Replaces** LFO 1 source (per §5.5). Sums with ADSR 1·EnvDepth 1 and panel Freq knob. |
| VCF 1 Reso | −5..+5 V | Resonance | Reso 1 knob | Sums |
| VCF 2 In | audio | VCF 2 audio input | Wavefolder out | **Override** |
| VCF 2 Freq | −5..+5 V | Cutoff modulation | LFO 2 | **Replaces** LFO 2 source |
| VCF 2 Reso | −5..+5 V | Resonance | Reso 2 knob | Sums |

### VCAs (§12.4) — 4 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| VCA 1 In | audio | VCA 1 audio input | Filter Mix | **Override** |
| VCA 1 CV | −5..+5 V | Gain control | ADSR 2 | **Override** — replaces ADSR 2; sums with Bias 1 |
| VCA 2 In | audio | VCA 2 audio input | VCF 2 out | **Override** |
| VCA 2 CV | −5..+5 V | Gain control | ADSR 2 | **Override** — sums with Bias 2 |

Bias knob: CCW = VCA fully under CV control (closed at 0 V); CW = VCA forced
fully open regardless of CV (§6).

### External audio (§12.5) — 2 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| Ext In | audio | Sums external audio into the filter feed; level set by Ext Level knob | Rear Input jack | Duplicate of rear (3.5 mm vs. 6.35 mm TS) |
| Out (In) | audio | Direct to main output, **bypassing the entire internal path** | — | **Override** — replaces VCA 1 → main out. Not affected by Ext Level (§4 / §12.5). |

### LFOs (§12.6) — 6 jacks

| Jack | Range | Function | Insert behaviour |
|------|-------|----------|-------------------|
| LFO 1 Trig | gate | Resets LFO 1 cycle if 1-Shot is on (and/or retrigger) | — |
| LFO 2 Trig | gate | Resets LFO 2 cycle if Retrig is on (and/or 1-shot) | — |
| LFO 1 Rate | −5..+5 V | Exp rate mod | Sums with Rate 1 knob |
| LFO 2 Rate | −5..+5 V | Exp rate mod | Sums with Rate 2 knob |
| LFO 1 Shape | −5..+5 V | Sweeps LFO 1 wave (in blend mode) or steps through | Sums with shape selection |
| LFO 2 Shape | −5..+5 V | Sweeps LFO 2 wave | Sums |

LFO Trig jacks are silent unless the relevant LFO has 1-Shot or Retrig
enabled (§9.5, §9.7).

### Envelopes (§12.7) — 4 jacks

| Jack | Range | Function | Normalled from | Insert behaviour |
|------|-------|----------|----------------|-------------------|
| ADSR 1 gate | gate | Triggers ADSR 1 | MIDI gate | **Override** — replaces MIDI gate |
| ADSR 2 gate | gate | Triggers ADSR 2 | MIDI gate | **Override** |
| ASR 1 gate | gate | Triggers ASR 1 | MIDI gate | **Override** |
| ASR 2 gate | gate | Triggers ASR 2 | MIDI gate | **Override** |

### Utilities (§12.8 – §12.12) — 8 jacks

| Jack | Range | Function | Normalled from |
|------|-------|----------|----------------|
| Atten 1 In | −5..+5 V | Source for Attenuverter 1 (output → Out patchbay) | ADSR 1 |
| Atten 2 In | −5..+5 V | Source for Attenuverter 2 | LFO 1 (bipolar) |
| Mult In | any | Splits to two outputs (Mult 1 / Mult 2 in Out patchbay) | — |
| CV Mix In 1 | −5..+5 V | Source A for the CV crossfader | LFO 1 |
| CV Mix In 2 | −5..+5 V | Source B for the CV crossfader | LFO 2 |
| Assign In | −5..+5 V | Routed to a user-selected destination at depth 0–100% (None / Osc 1 / Osc 2 / Osc 1&2 / VCF 1 / VCF 2 / VCF 1&2) | — |
| Sum A | −5..+5 V | One input of the Sum utility | sub-osc 1 |
| Sum B | −5..+5 V | Other input of the Sum utility | sub-osc 2 |

Attenuverters: panel knob = ±1× with mute at centre. Patching either
attenuverter's IN replaces its default normal (ADSR 1 / LFO 1).

## 4. Out jacks (24; §13)

| Jack | What it carries | Notes |
|------|----------------|-------|
| OSC 1, OSC 2, OSC MIX | VCO outputs | Tap before wavefolder |
| WF | Wavefolder output | Post-wavefolder, pre-VCF |
| VCF 1, VCF 2, VCF MIX | Filter outputs | VCF MIX is post Filter-Mix knob |
| VCA 1, VCA 2 | Final amplitude-shaped audio | Tap does **not** break main out (§6) |
| MULT 1, MULT 2 | Splits of MULT IN | If no MULT IN cable, sources can be set in SynthTribe |
| ATT 1, ATT 2 | Attenuverter outputs | Default sources: ADSR 1 / LFO 1 (bipolar) |
| CV MIX | Crossfaded sum of CV Mix In 1 / In 2 | Default: LFO 1 / LFO 2 |
| SUM | Sum A + Sum B | Default: sub-osc 1 + sub-osc 2 |
| ADSR 1, ADSR 2, ASR 1, ASR 2 | Envelope CV outputs | Available even when internally normalled |
| LFO 1 UNI, LFO 1 BI, LFO 2 UNI, LFO 2 BI | LFO outputs | UNI = 0..+5 V; BI = −5..+5 V |
| ASSIGN OUT | User-selectable: OSC 1 CV / OSC 2 CV / velocity / mod wheel / aftertouch | Source set via second-panel (§15) or SynthTribe |

## 5. Switches & buttons

### Oscillator section (§3)

| Control | Type | Effect |
|---------|------|--------|
| Range 1, Range 2 | 3-LED button | 32′ / 16′ / 8′ octave (cycles 8 → 32 then "free" 10-octave with all three LEDs lit). Long-press = secondary functions (sub-osc shape via LFO encoder; switched-vs-blended via Para; tuning enable/disable via Sync; linear vs exp FM via Wave Mode/F/S; MIDI control disable via Env Retrig). |
| Para | toggle | Paraphonic mode: MIDI note 1 → VCO 1, note 2 → VCO 2 (§3.6). |
| Sync | toggle | Hard-syncs VCO 2 to VCO 1. Long-press = note priority for both mono and paraphonic modes; selects Assign-Out source; toggles Polychain / MIDI clock fwd / ASR retrigger mode. |

### Wavefolder section (§4)

| Control | Type | Positions | Notes |
|---------|------|-----------|-------|
| Mode | 4-pos LED rotary | AM / ½ / 1 / BP | AM: Sym disabled, Folds = AM amount. BP: bypass — but **Folds CV / Sym CV jacks: behaviour in BP not specified in manual** (open question). |

### Filter section (§5)

| Control | Type | Effect |
|---------|------|--------|
| VCF n Mode | 3-pos cycling button | LPF / BPF / HPF. Long-press = secondary mod source (none / velocity / mod wheel / aftertouch) — picked via LFO encoder, depth via LFO Shift + encoder. |
| Link | tri-state (off / Red / White) | Off: independent. Red: VCF 2 mod source = VCF 1's. White: inverse of VCF 1 (§5.4). Replaces VCF 2's normal (LFO 2). |
| Soft | tri-state | Softens resonance saturation on selected filter(s). LEDs: red = VCF 1 only, white = VCF 2 only, both = both. |
| Key | tri-state | Enables 1 V/oct kbd tracking on selected filter(s). |

### Envelope sections (§8, §10)

| Control | Effect |
|---------|--------|
| F/S (ADSR) | Fast vs Slow time range. Per-ADSR, selected via Shift. |
| Retrig (ADSR) | New MIDI notes restart envelope cycle. **MIDI-only.** Off = legato. |
| Loop / Bounce / Sust / Inv / Rev / Retrig (ASR) | All shared between ASR 1 / ASR 2 via the LFO Shift button. Loop: cycles A→R while gate held. Bounce: A→R→A→R. Sustain: holds at peak. Invert: 0→+10 V becomes 0→−10 V. Reverse: A and R swap. Retrig: every new gate restarts. ASR retrigger mode is configurable: **"Neutron"** (new notes retrigger) or **"Proton"** (held notes block retrigger) — §15. |

### LFO section (§9)

| Control | Effect |
|---------|--------|
| Shift | Selects which LFO (Red = LFO 1, White = LFO 2) the shared encoder + buttons act on. Also selects which ASR is acted on by the ASR buttons. |
| 1-Shot (LFO 1) | LFO 1 trig jack resets cycle. Long-press = LFO 1 secondary (sync enable, blend toggle, key-track depth, key-track base note). |
| Retrig (LFO 2) | Same as 1-Shot but for LFO 2; also LFO 2's secondary access. |
| Sync | Locks selected LFO to MIDI clock (must be enabled in secondary first). |

### LEDs (red vs white, §2)

> *"Throughout your Proton a red LED indicates that the first of any dual
> item is being controlled where controls are shared, with a white LED
> indicating that the second is under control."*

So: Red = VCO 1 / VCF 1 / LFO 1 / ADSR 1 / ASR 1; White = the second of each pair.

## 6. Pots & knobs (per-section ranges from manual)

### VCOs

| Knob | Range |
|------|-------|
| Tune n | ±13 semitones |
| Width n | 10 % to 90 % duty (square / tone-mod waves only) |
| Sub Mix n | CCW = sub osc only … CW = main only |
| Osc Mix | CCW = VCO 1 only … CW = VCO 2 only |

VCO frequency range: **0.7 Hz – 50 kHz**, controllable from MIDI or
external CV in **−5 V to +5 V** range (§3 intro).

### Wavefolder, filters, VCAs

| Knob | Range |
|------|-------|
| Folds | 0 (silence) → max folding |
| Sym | bipolar; no effect in AM / BP |
| Freq n | **10 Hz – 15 kHz** (§5.2) |
| Reso n | 0 → self-oscillation |
| Mod Depth n | scales **the active mod source** (LFO n, or whatever's patched into VCF n Freq CV — §5.5) |
| Env Depth n | scales ADSR 1's contribution to VCF n |
| Filter Mix | CCW = VCF 1 only … CW = VCF 2 only (into VCA 1) |
| Noise Level | 0 → equal to oscillator mix level |
| Bias n | CCW = VCA fully CV-controlled … CW = VCA forced fully open |

### Levels

| Knob | Notes |
|------|-------|
| Main Vol | Main 6.35 mm output level |
| Ext Level | External-input attenuator into the filter feed (does **not** affect Out (In) jack) |
| Phones | Independent of Main Vol — separate dedicated control on the rear |

### Envelopes

| Knob | Fast range | Slow range |
|------|-----------|-----------|
| ADSR Atk | 300 µs → 7 s | up to 42 s |
| ADSR Dec | 2.4 ms → 20 s | up to 1 min 56 s |
| ADSR Sus | 0 (no sustain) → full | — |
| ADSR Rel | 1.5 ms → 24 s | up to 2 min |
| ASR Atk | 0 ms → 8 s | (no F/S) |
| ASR Rel | 0 ms → 31 s | (no F/S) |

> **Warning** (§8.4): "if Sustain is fully CCW then Release will not function" — a quirk to bake into a patch generator.

### LFOs

| Knob | Range |
|------|-------|
| Rate n | **0.01 Hz – 200 Hz** (§9.1) |
| Depth n | 0 → full amplitude |

### Utilities

| Knob | Effect |
|------|--------|
| CV Mix | Crossfader between CV Mix In 1 / In 2 (default LFO 1 / LFO 2) |
| Portamento | Glide time, MIDI-only |
| Atten 1 / 2 | ±1× attenuverter; centre = mute |

## 7. Modulation matrix (panel-accessible)

Triple = (source, destination, depth control). "Norm" = active without any patch.

| Source | Destination | Depth | Notes |
|--------|-------------|-------|-------|
| LFO 1 | VCF 1 freq | Mod Depth 1 | norm; **defeated** if anything is patched into VCF 1 Freq CV |
| LFO 2 | VCF 2 freq | Mod Depth 2 | norm; **defeated** by VCF 2 Freq CV patch |
| LFO 1 (or inv) | VCF 2 freq | Mod Depth 2 | engaged by Link Red / White; replaces LFO 2 |
| ADSR 1 | VCF 1 freq | Env Depth 1 | norm; sums with the active LFO/patch path |
| ADSR 1 | VCF 2 freq | Env Depth 2 | norm |
| ADSR 2 | VCA 1 gain | (no knob — Bias adds offset; defeat by patching VCA 1 CV) | norm |
| ADSR 2 | VCA 2 gain | (Bias adds offset; defeat by patching VCA 2 CV) | norm |
| Kbd CV | VCO 1, VCO 2, Sub osc 1, Sub osc 2 | always 1 V/oct | norm; OSC n CV jack adds |
| Kbd CV | VCF 1 / 2 cutoff | binary (Key btn) | full 1 V/oct when on |
| Kbd CV | LFO 1 / 2 rate | LFO key-track depth (0–100 %) | secondary panel; base note configurable |
| Velocity / Mod Wheel / Aftertouch | VCF 1 freq | secondary VCF 1 mod depth | one source per VCF |
| Velocity / Mod Wheel / Aftertouch | VCF 2 freq | secondary VCF 2 mod depth | one source per VCF |
| Assign In jack | OSC 1 / OSC 2 / OSC 1&2 / VCF 1 / VCF 2 / VCF 1&2 (one) | Assign In depth 0–100 % | secondary panel |
| Atten 1 in (or default ADSR 1) | Atten 1 OUT → patch | Atten 1 knob | utility |
| Atten 2 in (or default LFO 1 bi) | Atten 2 OUT → patch | Atten 2 knob | utility |

## 8. Implications for a patch generator

- **Three "always-on" mod paths** the panel cannot disable: ADSR 2 → both VCAs, LFO n → VCF n. All defeated **only** by patching the destination's CV jack. A generator should treat the Bias knob as a "force-VCA-open" override and the Mod Depth knob as the LFO/external-mod attenuator.
- **Patched freq CV replaces, not sums.** Treat each VCF Freq CV jack as a single-source slot scaled by Mod Depth — *not* as an additive bus on top of the LFO.
- **Filters are normally parallel.** Series mode requires a patch cable: patch VCF 1 OUT → VCF 2 IN. The Filter Mix knob then has limited effect (output of VCF 2 — which is now post-VCF 1 — into VCA 1). VCA 2 still gets its feed from the VCF 2 OUT tap.
- **VCA 2 is NOT downstream of Filter Mix.** It taps directly off VCF 2. For mono use, route everything through VCA 1 (which receives Filter Mix). For stereo, send VCF 1 → VCA 1 and VCF 2 → VCA 2 (which is the default normalled state).
- **Drum-style patches:** use the ASR envelopes for VCAs (with Loop / Bounce shape) rather than ADSR 2, by patching ASR n OUT → VCA n CV. ADSR 2 then frees up for percussion-shape on the filters.
- **Key-tracking on filters is binary** (on/off via the Key button). 1 V/oct is implied; manual doesn't quote a coefficient — but the SysEx surface (§20) doesn't expose a per-cent depth either, so this should be safe to assume as full tracking. Worth a one-octave bench check before relying on it for self-oscillation tuning.
- **Envelope retrigger requires MIDI** (§8.6). CV/gate triggers don't restart held notes — they just gate.
- **Sustain = 0 disables Release** (§8.4) — important corner case for fade-out patches.
- **MIDI clock forwarding** is configurable via the second panel (§15): off by default, can be enabled. Useful for daisy-chained tempo-locked patches.
- **Polychain support** (§17 / §18) is built in — multiple Protons can divide voices over MIDI. Out of scope for a patch generator on a single unit, but relevant context.
- **Linear FM** (§15: Osc 1 Range + Wave Mode) on each VCO is selectable. The default is exponential — a generator that wants clean through-zero-style FM should explicitly enable linear FM for the carrier.
- **Auto-calibration** is built-in: hold Osc Para + ASR Retrig for 2 s (§15). Worth scheduling at the start of a session — VCOs drift.
- **Factory restore**: hold Osc Para + Osc Sync at power-up (§16).

## 9. SysEx surface (§20) — for future remote control

The Proton accepts System Exclusive messages for almost every parameter
and setting. Format:

```
F0  00 20 32  00 01 25  00  0x74  0x10  <SSPKT>  <Value>  F7
   |Manuf ID  |Model ID  |Dev | PKT | SPKT | sub-spkt | param | end
```

Notable parameter SSPKTs (full table is on pp. 32–35 of the manual):

| SSPKT | Parameter | Range | Notes |
|-------|-----------|-------|-------|
| 0x0F | Polychain enable | 0/1 | |
| 0x13 | Mono key priority | 0/1/2 | low/high/last |
| 0x14 | Paraphonic key priority | 0/1/2 | |
| 0x17 / 0x18 | Assign Out 1 / 2 source | 0..4 | OSC1 CV / OSC2 CV / velocity / mod wheel / aftertouch |
| 0x19 / 0x1A | Min / max MIDI note | 0x0C – 0x60 | C0..C7 |
| 0x1B | Mute out-of-range notes | 0/1 | |
| 0x1D | MIDI clock forward | 0/1 | |
| 0x1E | Assign In destination | 0..6 | None/Osc1/Osc2/Osc1&2/VCF1/VCF2/VCF1&2 |
| 0x1F | Assign In depth | 0..0x65 (= 0..100 %) | |
| 0x21 | Local control on/off | 0/1 | "second panel enabled / disabled" |
| 0x23 | VCF link | 0/1/2 | none / Red / White |
| 0x24 / 0x25 | VCF Soft / Key | 0..3 | per-filter or both |
| 0x26 / 0x2A | VCF 1 / 2 mode | 0/1/2 | HPF/BPF/LPF |
| 0x27 / 0x2B | VCF 1 / 2 secondary mod source | 0..3 | none / velocity / mod wheel / aftertouch |
| 0x28 / 0x2C | VCF 1 / 2 secondary mod depth | 0..0xFC | |
| 0x30 / 0x3A | LFO 1 / 2 shape | 0..4 | sine/triangle/saw/square/ramp |
| 0x31 / 0x3B | LFO blend / switched | 0/1 | |
| 0x32 / 0x3C | LFO 1-shot | 0/1 | |
| 0x33 / 0x3D | LFO retrig | 0/1 | |
| 0x36 / 0x40 | LFO MIDI clock sync | 0/1 | |
| 0x44 | Wavefolder mode | 0..3 | AM / ½ / 1 / BP |
| 0x46 | Osc Sync | 0/1 | |
| 0x47 | Paraphony | 0/1 | mono / paraphonic |
| 0x49 | Sub osc shape | 0..3 | pulse / saw / triangle / sine (no ramp option) |
| 0x4A / 0x56 | Osc 1 / 2 range | 0..3 | 8' / 16' / 32' / free |
| 0x50 / 0x5C | Osc 1 / 2 linear FM | 0/1 | |
| 0x55 / 0x61 | Osc 1 / 2 portamento | 0..0x18 | semitones of glide |
| 0x63 | Envelope F/S | 0/1 | fast/slow |
| 0x64 | Envelope retrigger | 0/1 | |
| 0x65 | Envelope retrigger mode | 0/1 | **Neutron** / **Proton** |
| 0x67 – 0x74 | ASR 1 / 2 each: loop, bounce, sustain, inverse, reverse, retrigger, decay | 0/1 | |

Reset-all SysEx: `F0 00 20 32 00 01 25 00 7D F7`.

This is the surface a remote patch-maker would target — every voicing and
secondary-panel choice has a SysEx parameter. (Per-knob continuous values
like Freq / Reso / Cutoff aren't in this table — they're either real-pot
analog or accessed via MIDI CC, which the manual lists at §19 as **only**
Mod Wheel CC #1 and Sustain CC #64.)

## 10. Open questions / required experiments

Down to four since the manual settled most of the prior ambiguities.

### Q1 — Wavefolder BP mode + Folds CV / Sym CV jacks

Mode = BP. Patch a slow LFO into Folds CV. Listen. Silent → Folds CV is
disabled in BP. Audible → BP only nulls the panel knob; CV jacks still work.
Manual says BP "passes the incoming waveform unaltered" but doesn't
disambiguate whether the Folds-CV summing junction is upstream or downstream
of the bypass node.

### Q2 — Link button precedence over a patched VCF 2 Freq CV

Engage Link Red. Patch a slow square LFO into VCF 2 Freq CV jack. Per §5.5
the Freq-CV patch *replaces* the LFO 2 normal, but the Link button rerouted
that LFO 2 normal to LFO 1's source — so does Link still apply when LFO 2 is
already overridden? Expected: patch wins (single-source slot, last-wins),
but worth confirming with a scope.

### Q3 — VCF Key tracking depth

Self-oscillate VCF 1 (Reso max, no audio in). Key on. Tune VCF 1 to a
known pitch at C2. Play C3 — exact octave shift = full 1 V/oct tracking;
less = partial coefficient. The manual gives no per-cent setting (no SysEx
parameter for it either), so this should be 100 % — but worth a single
bench measurement.

### Q4 — Filter resonance softening: does Soft also affect self-oscillation pitch?

Self-oscillate. Engage Soft (red for VCF 1). Compare frequency and amplitude
to Soft-off. Manual only describes the *resonance saturation* feel, but
softening could shift the self-osc pitch slightly. Capture in a tuner.

---

## Sources

- **Behringer PROTON User Manual V0.0** (PDF supplied locally, sections referenced inline).
- **Behringer Neutron** — see `docs/NEUTRON_SIGNAL_ROUTING.md`. The firmware's
  *"Neutron mode"* / *"Proton mode"* labels in §15 (ASR retrigger) acknowledge
  the lineage, but architecturally the two units are different (Neutron =
  single VCF + LFO, two ADSRs, BBD delay, overdrive, 32 IN; Proton = dual VCF
  + dual LFO, two ADSR + two ASR, wavefolder, no delay, 40 IN). **Do not
  infer Proton routing or normals from Neutron behaviour** — the first round
  of docs in this repo made that mistake and got several details wrong.
