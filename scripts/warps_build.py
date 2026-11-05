"""
PlatformIO extra script: compile Warps' DSP from third_party/parasites/ alongside Boy's firmware
(src/teensy_move), for its mode 3. Parasites is Matthias Puech's fork of Mutable Instruments' code:
Warps with eight further modes, and its own stmlib, so it doesn't mix with third_party/parasites.

Only the DSP and its tables are built. Warps' own hardware layer (warps.cc, cv_scaler.cc, ui.cc,
settings.cc, drivers/) is replaced by src/teensy_move/warps_mode.cpp.
"""
import sys
from os.path import isfile, join

Import("env")

PROJECT = env.subst("$PROJECT_DIR")
if not isfile(join(PROJECT, "third_party", "parasites", "stmlib", "dsp", "units.cc")):
    sys.stderr.write("\nwarps_build: third_party/parasites or its stmlib is missing.\n"
                     "Run: git submodule update --init --recursive\n\n")
    env.Exit(1)

# The local shim (src/teensy_move/warps/drivers/debug_pin.h) must come BEFORE
# third_party/parasites/ so it shadows the original, which needs the STM32F4's headers.
env.Prepend(CPPPATH=[
    env.subst("$PROJECT_DIR/src/teensy_move"),
    env.subst("$PROJECT_DIR/third_party/parasites"),
])

# Warps DSP (all .cc in dsp/: modulator, oscillator, vocoder, filter_bank)
env.BuildSources(
    "$BUILD_DIR/warps_dsp",
    "$PROJECT_DIR/third_party/parasites/warps/dsp",
)

# Warps resources (lookup tables)
env.BuildSources(
    "$BUILD_DIR/warps_resources",
    "$PROJECT_DIR/third_party/parasites/warps",
    "+<resources.cc>",
)

# stmlib DSP (units.cc: the SemitonesToRatio tables)
env.BuildSources(
    "$BUILD_DIR/stmlib_dsp",
    "$PROJECT_DIR/third_party/parasites/stmlib/dsp",
    "+<units.cc>",
)

# stmlib utils (random.cc: the noise carrier)
env.BuildSources(
    "$BUILD_DIR/stmlib_utils",
    "$PROJECT_DIR/third_party/parasites/stmlib/utils",
    "+<random.cc>",
)
