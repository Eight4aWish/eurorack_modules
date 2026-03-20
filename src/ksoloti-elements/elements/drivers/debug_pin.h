// Local shim: replaces elements/drivers/debug_pin.h from the Mutable repo.
// Provides empty stubs — we don't need GPIO debug timing on the Ksoloti.

#ifndef ELEMENTS_DRIVERS_DEBUG_PIN_H_
#define ELEMENTS_DRIVERS_DEBUG_PIN_H_

namespace elements {

class DebugPin {
 public:
  static void Init() { }
  static void High() { }
  static void Low() { }
};

#define TIC
#define TOC

}  // namespace elements

#endif  // ELEMENTS_DRIVERS_DEBUG_PIN_H_
