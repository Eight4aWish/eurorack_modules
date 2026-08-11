"""
PlatformIO extra script: compile Mutable Instruments Elements DSP sources
from third_party/eurorack/ alongside the main ksoloti_elements firmware.

third_party/eurorack is a git submodule pinned to upstream, so the resonator
resolution patch is applied here at build time rather than left as a manual
step. Upstream ships 52 modes, which overruns the CPU budget at 48 kHz - and a
build that skipped the patch produced glitching audio with nothing to say why.
"""
import subprocess
import sys
from os.path import join

Import("env")

# --- Apply the vendored-source patch, or refuse to build -------------------------------
#
# Idempotent: if voice.cc already carries the reduced resolution there is nothing to do,
# so this is safe on every rebuild and after a submodule reset alike.

PROJECT = env.subst("$PROJECT_DIR")
SUBMODULE = join(PROJECT, "third_party", "eurorack")
VOICE = join(SUBMODULE, "elements", "dsp", "voice.cc")
PATCH = join(PROJECT, "patches", "ksoloti_elements-resonator-resolution.patch")
MARKER = "set_resolution(36)"


def _voice_source():
    try:
        with open(VOICE) as f:
            return f.read()
    except OSError:
        return ""


src = _voice_source()
if not src:
    sys.stderr.write(
        "\nelements_build: %s is missing.\n"
        "Run: git submodule update --init --recursive\n\n" % VOICE)
    env.Exit(1)
elif MARKER not in src:
    print("elements_build: applying %s" % PATCH)
    r = subprocess.run(["git", "apply", PATCH], cwd=SUBMODULE,
                       capture_output=True, text=True)
    if MARKER not in _voice_source():
        sys.stderr.write(
            "\nelements_build: could not apply the resonator resolution patch.\n"
            "%s\nUpstream ships 52 modes, which overruns the CPU budget at 48 kHz,\n"
            "so the build is stopped rather than producing glitching audio.\n\n"
            % (r.stderr.strip() or "git apply reported no error"))
        env.Exit(1)

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
