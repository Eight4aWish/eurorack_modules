# PlatformIO's Teensy toolchain (toolchain-gccarmnoneeabi-teensy) is x86_64 only, and an arm64 Mac
# without Rosetta cannot run it. Put PlatformIO's arm64 xPack GCC (toolchain-gccarmnoneeabi) first
# on the build's PATH instead. Runs after the platform's own setup, so it wins; does nothing elsewhere.
import os
import platform

Import("env")

if platform.system() == "Darwin" and platform.machine() == "arm64":
    packages = env.subst("$PROJECT_PACKAGES_DIR")
    xpack = os.path.join(packages, "toolchain-gccarmnoneeabi", "bin")
    if os.path.isfile(os.path.join(xpack, "arm-none-eabi-gcc")):
        env.PrependENVPath("PATH", xpack)
        # the Teensy package's CMSIS-DSP library (libarm_cortexM7lfsp_math.a) is ARM code, so it links as is
        env.Append(LIBPATH=[os.path.join(packages, "toolchain-gccarmnoneeabi-teensy", "arm-none-eabi", "lib")])
    else:
        print("arm64_toolchain.py: no xPack GCC; install it with: pio pkg install -g -t platformio/toolchain-gccarmnoneeabi")
