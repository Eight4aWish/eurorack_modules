"""
PlatformIO extra script: compile Mutable Instruments Elements DSP sources
from third_party/eurorack/ alongside the main ksoloti-elements firmware.

NOTE: third_party/eurorack is a git submodule. Before building, apply the
resonator resolution patch:
    cd third_party/eurorack
    git apply ../../patches/ksoloti-elements-resonator-resolution.patch
"""
Import("env")

# Enable hardware FPU for Cortex-M4F (STM32F429).
# Must be set on compiler, assembler, AND linker to avoid ABI mismatch.
fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
env.Append(CCFLAGS=fpu_flags)
env.Append(ASFLAGS=fpu_flags)
env.Append(LINKFLAGS=fpu_flags)

# Local shims (src/ksoloti-elements/) must come BEFORE third_party/eurorack/
# so our debug_pin.h stub shadows the hardware-dependent original.
env.Prepend(CPPPATH=[
    env.subst("$PROJECT_DIR/src/ksoloti-elements"),
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
