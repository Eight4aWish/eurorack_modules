# scripts/capture_firmware.py
import os
from shutil import copy2

Import("env")

def capture_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname  = env.subst("$PROGNAME")
    env_name  = env.subst("$PIOENV")
    project_dir = env.subst("$PROJECT_DIR")

    # Common output formats PlatformIO produces
    candidates = [
        progname + ".elf",   # always there
        progname + ".bin",   # STM32, ESP32, etc.
        progname + ".hex",   # Teensy
        progname + ".uf2",   # Pico 2 W
    ]

    out_dir = os.path.join(project_dir, "build_artifacts")
    os.makedirs(out_dir, exist_ok=True)

    for fname in candidates:
        src = os.path.join(build_dir, fname)
        if os.path.exists(src):
            # e.g. build_artifacts/pico2w.elf, daisyseed_duino.bin, etc.
            dst = os.path.join(out_dir, f"{env_name}{os.path.splitext(fname)[1]}")
            copy2(src, dst)
            print(f"[capture] {env_name}: copied {fname} -> {dst}")

# Run after ELF is linked (which implies a successful build)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", capture_firmware)
