# Teensy 4.1 — `teensy_move`: Boy

Boy on the analogue board Rev A (Teensy 4.1, `teensy_core_midi`), in a firmware with modes. It took
over `teensy_move` on 2026-11-01.

The old Boy, on its hand-built hardware with the Teensy Expander, is now
[`teensy_move_old`](TEENSY_MOVE_OLD.md). Once the new Boy has taken over, the old one is
decommissioned and its code archived or deleted, when David says.

The board, its pin map, the chips and the start-up rules are in
[`eurorack_electronics/docs/analogue_board_reva_firmware.md`](https://github.com/Eight4aWish/eurorack_electronics/blob/main/docs/analogue_board_reva_firmware.md).
Read it before changing `board.cpp`.

**Status, 2026-11-01:** mode 1 is on Boy and works on the bench (David): voices, drums, clock
and reset, the encoder and the chooser. TRS MIDI in is the one thing not yet tried: see
[First flash](#first-flash).

**2026-11-02:** Warps, now mode 3, is on Boy. The codec runs at 96 kHz, and the internal carrier
is in tune and clean in all its shapes (David).

**2026-11-04:** CV in works, by ear, from V/oct into Warps' internal oscillator (David), the first
time a Rev A CV input has read another module. Tracking and calibration are for later, on purpose.
Audio in and the frequency shifter are still to try: see [Mode 3 on the bench](#mode-3-on-the-bench).

**2026-11-05:** the set of modes is Move, Chordal, Warps (David). Chordal is new and goes in as mode 2,
so Warps moves to mode 3, and now runs Parasites' DSP, with stock Warps as its META mode. Flashing this
resets Warps' settings once, because its EEPROM page moved. Flashed the same day: Chordal starts and
runs at 26 % CPU on PAD (the serial monitor). Nothing has been played or heard through it on the module
yet, and Parasites hasn't been tried.

## Modes

The mode is chosen at power-on and holds for the session, because modes set the six CV jacks as
inputs or outputs, and that shouldn't change under a patch.

- **The chooser** starts on the last mode. Turn the encoder to pick another and press to start.
  Untouched, it starts the last mode after 3 s, so a rack powering up comes back as it was.
- **Back to the chooser without a power cycle:** hold the encoder's push for 2 s in any mode. The
  board goes to its power-on state (jack switches off, gate outs low and off, DAC at 0 V) and the
  Teensy restarts into the chooser, which then waits for a press. A restart means every mode starts
  from a clean Teensy and none has to undo another's audio or MIDI set-up.

| Mode | What | Status |
|---|---|---|
| 1 | **Move companion** | works on the bench, 2026-11-01; TRS MIDI in not yet tried |
| 2 | **Chordal**: one finger plays four notes that lead into the next chord, from a bank of 16 patches | flashed 2026-11-05; runs at 26 % CPU on PAD, not yet played |
| 3 | **Warps**, a port of Mutable's source with Parasites' eight further modes (`third_party/parasites`, MIT), and Chordal's engine as IN 1 | stock Warps on the bench, 2026-11-02: 96 kHz and the internal carrier pass. Parasites flashed 2026-11-05, not yet tried |

## Mode 1, the Move companion

| MIDI | Out |
|---|---|
| ch 1–3 notes | GATE1–3 (+5 V) and PITCH1–3, 1 V/oct, note 36 at 0 V (the old Boy's law); notes above about 99 clip |
| ch 1–3 pitch bend | ±2 semitones |
| ch 10 note 36, note 37 | DRM1, DRM2: 500 µs pulses |
| clock | CLK: a 5 ms pulse every 16th (24 ppqn ÷ 6) |
| Start | RST: an 8 ms pulse; Stop drops every gate |

- **MIDI comes in on USB** (the Move) **and the TRS input** (Type A), merged. If both send clock,
  CLK runs at the sum.
- **Encoder:** a long press (0.6 s) sends a reset pulse; hold 2 s for the chooser.
- **Screen:**
  - row 1: the gates, as note names while they're on, then the drums and `CLK` while clock arrives;
  - row 2: the three pitch voltages;
  - row 3: the last MIDI event and its input for a second.
- **The audio outs** sit at 0 V: the codec runs, sending silence.

**What changed from the old Boy:**
- three voices, not four; no mod outs, so no velocity or CC#42;
- two drums, not four;
- no FX;
- no pots, and the encoder replaces the button;
- TRS MIDI in.

Fixed in passing:
- **Pitch bend.** Teensy's `usbMIDI` already centres the bend at 0, and the old Boy subtracted
  8192 again, so a centred bend left a voice two semitones flat.
- **The screen's I2C clock.** Adafruit's library sets the bus back to 100 kHz after each of its
  transfers unless told otherwise (`clkAfter`), and the old Boy's `oled.begin()` and `oled.dim()`
  left it there. So each row it pushed took about 12 ms, not the 3 ms its comment counts on, and
  MIDI, clock and drum pulses waited that long. Boy keeps 400 kHz.
- **Screen stalls.** Boy also sends a changed row in four 32-byte pieces, one per pass, so the loop
  never waits more than about 1 ms on the screen.

**Pitch calibration** is one measured gain for all six CV outs. Every jack read 1.8% under the
design formula with a meter. Per-channel calibration in the EEPROM (spec requirement 10), and the
board's load servo (requirement 9), aren't written yet.

## Mode 2, Chordal

One finger plays a chord of four notes, and each chord leads smoothly into the next. It brings back
the old Boy's chord mode and drone, which saw little use because that Boy's audio path was noisy,
and goes well past them. The engine is plain C++ with no Arduino in it (`chord_engine.{h,cpp}`), so
`scripts/host/chord_render.cpp` runs the same code on a Mac.

**Status, 2026-11-05:** flashed, and it starts and runs at 26 % CPU on PAD. The sound has only been
checked in the Mac render, by measurement: see [the bench list](#mode-2-on-the-bench). It plays from MIDI
only: all six CV jacks are outputs here.

| Boy | Chordal |
|---|---|
| MIDI, USB and TRS, any channel but 10 | a note plays a chord; bend is ±2 semitones. Transport stop, All Sound Off and All Notes Off end it within a second, whatever HOLD says |
| audio out L, R | the chord, in stereo |
| audio in L, R | summed, they ring the strings |
| PITCH1, PITCH2, PITCH3, GATE1 | the four notes, V/oct, gliding as they lead (note 36 at 0 V, as mode 1) |
| GATE2 | 5 V while a chord is on |
| GATE3 | the motion, slowly, ±5 V |
| CLK, DRUM2, DRUM1, RST | a 5 ms trigger as each note enters, in that order |

**The sound.** Four notes, a bass and three above it. Each note is five detuned oscillators spread
across the stereo field, through a low-pass that tracks the note, blooms as it enters and breathes
with MOTION. Each also has a tuned string, a comb resonator that the audio inputs, a little air
noise and a pluck can all ring. So with SOUND up, drums patched in come out in the chord.

**The bank.** PATCH, at the top of the list, picks from sixteen starting points and changes the sound
as it turns. A patch sets the sound, STACK and OCT, and leaves KEY, HARM, VOICE and HOLD alone. Change
anything after that and a `*` follows the patch's name; the settings saved are what you hear. They're
levelled against each other on the Mac: the ones that sustain sit within about 2 dB.

| | | | |
|---|---|---|---|
| 1 PAD | 2 DRONE, on open fifths | 3 CHOIR | 4 SUPERSAW |
| 5 SWEEP, a resonant filter moving | 6 BRASS | 7 ORGAN | 8 PIPES, in octaves |
| 9 REEDS, odd partials | 10 MALLET, a marimba's | 11 BELLS, a church bell's | 12 GLASS, a bar's partials and a string |
| 13 PLUCK | 14 HARP, strummed | 15 BOWED | 16 RING: patch audio in, and it sounds in the chord |

**The gestures** are the same as every other page. The chord's name is on the top row, and these
are the items:

| Item | Sets |
|---|---|
| PATCH | the bank |
| KEY | C to B |
| HARM | MAJOR, MINOR, DORIAN, MIXOLYD, LYDIAN, PHRYGIAN, HARM MIN: any note plays the chord built on it. Then the old chord mode's 40 progressions: white keys are chords 1-7, a black key plays the white key below it, and A# is the eighth |
| STACK | how a scale builds its chords: 7TH, 9TH, SUS, QUARTAL, OPEN, FIFTHS |
| VOICE | LEAD picks whichever inversion moves least from the last chord; Root, Inv1, Inv2, Drop2 and Spread are fixed |
| OCT | −2 to +2 |
| SOUND | oscillators to strings |
| OSCS | how each note's five oscillators are tuned: DETUNE, ORGAN, HOLLOW, OCTAVES, BAR, BELL. All cost the same |
| SHAPE | sine, triangle, saw |
| BRIGHT, RESO | the filter, and its resonance |
| SPREAD, MOTION | detune and stereo width; how much it drifts and breathes |
| SWELL | attack, release and how long the strings ring |
| DECAY | 0: a held note stays. Above it, the note falls away, in 60 ms to 6 s |
| STRUM, GLIDE | the gap between notes entering; how long a note takes to reach the next chord |
| HOLD | on: a chord stays until the next one |
| LEVEL | the output |

The settings are kept in the EEPROM. Warps' CHORD switch uses the same engine with whatever Chordal
last saved.

## Mode 3, Warps

Mutable Instruments' meta-modulator, with the eight further modes of Matthias Puech's
[Parasites](https://mqtthiqs.github.io/parasites/warps) firmware. Its DSP is built unchanged from
`third_party/parasites` by `scripts/warps_build.py`; `warps_mode.cpp` takes the place of Warps' own
codec, CV, knob and button code. Parasites brings its own stmlib, so nothing here is built from
`third_party/eurorack` any more.

**How close META is to stock Warps.** Parasites keeps stock Warps as its Meta mode, and Boy starts
there. The same input through both, on a Mac (2026-11-05): XFADE, DIODE RM, DIGI RM, XOR and COMPARE
are sample-identical with an external carrier, and with the internal sine and nothing in IN 1. Three
things differ, all as Parasites' manual says:
- the wavefolder is inverted and about 1.9 dB quieter;
- the vocoder's limiter takes a gain of 1.6, not 1.4;
- audio on IN 1 modulates the internal carrier eight times harder.

| Boy | Warps |
|---|---|
| audio in L | IN 1, the carrier (with the internal oscillator on, it phase-modulates it) |
| audio in R | IN 2, the modulator |
| audio out L | OUT |
| audio out R | AUX |
| PITCH1 | Level 1 CV, and V/oct with the internal oscillator on |
| PITCH2 | Level 2 CV |
| GATE1 | Algorithm CV |
| GATE2 | Timbre CV |
| MIDI, USB and TRS, any channel but 10 | plays the chords, with CHORD on |
| GATE3, PITCH3, the gate outs | unused |

All six CV jacks are inputs: every jack switch stays off for the session.

**The mask.** The panel's labels are mode 1's, so Warps and Chordal each have a 3D-printed face mask:
`build123d/panels/teensy_move_reva_mask.py` (not printed or fitted yet). It says what the CV and gate jacks
do, and leaves the screen, the encoder and the audio jacks and their labels showing. Knurled M3 thumbscrews
in the two lower rack slots hold it on; it adds 1.6 mm, so they need that much more length than the panel's
screws.

**The encoder** stands in for Warps' four knobs and its button, and for Parasites' hold of the button
to change mode. The screen lists seven items, four at a time:

| Item | Warps | Shows |
|---|---|---|
| MODE | hold the button, turn the big knob | DOPPLER, FOLD, CHEBY, SHIFT, CRUSH, COMPARE, VOCODER, DELAY, META |
| LEV1, LEV2 | the Level knobs | 0–100 %, and the jack, `*` when a cable is found |
| ALGO | the Algorithm knob | the algorithm at the knob plus CV: XFADE, FOLD, DIODE RM, DIGI RM, XOR, COMPARE, VOCODER |
| TIMB | the Timbre knob | 0–100 % |
| CARR | the button: the carrier | EXT (audio in L), SINE, TRI, SAW |
| CHORD | ours: Chordal's engine replaces audio in L | off, or the chord being played. With MODE on VOCODER and CARR on EXT, a voice on audio in R sings the chords |

**The modes.** The names on the screen change with the mode, from Parasites' manual. The jacks stay
the same: each CV adds to its item.

| MODE | ALGO becomes | TIMB becomes | LEV1, LEV2 become | CARR becomes |
|---|---|---|---|---|
| META, stock Warps | ALGO | TIMB | | |
| DOPPLER, a binaural panner: two sources circling the head | X | Y | RATE and DEPTH of the circling | SPACE: TINY, SMALL, LARGE, HUGE |
| FOLD | FOLDS | BIAS | | |
| CHEBY, a Chebyshev waveshaper: with SINE, a simple additive voice | ORDER, 0 to 16 | GAIN | | |
| SHIFT, the frequency shifter, stock's easter egg | FREQ | MIX, up against down | FDBK, WET | |
| CRUSH, a dual bit-mangler | BITS | XMOD: dry, AND, XOR | | |
| COMPARE, with a Chebyshev after it | FUNC, eight comparisons | ORDER | | |
| VOCODER | WARP, the formant shift | RELS, the release; from about 93 % it freezes the bands | | |
| DELAY, a tape loop with two heads | SPEED: 50 % is stopped, below it runs backwards | LEN, the distance between the heads | FDBK, WET | TYPE: OPEN, DUAL, TAPE, PING |

There's no crossfade between modes and no CV for MODE, as in Parasites: a change of mode jumps.

The gestures are the chooser's (David, 2026-11-02: one gesture to scroll on both pages):
- **Turn** to move the marker `>`.
- **Press** to edit the marked item: its row lights. Turn to set it; turning fast takes bigger
  steps.
- **Press again** to let it go and scroll on.
- **Hold 2 s** for the chooser.

The settings are kept in the EEPROM, written 3 s after the last change, so the mode comes back as
it was left.

**As Warps:**
- **96 kHz,** Warps' own rate. The Teensy Audio library isn't tied to the audio shield, and its
  rate is one constant (`AUDIO_SAMPLE_RATE_EXACT`), from which it derives MCLK as 256 fS. At
  96 kHz that's 24.576 MHz, which the PCM3060 accepts for both its ADC and DAC (SLAS533B, p.15,
  Table 2), with BCK at 64 fS. Blocks are 96 samples (`AUDIO_BLOCK_SAMPLES`): Warps' largest,
  and its own host test's size. Mode 1 runs at 96 kHz too, sending silence.
- **Levels:**
  - Inputs gain 22/16. Warps' codec reaches full scale at 16 Vpp (`warps.cc`) and Boy's at
    22 Vpp. They're negated too, since Boy's inputs invert.
  - Outputs are 1:1: Warps' full scale is about 19.5 Vpp (its WM8731 times 6.9, `warps_v30.pdf`
    sheet 2), and Boy's measured 19.4.
  - Warps' outputs are AC-coupled (2.7 Hz) and Boy's aren't. A one-pole high-pass at 2.7 Hz
    does the capacitor's job, so the comparator and XOR don't put DC on Boy's outputs.
- **CV:** Warps' scaling, per volt. Level 1 and 2 read 0.109 of its ADC's range a volt, and
  Algorithm and Timbre 0.100; its CV scaler code is followed line for line. Its filters run once
  a 96-sample block here (1 kHz), against once a 60-sample block on Warps (1.6 kHz), so each
  one-pole coefficient is rescaled to keep its time constant.
- **Unpatched jacks.** Warps sends noise through each Level jack's switched contact to find out
  whether anything is patched. When nothing is, the Level knob sets the level outright, and
  Level 1's V/oct reads as if 2 V were patched. Boy has no switched contacts. Instead, an open
  input sits still at the ADC's bias, +0.207 V. So a jack that reads within 20 mV of that, steady
  to 4 mV, for 100 ms counts as open until it leaves that window.
  - A patched source held still in that window reads as open (spec requirement 8). On a Level
    jack, that sets the level to the knob's.
  - An envelope's slow tail passing through the window keeps moving, so it stays patched.

**The serial monitor** (`pio device monitor`) prints every 2 s:
- the audio CPU, now and at most;
- audio blocks in use;
- all six CV voltages, and which of the four in use read open.

## Files

- `src/teensy_move/board.{h,cpp}`: the board.
  - the EEPROM probe and the expander;
  - the DAC start-up, and the CV ins (ADS8688);
  - the gate word;
  - the encoder and its push;
  - settings in the EEPROM: page 0x1FC0, with each mode's page below it;
  - the screen.
- `src/teensy_move/main.cpp`: the mode table and the chooser.
- `src/teensy_move/move_mode.{h,cpp}`: mode 1.
- `src/teensy_move/chordal_mode.{h,cpp}`: mode 2, on `chord_engine.{h,cpp}` and
  `chord_library.h` (the old chord mode's 40 progressions).
- `src/teensy_move/midi_notes.{h,cpp}`: notes from USB and TRS, for modes 2 and 3.
- `src/teensy_move/warps_mode.{h,cpp}`: mode 3. `src/teensy_move/warps/drivers/debug_pin.h`
  stands in for Warps' STM32 debug pin.
- `scripts/warps_build.py`: compiles Warps' DSP and the stmlib it needs.
- `scripts/host/`: `build_chord_render.sh` builds `chord_render`, which renders the chord engine and
  Warps' DSP to a file on a Mac, to hear changes before flashing. Its `parasites` scene is a tour of the
  modes.

## Building and flashing

```sh
pio run -e teensy41              # build
pio run -e teensy41 -t upload    # flash: in the foreground, never in the background
```

This Mac is arm64 with no Rosetta:
- `scripts/arm64_toolchain.py` swaps in PlatformIO's arm64 GCC;
- `teensy-gui` (the Teensy Loader app) replaces `teensy-cli`.

`scripts/warps_build.py` compiles Warps from the `third_party/parasites` submodule; run
`git submodule update --init --recursive` first on a fresh clone.

The Teensy has VIN–VUSB cut, so the rack has to be on. Uploads often report `Error -1` and still
program the board: check before retrying.

## First flash

**Run 2026-11-01:** everything works except step 5, TRS, which hasn't been tried yet.

With nothing patched, then patching one thing at a time:

1. **Boot.**
   - The chooser shows `> 1 MOVE companion` and starts it after 3 s.
   - The screen then shows `MOVE   hold: menu`.
   - The Move still sees the module as before.
2. **Voices.** Play ch 1, 2 and 3 from the Move.
   - Each GATE goes to +5 V while a note is held.
   - Each PITCH steps 1 V an octave: meter C1 (note 36) at 0 V and C2 at 1 V.
   - The screen names the notes.
3. **Drums.** Ch 10 notes 36 and 37 should fire DRM1 and DRM2. A drum module or the ES-10 shows a
   pulse that a meter misses.
4. **Clock.** With Move playing, CLK pulses every 16th, and `CLK` shows on screen. Start pulses RST.
5. **TRS.** The same from a MIDI source on the TRS jack (Type A).
6. **Encoder.**
   - A long press pulses RST.
   - Hold 2 s: the chooser comes back and waits for a press, without restarting the rack.
7. **Audio outs** at 0 V DC.

## Mode 2 on the bench

`pio device monitor` prints the CPU, the blocks, the chord and its four notes every 2 s. **Note the
CPU first:** it decides how much of this can run alongside Warps.

1. **Choose 2.** Hold 2 s, turn to `2 CHORDAL`, press.
2. **Play the Move into USB.** Out L and R play the chord, and its name is on the top row. Turn HOLD
   off: the chord stops when the pad lifts.
   - **Turn PATCH through all sixteen,** against `chord_render bank`. 12 GLASS runs the oscillators and
     the strings together, so it should show the highest CPU.
   - Change a patch, wait 3 s, restart: it comes back as it was left, with the `*`.
3. **Move between chords** with STACK on 7TH: the notes on the monitor should barely move.
4. **Stereo.** Take L and R one at a time, with SPREAD and MOTION up: they should clearly differ.
5. **Strings.** SOUND at 100 %. Patch drums into audio in L. They should ring in the chord.
6. **CV.** PITCH1-3 and GATE1 into four oscillators, or one at a time on a tuner. Don't chase the
   tuning. GATE2 follows the chord, GATE3 moves slowly, and the gate outs trigger as the notes enter.
7. **TRS MIDI in.** The first time on this board: Move or another source into the TRS jack.
8. **In Warps** (mode 3): CHORD on, CARR on EXT, MODE on VOCODER, a voice or drums into audio in R,
   and play the Move. Read the CPU line: Warps and the chords together are what has to fit. RELS
   from about 93 % freezes the vocoder's bands, so a held vowel keeps singing the chords.

## Mode 3 on the bench

**Run 2026-11-02, so far:** steps 2 and 3 pass. LEV1 at 0 % gave C2 on a tuner, so the codec
is at 96 kHz. The sine is clean on the Tiliqua's scope, and so are the other carrier shapes. The
first gestures, a press to step through the items, were awkward against the chooser's turn to
scroll, so Warps now uses the chooser's. David: "perfect".

`pio device monitor` prints the CPU and the CV ins every 2 s.

1. **Mode 1 again, at 96 kHz.** The whole firmware now runs the codec at 96 kHz. In mode 1 that
   only matters for step 7 above: the audio outs should still sit at 0 V.
2. **Choose 3.** Hold 2 s, turn to `3 WARPS`, press.
   - The screen lists LEV1, LEV2, ALGO and TIMB.
   - With nothing patched, the monitor shows `open 1111`, and no jack on screen has a `*`.
3. **The rate, by ear.** Set CARR to SINE, ALGO to XFADE, TIMB to 0 % and LEV1 to 0 %.
   - Out L plays Warps' internal sine. Warps' pitch table is built for 96 kHz, so at 0 % it
     should be C2, 65.4 Hz, on a tuner. LEV1 at 100 % gives C7.
   - A note about 13 semitones flat means the codec isn't at 96 kHz.
   - No sound at all: the PCM3060 may not have locked at 256 fS. Check Out R, then step 1.
4. **Audio in.** CARR to EXT. Patch an oscillator into audio in L (the carrier) and another into
   in R (the modulator), at about 10 Vpp. Turn ALGO through all seven algorithms. Out R (AUX)
   carries the two inputs mixed.
5. **CV ins.** The first time any of Rev A's CV inputs reads another module. One at a time:
   - **V/oct into PITCH1,** with CARR on SINE, ALGO on XFADE, TIMB at 0 % and LEV1 at 40 %. Out L:
     nothing patched, C4. Patched at 0 V, C2; 1 V, C3; 2 V, C4; 3 V, C5. From a 1 kΩ output expect
     about 13 cents flat an octave, the same loading as any 100 kΩ input; don't chase it.
   - **The reading:** the monitor's voltage against a meter at the source, steady and in both polarities.
     It should agree to within a few tens of millivolts. A source at 0 V should read about 0 V and show
     a `*`; an open jack reads +0.207 V.
   - **An LFO into GATE1:** the ALGO name moves, and the jack shows `G1*`.
   - **An envelope into PITCH2:** the modulator's level follows it, over 0 to about 6 V.
   - **Into GATE2:** Timbre moves.
   - **GATE3 and PITCH3:** Warps doesn't use them, so check them on the monitor's reading alone.
   - Take each cable out: its `*` should go within about a tenth of a second.
6. **Output DC.** ALGO on COMPARE, both inputs patched: the outs average 0 V on a meter.
7. **MODE on SHIFT,** the frequency shifter, with CARR on SINE. FREQ shifts whatever's on the inputs up
   or down either side of 50 %. OUT and AUX are the two directions.
8. **The other modes,** each by ear, with `chord_render parasites` as the reference: DOPPLER (take both
   outs as a stereo pair), FOLD, CHEBY with CARR on SINE, CRUSH, COMPARE, VOCODER and DELAY in all four
   TYPEs. Note the CPU in each: the monitor names the mode.
9. **Back to the chooser:** hold 2 s.

Note the CPU figures: Warps ran on a 168 MHz Cortex-M4.
