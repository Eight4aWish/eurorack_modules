# chaos_core (copy)

The chaotic-attractor DSP behind **Secret**: twelve RK4 models, `Voice` (pitch in Hz,
TAME, the gated envelope, DC blocking, the limiter) and the panel-order registry. Its
only dependency is `<math.h>`.

This is a copy. The library is developed in
[`eurorack_daisy_patch_init/common/chaos_core`](https://github.com/Eight4aWish/eurorack_daisy_patch_init/tree/main/common/chaos_core),
with Secret on the Alchemy Lab, and its README there is the documentation: the models,
the step caps, TAME, the divergence guard and the host tools that measure them.

Copied on 2026-11-11 from that repo's commit `f1e8ca8` (*Secret: delayed-feedback TAME,
480 MHz caps, zero-crossing note starts*) for Secret on the Teensy Chaos hardware
([`src/teensy_chaos/`](../../src/teensy_chaos/main.cpp)). Only what the firmware
compiles came across: `include/chaos_core/` without `Candidates.h` and its tables, and
`src/Registry.cpp`. The host tools stay in the canonical copy.

To bring it up to date, copy the same files again and change the commit above:

```sh
SRC=../eurorack_daisy_patch_init/common/chaos_core
for f in Attractors.h Bank2.h ChaosBase.h OdeModel.h PitchTables.h PitchTablesBank2.h Registry.h Voice.h; do
  cp "$SRC/include/chaos_core/$f" libs/chaos_core/include/chaos_core/
done
cp "$SRC/src/Registry.cpp" libs/chaos_core/src/
```

The step caps (`maxStepsPerSecond`) were measured on the Alchemy Lab at 480 MHz, and a
cap is per chip. The Teensy's bench build (`pio run -e teensy_chaos_bench`) ran every
model at those caps on 2026-11-11: 43–51% of a 128-sample block at TAME 1, the worst
case, with cycles per step within a few percent of the Alchemy Lab's, so the caps are used
as they are. If a later copy changes them, run the bench again; a Teensy-only cap would be
set in `src/teensy_chaos/main.cpp`, not in this copy.
