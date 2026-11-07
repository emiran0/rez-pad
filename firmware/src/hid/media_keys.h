// HID consumer-control media keys: the offline fallback for media.* actions when the host app is
// not connected (CLAUDE.md, "Offline behavior"). Keys are sent from a small task, so callers
// never block on USB.
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

uint32_t sentCount();
uint32_t droppedCount();

}  // namespace hid
}  // namespace rp
