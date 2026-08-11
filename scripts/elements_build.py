"""
PlatformIO extra script: compile Mutable Instruments Elements DSP sources
from third_party/eurorack/ alongside the main ksoloti_elements firmware.

third_party/eurorack is a git submodule pinned to upstream, so the resonator
resolution is set here at build time rather than left as a manual step - a
build that skipped it used whatever upstream happened to ship, silently.
"""
import re
import sys
from os.path import join

Import("env")

# --- Resonator resolution ---------------------------------------------------------------
#
# Elements sets this inside vendored source, and Voice::resonator_ is private with no
# accessor, so it cannot be set from our own code. Rather than carry a patch file that
# breaks whenever upstream moves, the line is rewritten in place to whatever RESOLUTION
# says. Idempotent, and independent of what upstream happens to ship.
#
# Budget is 84000 cycles per block (16 samples at 32 kHz, 168 MHz). Mutable ship 52 with
# the comment "Runs with 56 extremely tightly" on the same budget. LED2 lights at 95%.

RESOLUTION = 44

PROJECT = env.subst("$PROJECT_DIR")
VOICE = join(PROJECT, "third_party", "eurorack", "elements", "dsp", "voice.cc")
PATTERN = re.compile(r"(resonator_\.set_resolution\()(\d+)(\))")

try:
    with open(VOICE) as f:
        src = f.read()
except OSError:
    sys.stderr.write("\nelements_build: %s is missing.\n"
                     "Run: git submodule update --init --recursive\n\n" % VOICE)
    env.Exit(1)
    src = ""

found = PATTERN.search(src)
if not found:
    sys.stderr.write("\nelements_build: no set_resolution() call in %s — upstream changed?\n"
                     "Refusing to build rather than guess the resonator resolution.\n\n" % VOICE)
    env.Exit(1)
elif int(found.group(2)) != RESOLUTION:
    print("elements_build: resonator resolution %s -> %d" % (found.group(2), RESOLUTION))
    with open(VOICE, "w") as f:
        f.write(PATTERN.sub(r"\g<1>%d\g<3>" % RESOLUTION, src, count=1))
else:
    print("elements_build: resonator resolution %d" % RESOLUTION)

# Enable hardware FPU for Cortex-M4F (STM32F429).
# Must be set on compiler, assembler, AND linker to avoid ABI mismatch.
fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
env.Append(CCFLAGS=fpu_flags)
env.Append(ASFLAGS=fpu_flags)
env.Append(LINKFLAGS=fpu_flags)

# Local shims (src/ksoloti_elements/) must come BEFORE third_party/eurorack/
# so our debug_pin.h stub shadows the hardware-dependent original.
env.Prepend(CPPPATH=[
    env.subst("$PROJECT_DIR/src/ksoloti_elements"),
    env.subst("$PROJECT_DIR/third_party/eurorack"),
])

# Elements DSP core (all .cc in dsp/)
env.BuildSources(
    "$BUILD_DIR/elements_dsp",
    "$PROJECT_DIR/third_party/eurorack/elements/dsp",
)

# Elements resources (lookup tables + sample data)
env.BuildSources(
    "$BUILD_DIR/elements_resources",
    "$PROJECT_DIR/third_party/eurorack/elements",
    "+<resources.cc>",
)

# stmlib DSP (units.cc — SemitonesToRatio LUT)
env.BuildSources(
    "$BUILD_DIR/stmlib_dsp",
    "$PROJECT_DIR/third_party/eurorack/stmlib/dsp",
    "+<units.cc>",
)

# stmlib utils (random.cc)
env.BuildSources(
    "$BUILD_DIR/stmlib_utils",
    "$PROJECT_DIR/third_party/eurorack/stmlib/utils",
    "+<random.cc>",
)
