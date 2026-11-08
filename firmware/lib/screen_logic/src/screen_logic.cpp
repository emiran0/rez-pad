#include "screen_logic.h"

namespace rp {
namespace screen {

bool inWindow(uint16_t fromMin, uint16_t toMin, int nowMin) {
  if (nowMin < 0 || fromMin == toMin) return false;
  if (fromMin < toMin) return nowMin >= fromMin && nowMin < toMin;
  return nowMin >= fromMin || nowMin < toMin;  // wraps past midnight
}

void Policy::setSettings(const Settings& s) {
  s_ = s;
  day_ = s.brightness > 100 ? 100 : s.brightness;
  nightLevel_ = s.nightBrightness > 100 ? 100 : s.nightBrightness;
}

bool Policy::night(int localMin) const {
  return s_.nightEnabled && inWindow(s_.nightFromMin, s_.nightToMin, localMin);
}

uint8_t Policy::level(int localMin) const { return night(localMin) ? nightLevel_ : day_; }

bool Policy::awake(uint32_t nowMs) const {
  return woken_ && static_cast<int32_t>(wakeUntilMs_ - nowMs) > 0;
}

uint8_t Policy::target(int localMin, uint32_t nowMs) const {
  if (manualOff_) return 0;
  const uint8_t lvl = level(localMin);
  const bool shouldSleep = (suspended_ && s_.sleepOnPcSleep) || lvl == 0;
  if (!shouldSleep) return lvl;
  if (!awake(nowMs)) return 0;
  return lvl < kWakeLevel ? kWakeLevel : lvl;
}

bool Policy::onInput(int localMin, uint32_t nowMs) {
  if (manualOff_) {
    manualOff_ = false;
    return true;
  }
  const bool dark = target(localMin, nowMs) == 0;
  // Keep a woken screen on while it is being used (only matters while it should sleep).
  woken_ = true;
  wakeUntilMs_ = nowMs + kWakeMs;
  return dark;
}

void Policy::turnOff() { manualOff_ = true; }

void Policy::adjust(int detents, int localMin, uint32_t nowMs) {
  int v = static_cast<int>(target(localMin, nowMs)) + detents * kKnobStep;
  if (v < kKnobMin) v = kKnobMin;
  if (v > 100) v = 100;
  if (night(localMin)) nightLevel_ = static_cast<uint8_t>(v);
  else day_ = static_cast<uint8_t>(v);
}

void Policy::setUsbSuspended(bool suspended) {
  if (suspended == suspended_) return;
  suspended_ = suspended;
  woken_ = false;  // falls asleep right away; on resume the normal level applies
}

}  // namespace screen
}  // namespace rp
