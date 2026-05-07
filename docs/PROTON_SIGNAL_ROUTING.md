# Behringer Proton — Signal Routing Reference

> **The Proton is NOT a Pro-One clone.** That's the Behringer **PRO-1** — a separate
> desktop module sold from 2018. The Proton is an original Behringer design,
> explicitly described as "Behringer cloning themselves" — a successor / expansion
> of the Neutron. Dual VCOs (5 blendable waves + per-VCO sub-osc), wavefolder,
> dual multimode VCFs, dual VCAs, 2× ADSR, 2× ASR (with loop / bounce / invert /
> reverse), 2× LFO, 64-jack patchbay (40 in / 24 out), 68 panel controls. There
> is no Pro-One-style "MOD MIX / DIRECT" routing switch — modulation is normalled
> in parallel and overridden by patching.

## 1. Block diagram (default normalled path)

```
                    [MIDI / USB]
                         |
                    keyboard CV (1V/oct) + gate
                         |
        +----------------+----------------+
        |                                 |
   [VCO 1]                            [VCO 2]
   - Tune (±13 st)                    - Tune
   - Range (32/16/8 ft)               - Range
   - Shape (5: tone-mod/pulse/        - Shape
            saw/tri/sine; switch      - PW
            or blend mode)            - Sub-mix
   - PW                               |
   - Sub-mix (main vs. sub)       sub-osc 2 ──→ Sum-Out (default if no Sum A/B)
   |                                  |
   sub-osc 1 ──→ Sum-Out              |
        |                             |
        +-------- Osc Mix knob -------+
                         |
                    [Osc Mix]            ← (also tapped to OSC 1 / OSC 2 / OSC MIX OUT jacks)
                         |
                + ─── [Noise Gen] (Noise Level knob, mixed into VCF input bus)
                |        ext-in (Ext Level knob attenuates ext-in path)
                |
                v
                  [Wavefolder]
                  - Folds, Symmetry
                  - Mode: AM | ½ | 1 | BP
                         |
            +────────────+────────────+
            |                         |
        [VCF 1]                   [VCF 2]
        Mode: LPF/BPF/HPF         Mode: LPF/BPF/HPF
        Freq, Reso                Freq, Reso
        Mod Depth (LFO1)          Mod Depth (LFO2)
        Env Depth (ADSR1)         Env Depth (ADSR1)
        Key, Soft                 Key, Soft
            |                         |
            +-- Filter Mix knob ──────+──── (tap) ──→ [VCA 2]
            |    (CCW = VCF1, CW = VCF2)              Bias
            |                                         CV: ADSR2
        [VCA 1]                                          |
        Bias                                       [VCA 2 OUT jack]
        CV: ADSR2
            |
        [Main Out / Phones]
```

Always-on modulation web (parallel; user can only override by patching the destination jack):

```
LFO1  ─(Mod Depth1)─→ VCF1 freq
LFO2  ─(Mod Depth2)─→ VCF2 freq
ADSR1 ─(Env Depth1)─→ VCF1 freq
ADSR1 ─(Env Depth2)─→ VCF2 freq
ADSR2 ─────────────→ VCA1 CV, VCA2 CV   (hardwired; Bias adds offset)
KbdCV ─(Key btn)───→ VCF1 / VCF2 tracking
KbdCV ─(always)────→ VCO1, VCO2, sub-oscs
```

Filters are **always parallel.** There is no series / parallel switch. The
Filter Mix knob is a crossfader between the two parallel outputs into VCA1.
VCA2 is normalled directly off VCF2 ([manuals.plus](https://manuals.plus/behringer/proton-analog-paraphonic-semi-modular-synthesizer-manual)):
> "VCA 1 receives the output of the filter mix, and VCA 2 receives the output of VCF 2."

## 2. CV input table (40 IN jacks)

All `−5 V to +5 V` inputs are bipolar around 0 V. Audio inputs are AC-coupled
module-level (~10 Vpp). Inserting a plug breaks the normal where indicated.

| #  | Jack                | Range            | Semantic                                                        | Normalled From            | Insert overrides? |
|---:|---------------------|------------------|-----------------------------------------------------------------|---------------------------|-------------------|
|  1 | OSC 1 CV            | −5..+5 V         | Pitch (1 V/oct, sums with kbd)                                  | Internal MIDI/kbd CV      | Sums              |
|  2 | OSC 2 CV            | −5..+5 V         | Pitch (1 V/oct, sums with kbd)                                  | MIDI/kbd CV               | Sums              |
|  3 | OSC 1+2 CV          | −5..+5 V         | Pitch to both                                                   | —                         | Sums              |
|  4 | OSC 1 PW            | −5..+5 V         | Pulse-width modulation                                          | Panel PW knob             | Sums              |
|  5 | OSC 2 PW            | −5..+5 V         | Pulse-width modulation                                          | Panel PW knob             | Sums              |
|  6 | OSC 1 Shape         | −5..+5 V         | Sweeps shape (or blends if blend mode)                          | Panel Shape               | Sums              |
|  7 | OSC 2 Shape         | −5..+5 V         | Sweeps shape                                                    | Panel Shape               | Sums              |
|  8 | Wavefolder Audio In | audio            | Replaces Osc Mix into wavefolder                                | Internal Osc Mix          | **Yes**           |
|  9 | Folds CV            | −5..+5 V         | Folds amount                                                    | Panel Folds               | Sums              |
| 10 | Symmetry CV         | −5..+5 V         | Asymmetry                                                       | Panel Symmetry            | Sums              |
| 11 | VCF 1 Audio In      | audio            | Replaces wavefolder feed                                        | Wavefolder out            | **Yes**           |
| 12 | VCF 1 Freq CV       | −5..+5 V         | Cutoff modulation                                               | LFO1·ModDepth1 + ADSR1·EnvDepth1 | Sums w/ internals (open Q1) |
| 13 | VCF 1 Reso CV       | −5..+5 V         | Resonance                                                       | Panel Reso                | Sums              |
| 14 | VCF 2 Audio In      | audio            | Replaces wavefolder feed                                        | Wavefolder out            | **Yes**           |
| 15 | VCF 2 Freq CV       | −5..+5 V         | Cutoff modulation                                               | LFO2·ModDepth2 + ADSR1·EnvDepth2 | Sums (open Q1)    |
| 16 | VCF 2 Reso CV       | −5..+5 V         | Resonance                                                       | Panel Reso                | Sums              |
| 17 | VCA 1 Audio In      | audio            | Replaces filter-mix feed                                        | Filter Mix                | **Yes**           |
| 18 | VCA 1 CV            | −5..+5 V (0..+5 usable) | Gain control                                              | ADSR2                     | **Yes** — replaces ADSR2 normal; sums with Bias |
| 19 | VCA 2 Audio In      | audio            | Replaces VCF2 feed                                              | VCF 2 out                 | **Yes**           |
| 20 | VCA 2 CV            | −5..+5 V (0..+5 usable) | Gain control                                              | ADSR2                     | **Yes** — sums w/ Bias |
| 21 | Ext In              | audio            | External audio into filter feed (Ext Level attenuates)          | Rear-panel jack mirror    | Duplicate of rear |
| 22 | Out (In)            | audio            | Direct to main out, bypassing internal flow; not affected by Ext Level | —                | **Yes** — overrides VCA1 to main out |
| 23 | LFO 1 Trig          | gate             | Resets LFO1 cycle if 1-Shot or Retrig enabled                   | —                         | —                 |
| 24 | LFO 2 Trig          | gate             | Resets LFO2 cycle if 1-Shot or Retrig enabled                   | —                         | —                 |
| 25 | LFO 1 Rate CV       | −5..+5 V         | Exp rate mod                                                    | Panel Rate                | Sums              |
| 26 | LFO 2 Rate CV       | −5..+5 V         | Exp rate mod                                                    | Panel Rate                | Sums              |
| 27 | LFO 1 Shape CV      | −5..+5 V         | Sweeps LFO1 wave (blend or switch)                              | Panel selector            | Sums              |
| 28 | LFO 2 Shape CV      | −5..+5 V         | Sweeps LFO2 wave                                                | Panel selector            | Sums              |
| 29 | ADSR 1 Gate         | gate             | Triggers ADSR1                                                  | Internal MIDI gate (open Q3) | **Yes**         |
| 30 | ADSR 2 Gate         | gate             | Triggers ADSR2                                                  | MIDI gate                 | **Yes**           |
| 31 | ASR 1 Gate          | gate             | Triggers ASR1                                                   | MIDI gate (open Q3)       | **Yes**           |
| 32 | ASR 2 Gate          | gate             | Triggers ASR2                                                   | MIDI gate (open Q3)       | **Yes**           |
| 33 | Atten 1 In          | −5..+5 V         | Source for Atten 1                                              | ADSR1                     | **Yes**           |
| 34 | Atten 2 In          | −5..+5 V         | Source for Atten 2                                              | LFO1 bipolar              | **Yes**           |
| 35 | Mult In             | any              | Splits to Mult 1 + Mult 2                                       | —                         | —                 |
| 36 | CV Mix In 1         | −5..+5 V         | Source A for CV Mix                                             | LFO1                      | **Yes**           |
| 37 | CV Mix In 2         | −5..+5 V         | Source B for CV Mix                                             | LFO2                      | **Yes**           |
| 38 | Assign In           | −5..+5 V         | Routed to user-selected target (none / OSC1 / OSC2 / OSC1+2 / VCF1 / VCF2 / VCF1+2) with depth 0–100% | — | — |
| 39 | Sum A               | −5..+5 V         | Sums with Sum B                                                 | sub-osc 1                 | **Yes**           |
| 40 | Sum B               | −5..+5 V         | Sums with Sum A                                                 | sub-osc 2                 | **Yes**           |

### 24 OUT jacks

OSC1, OSC2, OSC MIX, WAVEFOLDER, VCF1, VCF2, VCF MIX, VCA1, VCA2, MULT1, MULT2,
ATTEN1, ATTEN2, CV MIX, SUM, ADSR1, ADSR2, ASR1, ASR2, LFO1 UNI (0..+5 V),
LFO1 BI (−5..+5 V), LFO2 UNI, LFO2 BI, ASSIGN OUT (selectable source: OSC1 CV /
OSC2 CV / velocity / mod wheel / aftertouch).

## 3. Switch / mode table

| Switch                       | Section    | Positions                                            | Effect                                                                                                  | Overrides patched CV?                                              |
|------------------------------|------------|------------------------------------------------------|---------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------|
| Range (per VCO)              | Osc        | 32′ / 16′ / 8′                                       | Octave; long-press → secondary (sub-osc waveform via LFO encoder; blend toggle via Para)                | No                                                                 |
| Shape (per VCO)              | Osc        | 5-pos rotary: tone-mod/pulse/saw/tri/sine            | Discrete or blended morph                                                                               | No (Shape CV jack always sums)                                     |
| Sync btn                     | Osc        | momentary toggle                                     | Hard-syncs Osc 2 to Osc 1; long-press → mono priority / Assign-Out source                              | n/a                                                                |
| Para btn                     | Osc        | toggle                                               | Paraphonic split: MIDI note 1 → VCO1, note 2 → VCO2                                                     | Disables global kbd-CV summing into both VCOs; each VCO tracks its own note |
| Wavefolder Mode              | Folder     | AM / ½ / 1 / BP                                      | AM: Sym disabled, Folds = AM amount; ½: half-strength; 1: full; BP: bypass                              | No (BP nulls panel knobs only — open Q4 re: jacks)                 |
| VCF Mode (per VCF)           | Filter     | LPF / BPF / HPF                                      | Filter type; long-press → secondary mod source (none / velocity / modwheel / aftertouch) + depth        | No                                                                 |
| Link btn                     | Filter     | off / Red / White                                    | off: independent; Red: VCF2 mod source = VCF1 mod source (replaces, not sums); White: inverted copy     | **Yes** — VCF2 internal normal is replaced (open Q5 re: patched-jack precedence) |
| Soft btn                     | Filter     | off / VCF1 / VCF2 / both                             | Softens resonance saturation                                                                            | No                                                                 |
| Key btn                      | Filter     | off / VCF1 / VCF2 / both                             | Enables 1 V/oct kbd tracking on selected VCF(s)                                                         | No                                                                 |
| Shift btn                    | LFO/ASR    | toggles Red ↔ White                                  | Selects which LFO (or ASR) the shared encoder/buttons act on                                            | n/a                                                                |
| 1-Shot btn                   | LFO1       | toggle                                               | LFO1 trig jack resets cycle once; long-press → secondary (sync enable / blend / key-track depth)        | No                                                                 |
| Retrig btn                   | LFO2       | toggle                                               | LFO2 trig jack resets; long-press → LFO2 secondary                                                      | No                                                                 |
| Sync btn (LFO)               | LFO        | toggle (per-LFO via Shift)                           | Locks LFO to MIDI clock (must be enabled in secondary first)                                            | n/a                                                                |
| F/S btn                      | ADSR       | toggle (per-ADSR via Shift)                          | Fast/Slow time range. Fast: A 300 µs–7 s, D 2.4 ms–20 s, R 1.5 ms–24 s. Slow: A→42 s, D→~116 s, R→~120 s | n/a                                                                |
| Retrig btn                   | ADSR       | toggle                                               | Re-triggers envelope on each new note (legato off)                                                      | n/a                                                                |
| Loop / Bounce / Sustain / Invert / Reverse / Retrig | ASR | shared via Shift                            | Loop: cycles A→R; Bounce: A→R→A→R; Sustain: holds peak; Invert: flip polarity (0..−10 V); Reverse: swap A↔R; Retrig: fresh cycle | n/a                                                                |

## 4. Pot table

| Pot                  | Section  | Range                | Scales                                                                            | CV-summable?                                                                  |
|----------------------|----------|----------------------|-----------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| Tune (per VCO)       | Osc      | ±13 st               | Coarse pitch                                                                      | + OSC n CV jack                                                               |
| Pulse Width (per VCO)| Osc      | ~10–90% duty         | Pulse waveform width                                                              | + PW jack                                                                     |
| Sub-Mix (per VCO)    | Osc      | CCW = sub … CW = main | Crossfade sub vs. main                                                            | No                                                                            |
| Osc Mix              | Osc      | CCW = VCO1 … CW = VCO2 | VCO crossfade into Osc Mix bus                                                    | No                                                                            |
| Folds                | Folder   | 0–max                | Wavefold drive (in AM = amplitude attenuator on input)                            | + Folds CV                                                                    |
| Symmetry             | Folder   | bipolar feel         | Asymmetric fold offset (no effect in AM/BP)                                       | + Sym CV                                                                      |
| Freq (per VCF)       | Filter   | ~10 Hz–15 kHz        | Cutoff                                                                            | + Freq CV jack + ADSR1·EnvDepth + LFOn·ModDepth                               |
| Reso (per VCF)       | Filter   | 0 → self-osc         | Resonance                                                                         | + Reso CV                                                                     |
| Mod Depth (per VCF)  | Filter   | 0 → +5 V scale       | Attenuates **internal LFO** path into VCF (NOT the patched Freq CV)               | Scales internal normal only — open Q1                                         |
| Env Depth (per VCF)  | Filter   | 0 → full             | Attenuates **internal ADSR1** path into VCF                                       | Same — open Q1                                                                |
| Filter Mix           | Filter   | CCW = VCF1 … CW = VCF2 | Crossfade VCF1/VCF2 into VCA1 (post-VCF)                                          | No                                                                            |
| Noise Level          | Filter   | 0 → osc-mix-level    | White-noise mixed into filter input bus                                           | No                                                                            |
| Bias (per VCA)       | VCA      | CCW … CW             | DC offset gain (CCW = fully CV-controlled, CW = fully open)                       | + VCA CV jack — open Q2 (clip behaviour)                                      |
| Ext Level            | Ext      | 0 → unity            | Gain on rear/front Ext In (does NOT affect Out (In) jack)                         | n/a                                                                           |
| Volume               | Output   | 0 → max              | Main out (headphone level claimed independent — open Q6)                          | n/a                                                                           |
| Atk/Dec/Sus/Rel (per ADSR) | ADSR | per F/S range      | Envelope segments                                                                 | n/a                                                                           |
| Atk/Rel (per ASR, shared)  | ASR  | 0–8 s / 0–31 s     | Envelope segments                                                                 | n/a                                                                           |
| Rate (per LFO)       | LFO      | 0.01–200 Hz          | Frequency                                                                         | + Rate CV                                                                     |
| Depth (per LFO)      | LFO      | 0 → +5 V             | LFO output amplitude                                                              | n/a                                                                           |
| LFO encoder          | LFO/secondary | continuous      | Secondary-panel value entry (sub waveform, secondary mod source, depths, etc.)    | n/a                                                                           |
| Atten 1 / Atten 2    | Util     | −1× … 0 … +1×        | Attenuverter; centre is mute                                                      | n/a                                                                           |
| CV Mix               | Util     | CCW = src1 … CW = src2 | Crossfader between CV Mix In 1 / In 2                                             | n/a                                                                           |
| Portamento           | Glide    | off → long           | MIDI-only glide between successive notes                                          | n/a                                                                           |

## 5. Modulation matrix (panel-accessible)

`norm` = active without any patch cable. Triple = (source, destination, depth control).

| Source                | Destination                        | Depth                                          | Notes                                                              |
|-----------------------|------------------------------------|------------------------------------------------|--------------------------------------------------------------------|
| LFO1                  | VCF1 freq                          | Mod Depth 1                                    | norm; defeated only by patching VCF1 Freq CV (open Q1)             |
| LFO2                  | VCF2 freq                          | Mod Depth 2                                    | norm; same                                                         |
| LFO1                  | VCF2 freq                          | Mod Depth 2                                    | engaged by **Link** Red; replaces LFO2                             |
| LFO1 (inverted)       | VCF2 freq                          | Mod Depth 2                                    | engaged by **Link** White                                          |
| ADSR1                 | VCF1 freq                          | Env Depth 1                                    | norm; additive with LFO1                                           |
| ADSR1                 | VCF2 freq                          | Env Depth 2                                    | norm                                                               |
| ADSR2                 | VCA1 gain                          | (none — fixed)                                 | norm; offset by VCA1 Bias                                          |
| ADSR2                 | VCA2 gain                          | (none)                                         | norm; offset by VCA2 Bias                                          |
| Kbd CV                | VCO1 pitch                         | always 1 V/oct                                 | norm; +OSC1 CV jack additive                                       |
| Kbd CV                | VCO2 pitch                         | always 1 V/oct                                 | norm; +OSC2 CV jack                                                |
| Kbd CV                | VCF1 cutoff                        | full or 0 (Key btn)                            | binary tracking — open Q7 (depth coefficient)                      |
| Kbd CV                | VCF2 cutoff                        | full or 0 (Key btn)                            | binary tracking                                                    |
| Kbd CV                | LFO1 rate                          | LFO1 key-track depth (0–100%)                  | secondary-panel                                                    |
| Kbd CV                | LFO2 rate                          | LFO2 key-track depth (0–100%)                  | secondary-panel                                                    |
| Velocity / ModWheel / AT | VCF1 freq                       | secondary depth                                | one source per VCF, set via VCF Mode long-press                    |
| Velocity / ModWheel / AT | VCF2 freq                       | secondary depth                                | same                                                               |
| Assign In jack        | OSC1 / OSC2 / OSC1+2 / VCF1 / VCF2 / VCF1+2 (one) | Assign depth 0–100%               | secondary via Wavefolder Mode long-press                           |
| Atten 1 in (or default ADSR1) | Atten 1 out → patch          | Atten 1 knob                                   | utility — output is patched, not normalled to a destination        |
| Atten 2 in (or default LFO1 bi) | Atten 2 out → patch        | Atten 2 knob                                   | utility                                                            |
| CV Mix In 1+In 2 (or default LFO1+LFO2) | CV Mix Out → patch  | CV Mix crossfade                               | utility                                                            |
| Sum A+Sum B (or default sub1+sub2) | Sum Out → patch          | (no knob; pure sum)                            | utility                                                            |

**Implications for a patch maker.** Three "always-on" mod paths the user cannot
disable from the panel: `ADSR2 → VCA` (defeat by patching VCA n CV with
something else, or by setting Bias to fully open), and the two `LFO → VCF`
normals (defeat by patching VCFn Freq CV — pending open Q1). Mod Depth and Env
Depth knobs scale only the internal normal, so a patch generator should treat
them as scalars on `LFOn` and `ADSR1` respectively, and treat any patched VCFn
Freq CV as a separate additive bus.

## 6. Pitfalls / gotchas for programmatic patches

- **Filter is always parallel.** No series mode unless you patch VCF1 OUT → VCF2 audio in (which then overrides VCF2's wavefolder feed).
- **Filter Mix is post-VCF.** Both filters always run; the knob just blends their outputs into VCA1.
- **VCA2 is a side-chain off VCF2,** bypassing Filter Mix — useful for stereo, surprising for mono.
- **ADSR2 is hardwired to both VCAs.**
- **ADSR1 is hardwired to both VCFs,** scaled per-filter by Env Depth.
- **Wavefolder BP mode** mutes folder processing but the audio still flows through the path.
- **`Out (In)` jack** overrides the entire internal path to the main output — useful for inserting external FX returns.
- **Sum jack defaults to sub-osc 1 + sub-osc 2** when nothing is patched into Sum A/B.
- **Atten 1 default = ADSR1, Atten 2 default = LFO1 bipolar.** Free attenuated copies even with no input patched.
- **Assign In = single destination at a time.**
- **Paraphonic mode** breaks the "kbd CV → both VCOs" mental model; OSC1+2 CV jack still sums.
- **Link button replaces (not sums) VCF2's mod source.** Manual: "VCF 2 will be modulated by the VCF 1 modulation source rather than that of VCF 2."
- **LFO Trig jacks do nothing** unless 1-Shot (LFO1) or Retrig (LFO2) is enabled.

## 7. Open questions / required experiments

### Q1 — Does Mod Depth / Env Depth scale only the internal normal, or all sources at the Freq CV jack?

Patch +1 V DC into VCF1 Freq CV. VCF1 LPF, Reso 0, sine osc through (BP wavefolder) → VCF1. Sweep **Mod Depth 1**: if cutoff moves, the knob also scales the patched CV (sums with internal). If cutoff is unchanged, Mod Depth scales only the internal normal. Repeat for Env Depth 1 while gating ADSR1. **Expected**: Mod / Env Depth scale only the internal source.

### Q2 — VCA Bias + CV summing: linear add or saturate?

Patch ADSR2 into VCA1 CV (replacing the internal normal). Sine through VCA1 audio. Bias = 0: shape follows envelope. Bias = noon: scope VCA1 OUT vs ADSR2 OUT — does the floor lift linearly (additive) or does the peak clip (saturating sum)? **Expected**: linear additive up to a clip ceiling.

### Q3 — Are ADSR1, ADSR2, ASR1, ASR2 auto-gated by MIDI note-on with no patch?

No cables. Send a MIDI note. Scope all four envelope OUT jacks. Are all four firing on the same gate? **Expected**: yes (semi-modular convention), but the manual is silent.

### Q4 — Wavefolder BP mode: do the Folds CV / Sym CV jacks still respond?

Mode = BP. Patch a slow LFO into Folds CV. Listen. Silent → true bypass; modulating → BP only nulls the panel knobs.

### Q5 — Link button + patched VCF2 Freq CV: which wins?

Engage Link Red. Patch a slow square LFO into VCF2 Freq CV jack. If VCF2 cutoff follows the patched LFO → patch wins; if it follows VCF1's source → Link wins. Try Link White (inverted) for clarity. **Expected**: patch wins (typical semi-modular precedence).

### Q6 — Headphone level: independent of Volume knob?

Trivial knob test. Manual claims independent; cross-checks contradict.

### Q7 — Filter keyboard tracking depth: 100% 1 V/oct or partial?

Self-oscillate VCF1 (Reso max, no audio in). Key btn on. Tune VCF1 to a known pitch at C2. Play C3 — exactly one octave shift = 100% tracking; less = partial coefficient (50% common on Roland-style designs).

### Q8 — Assign In depth: linear scalar to ±5 V?

Patch +5 V DC into Assign In, dest = OSC1. Depth 100% → measure pitch shift. Compare to OSC1 CV jack with same +5 V (5 octaves at 1 V/oct). Equal → linear scalar to a ±5 V internal bus. Less → smaller internal range.

### Q9 — VCF mode (LPF / BPF / HPF) consistency

Sweep cutoff with white noise on each mode; capture spectrum in a DAW analyzer. Compare slope and resonance peak height per mode. **Expected**: same slope (~12 dB/oct), Soft btn affects resonance compression equally.

### Q10 — Auto-calibration drift after warm-up

After power-up auto-cal, hold C4 every 5 min for 30 min, feed OSC MIX OUT to a tuner, log cents. > 10¢ drift @ 30 min argues for a re-cal trigger before serious work.

## Sources

- Behringer Proton user manual (extract via [manuals.plus](https://manuals.plus/behringer/proton-analog-paraphonic-semi-modular-synthesizer-manual)) — primary
- [Behringer product page](https://www.behringer.com/product.html?modelCode=0718-AAO)
- [Synthtopia 2024-07 spec rundown](https://www.synthtopia.com/content/2024/07/17/behringer-proton-official-specs-intro-video-is-it-their-best-analog-monosynth-yet/)
- [Synthtopia 2024-09 hands-on review](https://www.synthtopia.com/content/2024/09/23/behringer-proton-synthesizer-hands-on-review/)
- [Synthanatomy 2025-05 firmware 1.0.3](https://synthanatomy.com/2025/05/behringer-proton-neutron-semi-modular-analog-synthesizer-on-steroids.html)
- [MusicTech feature](https://musictech.com/news/gear/behringer-proton-semi-modular-clones-itself-neutron-wave-morphing/)
- [SonicLAB review post](https://sonicstate.com/news/2025/06/05/soniclab-berhinger-original-proton-synth/)
- [Modwiggler thread t=259428](https://modwiggler.com/forum/viewtopic.php?t=259428) (and pages 2–4)
- [Synthmagazine — XNBeatsMusic Patchbay piece](https://synthmagazine.com/xnbeatsmusics-behringer-proton-the-patchbay-pandemonium/)

For comparison only — not the Proton — Behringer **PRO-1** = the Pro-One clone:
[Sonicstate 2018](https://sonicstate.com/news/2018/03/15/behringers-pro-one-clone-revealed/),
[Synthanatomy 2019](https://synthanatomy.com/2019/10/behringers-sequential-pro-1-clone-in-eurorack-is-ready-for-pre-order.html).
