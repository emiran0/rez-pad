// Native USB port (TinyUSB): composite device with CDC (protocol link, docs/protocol.md) and
// HID consumer control (media keys when the host app is not running).
//
// Identity: VID 0x303A (Espressif) / PID 0x4005 (TinyUSB's pre-allocated PID for CDC + HID on the
// Espressif VID), manufacturer and product "rez-pad", serial "RP-<chip MAC>". The host finds the
// device by VID/PID plus the product string.
#pragma once

#include <Arduino.h>

class USBCDC;

namespace rp {
namespace usb {

constexpr uint16_t kVid = 0x303A;
constexpr uint16_t kPid = 0x4005;

bool begin();

// Enumerated by the host and not suspended.
bool mounted();
bool suspended();
const char* serialNumber();

// The protocol CDC interface (used by the link task from Phase 5).
USBCDC& cdc();

}  // namespace usb
}  // namespace rp
