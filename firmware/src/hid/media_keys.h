// USB HID output: consumer-control media keys (the offline fallback for media.* actions when the
// host app is not connected, CLAUDE.md "Offline behavior") and a keyboard for button shortcuts.
// Both share one HID interface (report ids). Keys are sent from a small task, so callers never
// block on USB.
#pragma once

#include <stdint.h>

#include "app/actions.h"

namespace rp {
namespace hid {

// Registers the HID interface; called by usb::begin() before USB starts.
void begin();

// Sends the key(s) for a media action. Returns false for actions without a HID equivalent
// (media.seek) or when USB is not connected.
bool sendMediaAction(actions::Action a, int value);

// Presses and releases a key shortcut (HID modifier bits and keyboard usage, actions::parseKeys).
bool sendKeys(uint8_t mods, uint8_t key);

uint32_t sentCount();
uint32_t droppedCount();

}  // namespace hid
}  // namespace rp
