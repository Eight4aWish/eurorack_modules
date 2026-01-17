# Teensy Move – Expander drum trigger pulse width

## Symptom
Some drum modules (notably hi-hats with variable decay / gate-sensitive trigger inputs) can sound “too open” when driven from the Teensy Move expander drum outputs.

The Teensy Move expander uses a 74HC595 output latch plus an inverter stage. In practice, the target module sees a trigger that is defined by a **HIGH → LOW → HIGH** transition. If the low (or the effective pulse) is too long, some circuits interpret it as a gate.

## Firmware change
The expander drum outputs are no longer held active for a coarse `millis()` duration.

Instead, each drum output uses a **microsecond-based** timeout:

- `DRUM_TRIG_US[]` in [src/teensy-move/main.cpp](../src/teensy-move/main.cpp)

Default is **500µs (0.5ms)** on all four drum channels.

### Default polarity
By default the expander drum outputs now idle **LOW** at the jack and produce a short **HIGH** pulse (i.e. a conventional rising-edge trigger).

Because the expander hardware includes an inverter stage, this corresponds to keeping the 74HC595 drum bits **HIGH at rest** and pulling them **LOW briefly** during a trigger.

## Tuning
If you still hear overly-long/open behavior, reduce the value(s):

- Try **1000µs** (1ms)
- Try **500µs** (0.5ms)

If a destination is missing triggers, increase modestly:

- Try **3000–5000µs**

## Notes
- The Teensy loop updates the 595 image frequently, but the trigger width is controlled by the `micros()` timeout.
- If you need different widths per drum output, edit the four entries in `DRUM_TRIG_US[]`.
