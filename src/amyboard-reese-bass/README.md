# AMYboard Reese Bass

A Tulip sketch for the AMYboard: a 5-oscillator Reese bass with an OLED menu for
per-oscillator wave/detune/octave plus amp and filter envelopes. The rotary
encoder navigates/edits parameters; the button selects/confirms. Plays from MIDI
(ch1) or **CV1 = 1 V/oct pitch, CV2 = gate**.

This is a backup of the sketch that was living only on the board (it autostarted
as Tulip's "current sketch"). It was captured here before swapping the boot app
over to [the patch bank](../amyboard-patchbank/), so it isn't lost.

- `sketch.py` — the full sketch; drop it at `/user/current/sketch.py` to make it
  the boot app again (it uses the `amyboard` module's `init_display`,
  `init_buttons`, `read_encoder`, and `cv_in` helpers).

Only one sketch can be the autostart at a time, so the board boots **either**
this **or** the patch bank, not both.
