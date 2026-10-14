# Big Genes patches

Ksoloti patches for the Big Genes, separate from Girl's firmware
([`src/ksoloti_elements`](../../src/ksoloti_elements/)). They were generated and edited
directly as `.axp` XML, to avoid the slow GUI patcher during iterative work.

## Files
- `move_midi_drums_cv_gate.axp`
  - Ableton Move MIDI performance patch
  - MIDI ch10 drums + MIDI→CV/Gate for ch1/ch2
  - OLED diagnostics on Big Genes display, from four inline `patch/object` helpers
- `big_genes_template.axp`
  - The Big Genes hardware I/O laid out for fast patching

Ksoloti's own Big Genes help patches (`test all`, `cv in`, `cv out`, `pot`, and others)
are not copied here: they ship in
[ksoloti-objects](https://github.com/ksoloti/ksoloti-objects) under
`objects/ksoloti/big genes/` (GPL-3.0). `test all big genes` is the known-good hardware
sanity test referred to below.

## move_midi_drums_cv_gate.axp behavior
- MIDI channel 10 (drums)
  - Note 36 = Kick
  - Note 38 = Snare
  - Note 42 = Hat
- MIDI channel 1
  - Note → CV1 pitch (`ksoloti/big genes/cv out b`)
  - Gate → Gate1 (`ksoloti/big genes/gate out`, gate=1)
- MIDI channel 2
  - Note → CV2 pitch
  - Gate → Gate2
- OLED diagnostics (`ksoloti/big genes/display`, narrow font)
  - Line 1: header
  - Line 2: CH1 note + gate
  - Line 3: CH2 note + gate
  - Line 4: drum gate activity

## Important fixes already applied
These were required to get the patch to compile/run on this system:

1) Patch compile error (missing header)
- Error was: `fatal error: chibios/os/various/chprintf.h: No such file or directory`
- Fix: inline diagnostic `patch/object` helpers now include `os/various/chprintf.h`.

2) CV out attribute mismatch
- The CV out object `offset` selection must match the installed object menu entries.
- Fix: set `offset = "E4 (0) = 0V (Axoloti)"` (matches Ksoloti's `test all big genes` help patch).

3) Gate wiring
- `ksoloti/midi/in/keyb ch` has both `gate` and `gate2`.
- Fix: Big Genes Gate outputs and OLED diagnostics use `gate` (not `gate2`), because `gate2` behavior/timing was not reliable for the external gate outputs.

4) Drum triggering reliability
- Drum triggering previously used `logic/change` → `timer/pulselength` → `env/adsr`.
- Fix: removed `logic/change` and now trigger `timer/pulselength` directly from each drum MIDI gate (pattern similar to upstream `1d_stepseq` demo).
- Shortened drum ADSR releases to reduce overly-long decays.

## Current status (as of 2026-01-05)
- Ksoloti's `test all big genes` help patch works on the module.
- `move_midi_drums_cv_gate.axp` compiles and runs.
- External gate outputs (Gate1/Gate2) confirmed working.
- Drums: MIDI activity/gates are detected (OLED shows activity), but no audible drum output confirmed yet.
- Pitch CV output not yet confirmed (could be calibration or hardware/module side).

## Suggested next steps (debug checklist)
1) Confirm audio output path in `move_midi_drums_cv_gate.axp`
- Temporarily replace the drum mix with a simple oscillator (e.g., `osc/sine` or similar) routed directly into `drj/audio/out_stereo_vol` to verify audio out works in this patch.

2) If audio out works, isolate drums
- Route only kick directly to output, then only snare, then only hat.
- If a voice is silent, inspect its signal chain (VCA, filter, comb, envelope).

3) Confirm pitch CV
- Use the CV section of the `test all big genes` help patch to confirm CV1/CV2 are physically correct.
- Then test Move ch1/ch2 note output and verify that CV changes across notes.
- If needed, re-run/calibrate 1V/oct using the existing CV out help patch.

## Notes
- Big Genes has 2 CV outs + 2 digital gate outs. If you need a CV envelope for an external VCA, it would require using one CV output for envelope (sacrificing one pitch CV). Current patch keeps both pitches and leaves amplitude shaping to external modules (e.g., ADSR + VCA in the rack).
