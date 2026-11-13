#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Baghurst
#
# Build (and flash) the Rev B Pico 2 W apps in src/revb with the Pico SDK and CMake, using the
# packages PlatformIO already holds for the pico2w_oc env: the Pico SDK inside Arduino-Pico, its
# arm64 GCC, and ninja. Nothing else to install. The SDK builds its own picotool (2.1.1) into the
# build directory the first time.
#
#   scripts/revb.sh                build every app
#   scripts/revb.sh mod2           build one
#   scripts/revb.sh flash mod2     build it, then copy it to the Pico: hold BOOTSEL while
#                                  plugging the Pico in first, so it shows as the RP2350 drive
#
# Apps come out as build/revb/apps/<app>/<app>.uf2. docs/REVB_COMPUTER.md has the rest.

set -e
cd "$(dirname "$0")/.."
PK="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages"
SDK="$PK/framework-arduinopico/pico-sdk"
GCC="$PK/toolchain-rp2040-earlephilhower"
if [ ! -d "$SDK" ] || [ ! -x "$GCC/bin/arm-none-eabi-gcc" ]; then
  echo "PlatformIO's Pico packages are missing: run 'pio pkg install -e pico2w_oc' once" >&2
  exit 1
fi
export PATH="$GCC/bin:$PK/tool-ninja:$PATH"

flash=
if [ "$1" = flash ]; then
  flash=1
  shift
  [ -n "$1" ] || { echo "usage: scripts/revb.sh flash <app>" >&2; exit 1; }
fi

B=build/revb
if [ ! -f "$B/build.ninja" ]; then
  cmake -S src/revb -B "$B" -G Ninja -DPICO_SDK_PATH="$SDK" -DPICO_TOOLCHAIN_PATH="$GCC"
fi
if [ -n "$1" ]; then
  cmake --build "$B" --target "$1"
else
  cmake --build "$B"
fi

# Flash by copying the UF2 to the drive the Pico shows in BOOTSEL mode. (The picotool the SDK builds
# here has no USB support without libusb's pkg-config, and PlatformIO's picotool 2.0.0 can't read
# SDK 2.2 images.)
if [ -n "$flash" ]; then
  VOL=/Volumes/RP2350
  if [ ! -d "$VOL" ]; then
    echo "No $VOL: hold BOOTSEL while plugging the Pico in, then run this again" >&2
    exit 1
  fi
  cp "$B/apps/$1/$1.uf2" "$VOL/" && echo "$1 copied to $VOL; the Pico reboots into it"
fi
