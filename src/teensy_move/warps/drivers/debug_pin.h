// Local shim: replaces warps/drivers/debug_pin.h from the Mutable repo, which needs the STM32F4's
// headers. scripts/warps_build.py puts src/teensy_move ahead of third_party/eurorack, so
// modulator.cc finds this one. Empty stubs: Boy has no debug pin.

#ifndef WARPS_DRIVERS_DEBUG_PIN_H_
#define WARPS_DRIVERS_DEBUG_PIN_H_

namespace warps {

class DebugPin {
 public:
  static void Init() { }
  static void High() { }
  static void Low() { }
};

#define TIC
#define TOC

}  // namespace warps

#endif  // WARPS_DRIVERS_DEBUG_PIN_H_
