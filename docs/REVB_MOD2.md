# Wealth: MOD2 and Melon voices

HAGIWO's MOD2 voices and WGD Modular's Melon voices on **Wealth** (a working name; the analogue board Rev B with a
Pico 2 W), behind the **MOD2/Melon panel**. They are one app, `mod2`, playing one voice at a time,
chosen from a menu on the encoder. It uses the same driver as the Workshop Computer programs
(`src/revb/card/ComputerCard.h`, see [REVB_COMPUTER.md](REVB_COMPUTER.md)).

**Status (2026-11-13): nine drum voices build, and render on a computer; not yet run on the
board.** The Rev B boards are in the post.

## How a voice is ported

The MOD2 and Melon sketches drive the RP2350's hardware directly (its ADC, its PWM, often a DMA
and the second core), so no header can run them unchanged. Each voice is one sketch in
`src/revb/apps/mod2/voices/`, its sound engine kept as published (the same constants, the same
maths in the same order), with a new frame around it (`mod2.cpp`, `frame.h`):

- **Each voice runs at its sketch's own sample rate**, 31.25, 36.6 or 48 kHz, and gives the PWM
  level the sketch would have written, with its resolution and rounding. The frame carries that to
  the codec's 47 973.6 Hz by linear interpolation. The 48 kHz voices run one sample per codec
  sample, so they play 0.06 % flat, under a cent.
- **The MOD2's output stage is in software:** the PWM's average through the two-pole RC filter
  (JP1, on the filter switch) and the ×3.06 amplifier, AC-coupled on OUT and DC-coupled on OUT DC
  (JP2).
- **The panel is read as a MOD2 reads it.** POT3 and the CV share the MOD2's A2 through an
  inverting stage, so the frame hands each sketch A2 inverted, and each corrects it, or doesn't, as
  published.
- **Hits start at once.** Five of the sketches (clap, claves, hi-hat, kick and FM drum) build each
  hit into a table when triggered and then play the table, so on a MOD2 a hit starts after the build
  (about 20 ms on the clap). The ports work out the same samples one at a time as they play.
  Checked on a computer against the originals' table code, transcribed: the PWM levels are
  identical in 14 settings, about 230 000 samples. Retro kick and laser took a trigger once per
  audio buffer (up to 11 and 8 ms late); here a trigger starts the next sample.
- **What isn't kept:** the sketches' own ADC averaging (the driver smooths the pots), their
  debouncing, and the floating ADC pin that seeds the clap's and hi-hat's noise tables (a fixed
  seed instead, so every boot has the same noise where each MOD2 has its own).

**Settings.** What each sketch keeps in EEPROM (the kick's and FM drum's six parameters, the
metal's octave, the laser's six) and the voice last chosen live in one flash sector. They are
written 1.5 s after the last change, once the voice is quiet: writing flash stops the audio for a
few tens of milliseconds, as `EEPROM.commit()` does on a MOD2, and OUT goes quiet and OUT DC to 0 V
for it. The sector is the one below Bends', so neither app loses the other's settings.

## The MOD2/Melon panel

The Computer panel's cells and holes, so one wired board runs either family. Blank cells are
left undrilled.

| Row | Left | Middle | Right |
| --- | --- | --- | --- |
| 1 | Screen | Encoder (push = BUTTON) | Filter switch |
| 2 | POT1 | POT2 | POT3 |
| 3 | IN1 | CV | — |
| 4 | IN2 | — | — |
| 5 | — | — | OUT |
| 6 | — | — | OUT DC |

| MOD2 | Panel | Rev B |
| --- | --- | --- |
| POT1, POT2, POT3 | Main, X, Y cells | `KnobVal(Main/X/Y)` |
| IN1 (GPIO7), IN2 (GPIO0) | Pulse in 1, 2 | `PulseIn1`, `PulseIn2` |
| CV (on A2, with POT3) | CV in 1 | added to Y: 0 to +5 V spans the pot |
| BUTTON (GPIO6) | the encoder's push | press and release, with how long it was held |
| JP1, the RC filter's cutoff | the filter switch, in Z's cell and wired as Z | up 15.9 kHz, down 5.0 kHz |
| JP2, the output coupling | two jacks | OUT is AC-coupled (1.6 Hz), OUT DC is DC-coupled |
| LED (GPIO5), Melon's WS2812 | the screen | LED 0 |

**Turning the encoder changes the voice**, in this order: kick, retro kick, snare, clap, hi-hat,
claves, FM drum, metal, laser. The screen's top line names the voice and its page or mode.

OUT DC is what the MOD2 gives with JP2 closed: 0 to about +10 V, +5 V at rest. For a drum, use
OUT.

## Which voices

MOD2 has 19 voices (`modulove/MOD2`, `Firmware/`) and Melon 17 (`wgd-modular/melon-firmwares`).
About 11 are the same HAGIWO voices on both, so about 25 are distinct; most are HAGIWO's own, with
Melon's drums and fxloperformer by Vincent Maurer and RALPS by mxzrmxzrmxzr. Melon's versions of
the six shared drums (clap, claves, FM drum, hi-hat, kick, metal hit) are MOD2's engines with
comments trimmed and its RGB LED added, so the ports follow MOD2's.

**Ported (2026-11-13): the nine drums,** below.

**Not ported (David, 2026-11-13):**

- **Voices that copy a module already in the rack.** Braids (on both) and Melon's reese_bass, a
  Braids clone, are Joy; MOD2's Tides is the Behringer Waves.
- **Sample players.** MOD2's breakbeats and sample, and Melon's sample_player and drums (a
  Grids-style drum machine whose kits are mostly sampled 808, 909, 606, LinnDrum and Simmons).

**Still to port:** MOD2's flux, mod303, radio, spiral, square_vco and vco (mod303, radio and vco
are on Melon too), and Melon's fxloperformer, palimpsest, RALPS and string_mouth.

## The voices

Every voice's source header says most of its code was generated by AI. All are CC0 1.0. "Y" means
POT3 with CV in 1 added; IN1 and the push trigger unless a table says otherwise.

### Kick

HAGIWO's MOD2 Kick Ver1.2 (`Firmware/kick`): a sine kick with a curved pitch sweep, soft clip and
decay, six parameters on two pages. The push flips the page (KICK 1, KICK 2; it starts on 2, as the
original does). After a flip, a pot leaves the value alone until it passes the value it had
(pickup). IN2 high at the trigger halves the level.

| Control | KICK 1 | KICK 2 |
| --- | --- | --- |
| Main | pitch, ×0.5 to ×2, live while it plays | start frequency, 3 to 1026 Hz |
| X | soft clip, 0.5 to 10 | end frequency, 2 to 512 Hz |
| Y | decay, longer clockwise | sweep curve, 32 shapes |

### Retro kick

HAGIWO's MOD2 Retro Game Kick Ver1.1 (`Firmware/retro_game_kick`): a triangle body, a pulse body
and a short click.

| Control | Does |
| --- | --- |
| Main | decay, 20 to 120 ms |
| X | pitch, 32 to 300 Hz |
| Y | sweep and morph: more sweep, a narrower pulse, more drive |
| IN2 | high while it plays: half level |

### Snare

HAGIWO's MOD2 LFSR Snare Ver1.0 (`Firmware/snare`): a retro-game-style snare from a 15-bit LFSR.

| Control | Does |
| --- | --- |
| Main | decay, 15 to 400 ms |
| X | the LFSR's clock, 1 to 32 kHz |
| Y | pitch-envelope amount, 0 to 4 octaves |
| IN2 | high within 15 ms of the trigger: half level |

### Clap

HAGIWO's MOD2 Clap Ver1.1 (`Firmware/clap`): an 808-style clap, two 4 ms noise bursts 15 ms apart
and a noise tail, through a band-pass filter.

| Control | Does |
| --- | --- |
| Main | filter width, wide to narrow (Q 0.5 to 4) |
| X | tail, 20 to 200 ms |
| Y | filter frequency, 50 Hz to 8 kHz |
| IN2 | high at the trigger: half level |

### Hi-hat

HAGIWO's MOD2 HiHat Ver1.1 (`Firmware/hihat`): blue or white noise through a band-pass filter.
A short push triggers, on release; a push held half a second swaps the noise (HAT BLUE, HAT WHITE).

| Control | Does |
| --- | --- |
| Main | decay, longer clockwise |
| X | decay again, longer clockwise: the original calls it decay curve, but it scales the same exponential as Main |
| Y | filter frequency, 100 Hz to 16 kHz |
| IN2 | high at the trigger: half level |

### Claves

HAGIWO's MOD2 Claves Ver1.1 (`Firmware/claves`): a sine-to-triangle morph under an exponential
decay. It starts 5 ms after the trigger, as the original does, so a pitch CV sent with the trigger
has settled.

| Control | Does |
| --- | --- |
| Main | decay, shorter clockwise |
| X | sine to triangle |
| Y | pitch, V/oct: 50 Hz at 0 V and Y fully anticlockwise, up to 1.5 kHz |

### FM drum

HAGIWO's MOD2 FM Drum Ver1.2 (`Firmware/fm_drum`): a two-operator FM voice on two pages, with
pickup as the kick's. Y is the same modulation index on both pages, and it also sets the soft clip's
drive, so there are five parameters, not the six its header counts. IN2 high at the trigger halves
the level.

| Control | FM DRUM 1 | FM DRUM 2 |
| --- | --- | --- |
| Main | pitch, 30 to 1200 Hz | decay, longer clockwise |
| X | operator ratio, 0.5 to 8 | ratio envelope, less clockwise |
| Y | modulation index, 1 to 10 | modulation index, 1 to 10 |

### Metal

HAGIWO's MOD2 01 Metal Hit Ver1.0 (`Firmware/metal`): six partials, a noise strike and a
shimmer, like striking metal. The controls are read 5 ms after the trigger, so a V/oct CV sent with
it has settled. The push puts the pitch up two octaves, or back (METAL +2), and is remembered.

| Control | Does |
| --- | --- |
| Main | ring decay, 40 ms to 1.2 s |
| X | spread: harmonic partials fully anticlockwise, inharmonic and wider from there |
| Y | strike tune, V/oct: 40 Hz at 0 V and Y fully anticlockwise |
| IN2 | high at the trigger: half level |

### Laser

HAGIWO's MOD2 Arcade Laser Ver1.0 (`Firmware/laser`): a one-shot laser from a 12-shape wavetable
morph, six parameters on two pages. After a flip a pot takes over once it moves (soft takeover),
and the parameters are remembered. IN2 high within 15 ms of the trigger halves the level.

| Control | LASER 1 | LASER 2 |
| --- | --- | --- |
| Main | decay, 20 to 500 ms | pitch width, 0 to 4 octaves above the end pitch |
| X | decay envelope shape | pitch envelope time, 12 to 220 ms |
| Y | wavetable morph, 12 shapes | end pitch, 45 to 2400 Hz |

## Hearing them before the board

`host_render.cpp` runs every voice on a computer through the same frame code, four hits each at
different pot settings, and writes OUT as a WAV per voice:

```sh
c++ -std=c++17 -O2 -o build/mod2_render src/revb/apps/mod2/host_render.cpp
build/mod2_render build/mod2_wav
```

## Building and flashing

```sh
scripts/revb.sh mod2          # build/revb/apps/mod2/mod2.uf2
scripts/revb.sh flash mod2    # and copy it to the Pico, held in BOOTSEL
```

## On the bench

Beyond [REVB_COMPUTER.md](REVB_COMPUTER.md)'s checklist:

1. Every voice from IN1 and from the push; the menu wraps both ways; the filter switch darkens it.
2. OUT DC rests near +5 V and OUT near 0 V. Hits peak at about ±5 V on OUT on the computer.
3. The heaviest voices for the processor are the retro kick and the laser, at their shortest
   settings and triggered fast: listen for gaps.
4. The settings survive a power cycle: a kick page, the metal's octave, the voice.
5. Beside the breadboard MOD2, once that's built: the same voice, the same pots, by ear.
