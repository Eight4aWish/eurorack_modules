// Noise test firmware for the old Boy (retired 2026-11-05): Teensy 4.1 on a Teensy Audio Shield
// (SGTL5000), its OLED gone. Temporary: for the noise comparison in
// eurorack_electronics/docs/noise_test.md.
//
// The schedule is noise_schedule.h, the same as every other board's, without the screen states.
// The codec is set as the old Boy set it (d9c6b47^, src/teensy_move_old/main.cpp): line in at
// level 6, line out at level 14 (~3.0 Vpp, then x3.4 on the board to ~10 Vpp), the ADC high-pass
// frozen. The old Boy ran at the library's 44.1 kHz; this runs 48 kHz like the rest, so the
// SGTL5000's SYS_FS is set to 48 kHz too (the library's enable() hard-codes 44.1).

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>

#include "noise_stream.h"

class Sgtl5000At48k : public AudioControlSGTL5000 {
 public:
  void SetFs48k() { write(0x0004, 0x0008); }  // CHIP_CLK_CTRL: SYS_FS = 48 kHz, MCLK = 256 fs
};

static noise::Player player;
AudioInputI2S i2s_in;
NoiseStream ns(&player);
AudioOutputI2S i2s_out;
AudioConnection c0(i2s_in, 0, ns, 0), c1(i2s_in, 1, ns, 1), c2(ns, 0, i2s_out, 0), c3(ns, 1, i2s_out, 1);
static Sgtl5000At48k sgtl5000;

void setup() {
  player.Begin(false);
  AudioMemory(8);
  sgtl5000.enable();
  sgtl5000.SetFs48k();
  sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000.adcHighPassFilterFreeze();
  sgtl5000.lineInLevel(6);
  sgtl5000.lineOutLevel(14);
  sgtl5000.volume(0.8f);
}

void loop() {}
