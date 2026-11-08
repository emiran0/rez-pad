// Screen power: applies the backlight policy (lib/screen_logic) with short fades. Night dimming
// from the synced clock, dark while the PC sleeps (USB suspend), wake on input, display.toggle and
// the brightness knob. While the backlight is off, LVGL stops rendering (no SPI traffic, less heat
// near the sensor) and the first frame is drawn before the backlight comes back.
// UI task only.
#pragma once

#include <stdint.h>

#include "config_logic.h"
#include "screen_logic.h"

namespace rp {
namespace screen {

// Config levels (drops knob changes). Call at boot and after every config.
void apply(const cfg::Config& c);

// Every UI loop iteration, before ui::loop().
void tick(uint32_t nowMs);

// Input event arrived. Returns true when it only woke the screen.
bool onInput();

void turnOff();
void adjust(int detents);

uint8_t target();
const Policy& policy();

}  // namespace screen
}  // namespace rp
