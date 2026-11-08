// Backlight policy (pure, host-testable): which brightness the screen should have right now, from
// the config (day level, night window and level, "off while the PC sleeps"), the knob, the
// display.toggle action, USB suspend and recent input. The firmware feeds it and fades the
// backlight to target().
#pragma once

#include <stdint.h>

namespace rp {
namespace screen {

// Brightness a screen woken by input gets when its normal level is 0 (night level 0).
constexpr uint8_t kWakeLevel = 10;
// A screen woken while it should be off (PC asleep, night level 0) stays on this long after the
// last input.
constexpr uint32_t kWakeMs = 30000;
// Knob step and lowest level the knob can set.
constexpr uint8_t kKnobStep = 5;
constexpr uint8_t kKnobMin = 5;

struct Settings {
  uint8_t brightness = 80;
  bool nightEnabled = false;
  uint16_t nightFromMin = 23 * 60;  // minutes since midnight
  uint16_t nightToMin = 7 * 60 + 30;
  uint8_t nightBrightness = 15;
  bool sleepOnPcSleep = true;
};

// True when `nowMin` is inside [from, to), which may wrap past midnight. from == to is empty.
bool inWindow(uint16_t fromMin, uint16_t toMin, int nowMin);

class Policy {
 public:
  // New config: takes its levels (drops knob changes) and keeps the off/suspend state.
  void setSettings(const Settings& s);

  // `localMin` is the local time in minutes since midnight, or -1 when unknown (no night then).
  bool night(int localMin) const;
  uint8_t target(int localMin, uint32_t nowMs) const;

  // Any knob or button input. Returns true when it only woke the screen and must not run its
  // action (the screen was dark).
  bool onInput(int localMin, uint32_t nowMs);

  // display.toggle: dark until the next input.
  void turnOff();
  bool manualOff() const { return manualOff_; }

  // display.brightness: steps the level of the current period (day or night) from what is shown.
  void adjust(int detents, int localMin, uint32_t nowMs);

  void setUsbSuspended(bool suspended);
  bool usbSuspended() const { return suspended_; }

 private:
  bool awake(uint32_t nowMs) const;
  uint8_t level(int localMin) const;

  Settings s_;
  uint8_t day_ = 80;
  uint8_t nightLevel_ = 15;
  bool manualOff_ = false;
  bool suspended_ = false;
  bool woken_ = false;
  uint32_t wakeUntilMs_ = 0;
};

}  // namespace screen
}  // namespace rp
