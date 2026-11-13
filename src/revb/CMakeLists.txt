# Firmware for the analogue board Rev B with a Pico 2 W: the Pico SDK, not PlatformIO (the
# Workshop Computer's programs are Pico SDK projects, and ComputerCard is written for it).
#
#   cmake -S src/revb -B build/revb -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build/revb -j
#
# Each app comes out as build/revb/apps/<app>/<app>.uf2. docs/REVB_COMPUTER.md has the details.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(revb C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(card)
add_subdirectory(apps/snare)
add_subdirectory(apps/bends)
