# Third-Party Acknowledgments

This repository contains modules that build on top of other open-source
projects. This file lists the material third-party code each module
depends on, by module, with license. The headline thing to know:

> **The `esp32_clklink` firmware binary is licensed GPL-2.0-or-later**
> because it links against Ableton Link. All other modules are
> permissively licensed (typically MIT) and can be used without that
> obligation. See [LICENSE](LICENSE) and
> [LICENSE.esp32_clklink](LICENSE.esp32_clklink) for the legal terms.

## esp32_clklink

The Eurorack clock / reset / manual-CV module with Ableton Link sync.

| Component | License | Role |
|---|---|---|
| [Ableton Link](https://github.com/Ableton/link) | GPL-2.0-or-later | beat & tempo sync protocol |
| [docwilco/esp_abl_link](https://github.com/docwilco/esp_abl_link) | GPL-2.0-or-later | ESP-IDF wrapper around Link's C API |
| [asio](https://think-async.com/Asio/) | Boost Software License 1.0 | networking, pulled in by Link |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | runtime, WiFi, FreeRTOS, lwIP |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1 | Wire / WiFi / analogRead APIs |
| [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) | Apache-2.0 | PlatformIO platform fork pinning Arduino 3.x + IDF 5.5 |
| [Adafruit MCP4728](https://github.com/adafruit/Adafruit_MCP4728) | MIT | DAC driver |

## ksoloti_elements

A port of Mutable Instruments Elements to the Ksoloti Big Genes board.

| Component | License | Role |
|---|---|---|
| [Mutable Instruments Elements](https://github.com/pichenettes/eurorack/tree/master/elements) (Émilie Gillet) | MIT | modal-synthesis DSP — *the entire voice engine* |
| [Mutable Instruments stmlib](https://github.com/pichenettes/eurorack/tree/master/stmlib) (Émilie Gillet) | MIT | DSP primitives used by Elements |
| [STM32Cube HAL](https://github.com/STMicroelectronics/STM32CubeF4) | BSD-3-Clause | STM32F4 hardware abstraction |

The Mutable Instruments sources are vendored as a git submodule under
`third_party/eurorack/`. Each file in that tree retains its original
copyright header and MIT permission notice.

## nanoesp32_corthex

The AI-driven Eurorack voice with an embedded web UI and Plaits + Swords +
Four Play patch.

| Component | License | Role |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | runtime |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1 | Wire / WiFi / GPIO APIs |
| [ESPAsyncWebServer](https://github.com/esp32async/ESPAsyncWebServer) | LGPL-3.0 | HTTP + WebSocket server |
| [AsyncTCP](https://github.com/esp32async/AsyncTCP) | LGPL-3.0 | TCP backend for the web server |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) (Benoît Blanchon) | MIT | JSON parsing for the LLM API responses |
| [Adafruit MCP4728](https://github.com/adafruit/Adafruit_MCP4728) | MIT | DAC driver |

## pico2w_oc

The Raspberry Pi Pico 2 W "output module" — 4 CV outs + 2 CV ins + display
+ MIDI.

| Component | License | Role |
|---|---|---|
| [Arduino-Pico](https://github.com/earlephilhower/arduino-pico) (Earle F. Philhower III) | LGPL-2.1 | RP2350 Arduino framework |
| [Adafruit MCP4728](https://github.com/adafruit/Adafruit_MCP4728) | MIT | DAC driver |
| [Adafruit ADS1X15](https://github.com/adafruit/Adafruit_ADS1X15) | BSD-3-Clause | ADC driver |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) + [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | BSD-3-Clause / MIT | OLED display |
| [Bounce2](https://github.com/thomasfredericks/Bounce2) (Thomas Ouellet Fredericks) | MIT | button debouncing |
| [FortySevenEffects MIDI Library](https://github.com/FortySevenEffects/arduino_midi_library) | MIT | MIDI parsing |

## daisy_mfx

Multi-effects unit on the Electrosmith Daisy Seed.

| Component | License | Role |
|---|---|---|
| [DaisyDuino](https://github.com/electro-smith/DaisyDuino) (Electrosmith) | MIT | Daisy hardware abstraction + DSP library |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) + [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | BSD-3-Clause / MIT | OLED display |

## teensy_chaos, teensy_move

Teensy 4.1 modules — chaotic/fractal synthesis (`teensy_chaos`) and
CV/Chord/Drone utility (`teensy_move`).

| Component | License | Role |
|---|---|---|
| [Teensyduino](https://github.com/PaulStoffregen/cores) (PJRC) | MIT (with the standard PJRC clause for Teensy hardware) | Teensy 4.1 core + USB stack |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) + [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | BSD-3-Clause / MIT | OLED display |

## Build & tooling (not redistributed)

- [PlatformIO](https://platformio.org/) (Apache-2.0) — build system
- [dfu-util](http://dfu-util.sourceforge.net/) (GPL-2.0) — USB DFU flasher
- [esptool.py](https://github.com/espressif/esptool) (GPL-2.0) — ESP32 flasher
- [xtensa-esp-elf](https://github.com/espressif/crosstool-NG) (GPL with exception) — ESP32 cross-compiler

These tools run on the developer machine to build/flash firmware; they
aren't redistributed as part of any module's binary.

## Reporting omissions

If you spot a dependency that isn't acknowledged here, please open an
issue or pull request against this repository.
