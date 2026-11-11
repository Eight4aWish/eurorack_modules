# Noise test firmware (temporary, not a module)

`src/noise_test/`, envs `noise_boy` and `noise_oldboy`: the noise-comparison schedule for the two
Teensy boards. Method, what to plug and the procedure are in
[`eurorack_electronics/docs/noise_test.md`](https://github.com/Eight4aWish/eurorack_electronics/blob/main/docs/noise_test.md).

- `noise_schedule.h`: the schedule, kept identical to `eurorack_daisy_patch_init/noise_test/src/`.
  30 s of silence after power-up, then three takes of output silence, a 997 Hz tone at −6 dBFS and
  input-to-output pass-through, at each screen state.
- `noise_stream.h`: the schedule as a Teensy Audio object; 48 kHz, 48-sample blocks.
- `noise_boy.cpp`: Boy Rev A. `board::begin()` as Boy, then only audio and the 128x32 screen
  (Adafruit SSD1306, 400 kHz) run: no encoder, CV or expander polling.
- `noise_oldboy.cpp`: the old Boy. The SGTL5000 as the old Boy set it (line in 6, line out 14,
  ADC high-pass frozen), at 48 kHz, which Teensy's driver doesn't set by itself. No screen.

Flash in the foreground (`pio run -e noise_boy -t upload`), and reflash env `teensy41` on Boy after.
The old Boy's own firmware is at `d9c6b47^` (env `teensy_move_old`), from before its retirement.
