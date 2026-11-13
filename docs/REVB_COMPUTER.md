# Wealth: Workshop Computer programs

**Wealth** (*twelve for wealth*, a working name: the names are undecided (David, 2026-11-13): Health and Wealth, or Boy's name for every Rev A and Rev B board) is the analogue board Rev B (`pico_revb`) with a **Pico 2 W**,
hosting other people's apps. This is its firmware behind the **Computer panel**: Music Thing Workshop Computer programs, built unchanged against a Rev B
version of `ComputerCard.h`. The first is **Bends**. MOD2 and Melon voices share the same driver;
see [REVB_MOD2.md](REVB_MOD2.md). The other Rev B module, the ESP32-P4 gesture controller, is
**Health**.

**Status (2026-11-13): builds, not yet run.** The Rev B boards are on order. The two PIO programs
pass a cycle-level simulation (`revb_i2s`: words out and in, left and right in order, I2S's
one-bit delay; `revb_conv`: every frame reaches its chip with 32 clocks, and every reply comes
back). Nothing has met the hardware. The bench checklist is at the end.

Board notes: `eurorack_electronics/docs/analogue_board_revb.md` (pin map §3, panel §2) and the
spec's §15 requirements. Rev A's firmware notes (`analogue_board_reva_firmware.md`) hold the
measured values this driver starts from.

## Build and flash

Pico SDK and CMake, not a PlatformIO env (David, 2026-11-13): Workshop Computer programs are Pico SDK
projects, and CMake builds them as published. PlatformIO's own `picosdk` framework would mean
translating each program's CMake file, and its Arduino framework would mean reworking them.
`scripts/revb.sh` builds with the packages PlatformIO already holds for `pico2w_oc` (the Pico SDK
2.2.1 inside Arduino-Pico, its arm64 GCC 14.3, ninja), so there is nothing else to install:

```sh
scripts/revb.sh               # every app
scripts/revb.sh mod2          # one
scripts/revb.sh flash mod2    # build it and copy it to the Pico, held in BOOTSEL (the RP2350 drive)
```

Each app comes out as `build/revb/apps/<app>/<app>.uf2`. The SDK builds its own picotool (2.1.1)
into `build/revb` the first time, for the UF2s. Flashing copies to the drive, because that picotool
has no USB here (no libusb `pkg-config`) and PlatformIO's picotool 2.0.0 can't read SDK 2.2 images.
Set `PICO_SDK_PATH` yourself and run CMake directly to use another SDK.

| App | Source | Code | RAM |
| --- | --- | --- | --- |
| `bends` | `third_party/workshop_computer/bends/` (unmodified) | 74 KB | 224 KB of 520 |
| `mod2` | `src/revb/apps/mod2/` | 64 KB | 121 KB |

Each app is its own image for now, one at a time in flash. A launcher that keeps several in flash
and boots the one chosen comes later. **The target (David, 2026-11-13): 15 Workshop Computer
programs plus one MOD2/Melon app** holding every ported voice behind a voice menu. **The selection
is strict:** of the 148 published programs, each has to earn its place, and none that duplicates a
module already in the rack goes in (Braids, Plaits, Clouds, Rings, Elements, Beads, Grids, Peaks,
Stages, Tides, Warps, the Turing Machine family; `eight4awish/MODULES.md` has the rack). That fits the
Pico 2 W's 4 MB with room to spare: 213 of the 225 releases for 2 MB cards need 512 KB or less, and
the median is about 96 KB. The boot ROM's partition table holds at most 16 partitions
(`PARTITION_TABLE_MAX_PARTITIONS`). If the launcher can't sit outside them, it keeps its own slot
table and boots each app with `rom_chain_image()` instead. Settings go in the EEPROM, not flash.

## The driver: `src/revb/card/ComputerCard.h`

The same class, calls and value ranges as Chris Johnson's ComputerCard 0.4.0 (14 June 2026, MIT),
so a program written against it builds for Rev B as it stands. That includes 0.4.0's
`AudioInMillivolts()`, `InputsCalibrated()` and `FlashSizeBytes()`. Behind it:

| ComputerCard | Rev B |
| --- | --- |
| Audio in and out, ±2048 ≈ ±6 V | PCM3060 over I2S (PIO0, DMA). Full scale is about ±11 V in, ±9.7 V out. `AudioOut()` clips at ±2048, as a Workshop Computer's DAC write does; `AudioOutVolts()` reaches the full range |
| CV in, ±2048 ≈ ±6 V | ADS8688, ±10.24 V range |
| CV out, ±2048 ≈ ±6 V; `Precise`, `MIDINote`, `Millivolts` | DAC8568 at ±10 V |
| Pulse in | Gate inputs (LM393s), read every sample |
| Pulse out | The 74HCT595, latched from the background loop: up to about 1 ms late |
| Knobs, Z | MCP3208, each read every millisecond, smoothed |
| Six LEDs | Drawn on the screen as a 2×3 grid |
| `Connected()` / `Disconnected()` | Open-jack detection (below) |

**Timing.** clk_sys runs at **196.5 MHz** (12 MHz × 131 / 8). MCLK on GPOUT0 and the I2S program
both divide it by 16 (32 at half rate), so MCLK is 256 fs and the sample rate is
**47 973.6 Hz**, 1 cent flat of a Workshop Computer's fixed 48 kHz. Canonical ComputerCard has no
other rate. Bends ships a copy that adds `COMPUTERCARD_SAMPLE_RATE_DIV` (defaulting to 2) and asks
for 2 itself, so it runs the codec at 23 986.8 Hz; Rev B honours the define and defaults to 1.
Audio moves in DMA blocks of four frames and plays two to three blocks after it arrives, about
0.2 ms at 48 kHz. Each output buffer is zeroed by DMA once sent, and the interrupt takes the buffer
to fill from the DMA's position, not a count. So if the core running the audio stops (a flash
write, say), the outputs fall silent within three blocks instead of looping them, and the audio
picks up cleanly when it resumes. The
converter bus (PIO1, 12.3 MHz SCK) sends one batch of frames per block, by DMA: four ADC reads and
two DAC writes. CV in and out move once a block (12 kHz at 48 kHz). The interrupt runs on the
core that calls `Run()`; Bends calls it from core 1, as on a Workshop Computer.

**Jack detection.** There is no normalisation probe (dropped from the board on 2026-10-16). An open
CV jack reads the ADS8688's bias, +0.207 V on Rev A. An audio jack is read the same way through
its DC tap, at about +0.19 V because of the jack's own 1 MΩ (calculated, not yet measured). An
input that stays within 25 mV of its open level, steady to 8 mV, for 100 ms counts as unpatched. A
patched source resting there can't be told from an open jack (requirement 8). A source resting at
0 V, such as a silent output, reads as patched. A **pulse input** counts as patched once an edge
has arrived in the last 2 s. As on a Workshop Computer, an unpatched audio or CV input reads 0. Build
with `REVB_AUDIO_DETECT=0` on a board without the DC taps.

**Start-up** follows the spec: nothing but I2C starts unless the EEPROM answers (requirement 1).
The expander's output register is written before its pins become outputs, since it powers up all
ones, which would close every jack switch. The panel bus starts next, and each knob and Z start
from their first reading rather than smoothing up from zero, so a program that reads Z at power-up
sees where it is (Bends takes Z held down then as a factory reset). The DAC holds 0 V on every
channel before its reference comes on and before CV out 1's switch closes (requirement 3). A whole gate word is latched before the 595's outputs are enabled
(requirement 4). The ADC is set to ±10.24 V on every channel (requirement 8).

**Not like a Workshop Computer:**
- CV outs use the board's nominal transfer until Rev B's EEPROM calibration is written.
  `CVOutsCalibrated()` returns false. The two-way channel's ~1 % sag into the board's own load is
  allowed for.
- `PULSE_1_RAW_OUT` and `PULSE_2_RAW_OUT` name GP13 and GP12. On a Pico board those reach only
  hole J102, so a program that drives them directly (Bends' Texture) harms nothing; its output
  goes nowhere.
- `USBPowerState()` is `Unsupported`, `HardwareVersion()` is `Unknown`, and `UniqueCardID()`
  is the Pico's own ID. `InputsCalibrated()` is false and `AudioInMillivolts()` nominal;
  `FlashSizeBytes()` is the build's 4 MB rather than the chip's own report.
- Extensions for programs written for Rev B: `AudioOutVolts()`, `EncoderTurns()`, `PushHeld()`,
  `PushPressed()`, `DisplayName()` and `SampleRate()`.

## The Computer panel

The Workshop Computer's 4×3 jack grid turned clockwise: columns Pulse, CV, Audio; rows in 1,
in 2, out 1, out 2. "R" holes are beside the PCB's right strip (the panel's left), "LO" beside
its left. Cell numbers are the deck's JPS cells.

| Row | Left | Middle | Right |
| --- | --- | --- | --- |
| 1 | Screen | Encoder | Z switch |
| 2 | Main | X | Y |
| 3 | Pulse in 1 | CV in 1 | Audio/CV in 1 |
| 4 | Pulse in 2 | CV in 2 | Audio/CV in 2 |
| 5 | Pulse out 1 | CV out 1 | Audio out 1 |
| 6 | Pulse out 2 | CV out 2 | Audio out 2 |

| Panel | Cell | Hole(s) | Channel |
| --- | --- | --- | --- |
| Screen | JPS1 | J703 (RSI 13-16): I2C and power | SSD1306 at 0x3C, 64×48 (0.66"), the screen on Joy and the MultiFX patch.init |
| Encoder A, B | JPS2 | GIN2 (R21), GIN4 (R33) | gate inputs 2 and 4; common to a +3V3D hole, not POT_REF |
| Encoder push | JPS2 | SW2 (R6) | TCA9555 P11 |
| Z | JPS3 | POT3 (LO2), POT_REF (LO1), GND | MCP3208 ch 2: up to POT_REF, down to GND, the common to POT3 with 100k to POT_REF and 100k to GND, so the middle reads about half |
| Main | JPS4 | POT4 (R8), POT_REF (R7) | MCP3208 ch 3 |
| X | JPS5 | POT5 (LO5), POT_REF (LO4) | MCP3208 ch 4 |
| Y | JPS6 | POT6 (LO8), POT_REF (LO7) | MCP3208 ch 5 |
| Pulse in 1 | JPS7 | GIN1 (R18) | gate input 1, GP6 |
| CV in 1 | JPS8 | CV2 (R20) | ADS8688 ch 1 |
| Audio/CV in 1 | JPS9 | AIN1 (R32), **and** CV4 (LO17) | codec left; its DC tap on ADS8688 ch 3 |
| Pulse in 2 | JPS10 | GIN3 (R24) | gate input 3, GP4 |
| CV in 2 | JPS11 | CV5 (LO20) | ADS8688 ch 4 |
| Audio/CV in 2 | JPS12 | AIN2 (R38), **and** CV6 (LO23) | codec right; its DC tap on ADS8688 ch 5 |
| Pulse out 1 | JPS13 | GOUT1 (LO18) | 595 QA |
| CV out 1 | JPS14 | CV3 (R23) | DAC8568 ch 2, switched to output |
| Audio out 1 | JPS15 | AOUT1 (LO34) | codec left |
| Pulse out 2 | JPS16 | GOUT2 (LO21) | 595 QB |
| CV out 2 | JPS17 | CVO7 (LO39) | DAC8568 ch 6 |
| Audio out 2 | JPS18 | AOUT2 (LO38) | codec right |

Four wires cross the board: AIN1 and AIN2 to the codec inputs, and GOUT1 and GOUT2. Run them on
the deck face, not under the module. Cross high, clear of the power header, and use thin shielded
cable for the two audio-input runs, with the shield grounded at one end on an AGND hole near the
codec. Unused: CV1, CVI7, GOUT3-4, POT1-2, MIDI.

**The screen** shows the program's name, the six LEDs (filled when bright, outlined when dim),
Z's position, and which inputs count as patched (A, C, P for jacks 1 and 2). For a 0.49" 64×32
panel build with `REVB_SCREEN_H=32`. `REVB_SCREEN_COL_OFFSET` (32) places either in the
controller's 128 columns.

## Bends

Vincent Maurer's six-stage stereo multi-effect, glitch looper and degradation engine
([Workshop Computer card 45](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/45_bends),
MIT, v1.0.0). Its source is vendored unmodified from commit `6d47f07`. Its README is beside it
and holds the controls.

On Rev B:
- It runs at 24 kHz (it asks for `COMPUTERCARD_SAMPLE_RATE_DIV 2`), DSP on core 1, the interface on
  core 0, as on a Workshop Computer.
- **Jack detection works for Audio 1-2 and CV 1-2** (normalling Audio 2 to Audio 1, Extended Mono
  Mode). Clock Sync on Pulse 1 counts as patched once a clock arrives.
- **Texture is lost, so Pulse out 2 is silent in Bends.** Texture is the only thing Bends puts on
  Pulse out 2: its microsound engine as 10-bit PWM at about 190 kHz, written straight to the
  pulse-out GPIO. On Rev B the pulse outs go through the 595, which can't carry it, so the PWM
  goes to GP12 and hole J102. To get it back, run a wire from J102's GP12 hole through 1 kΩ, with
  clamps, to a jack (untried).
- Presets save to the top sector of the Pico 2 W's flash, as on a 2 MB program card. The audio
  falls silent during a save (a sector erase, tens of milliseconds) while core 1 is paused, then
  picks up again; the I2S clocks keep running.
- It sets 192 MHz itself; `Run()` then sets 196.5 MHz, which the codec's clocks need.

## Bench checklist, first power-up

1. **Clocks,** each 256, 64 and 1 times the sample rate: for Bends 6.14 MHz on GP21 (MCLK),
   1.535 MHz on BCK and 23.99 kHz on LRCK; for `mod2` 12.28 MHz, 3.07 MHz and 47.97 kHz.
2. **Audio:** a sine through each input to each output with `bends` set clean; level and polarity
   at the jacks. A DC level out sets `CODEC_OUT_VOLTS` (Rev A's 9.72 V until measured).
3. **Converter bus:** CV out 1 and 2 at −5, 0 and +5 V on a meter. CV in 1 and 2 read back.
4. **Open-jack levels:** each CV input and each audio tap with nothing patched. Set `OPEN_CV` and
   `OPEN_AUDIO` from the readings.
5. **Panel:** the knobs' direction and range, Z's three readings, the encoder's direction, the push.
6. **Screen:** the size and column offset of the panel fitted.
