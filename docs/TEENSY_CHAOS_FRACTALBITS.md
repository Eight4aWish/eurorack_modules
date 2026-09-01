# Teensy Chaos — "Fractal Bits" design plan

> **Status: superseded, kept as a record.** v2 scope is oscillator algorithms
> only — see [TEENSY_CHAOS_V2.md](TEENSY_CHAOS_V2.md). The percussion suite, the
> MIDI/trigger kit and the Oscillator|Kit menu below are **not** being built.
> This document is retained for the model-suitability analysis and the audio
> architecture notes, which remain accurate. The current shipping firmware is the
> CV chaos-oscillator with the gate-driven AD/SR envelope
> ([TEENSY_CHAOS.md](TEENSY_CHAOS.md)).

## Concept

Extend teensy_chaos beyond continuous-ODE drones into **finite / percussive**
fractal & digital algorithms ("Fractal Bits"), and let the module work either as
a **CV chaos oscillator** (today) or a **MIDI / trigger fractal drum kit**.
Teensy 4.1 + USB MIDI + the Audio Shield make this a **software-only** build — no
hardware change.

Two hardware constraints, both solved by going MIDI + pan-mix:
- Only two CV trigger inputs (RST, CLK) → use **USB MIDI** to trigger many voices.
- Only two audio outs → **stereo pan-mix** the kit voices into L/R, so the 2-out
  limit becomes *kit panning*.

## Menu / mode structure

Two-level menu (navigation TBD — see open questions):

```
TOP:  Oscillator  |  Kit
  Oscillator → tab through oscillator models
               (continuous ODEs + new dual-citizen models)
  Kit        → MIDI  |  Trigger
        MIDI    → 4 kit pieces, MIDI-note triggered, stereo pan-mix
                  button = randomise a new kit (models + params)
        Trigger → 2 kit pieces, triggered by RST + CLK, stereo pan-mix
```

- **Oscillator mode** = today's behaviour (CV / V-Oct, AD/SR envelope), with an
  expanded model list.
- **Kit mode** voices are built from the **same suite of new models** (the
  perc-capable ones).

## Model suite + suitability tagging

Each model carries a **suitability tag — osc / perc / both** — so the Oscillator
list and the Kit list draw from one suite but filter to what's musical in each,
overlapping on the dual-citizens. Tags are *starting hypotheses*; the boundary
cases are an ear thing, settled by prototype.

| Family            | Examples                                   | Lean   | Why |
|-------------------|--------------------------------------------|--------|-----|
| Continuous ODEs   | Rössler, Lorenz, Chua, Van der Pol, Duffing, Coupled Rössler | **osc**  | continuous, pitched, V/Oct-tracking (already shipping) |
| Discrete maps     | Logistic, Henon, Ikeda                     | **both** | periodic windows = pitched (V/Oct via iteration rate); chaotic = noise/perc |
| Fractal orbits    | Mandelbrot, Julia                          | **perc** | escaping points = finite bursts; inside-set tones don't track V/Oct |
| Bits — bytebeat   | t-driven formulae, e.g. `t*(t>>9 ^ t>>13)` | **both** | t-rate = pitch → glitchy pitched osc; also good perc |
| Bits — cellular automata | Rule 30 / 110                       | **perc** | bitstream → texture / noise |

The two properties that decide *pitched oscillator vs drum hit*:
1. **Periodic vs chaotic regime** — periodic has a pitch; chaotic is noise.
2. **Sustained vs finite** — runs forever (oscillator) or terminates (one-shot perc).

Only models where *rate of iteration / t-increment maps linearly to pitch* will
track 1 V/oct (discrete maps, bytebeat). Fractal orbits don't (pitch = orbit
period), so they lean perc-only.

## Envelope per mode (AD/SR vs AD)

The shipping gate-driven envelope is a **superset** (a short trigger collapses it
to an AD hit). Expose it differently per mode:

- **Oscillator mode → full AD/SR**: attack/decay front + sustain-while-held +
  release on note-off / gate-fall. Continuous voices want to sustain.
- **Kit mode → AD one-shot**: attack + decay, sustain off, note-off ignored by
  default. Optionally expose note-off as a **choke / open-hat** (note-on = open,
  note-off = release) on voices where that's musical — the one place a drum wants
  the gate.

Same envelope class under the hood; what changes per mode is how it's driven and
which controls are exposed (AD + SR in oscillator mode, just AD/decay per piece
in kit mode).

## Audio architecture

- **USB:** switch `[env:teensy_chaos]` from `USB_AUDIO_SERIAL` → `USB_MIDI_SERIAL`
  (USB audio is currently vestigial — no `AudioOutputUSB` is wired). Add `usbMIDI`
  handlers, as on teensy_move. (`USB_MIDI_AUDIO_SERIAL` if we ever want to also
  stream the kit over USB.)
- **Voice** = a `ChaosBase` model + its AD(/SR) envelope + level + pan. Refactor
  the current single stereo `AudioChaosEngine` into N reusable **mono** voices.
- **Stereo pan-mix:** each voice → an L `AudioMixer4` and an R `AudioMixer4` with
  pan-dependent gains → `AudioOutputI2S` (the same mixer/I2S pattern used in
  teensy_move's FX send).
  - MIDI kit: 4 voices → 2× `AudioMixer4` (all four inputs).
  - Trigger kit: 2 voices → same mixers, two inputs.
  - Oscillator mode: 1 voice (stereo X/Y as today, or mono → centre).
- **Triggering:**
  - MIDI kit: 4 kit notes (e.g. 36–39) → voice note-on, velocity → level, one-shot AD.
  - Trigger kit: RST → voice A, CLK → voice B.
- **Randomise kit** (MIDI mode): button assigns each piece a model (from the
  perc/both-tagged suite) + randomised parameters (random Mandelbrot point,
  random Logistic r, random pan, …) → a fresh kit per press.

## Controls (per mode) — detail TBD

- **Oscillator:** as today — CHAOS / RATE / CHAR / DEPTH + ENV page AD/SR.
- **Kit:** pots fine-tune the selected piece (tune / timbre / decay / pan), button
  randomises; piece-select method TBD (pot or button). Velocity from MIDI.
- **OLED:** kit view (4 or 2 pieces, selected piece, levels) vs oscillator view
  (phase plot, params).

## Phasing / build order

Build incrementally, **foreground-flash + bench-test each step** (per the upload
lesson — background uploads bricked the module once).

1. **Probe the boundary** — implement 2 dual-citizen models (Logistic + a
   bytebeat) as `ChaosBase` subclasses in the existing single-engine oscillator
   path; audition pitched-vs-noise to confirm the tags. *Lowest risk; pure
   additions, like the existing algorithms.*
2. **USB MIDI + one panned voice** — switch USB type, add `usbMIDI`, refactor to a
   Voice + L/R pan mixers, trigger one voice from MIDI. Proves the kit signal path.
3. **MIDI 4-piece kit** — 4 voices, note-mapping, AD one-shot envelopes, pan,
   randomise-kit.
4. **Trigger 2-piece kit** — RST / CLK → 2 voices.
5. **Menu layer** — wire Oscillator|Kit / MIDI|Trigger navigation + per-mode OLED
   and controls.
6. **Expand the suite** — Mandelbrot/Julia bursts, Henon, cellular automata; finalise
   suitability tags from how they actually sound.

## Open questions

- Exact menu navigation (button gestures vs a pot) given the small UI.
- Kit-mode pot mapping (piece-select + which params).
- MIDI note map (fixed 36–39 vs assignable) and per-voice choke option.
- Whether oscillator mode keeps V/Oct on the dual-citizen models (iteration-rate pitch).
- USB: stay MIDI+Serial, or MIDI+Audio+Serial to also stream the kit over USB.
